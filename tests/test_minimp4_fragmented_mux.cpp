#include "minimp4.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef TEST_PROJECT_ROOT
#define TEST_PROJECT_ROOT "."
#endif

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        exit(1);
    }
}

static int write_callback(int64_t offset, const void* buffer, size_t size, void* token) {
    FILE* file = static_cast<FILE*>(token);
    if (fseek(file, offset, SEEK_SET) != 0) {
        return 1;
    }
    return fwrite(buffer, 1, size, file) != size;
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.is_open(), "failed to open input file");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static size_t find_annexb_start(const std::vector<uint8_t>& data, size_t from) {
    for (size_t i = from; i + 3 < data.size(); ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                return i;
            }
            if (i + 4 < data.size() && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                return i;
            }
        }
    }
    return data.size();
}

static uint64_t read_be32(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint64_t>(data[offset]) << 24) |
           (static_cast<uint64_t>(data[offset + 1]) << 16) |
           (static_cast<uint64_t>(data[offset + 2]) << 8) |
           static_cast<uint64_t>(data[offset + 3]);
}

static uint64_t read_be64(const std::vector<uint8_t>& data, size_t offset) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value = (value << 8) | data[offset + i];
    }
    return value;
}

struct BoxInfo {
    std::string type;
    size_t offset;
};

static std::vector<BoxInfo> read_top_level_boxes(const std::vector<uint8_t>& data) {
    std::vector<BoxInfo> boxes;
    size_t offset = 0;
    while (offset + 8 <= data.size()) {
        uint64_t boxSize = read_be32(data, offset);
        std::string type(reinterpret_cast<const char*>(data.data() + offset + 4), 4);
        size_t headerSize = 8;
        if (boxSize == 1) {
            require(offset + 16 <= data.size(), "invalid extended MP4 box");
            boxSize = read_be64(data, offset + 8);
            headerSize = 16;
        } else if (boxSize == 0) {
            boxSize = data.size() - offset;
        }

        require(boxSize >= headerSize, "invalid MP4 box size");
        require(offset + boxSize <= data.size(), "MP4 box exceeds file size");

        boxes.push_back({type, offset});
        offset += static_cast<size_t>(boxSize);
    }
    return boxes;
}

static int find_box(const std::vector<BoxInfo>& boxes, const std::string& type) {
    for (size_t i = 0; i < boxes.size(); ++i) {
        if (boxes[i].type == type) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int main() {
    std::cout << "[Test] minimp4 fragmented mux..." << std::endl;

    const std::string inputPath = std::string(TEST_PROJECT_ROOT) + "/third_party/minimp4/vectors/foreman.264";
    const std::vector<uint8_t> annexb = read_file(inputPath);
    require(!annexb.empty(), "empty H264 fixture");

    char outputTemplate[] = "/tmp/t32_minimp4_fragmented_XXXXXX";
    int fd = mkstemp(outputTemplate);
    require(fd >= 0, "failed to create temp output");
    FILE* output = fdopen(fd, "wb+");
    require(output != nullptr, "failed to open temp output");

    MP4E_mux_t* muxer = MP4E_open(0, 1, output, write_callback);
    require(muxer != nullptr, "MP4E_open fragmented mode failed");

    mp4_h26x_writer_t writer;
    require(mp4_h26x_write_init(&writer, muxer, 352, 288, 0) == MP4E_STATUS_OK,
            "mp4_h26x_write_init failed");

    const unsigned frameDuration90k = 90000 / 30;
    size_t start = find_annexb_start(annexb, 0);
    int nalCount = 0;
    while (start < annexb.size() && nalCount < 180) {
        const size_t next = find_annexb_start(annexb, start + 3);
        const size_t nalSize = (next == annexb.size()) ? annexb.size() - start : next - start;
        if (nalSize > 4) {
            const int ret = mp4_h26x_write_nal(&writer, annexb.data() + start, static_cast<int>(nalSize), frameDuration90k);
            require(ret == MP4E_STATUS_OK, "mp4_h26x_write_nal failed");
            nalCount++;
        }
        start = next;
    }
    require(nalCount > 0, "no NAL units written");

    MP4E_close(muxer);
    mp4_h26x_write_close(&writer);
    fflush(output);
    fclose(output);

    const std::vector<uint8_t> mp4 = read_file(outputTemplate);
    unlink(outputTemplate);
    require(mp4.size() > 1024, "fragmented MP4 output too small");

    const std::vector<BoxInfo> boxes = read_top_level_boxes(mp4);
    const int ftyp = find_box(boxes, "ftyp");
    const int moov = find_box(boxes, "moov");
    const int moof = find_box(boxes, "moof");
    const int mdat = find_box(boxes, "mdat");

    require(ftyp >= 0, "missing ftyp box");
    require(moov >= 0, "missing initial moov box");
    require(moof >= 0, "missing moof box");
    require(mdat >= 0, "missing mdat box");
    require(ftyp < moov && moov < moof && moof < mdat, "unexpected fragmented MP4 box order");

    std::cout << "[PASS]" << std::endl;
    return 0;
}
