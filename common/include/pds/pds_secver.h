/**
 * @file pds_secver.h
 * @brief Bộ đếm chống rollback SEC_MIN lưu trong OTP (design.md mục 5.6).
 *
 * 32 entry × 8 byte tại 0x1FFF7000–0x1FFF70FF. Entry = [ver][~ver].
 * App có thể đọc SEC_MIN (để chẩn đoán); chỉ bootloader được nâng mức.
 */
#ifndef PDS_SECVER_H
#define PDS_SECVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PDS_OTP_SECVER_BASE     0x1FFF7000UL
#define PDS_OTP_SECVER_ENTRIES  32u

typedef enum {
    PDS_SECVER_OK = 0,
    PDS_SECVER_UNCHANGED,  /* ver <= SEC_MIN, không cần ghi */
    PDS_SECVER_FULL,       /* hết entry, không nâng được nữa */
    PDS_SECVER_ERR_FLASH,
} pds_secver_status_t;

/** SEC_MIN = giá trị lớn nhất trong các entry hợp lệ, 0 nếu chưa có. */
uint32_t pds_secver_min(void);

/** Ghi entry mới nếu ver > SEC_MIN (chỉ bootloader gọi). */
pds_secver_status_t pds_secver_raise(uint32_t ver);

#ifdef __cplusplus
}
#endif

#endif /* PDS_SECVER_H */
