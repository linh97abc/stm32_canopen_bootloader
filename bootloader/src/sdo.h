#ifndef BL_SDO_H
#define BL_SDO_H

#include <stdint.h>

#include "fdcan.h"

/** Số segment tối đa mỗi block (127 × 7 = 889 byte buffer RAM). */
#define SDO_BLKSIZE 127u

/** Timeout phía server giữa hai frame của một transfer. */
#define SDO_TIMEOUT_MS 2000u

void sdo_init(uint8_t node_id);
void sdo_handle(const can_frame_t *rx);
void sdo_tick(uint32_t now_ms);
/** Hủy transfer đang chạy mà không gửi abort (NMT reset/stop). */
void sdo_reset(void);

#endif
