/** Object dictionary tối thiểu của bootloader (design.md mục 4.1). */
#ifndef BL_OD_H
#define BL_OD_H

#include <stdbool.h>
#include <stdint.h>

/** Đọc (expedited, <= 4 byte). Trả về mã abort. */
uint32_t od_read(uint16_t index, uint8_t sub, uint8_t buf[4], uint8_t *len);

/* Ghi qua "sink": dùng chung cho expedited, segmented và block download.
 * size_known = false: không biết trước độ dài (expedited với s = 0). */
uint32_t od_write_begin(uint16_t index, uint8_t sub, bool size_known, uint32_t size);
uint32_t od_write_data(const uint8_t *data, uint32_t len);
uint32_t od_write_end(void);
void od_write_abort(void);

/* Biến của OD mà tầng CANopen dùng. */
extern uint16_t g_od_heartbeat_ms;   /* 0x1017 */
extern bool g_od_reset_request;      /* set khi Start program thành công */

#endif
