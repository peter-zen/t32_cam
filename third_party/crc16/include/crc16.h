#ifndef CRC16_H_
#define CRC16_H_
#include <stdint.h>
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif  
uint16_t cal_crc16(uint16_t crc, uint8_t const *buffer, size_t len);
#if defined(__cplusplus)
}
#endif
#endif /* CRC16_H_ */