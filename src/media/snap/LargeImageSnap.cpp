#ifndef SIMULATION_MODE

#include "LargeImageSnap.h"
#include "JpegEncoder.h"
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

// Include simd_resize_large.h for MXU2 types, then .c for static inline implementation
#include "simd_resize_large.h"
#include "simd_resize_large.c"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TAG "LargeSnap"

namespace media {

// From libimp.a — SIMD bilinear resize (NV12)
extern "C" void c_resize_simd(uint8_t* src, uint8_t* dst,
    int src_w, int src_h, int dst_w, int dst_h,
    bool is_clip, int clip_offset, int clip_w, int clip_h,
    int format, uint16_t* resize_buf);

// NV12 format enum (matches c_csp_t in header_set.h)
static const int C_CSP_NV12 = 0x004;

LargeImageSnap::LargeImageSnap()
    : cropBuf_(nullptr), resizeBuf_(nullptr), jpegBuf_(nullptr), simdTmpBuf_(nullptr)
    , sensorW_(0), sensorH_(0) {}

LargeImageSnap::~LargeImageSnap() {
    freeBuffers();
}

bool LargeImageSnap::allocateBuffers(int src_w, int src_h, int dst_w, int dst_h) {
    freeBuffers();

    sensorW_ = src_w;
    sensorH_ = src_h;

    int stripH = dst_h / (src_h / kCropHeight);
    if (stripH < 2) stripH = 2;
    stripH = align2(stripH);

    // Crop buffer: one strip of source NV12
    size_t cropSize = static_cast<size_t>(src_w) * kCropHeight * 3 / 2;
    cropBuf_ = static_cast<uint8_t*>(malloc(cropSize));
    if (!cropBuf_) return false;

    // Resize buffer: one strip of destination NV12 (needs physical contiguous for VBM)
    size_t resizeSize = static_cast<size_t>(dst_w) * stripH * 3 / 2;
    resizeBuf_ = static_cast<uint8_t*>(IMP_Encoder_VbmAlloc(resizeSize, 256));
    if (!resizeBuf_) {
        free(cropBuf_);
        cropBuf_ = nullptr;
        return false;
    }
    memset(resizeBuf_, 0, resizeSize);

    // JPEG output buffer
    size_t jpegSize = static_cast<size_t>(dst_w) * stripH * 3 / 2;
    jpegBuf_ = static_cast<uint8_t*>(malloc(jpegSize));
    if (!jpegBuf_) {
        freeBuffers();
        return false;
    }

    // SIMD temporary buffer
    int simdTmpSize = static_cast<int>(sizeof(uint16_t)) *
        (dst_w * 5 + stripH * 6 + 2592);
    simdTmpBuf_ = static_cast<uint16_t*>(malloc(simdTmpSize));
    if (!simdTmpBuf_) {
        freeBuffers();
        return false;
    }

    return true;
}

void LargeImageSnap::freeBuffers() {
    if (cropBuf_) { free(cropBuf_); cropBuf_ = nullptr; }
    if (resizeBuf_) { IMP_Encoder_VbmFree(resizeBuf_); resizeBuf_ = nullptr; }
    if (jpegBuf_) { free(jpegBuf_); jpegBuf_ = nullptr; }
    if (simdTmpBuf_) { free(simdTmpBuf_); simdTmpBuf_ = nullptr; }
}

void LargeImageSnap::cropStrip(uint8_t* dst, const uint8_t* src,
    int src_w, int /*src_h*/, int stripIndex) {
    int offset = stripIndex * kCropHeight;
    // Y plane
    for (int i = 0; i < kCropHeight; i++) {
        memcpy(dst + i * src_w, src + (offset + i) * src_w, src_w);
    }
    // UV plane
    int srcUvOffset = src_w * sensorH_;
    int dstUvOffset = src_w * kCropHeight;
    for (int j = 0; j < kCropHeight / 2; j++) {
        memcpy(dst + dstUvOffset + j * src_w,
               src + srcUvOffset + ((offset / 2 + j) * src_w), src_w);
    }
}

bool LargeImageSnap::resizeStrip(const uint8_t* cropBuf, uint8_t* resizeBuf,
    int src_w, int crop_h, int dst_w, int strip_h,
    int /*dst_w_total*/, int /*dst_h_total*/) {
    if (dst_w <= 7680) {
        c_resize_simd(const_cast<uint8_t*>(cropBuf), resizeBuf,
                      src_w, crop_h, dst_w, strip_h,
                      false, 0, 0, 0, C_CSP_NV12, simdTmpBuf_);
    } else {
        int ret = opencv_resize_crop_simd(const_cast<uint8_t*>(cropBuf), src_w, crop_h,
                                           resizeBuf, dst_w, strip_h);
        if (ret != 0) return false;
    }
    return true;
}

bool LargeImageSnap::encodeJpegStrip(const uint8_t* nv12Buf, uint8_t* jpegBuf,
    int w, int h, int quality, int& outLen) {
    // IMP_Encoder_InputJpege needs physical address for the source
    IMPFrameInfo frame;
    frame.width = w;
    frame.height = h;
    frame.size = w * h * 3 / 2;
    frame.phyAddr = static_cast<uint32_t>(IMP_Encoder_VbmV2P(reinterpret_cast<intptr_t>(nv12Buf)));
    frame.virAddr = reinterpret_cast<uint32_t>(const_cast<uint8_t*>(nv12Buf));

    int ret = IMP_Encoder_InputJpege(
        reinterpret_cast<uint8_t*>(frame.virAddr), jpegBuf,
        frame.width, frame.height, quality, &outLen);
    return (ret == 0 && outLen > 0);
}

int LargeImageSnap::findSosDataOffset(const uint8_t* jpegData, int len) {
    // Search for SOS marker (0xFF 0xDA), then skip SOS header
    for (int i = 0; i < len - 1; i++) {
        if (jpegData[i] == 0xff && jpegData[i + 1] == 0xda) {
            if (i + 3 >= len) return -1;
            int sosLen = (jpegData[i + 2] << 8) | jpegData[i + 3];
            return i + 2 + sosLen;  // offset to scan data
        }
    }
    return -1;
}

bool LargeImageSnap::snapLarge(const std::string& filename, int dst_w, int dst_h, int quality) {
    // Get sensor frame
    IMPFrameInfo frame;
    // Use channel 0 (HD stream) for source frame
    int sensorChn = 0;
    int ret = IMP_FrameSource_GetFrame(sensorChn, &frame);
    if (ret < 0) {
        return false;
    }

    int src_w = frame.width;
    int src_h = frame.height;
    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(frame.virAddr);

    // Calculate strip parameters
    int numStrips = src_h / kCropHeight;
    if (numStrips < 1) numStrips = 1;
    int stripH = dst_h / numStrips;
    if (stripH < 2) stripH = 2;
    stripH = align2(stripH);

    // Allocate buffers
    if (!allocateBuffers(src_w, src_h, dst_w, dst_h)) {
        IMP_FrameSource_ReleaseFrame(sensorChn, &frame);
        return false;
    }

    // Open output file
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        IMP_FrameSource_ReleaseFrame(sensorChn, &frame);
        freeBuffers();
        return false;
    }

    // Write JPEG header with final dimensions
    uint8_t headerBuf[512];
    uint32_t headerLen = jpegWriteHeader(headerBuf, dst_w, dst_h, quality);
    fwrite(headerBuf, 1, headerLen, fp);

    bool success = true;

    // Process each strip
    for (int i = 0; i < numStrips; i++) {
        // 1. Crop strip from source
        cropStrip(cropBuf_, srcData, src_w, src_h, i);

        // 2. Resize strip
        if (!resizeStrip(cropBuf_, resizeBuf_, src_w, kCropHeight, dst_w, stripH, dst_w, dst_h)) {
            success = false;
            break;
        }

        // 3. Software JPEG encode
        int jpegLen = 0;
        if (!encodeJpegStrip(resizeBuf_, jpegBuf_, dst_w, stripH, quality, jpegLen)) {
            success = false;
            break;
        }

        // 4. Extract scan data (after SOS header) and write to output
        int scanOffset = findSosDataOffset(jpegBuf_, jpegLen);
        if (scanOffset < 0) {
            success = false;
            break;
        }

        // Write scan data (from after SOS header to before EOI)
        int scanEnd = jpegLen;
        // Find EOI marker (0xFF 0xD9) at the end
        if (scanEnd >= 2 && jpegBuf_[scanEnd - 2] == 0xff && jpegBuf_[scanEnd - 1] == 0xd9) {
            scanEnd -= 2;
        }
        if (scanEnd > scanOffset) {
            fwrite(jpegBuf_ + scanOffset, 1, scanEnd - scanOffset, fp);
        }

        // 5. Write restart marker or EOI
        if (i == numStrips - 1) {
            // Last strip: write EOI
            uint8_t eoi[2];
            jpegWriteEoi(eoi);
            fwrite(eoi, 1, 2, fp);
        } else {
            // Intermediate strip: write RST marker
            uint8_t rst[2] = {0xff, static_cast<uint8_t>(0xd0 | (i & 7))};
            fwrite(rst, 1, 2, fp);
        }
    }

    // Extra EOI (reference code does this)
    if (success) {
        uint8_t eoi[2];
        jpegWriteEoi(eoi);
        fwrite(eoi, 1, 2, fp);
    }

    fclose(fp);
    IMP_FrameSource_ReleaseFrame(sensorChn, &frame);
    freeBuffers();

    return success;
}

} // namespace media

#endif // SIMULATION_MODE
