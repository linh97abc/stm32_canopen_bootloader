/**
 * @file blc_flash.h
 * @brief Driver flash nội STM32G4 tối giản: xóa page, ghi double-word,
 *        đọc có kiểm tra lỗi ECC 2-bit.
 *
 * Đọc một ô flash bị lỗi ECC 2-bit sinh NMI (FLASH_ECCR.ECCD). Firmware
 * (app và bootloader) BẮT BUỘC gọi blc_flash_nmi_handler() trong NMI_Handler:
 *
 *   void NMI_Handler(void) {
 *       if (!blc_flash_nmi_handler()) { ... NMI khác: xử lý/ reset ... }
 *   }
 */
#ifndef BLC_FLASH_H
#define BLC_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Đọc 1 double-word (addr căn 8). Trả về false nếu gặp lỗi ECC 2-bit. */
bool blc_flash_read_dw(uint32_t addr, uint64_t *out);

/** Ghi 1 double-word (addr căn 8, ô phải đang trống hoặc value == 0). */
bool blc_flash_program_dw(uint32_t addr, uint64_t value);

/** Xóa page chứa địa chỉ addr (chế độ single-bank). */
bool blc_flash_erase_page(uint32_t addr);

/**
 * Đảm bảo chip luôn boot từ flash chính, không đọc chân BOOT0 (PB8) lúc reset:
 * option byte nSWBOOT0 = 0, nBOOT0 = 1. Cần khi PB8 dùng làm CAN RX (bus rảnh
 * ở mức 1 -> BOOT0 = 1 -> chip vào ROM bootloader của ST).
 *
 * Đã đúng: trả về true ngay (chỉ đọc FLASH_OPTR). Chưa đúng: ghi option byte,
 * giữ nguyên các bit khác, rồi OBL_LAUNCH (reset để nạp option byte, không
 * quay lại). Trả về false nếu ghi lỗi (vd. RDP level 2), hoặc nếu lần reset
 * trước đã là OBL reset mà giá trị vẫn sai (tránh reset lặp vô hạn).
 */
bool blc_flash_ob_ignore_boot0_pin(void);

/**
 * Bộ đếm số lần lỗi ECC 2-bit đã gặp (tăng trong NMI). Dùng để kiểm tra
 * một đoạn đọc trực tiếp bằng con trỏ: lưu giá trị trước, so lại sau.
 */
uint32_t blc_flash_ecc_events(void);

/**
 * Gọi từ NMI_Handler. Trả về true nếu NMI do lỗi ECC flash (đã được xử lý,
 * có thể return), false nếu là nguồn NMI khác.
 */
bool blc_flash_nmi_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BLC_FLASH_H */
