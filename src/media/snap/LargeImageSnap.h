#ifndef __LARGE_IMAGE_SNAP_H__
#define __LARGE_IMAGE_SNAP_H__

#include <cstdint>
#include <string>

namespace media {

class LargeImageSnap {
public:
    LargeImageSnap();
    ~LargeImageSnap();

    // Capture a large JPEG image ( > sensor resolution)
    // dst_w/dst_h: target resolution (e.g. 3840x2160 for 8M)
    // quality: JPEG quality 1-99
    // Returns true on success
    bool snapLarge(const std::string& filename, int dst_w, int dst_h, int quality);

private:
    static constexpr int kCropHeight = 32;

    bool allocateBuffers(int src_w, int src_h, int dst_w, int dst_h);
    void freeBuffers();

    void cropStrip(uint8_t* dst, const uint8_t* src, int src_w, int src_h, int stripIndex);
    bool resizeStrip(const uint8_t* cropBuf, uint8_t* resizeBuf,
                     int src_w, int crop_h, int dst_w, int strip_h,
                     int dst_w_total, int dst_h_total);
    bool encodeJpegStrip(const uint8_t* nv12Buf, uint8_t* jpegBuf,
                         int w, int h, int quality, int& outLen);
    int findSosDataOffset(const uint8_t* jpegData, int len);

    // Aligned dimensions for NV12
    static int align2(int v) { return (v + 1) & ~1; }

    // Buffers
    uint8_t* cropBuf_;
    uint8_t* resizeBuf_;
    uint8_t* jpegBuf_;

    int sensorW_;
    int sensorH_;
};

} // namespace media

#endif // __LARGE_IMAGE_SNAP_H__
