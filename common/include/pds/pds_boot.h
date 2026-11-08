/**
 * @file pds_boot.h
 * @brief Chuyển từ App sang Bootloader qua backup register (design.md mục 3).
 *
 * App: khi nhận Stop program (0x1F51:01 = 0), gửi xong SDO response rồi gọi
 *      pds_boot_enter_bootloader().
 * Bootloader: gọi pds_boot_take_request() lúc khởi động.
 */
#ifndef PDS_BOOT_H
#define PDS_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chỉ số backup register TAMP_BKPxR dùng cho magic. App không được dùng thanh ghi này. */
#ifndef PDS_BOOT_BKP_INDEX
#define PDS_BOOT_BKP_INDEX     0u
#endif

#define PDS_BOOT_MAGIC_STAY_IN_BL  0x51524C42UL   /* "BLRQ" */

/** Ghi magic vào backup register rồi reset. Không trả về. */
__attribute__((noreturn)) void pds_boot_enter_bootloader(void);

/** Bootloader: true nếu có magic (đồng thời xóa magic). */
bool pds_boot_take_request(void);

#ifdef __cplusplus
}
#endif

#endif /* PDS_BOOT_H */
