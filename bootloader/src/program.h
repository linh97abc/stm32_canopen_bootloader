/**
 * Program download theo CiA 302-3: 0x1F50:01 (program data), 0x1F51:01 (control).
 * Các hàm trả về mã SDO abort (SDO_OK = thành công).
 */
#ifndef BL_PROGRAM_H
#define BL_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>

#define PROG_CTRL_STOP   0u
#define PROG_CTRL_START  1u
#define PROG_CTRL_CLEAR  3u

/* Giá trị đọc 0x1F51:01 */
#define PROG_STATUS_STOPPED    0u   /* có app hợp lệ, đang đứng ở bootloader */
#define PROG_STATUS_NO_PROGRAM 3u   /* không có app hợp lệ */

void program_init(bool layout_ok, bool app_valid, uint32_t sec_min);

bool program_app_valid(void);
uint8_t program_status(void);

/** Ghi 0x1F51:01. Start thành công -> *reset_after = true (reset sau khi gửi response). */
uint32_t program_control(uint8_t cmd, bool *reset_after);

/* Ghi stream vào 0x1F50:01 */
uint32_t program_dl_begin(bool size_known, uint32_t size);
uint32_t program_dl_data(const uint8_t *data, uint32_t len);
uint32_t program_dl_end(void);
void program_dl_abort(void);

#endif
