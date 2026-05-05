#include "JpegEncoder.h"
#include <cstring>

namespace media {

namespace {

// Standard JPEG luma quantization matrix
static const int kLumaQuantizer[64] = {
    16, 11, 12, 14, 12, 10, 16, 14,
    13, 14, 18, 17, 16, 19, 24, 40,
    26, 24, 22, 22, 24, 49, 35, 37,
    29, 40, 58, 51, 61, 60, 57, 51,
    56, 55, 64, 72, 92, 78, 64, 68,
    87, 69, 55, 56, 80, 109, 81, 87,
    95, 98, 103, 104, 103, 62, 77, 113,
    121, 112, 100, 120, 92, 101, 103, 99
};

// Standard JPEG chroma quantization matrix
static const int kChromaQuantizer[64] = {
    17, 18, 18, 24, 21, 24, 47, 26,
    26, 47, 99, 66, 56, 66, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

// Huffman table sizes
static const unsigned int kHtSize[4] = {31, 181, 31, 181};

// Huffman code length counts
static const uint8_t kHtLen[4][16] = {
    {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125},
    {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0},
    {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119},
};

// Huffman table class/destination selector
static const char kDhtSel[4] = {0x00, 0x10, 0x01, 0x11};

// Huffman values (pre-computed from reference)
static const uint8_t kHtVal[4][16][256] = {
    // DC luma
    {{0},
     {0x00},
     {0x01, 0x02, 0x03, 0x04, 0x05},
     {0x06},
     {0x07},
     {0x08},
     {0x09},
     {0x0a},
     {0x0b},
     {0}, {0}, {0}, {0}, {0}, {0}, {0}},
    // AC luma
    {{0},
     {0x01, 0x02},
     {0x03},
     {0x00, 0x04, 0x11},
     {0x05, 0x12, 0x21},
     {0x31, 0x41},
     {0x06, 0x13, 0x51, 0x61},
     {0x07, 0x22, 0x71},
     {0x14, 0x32, 0x81, 0x91, 0xa1},
     {0x08, 0x23, 0x42, 0xb1, 0xc1},
     {0x15, 0x52, 0xd1, 0xf0},
     {0x24, 0x33, 0x62, 0x72},
     {0},
     {0},
     {0x82},
     {0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36,
      0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
      0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
      0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95,
      0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3,
      0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
      0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
      0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}},
    // DC chroma
    {{0},
     {0x00, 0x01, 0x02},
     {0x03},
     {0x04},
     {0x05},
     {0x06},
     {0x07},
     {0x08},
     {0x09},
     {0x0a},
     {0x0b},
     {0}, {0}, {0}, {0}, {0}},
    // AC chroma
    {{0},
     {0x00, 0x01},
     {0x02},
     {0x03, 0x11},
     {0x04, 0x05, 0x21, 0x31},
     {0x06, 0x12, 0x41, 0x51},
     {0x07, 0x61, 0x71},
     {0x13, 0x22, 0x32, 0x81},
     {0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1},
     {0x09, 0x23, 0x33, 0x52, 0xf0},
     {0x15, 0x62, 0x72, 0xd1},
     {0x0a, 0x16, 0x24, 0x34},
     {0},
     {0xe1},
     {0x25, 0xf1},
     {0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43,
      0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
      0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82,
      0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
      0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
      0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5,
      0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3,
      0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}},
};

inline void writeByte(uint8_t*& p, uint8_t val) { *p++ = val; }
inline void writeWord(uint8_t*& p, uint16_t val) { *p++ = (val >> 8) & 0xff; *p++ = val & 0xff; }

} // anonymous namespace

void jpegMakeQuantTables(int quality, uint8_t* lumaQt, uint8_t* chromaQt) {
    if (quality < 1) quality = 1;
    if (quality > 99) quality = 99;

    int factor = quality;
    int q = (quality < 50) ? (5000 / factor) : (200 - factor * 2);

    for (int i = 0; i < 64; i++) {
        int lq = ((kLumaQuantizer[i] * q + 51) * 5) / 512;
        int cq = ((kChromaQuantizer[i] * q + 51) * 5) / 512;
        if (lq < 1) lq = 1; else if (lq > 255) lq = 255;
        if (cq < 1) cq = 1; else if (cq > 255) cq = 255;
        lumaQt[i] = static_cast<uint8_t>(lq);
        chromaQt[i] = static_cast<uint8_t>(cq);
    }
}

uint32_t jpegWriteHeader(uint8_t* buf, int width, int height, int quality) {
    uint8_t* p = buf;

    // SOI
    writeByte(p, 0xff);
    writeByte(p, M_SOI);

    // DQT - table 0 (luma)
    uint8_t lumaQt[64], chromaQt[64];
    jpegMakeQuantTables(quality, lumaQt, chromaQt);

    writeByte(p, 0xff);
    writeByte(p, M_DQT);
    writeWord(p, 67);  // length: 2 + 1 + 64 = 67
    writeByte(p, 0x00);  // Pq=0, Tq=0
    for (int i = 0; i < 64; i++) writeByte(p, lumaQt[i]);

    // DQT - table 1 (chroma)
    writeByte(p, 0xff);
    writeByte(p, M_DQT);
    writeWord(p, 67);
    writeByte(p, 0x01);  // Pq=0, Tq=1
    for (int i = 0; i < 64; i++) writeByte(p, chromaQt[i]);

    // DRI
    int restartInterval = width * height / 256;
    writeByte(p, 0xff);
    writeByte(p, M_DRI);
    writeWord(p, 4);  // length
    writeWord(p, static_cast<uint16_t>(restartInterval));

    // SOF0
    writeByte(p, 0xff);
    writeByte(p, M_SOF0);
    writeWord(p, 17);  // length: 2 + 1 + 2 + 2 + 1 + 3*3 = 17
    writeByte(p, 0x08);  // 8-bit precision
    writeWord(p, static_cast<uint16_t>(height));
    writeWord(p, static_cast<uint16_t>(width));
    writeByte(p, 0x03);  // 3 components
    // Y
    writeByte(p, 0x01);  // component ID
    writeByte(p, 0x22);  // H=2, V=2
    writeByte(p, 0x00);  // QT selector
    // Cb
    writeByte(p, 0x02);
    writeByte(p, 0x11);  // H=1, V=1
    writeByte(p, 0x01);
    // Cr
    writeByte(p, 0x03);
    writeByte(p, 0x11);
    writeByte(p, 0x01);

    // DHT - 4 tables (DC luma, AC luma, DC chroma, AC chroma)
    for (int j = 0; j < 4; j++) {
        writeByte(p, 0xff);
        writeByte(p, M_DHT);
        writeWord(p, static_cast<uint16_t>(kHtSize[j]));
        writeByte(p, kDhtSel[j]);
        for (int i = 0; i < 16; i++) writeByte(p, kHtLen[j][i]);
        for (int i = 0; i < 16; i++) {
            for (int m = 0; m < kHtLen[j][i]; m++) {
                writeByte(p, kHtVal[j][i][m]);
            }
        }
    }

    // SOS
    writeByte(p, 0xff);
    writeByte(p, M_SOS);
    writeWord(p, 12);  // length
    writeByte(p, 0x03);  // 3 components
    writeByte(p, 0x01);  // Cs1 (Y)
    writeByte(p, 0x00);  // Td=0, Ta=0
    writeByte(p, 0x02);  // Cs2 (Cb)
    writeByte(p, 0x11);  // Td=1, Ta=1
    writeByte(p, 0x03);  // Cs3 (Cr)
    writeByte(p, 0x11);  // Td=1, Ta=1
    writeByte(p, 0x00);  // Ss
    writeByte(p, 0x3f);  // Se
    writeByte(p, 0x00);  // Ah, Al

    return static_cast<uint32_t>(p - buf);
}

uint32_t jpegWriteHeaderToFile(FILE* file, int width, int height, int quality) {
    uint8_t buf[512];  // header is ~430 bytes
    uint32_t len = jpegWriteHeader(buf, width, height, quality);
    fwrite(buf, 1, len, file);
    return len;
}

void jpegWriteEoi(uint8_t* buf) {
    buf[0] = 0xff;
    buf[1] = M_EOI;
}

int jpegFindScanDataOffset(const uint8_t* data, int size) {
    // Search for SOS marker (0xFF 0xDA), then skip SOS header
    for (int i = 0; i < size - 1; i++) {
        if (data[i] == 0xff && data[i + 1] == M_SOS) {
            // SOS marker found at offset i
            // SOS length is at i+2..i+3
            if (i + 3 >= size) return -1;
            int sosLen = (data[i + 2] << 8) | data[i + 3];
            return i + 2 + sosLen;  // skip marker + length + payload
        }
    }
    return -1;
}

} // namespace media
