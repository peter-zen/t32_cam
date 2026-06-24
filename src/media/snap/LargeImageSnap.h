#ifndef __LARGE_IMAGE_SNAP_H__
#define __LARGE_IMAGE_SNAP_H__

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace media {

// Strip-based large JPEG capture: grabs a sensor NV12 frame, slices it into
// horizontal strips, SIMD-upscales each strip to the target size, JPEG-encodes
// each strip, then stitches the scan data into one large JPEG.
//
// Strip geometry is chosen so that:
//   - stripH is a multiple of 16 (JPEG 4:2:0 MCU height) and divides dst_h exactly
//   - source rows per strip is even (clean NV12 UV crop)
// so numStrips * stripH == dst_h and every strip seam lands on an MCU-row
// boundary — mandatory for the RST-marker stitching to decode correctly.
class LargeImageSnap {
public:
    LargeImageSnap();
    ~LargeImageSnap();

    // Capture a large JPEG image (> sensor resolution) via strip stitching.
    // dst_w/dst_h: target resolution, must be 16-aligned (e.g. 9216x5184 ~ 48M).
    // quality: JPEG quality 1-99.
    // Returns true on success; logs the reason on failure.
    bool snapLarge(const std::string& filename, int dst_w, int dst_h, int quality);

    // Same strip-stitch capture, but from a CALLER-SUPPLIED NV12 buffer instead
    // of acquiring a live frame. Used by the >8M burst flow: capture N NV12
    // frames to a sdcard temp dir first (fast, at sensor rate), then sequentially
    // strip-scale+encode each via this entry. src_w/src_h are the buffer's real
    // dimensions (sensor-native). Hardware only.
    bool snapLargeFromBuffer(const std::string& filename, int dst_w, int dst_h, int quality,
                             const uint8_t* srcNv12, int src_w, int src_h);

    // Same strip-stitch capture as snapLargeFromBuffer, but the source NV12 is
    // read strip-by-strip from a file (fseek+fread per strip) instead of a
    // caller-supplied RAM buffer. Used by the >8M burst flow so the full ~5.5MB
    // NV12 frame is NEVER held in RAM — only one strip's source rows (~0.6MB) at
    // a time. That is what lets burst fit the 32MB board (a full-frame vector
    // tipped it into OOM on memory-tight boots). src_w/src_h are the file's real
    // dimensions (sensor-native). Hardware only.
    bool snapLargeFromFile(const std::string& filename, int dst_w, int dst_h, int quality,
                           const std::string& nv12FilePath, int src_w, int src_h);

    // Capture one sensor-native JPEG with NO upscaling/stripping: GetFrameEx
    // -> copy into VBM -> InputJpege -> save. Validates the base fetch+encode
    // pipeline independent of strip stitching and SIMD resize.
    bool snapRaw(const std::string& filename, int quality);

private:
    // Pick the largest strip height <= cap that is 16-aligned and divides dst_h.
    // Returns -1 if no value in [16, cap] satisfies both.
    static int pickStripHeight(int dst_h, int cap);

    bool allocateBuffers(int src_w, int src_h, int dst_w, int dst_h,
                         int stripH, int cropRowsPerStrip);
    void freeBuffers();

    void cropStrip(uint8_t* dst, const uint8_t* src, int src_w, int src_h, int stripIndex);
    bool resizeStrip(const uint8_t* cropBuf, uint8_t* resizeBuf,
                     int src_w, int crop_h, int dst_w, int strip_h,
                     int dst_w_total, int dst_h_total);
    bool encodeJpegStrip(const uint8_t* nv12Buf, uint8_t* jpegBuf,
                         int w, int h, int quality, int& outLen);
    int findSosDataOffset(const uint8_t* jpegData, int len);

    // Shared strip-encode+stitch pipeline (geometry, buffers, JPEG DRI header,
    // the strip loop, cleanup). fillCropBuf(stripIndex) must populate cropBuf_
    // with that strip's source NV12 rows — memory memcpy (snapLargeFromBuffer)
    // or file fread (snapLargeFromFile). Single source of truth for the RST
    // stitching so the two sources can't drift.
    bool encodeLargeJpeg(const std::string& filename, int dst_w, int dst_h, int quality,
                         int src_w, int src_h,
                         const std::function<bool(int stripIndex)>& fillCropBuf);
    // Fill cropBuf_ with strip stripIndex's source NV12 rows from the NV12 file
    // (Y block + UV block via fseek+fread). Layout matches cropStrip.
    bool cropStripFromFile(FILE* fp, int stripIndex);

    // Align up to even (NV12 rows must be even)
    static int align2(int v) { return (v + 1) & ~1; }

    // One-strip working buffers (kept small to avoid a giant contiguous alloc)
    uint8_t* cropBuf_;
    uint8_t* resizeBuf_;   // VBM-backed (InputJpege needs a physical address)
    uint8_t* jpegBuf_;

    int sensorW_;
    int sensorH_;
    int cropRowsPerStrip_;  // source rows consumed per strip
};

} // namespace media

#endif // __LARGE_IMAGE_SNAP_H__
