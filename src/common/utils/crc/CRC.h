#ifndef CRC_H
#define CRC_H

#include <string>
#include <cstdint>

class CRC {
public:
    static bool calculate_crc16(const std::string &file_path, uint16_t &crc16);
};

#endif // CRC_H