#include <sstream>
#include <fstream>
#include <vector>
#include "crc16.h"
#include "CRC.h"

bool CRC::calculate_crc16(const std::string &file_path, uint16_t &crc16)
{
    // 2026-06-10 修复 zram 风暴: 之前的实现用 std::istreambuf_iterator 一路读到底
    //   = malloc(整个文件大小) + 拷贝整个文件到堆。32 MB mp4 文件会一次性分配
    //   32 MB 匿名内存,叠加业务层开销在 64 MB T32 上直接撞穿 zram (htc_main_app
    //   baseline 6 条 zram error) 或触发 OOM kill (desc_info + I2C 卡死场景)。
    //
    // 修复: 流式 CRC, 每次读 64 KB chunk, 内存占用从 32 MB 降到 64 KB (500x 减少)。
    //   CRC16 算法本身按块累加, 分块计算结果跟整文件一次性计算结果完全一致。
    //   64 KB chunk 大小是 page cache 友好值 (16 个 4 KB page), 性能几乎无差异。
    //
    // 验证: 详见 doc/knowledge/bugs/T32-zram-storm-root-cause-2026-06-10.md
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    constexpr size_t kChunkSize = 64 * 1024;  // 64 KB
    std::vector<unsigned char> buffer(kChunkSize);
    uint16_t crc = 0xFFFF;
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), kChunkSize);
        std::streamsize n = file.gcount();
        if (n <= 0) break;
        crc = cal_crc16(crc, buffer.data(), static_cast<size_t>(n));
    }
    crc16 = crc;
    return true;
}