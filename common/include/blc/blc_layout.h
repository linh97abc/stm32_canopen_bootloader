/**
 * @file blc_layout.h
 * @brief Phân vùng flash dùng chung cho bootloader và app (design.md mục 2).
 *
 * File này chỉ chứa macro nên được include được cả từ C lẫn linker script
 * (linker script được tiền xử lý bằng `gcc -E -P -x c -DBLC_LINKER_SCRIPT`).
 *
 * Dòng chip được chọn qua define CMSIS chuẩn (STM32G431xx, STM32G474xx...).
 */
#ifndef BLC_LAYOUT_H
#define BLC_LAYOUT_H

#ifdef BLC_LINKER_SCRIPT
#define BLC_U32(x) x
#else
#define BLC_U32(x) x##UL
#endif

#define BLC_FLASH_BASE         BLC_U32(0x08000000)

#if defined(STM32G431xx) || defined(STM32G441xx) || defined(STM32G491xx) || defined(STM32G4A1xx)
/* Category 2 / 4: luôn single-bank, page 2 KB. */
#define BLC_FLASH_PAGE_SIZE    BLC_U32(0x800)
#define BLC_CONFIG_START       BLC_U32(0x08005000)
#define BLC_APP_START          BLC_U32(0x08005800)
#define BLC_LAYOUT_NEEDS_SINGLE_BANK 0
#elif defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || \
      defined(STM32G483xx) || defined(STM32G484xx)
/* Category 3: bắt buộc option byte DBANK = 0 (single-bank, page 4 KB). */
#define BLC_FLASH_PAGE_SIZE    BLC_U32(0x1000)
#define BLC_CONFIG_START       BLC_U32(0x08005000)
#define BLC_APP_START          BLC_U32(0x08006000)
#define BLC_LAYOUT_NEEDS_SINGLE_BANK 1
#else
#error "blc_layout.h: chưa định nghĩa dòng chip STM32G4 (vd. -DSTM32G474xx)"
#endif

#define BLC_BOOTLOADER_START   BLC_FLASH_BASE
#define BLC_BOOTLOADER_SIZE    BLC_U32(0x5000)          /* 20 KB */
#define BLC_CONFIG_SIZE        BLC_FLASH_PAGE_SIZE       /* 1 page */

/* RAM mà bootloader dùng (SRAM1, nhỏ nhất trên G431 là 16 KB). */
#define BLC_BL_RAM_START       BLC_U32(0x20000000)
#define BLC_BL_RAM_SIZE        BLC_U32(0x4000)

#ifndef BLC_LINKER_SCRIPT

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

_Static_assert(BLC_CONFIG_START == BLC_BOOTLOADER_START + BLC_BOOTLOADER_SIZE,
               "config phải nằm ngay sau bootloader");
_Static_assert(BLC_APP_START == BLC_CONFIG_START + BLC_CONFIG_SIZE,
               "app phải nằm ngay sau config");
_Static_assert((BLC_APP_START % BLC_FLASH_PAGE_SIZE) == 0u, "APP_START phải căn page");

/** Địa chỉ ngay sau byte flash cuối cùng (đọc từ thanh ghi FLASHSIZE). */
uint32_t blc_layout_flash_end(void);

/** Địa chỉ kết thúc vùng app (= cuối flash). */
static inline uint32_t blc_layout_app_end(void) { return blc_layout_flash_end(); }

/**
 * Kiểm tra cấu hình chip có khớp phân vùng không.
 * Với G47x/G48x: trả về false nếu option byte DBANK = 1 (dual-bank).
 * Khi trả về false, tuyệt đối không được xóa/ghi flash theo bảng phân vùng này.
 */
bool blc_layout_check(void);

#ifdef __cplusplus
}
#endif

#endif /* !BLC_LINKER_SCRIPT */

#endif /* BLC_LAYOUT_H */
