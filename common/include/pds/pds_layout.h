/**
 * @file pds_layout.h
 * @brief Phân vùng flash dùng chung cho bootloader và app (design.md mục 2).
 *
 * File này chỉ chứa macro nên được include được cả từ C lẫn linker script
 * (linker script được tiền xử lý bằng `gcc -E -P -x c -DPDS_LINKER_SCRIPT`).
 *
 * Dòng chip được chọn qua define CMSIS chuẩn (STM32G431xx, STM32G474xx...).
 */
#ifndef PDS_LAYOUT_H
#define PDS_LAYOUT_H

#ifdef PDS_LINKER_SCRIPT
#define PDS_U32(x) x
#else
#define PDS_U32(x) x##UL
#endif

#define PDS_FLASH_BASE         PDS_U32(0x08000000)

#if defined(STM32G431xx) || defined(STM32G441xx) || defined(STM32G491xx) || defined(STM32G4A1xx)
/* Category 2 / 4: luôn single-bank, page 2 KB. */
#define PDS_FLASH_PAGE_SIZE    PDS_U32(0x800)
#define PDS_CONFIG_START       PDS_U32(0x08006000)
#define PDS_APP_START          PDS_U32(0x08006800)
#define PDS_LAYOUT_NEEDS_SINGLE_BANK 0
#elif defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || \
      defined(STM32G483xx) || defined(STM32G484xx)
/* Category 3: bắt buộc option byte DBANK = 0 (single-bank, page 4 KB). */
#define PDS_FLASH_PAGE_SIZE    PDS_U32(0x1000)
#define PDS_CONFIG_START       PDS_U32(0x08006000)
#define PDS_APP_START          PDS_U32(0x08007000)
#define PDS_LAYOUT_NEEDS_SINGLE_BANK 1
#else
#error "pds_layout.h: chưa định nghĩa dòng chip STM32G4 (vd. -DSTM32G474xx)"
#endif

#define PDS_BOOTLOADER_START   PDS_FLASH_BASE
#define PDS_BOOTLOADER_SIZE    PDS_U32(0x6000)          /* 24 KB */
#define PDS_CONFIG_SIZE        PDS_FLASH_PAGE_SIZE       /* 1 page */

/* RAM mà bootloader dùng (SRAM1, nhỏ nhất trên G431 là 16 KB). */
#define PDS_BL_RAM_START       PDS_U32(0x20000000)
#define PDS_BL_RAM_SIZE        PDS_U32(0x4000)

#ifndef PDS_LINKER_SCRIPT

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

_Static_assert(PDS_CONFIG_START == PDS_BOOTLOADER_START + PDS_BOOTLOADER_SIZE,
               "config phải nằm ngay sau bootloader");
_Static_assert(PDS_APP_START == PDS_CONFIG_START + PDS_CONFIG_SIZE,
               "app phải nằm ngay sau config");
_Static_assert((PDS_APP_START % PDS_FLASH_PAGE_SIZE) == 0u, "APP_START phải căn page");

/** Địa chỉ ngay sau byte flash cuối cùng (đọc từ thanh ghi FLASHSIZE). */
uint32_t pds_layout_flash_end(void);

/** Địa chỉ kết thúc vùng app (= cuối flash). */
static inline uint32_t pds_layout_app_end(void) { return pds_layout_flash_end(); }

/**
 * Kiểm tra cấu hình chip có khớp phân vùng không.
 * Với G47x/G48x: trả về false nếu option byte DBANK = 1 (dual-bank).
 * Khi trả về false, tuyệt đối không được xóa/ghi flash theo bảng phân vùng này.
 */
bool pds_layout_check(void);

#ifdef __cplusplus
}
#endif

#endif /* !PDS_LINKER_SCRIPT */

#endif /* PDS_LAYOUT_H */
