/**
 * Cửa sổ cứu node lúc boot (docs/design.md mục 3.1).
 *
 * Trước khi nhảy sang app hợp lệ, bootloader nghe CAN trong T ms. Bắt được
 * lệnh Stop (0x1F51:01 = 0) gửi tới NodeID của mình thì ở lại bootloader.
 * Phần này không đụng phần cứng (chỉ dùng fdcan_rx, clock_ms), để test được
 * trên PC. Bật clock/FDCAN và dọn dẹp nằm ở main.c.
 */
#ifndef BL_RESCUE_H
#define BL_RESCUE_H

#include <stdbool.h>
#include <stdint.h>

#include "fdcan.h"

/** T_min (ms), cố định lúc build. 0 = tắt cửa sổ. */
#ifndef BL_RESCUE_WINDOW_MIN_MS
#define BL_RESCUE_WINDOW_MIN_MS 20u
#endif

/** Số bit của frame SDO dài nhất (8 byte data, ID 11 bit), đã tính bit stuffing. */
#define BL_RESCUE_FRAME_BITS    135u
/** T tối thiểu tính theo số frame: T >= 6 × t_frame. */
#define BL_RESCUE_FRAMES        6u

/** T = max(T_min, 6 × t_frame) ở bitrate cho trước. 0 nếu cửa sổ bị tắt. */
uint32_t rescue_window_ms(uint32_t bitrate);

/**
 * true nếu f là SDO expedited download ghi 0x1F51:01 = 0 tới node_id,
 * byte lệnh 0x2F hoặc 0x22: đúng các lệnh Stop mà SDO server trả OK.
 */
bool rescue_is_stop(const can_frame_t *f, uint8_t node_id);

/**
 * Nghe trong window_ms kể từ lúc gọi (FDCAN đã chạy). Frame khác Stop bị bỏ
 * qua và không kéo dài cửa sổ. true và *stop = frame bắt được nếu có Stop.
 */
bool rescue_wait(uint8_t node_id, uint32_t window_ms, can_frame_t *stop);

#endif
