#ifndef __JPEG_ENCODER_H__
#define __JPEG_ENCODER_H__

#include <cstdint>
#include <cstdio>

namespace media {

// JPEG marker codes
enum JpegMarker : uint16_t {
    M_SOF0  = 0xc0,
    M_DHT   = 0xc4,
    M_RST0  = 0xd0,
    M_RST7  = 0xd7,
    M_SOI   = 0xd8,
    M_EOI   = 0xd9,
    M_SOS   = 0xda,
    M_DQT   = 0xdb,
    M_DRI   = 0xdd,
};

// JPEG header structure for large image assembly
struct JpegHeader {
    // Quantization table
    struct {
        uint32_t Lq;  // length
        uint32_t Pq;  // precision
        uint32_t Tq;  // identifier
    } qt[2];

    // Frame header
    int width;
    int height;
    int samplePrecision;
    int numComponents;

    // Restart interval
    int restartInterval;  // MCU count per restart interval

    // Scan header
    int numScanComponents;
};

// Build JPEG quantization tables for given quality (1-99)
// Outputs 64-byte luma and chroma quantization values
void jpegMakeQuantTables(int quality, uint8_t* lumaQt, uint8_t* chromaQt);

// Write complete JPEG header (SOI through SOS) to buffer
// Returns bytes written
uint32_t jpegWriteHeader(uint8_t* buf, int width, int height, int quality);

// Same as above but with an explicit restart interval (DRI), in MCUs per
// restart interval. Used when stitching independently-encoded strips into one
// large JPEG: DRI must equal the MCU count of one strip so each strip seam is
// a restart boundary and DC prediction resets cleanly.
uint32_t jpegWriteHeader(uint8_t* buf, int width, int height, int quality, int dri);

// Write JPEG header to FILE
uint32_t jpegWriteHeaderToFile(FILE* file, int width, int height, int quality);

// Write JPEG EOI marker to buffer
void jpegWriteEoi(uint8_t* buf);

// Find SOS marker in JPEG data, return offset to scan data (after SOS header)
// Returns -1 if not found
int jpegFindScanDataOffset(const uint8_t* data, int size);

} // namespace media

#endif // __JPEG_ENCODER_H__
