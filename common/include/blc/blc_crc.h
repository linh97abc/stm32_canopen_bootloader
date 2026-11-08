/**
 * @file blc_crc.h
 * @brief CRC dùng chung.
 *
 * - CRC-32: giống zlib (poly 0x04C11DB7, init 0xFFFFFFFF, reflect in/out,
 *   xorout 0xFFFFFFFF). Trên chip dùng khối CRC phần cứng; build với
 *   BLC_CRC32_SOFTWARE để dùng bản phần mềm (test trên PC).
 * - CRC-8: CRC-8/I-432-1 (poly 0x07, init 0x00, không reflect, xorout 0x55),
 *   dùng cho bản ghi config. xorout 0x55 khiến bản ghi toàn 0x00 không bao giờ
 *   có CRC đúng.
 */
#ifndef BLC_CRC_H
#define BLC_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CRC-32 (zlib). Bản phần cứng cấu hình lại khối CRC mỗi lần gọi, nên không
 * được gọi đồng thời từ ngắt nếu app cũng dùng khối CRC.
 */
uint32_t blc_crc32(const void *data, size_t len);

/** CRC-8/I-432-1. */
uint8_t blc_crc8(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BLC_CRC_H */
