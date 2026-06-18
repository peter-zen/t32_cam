#ifndef __LARGE_IMAGE_SNAP_H__
#define __LARGE_IMAGE_SNAP_H__

#include <cstdint>
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
