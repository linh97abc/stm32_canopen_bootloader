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

/* Giá trị đọc 0x1F57:01 (flash status, CiA 302-3): bit 0 = đang nạp,
 * bit 1..7 = mã lỗi (0 = không lỗi). */
#define PROG_FLASH_IN_PROGRESS     0x01u
#define PROG_FLASH_ERR_NO_PROGRAM  1u   /* không có app hợp lệ */
#define PROG_FLASH_ERR_FORMAT      3u   /* header/độ dài/CRC/chữ ký sai */
#define PROG_FLASH_ERR_WRITE       5u   /* xóa/ghi flash lỗi */
#define PROG_FLASH_ERR_ADDRESS     6u   /* image vượt vùng app */

void program_init(bool layout_ok, bool app_valid, uint32_t sec_min);

bool program_app_valid(void);
uint8_t program_status(void);
uint32_t program_flash_status(void);

/** Ghi 0x1F51:01. Start thành công -> *reset_after = true (reset sau khi gửi response). */
uint32_t program_control(uint8_t cmd, bool *reset_after);

/* Ghi stream vào 0x1F50:01. Không có app hợp lệ thì begin tự Clear nếu cần
 * (đọc 0x1F51 = 3 nghĩa là nạp được ngay, đúng mô hình CiA 302-3). */
uint32_t program_dl_begin(bool size_known, uint32_t size);
uint32_t program_dl_data(const uint8_t *data, uint32_t len);
uint32_t program_dl_end(void);
void program_dl_abort(void);

#endif
