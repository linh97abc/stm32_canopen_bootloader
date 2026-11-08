#ifndef BL_BOOT_JUMP_H
#define BL_BOOT_JUMP_H

#include <stdbool.h>

/** SP ban đầu nằm trong SRAM, reset vector là địa chỉ Thumb trong vùng app. */
bool boot_vectors_ok(void);

/** Nhảy sang app. Chỉ trả về nếu vector table không hợp lệ. */
void boot_jump_to_app(void);

#endif
