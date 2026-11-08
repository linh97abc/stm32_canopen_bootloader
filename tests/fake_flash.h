#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdint.h>

#define FAKE_FLASH_SIZE (512u * 1024u)

/** Map flash giả tại 0x08000000 và OTP tại 0x1FFF7000 (đúng địa chỉ thật). */
void fake_flash_init(void);
/** Xóa toàn bộ flash + OTP về 0xFF, bỏ mọi lỗi ECC giả. */
void fake_flash_reset(void);
/** Giả lập ô double-word bị lỗi ECC 2-bit (đọc qua pds_flash_read_dw trả false). */
void fake_flash_set_ecc(uint32_t addr);
/** Ghi thẳng 1 double-word (bỏ qua luật flash), dùng để dựng dữ liệu test. */
void fake_flash_poke(uint32_t addr, uint64_t v);
/** Ô trông trống nhưng ghi giá trị khác 0 thì lỗi (đến khi xóa page). */
void fake_flash_set_stuck(uint32_t addr);
extern unsigned g_fake_erase_count;

#endif
