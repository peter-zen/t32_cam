#ifndef SIMULATION_MODE

#include "LargeImageSnap.h"
#include "JpegEncoder.h"
#include "Logger.h"
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

// Include simd_resize_large.h for MXU2 types, then .c for static inline implementation
#include "simd_resize_large.h"
#include "simd_resize_large.c"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#define TAG "LargeSnap"

// Upper bound for strip height. Actual stripH is the largest value <= this cap
// that is a multiple of 16 and divides dst_h (see pickStripHeight).
static const int kStripHeightCap = 288;

namespace media {

LargeImageSnap::LargeImageSnap()
    : cropBuf_(nullptr), resizeBuf_(nullptr), jpegBuf_(nullptr)
    , sensorW_(0), sensorH_(0), cropRowsPerStrip_(0) {}

LargeImageSnap::~LargeImageSnap() {
    freeBuffers();
}

int LargeImageSnap::pickStripHeight(int dst_h, int cap) {
    int s = cap - (cap % 16);  // round cap down to a multiple of 16 (MCU height)
    for (; s >= 16; s -= 16) {
        if ((dst_h % s) == 0) return s;
    }
    return -1;
}

bool LargeImageSnap::allocateBuffers(int src_w, int src_h, int dst_w, int dst_h,
                                    int stripH, int cropRowsPerStrip) {
    (void)dst_h;
    freeBuffers();

    sensorW_ = src_w;
    sensorH_ = src_h;
    cropRowsPerStrip_ = cropRowsPerStrip;

    // Crop buffer: one strip of source NV12 (src_w x cropRowsPerStrip)
    size_t cropSize = static_cast<size_t>(src_w) * cropRowsPerStrip * 3 / 2;
    cropBuf_ = static_cast<uint8_t*>(malloc(cropSize));
    if (!cropBuf_) return false;

    // Resize buffer: one strip of destination NV12 (dst_w x stripH).
    // Must be physically contiguous for IMP_Encoder_InputJpege.
    size_t resizeSize = static_cast<size_t>(dst_w) * stripH * 3 / 2;
    resizeBuf_ = static_cast<uint8_t*>(IMP_Encoder_VbmAlloc(resizeSize, 256));
    if (!resizeBuf_) {
        free(cropBuf_);
        cropBuf_ = nullptr;
        return false;
    }
    memset(resizeBuf_, 0, resizeSize);

    // JPEG output buffer (worst case ~ NV12 size of one strip)
    size_t jpegSize = static_cast<size_t>(dst_w) * stripH * 3 / 2;
    jpegBuf_ = static_cast<uint8_t*>(malloc(jpegSize));
    if (!jpegBuf_) {
        freeBuffers();
        return false;
    }

    return true;
}

void LargeImageSnap::freeBuffers() {
    if (cropBuf_) { free(cropBuf_); cropBuf_ = nullptr; }
    if (resizeBuf_) { IMP_Encoder_VbmFree(resizeBuf_); resizeBuf_ = nullptr; }
    if (jpegBuf_) { free(jpegBuf_); jpegBuf_ = nullptr; }
}

void LargeImageSnap::cropStrip(uint8_t* dst, const uint8_t* src,
    int src_w, int /*src_h*/, int stripIndex) {
    int rows = cropRowsPerStrip_;
    int offset = stripIndex * rows;
    // Y plane
    for (int i = 0; i < rows; i++) {
        memcpy(dst + i * src_w, src + (offset + i) * src_w, src_w);
    }
    // UV plane
    int srcUvOffset = src_w * sensorH_;
    int dstUvOffset = src_w * rows;
    for (int j = 0; j < rows / 2; j++) {
        memcpy(dst + dstUvOffset + j * src_w,
               src + srcUvOffset + ((offset / 2 + j) * src_w), src_w);
    }
}

bool LargeImageSnap::resizeStrip(const uint8_t* cropBuf, uint8_t* resizeBuf,
    int src_w, int crop_h, int dst_w, int strip_h,
    int /*dst_w_total*/, int /*dst_h_total*/) {
    int ret = opencv_resize_crop_simd(const_cast<uint8_t*>(cropBuf), src_w, crop_h,
                                       resizeBuf, dst_w, strip_h);
    if (ret != 0) return false;
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
    // Get sensor frame (CH0 already enabled by caller)
    IMPFrameInfo *frame = nullptr;
    int sensorChn = 0;
    if (IMP_FrameSource_GetFrameEx(sensorChn, &frame) < 0) {
        Logger::log(LogLevel::ERROR, "snapLarge: GetFrame failed");
        return false;
    }
    int src_w = frame->width;
    int src_h = frame->height;
    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(frame->virAddr);

    bool ok = snapLargeFromBuffer(filename, dst_w, dst_h, quality, srcData, src_w, src_h);

    IMP_FrameSource_ReleaseFrameEx(sensorChn, frame);
    return ok;
}

bool LargeImageSnap::encodeLargeJpeg(const std::string& filename, int dst_w, int dst_h, int quality,
                                    int src_w, int src_h,
                                    const std::function<bool(int stripIndex)>& fillCropBuf) {
    // Target must be 16-aligned so MCU rows/cols are integral and seams land
    // on MCU boundaries (required for correct RST-marker stitching).
    if (dst_w <= 0 || dst_h <= 0 || (dst_w % 16) != 0 || (dst_h % 16) != 0) {
        Logger::log(LogLevel::ERROR, "snapLarge: dst %dx%d must be 16-aligned", dst_w, dst_h);
        return false;
    }

    // Choose strip geometry: stripH divides dst_h and is 16-aligned; src rows
    // per strip must evenly divide src_h and be even (clean NV12 UV crop).
    int stripH = pickStripHeight(dst_h, kStripHeightCap);
    if (stripH < 16) {
        Logger::log(LogLevel::ERROR, "snapLarge: no valid stripH for dst_h=%d (cap=%d)", dst_h, kStripHeightCap);
        return false;
    }
    int numStrips = dst_h / stripH;
    int cropRows = src_h / numStrips;
    if (numStrips < 1 || (src_h % numStrips) != 0 || cropRows < 2 || (cropRows % 2) != 0) {
        Logger::log(LogLevel::ERROR,
            "snapLarge: src_h=%d not evenly/parity divisible: numStrips=%d stripH=%d cropRows=%d",
            src_h, numStrips, stripH, cropRows);
        return false;
    }
    cropRows = align2(cropRows);

    Logger::log(LogLevel::INFO, "snapLarge: src=%dx%d dst=%dx%d q=%d stripH=%d numStrips=%d cropRows=%d",
                src_w, src_h, dst_w, dst_h, quality, stripH, numStrips, cropRows);

    if (!allocateBuffers(src_w, src_h, dst_w, dst_h, stripH, cropRows)) {
        Logger::log(LogLevel::ERROR, "snapLarge: allocateBuffers failed (resizeBuf=%dx%d)", dst_w, stripH);
        return false;
    }

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        Logger::log(LogLevel::ERROR, "snapLarge: open %s failed", filename.c_str());
        freeBuffers();
        return false;
    }

    // JPEG header with DRI = MCUs per strip, so each strip seam is a restart
    // boundary and DC prediction resets cleanly between strips.
    int mcuPerRow = dst_w / 16;
    int mcuRowsPerStrip = stripH / 16;
    int dri = mcuPerRow * mcuRowsPerStrip;
    uint8_t headerBuf[1024];  /* JPEG header is ~611 bytes (4 DHT tables dominate) */
    uint32_t headerLen = jpegWriteHeader(headerBuf, dst_w, dst_h, quality, dri);
    fwrite(headerBuf, 1, headerLen, fp);

    typedef std::chrono::steady_clock clock;
    long ms_crop = 0, ms_resize = 0, ms_encode = 0, ms_io = 0;
    auto t_total_begin = clock::now();

    bool success = true;
    for (int i = 0; i < numStrips; i++) {
        auto t0 = clock::now();
        if (!fillCropBuf(i)) {
            Logger::log(LogLevel::ERROR, "snapLarge: fillCropBuf failed at strip %d", i);
            success = false;
            break;
        }
        auto t1 = clock::now();

        if (!resizeStrip(cropBuf_, resizeBuf_, src_w, cropRows, dst_w, stripH, dst_w, dst_h)) {
            Logger::log(LogLevel::ERROR, "snapLarge: SIMD resize failed at strip %d", i);
            success = false;
            break;
        }
        auto t2 = clock::now();

        int jpegLen = 0;
        if (!encodeJpegStrip(resizeBuf_, jpegBuf_, dst_w, stripH, quality, jpegLen)) {
            Logger::log(LogLevel::ERROR, "snapLarge: InputJpege failed at strip %d", i);
            success = false;
            break;
        }
        auto t3 = clock::now();

        // Extract scan data (after SOS header); strip the per-strip EOI so
        // strips re-stitch cleanly under our own DRI/EOI scheme.
        int scanOffset = findSosDataOffset(jpegBuf_, jpegLen);
        if (scanOffset < 0) {
            Logger::log(LogLevel::ERROR, "snapLarge: SOS not found at strip %d", i);
            success = false;
            break;
        }
        int scanEnd = jpegLen;
        if (scanEnd >= 2 && jpegBuf_[scanEnd - 2] == 0xff && jpegBuf_[scanEnd - 1] == 0xd9) {
            scanEnd -= 2;  // strip the per-strip EOI
        }
        if (scanEnd > scanOffset) {
            fwrite(jpegBuf_ + scanOffset, 1, scanEnd - scanOffset, fp);
        }

        // Restart marker between strips; the final strip writes EOI instead.
        if (i == numStrips - 1) {
            uint8_t eoi[2];
            jpegWriteEoi(eoi);
            fwrite(eoi, 1, 2, fp);
        } else {
            uint8_t rst[2] = {0xff, static_cast<uint8_t>(0xd0 | (i & 7))};
            fwrite(rst, 1, 2, fp);
        }
        auto t4 = clock::now();

        ms_crop   += (long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        ms_resize += (long)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        ms_encode += (long)std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
        ms_io     += (long)std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count();
    }

    if (fflush(fp) != 0) {
        Logger::log(LogLevel::ERROR, "snapLarge: fflush %s failed", filename.c_str());
        success = false;
    }
    if (::fileno(fp) >= 0 && ::fsync(::fileno(fp)) != 0) {
        Logger::log(LogLevel::ERROR, "snapLarge: fsync %s failed", filename.c_str());
        success = false;
    }
    long fileBytes = ftell(fp);
    fclose(fp);
    freeBuffers();

    long ms_total = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - t_total_begin).count();

    Logger::log(LogLevel::INFO,
        "snapLarge: %s -> %s | total=%ldms (crop=%ld resize=%ld encode=%ld io=%ld) file=%ld bytes %dx%d",
        filename.c_str(), success ? "OK" : "FAIL",
        ms_total, ms_crop, ms_resize, ms_encode, ms_io, fileBytes, dst_w, dst_h);

    return success;
}

bool LargeImageSnap::snapLargeFromBuffer(const std::string& filename, int dst_w, int dst_h, int quality,
                                         const uint8_t* srcData, int src_w, int src_h) {
    return encodeLargeJpeg(filename, dst_w, dst_h, quality, src_w, src_h,
        [this, srcData, src_w, src_h](int i) -> bool {
            cropStrip(cropBuf_, srcData, src_w, src_h, i);
            return true;
        });
}

bool LargeImageSnap::snapLargeFromFile(const std::string& filename, int dst_w, int dst_h, int quality,
                                       const std::string& nv12FilePath, int src_w, int src_h) {
    FILE* fp = fopen(nv12FilePath.c_str(), "rb");
    if (!fp) {
        Logger::log(LogLevel::ERROR, "snapLargeFromFile: open %s failed", nv12FilePath.c_str());
        return false;
    }
    bool ok = encodeLargeJpeg(filename, dst_w, dst_h, quality, src_w, src_h,
        [this, fp](int i) -> bool { return cropStripFromFile(fp, i); });
    fclose(fp);
    return ok;
}

// Fill cropBuf_ with strip stripIndex's source NV12 rows read directly from the
// NV12 file (Y block then UV block, each contiguous -> one fseek+fread). Layout
// matches cropStrip so the rest of the pipeline is source-agnostic. This is the
// lever that lets >8M burst fit the 32MB board: the full ~5.5MB NV12 frame is
// never held in RAM, only one strip's source rows (~0.6MB) at a time.
bool LargeImageSnap::cropStripFromFile(FILE* fp, int stripIndex) {
    int rows = cropRowsPerStrip_;
    int offset = stripIndex * rows;
    size_t yBytes = static_cast<size_t>(rows) * sensorW_;
    size_t uvBytes = static_cast<size_t>(rows / 2) * sensorW_;
    if (fseek(fp, (long)offset * sensorW_, SEEK_SET) != 0) return false;
    if (fread(cropBuf_, 1, yBytes, fp) != yBytes) return false;
    long uvBase = (long)sensorW_ * sensorH_;
    if (fseek(fp, uvBase + (long)(offset / 2) * sensorW_, SEEK_SET) != 0) return false;
    if (fread(cropBuf_ + yBytes, 1, uvBytes, fp) != uvBytes) return false;
    return true;
}

bool LargeImageSnap::snapRaw(const std::string& filename, int quality) {
    IMPFrameInfo *frame = nullptr;
    int sensorChn = 0;
    if (IMP_FrameSource_GetFrameEx(sensorChn, &frame) < 0) {
        Logger::log(LogLevel::ERROR, "snapRaw: GetFrameEx failed");
        return false;
    }
    int w = frame->width;
    int h = frame->height;
    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(frame->virAddr);

    /* InputJpege needs a VBM-backed (physically contiguous) source. */
    size_t nv12sz = static_cast<size_t>(w) * h * 3 / 2;
    uint8_t* vbm = static_cast<uint8_t*>(IMP_Encoder_VbmAlloc(nv12sz, 256));
    if (!vbm) {
        Logger::log(LogLevel::ERROR, "snapRaw: VbmAlloc(%zu) failed", nv12sz);
        IMP_FrameSource_ReleaseFrameEx(sensorChn, frame);
        return false;
    }

    memcpy(vbm, srcData, nv12sz);
    IMP_FrameSource_ReleaseFrameEx(sensorChn, frame);

    uint8_t* jpeg = static_cast<uint8_t*>(malloc(nv12sz));
    if (!jpeg) { IMP_Encoder_VbmFree(vbm); return false; }
    int jpegLen = 0;
    int ret = IMP_Encoder_InputJpege(vbm, jpeg, w, h, quality, &jpegLen);
    IMP_Encoder_VbmFree(vbm);
    if (ret != 0 || jpegLen <= 0) {
        Logger::log(LogLevel::ERROR, "snapRaw: InputJpege failed ret=%d len=%d", ret, jpegLen);
        free(jpeg);
        return false;
    }

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) { free(jpeg); return false; }
    size_t written = fwrite(jpeg, 1, jpegLen, fp);
    bool synced = false;
    if (written == static_cast<size_t>(jpegLen)) {
        synced = (fflush(fp) == 0 && (::fileno(fp) < 0 || ::fsync(::fileno(fp)) == 0));
        if (!synced) {
            Logger::log(LogLevel::ERROR, "snapRaw: sync %s failed", filename.c_str());
        }
    }
    fclose(fp);
    free(jpeg);
    return written == static_cast<size_t>(jpegLen) && synced;
}

} // namespace media

#endif // SIMULATION_MODE
