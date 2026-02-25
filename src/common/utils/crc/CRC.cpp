#include <sstream>
#include <fstream>
#include <vector>
#include "crc16.h"
#include "CRC.h"

bool CRC::calculate_crc16(const std::string &file_path, uint16_t &crc16)
{
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::vector<unsigned char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    crc16 = cal_crc16(0xFFFF, buffer.data(), buffer.size());

    return true;
}