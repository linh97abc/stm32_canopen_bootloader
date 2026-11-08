/**
 * @file pds_app_header.h
 * @brief Định dạng header của image app (design.md mục 5).
 *
 * App khai báo header bằng macro PDS_APP_HEADER_DEFINE(), linker script của app
 * đặt section `.app_header` tại APP_START + 0x200. Tool `tools/pds_image.py`
 * điền `sig_algo`, `build_time`, `image_size`, `header_crc` lúc post-build.
 */
#ifndef PDS_APP_HEADER_H
#define PDS_APP_HEADER_H

#include <stdbool.h>
#include <stdint.h>

#include "pds/pds_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PDS_APP_HEADER_OFFSET     0x200u
#define PDS_APP_HEADER_ADDR       (PDS_APP_START + PDS_APP_HEADER_OFFSET)
#define PDS_APP_HEADER_MAGIC      0x48534450u              /* "PDSH" */
#define PDS_APP_HEADER_VERSION    1u

#define PDS_SIG_ALGO_RSA2048_PKCS1V15  0x0001u  /* RSA-2048, SHA-256, PKCS#1 v1.5 */
#define PDS_SIG_ALGO_RSA2048_PSS       0x0002u  /* giữ chỗ, bản đầu từ chối */

#define PDS_APP_VALID_FLAG        0x44494C4156505041ull    /* "APPVALID" */

#define PDS_APP_VERSION(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 24) | (((uint32_t)(minor) & 0xFFu) << 16) | ((uint32_t)(patch) & 0xFFFFu))

typedef struct __attribute__((packed)) {
    uint32_t magic;            /* 0x00 */
    uint16_t header_version;   /* 0x04 */
    uint16_t sig_algo;         /* 0x06 */
    uint32_t app_version;      /* 0x08: major<<24 | minor<<16 | patch */
    uint32_t security_version; /* 0x0C */
    uint32_t build_time;       /* 0x10: Unix time UTC */
    uint32_t image_size;       /* 0x14: bội số của 4 */
    uint32_t header_crc;       /* 0x18: CRC32 của byte 0x00–0x17 */
} pds_app_header_t;

_Static_assert(sizeof(pds_app_header_t) == 28u, "app_header_t phải 28 byte");

#define PDS_APP_HEADER_CRC_LEN    24u

/**
 * Khai báo header trong source của app (đúng 1 lần):
 *   PDS_APP_HEADER_DEFINE(PDS_APP_VERSION(1, 2, 3), 0);
 */
#define PDS_APP_HEADER_DEFINE(app_ver, sec_ver)                                    \
    __attribute__((section(".app_header"), used))                                  \
    const pds_app_header_t pds_app_header = {                                      \
        .magic = PDS_APP_HEADER_MAGIC,                                             \
        .header_version = PDS_APP_HEADER_VERSION,                                  \
        .sig_algo = 0xFFFFu,  /* tool ký điền */                                   \
        .app_version = (app_ver),                                                  \
        .security_version = (sec_ver),                                             \
        .build_time = 0u,     /* tool điền nếu = 0 */                              \
        .image_size = 0u,     /* tool điền */                                      \
        .header_crc = 0u,     /* tool điền */                                      \
    }

/** Độ dài chữ ký theo sig_algo, 0 nếu bootloader không hỗ trợ. */
static inline uint32_t pds_sig_len(uint16_t sig_algo)
{
    return (sig_algo == PDS_SIG_ALGO_RSA2048_PKCS1V15) ? 256u : 0u;
}

/** Địa chỉ valid flag: double-word đầu tiên sau image_crc. */
static inline uint32_t pds_app_flag_addr(uint32_t image_size)
{
    return (PDS_APP_START + image_size + 4u + 7u) & ~7u;
}

typedef enum {
    PDS_HDR_OK = 0,
    PDS_HDR_BAD_MAGIC,
    PDS_HDR_BAD_VERSION,
    PDS_HDR_BAD_CRC,
    PDS_HDR_BAD_SIG_ALGO,
    PDS_HDR_BAD_SIZE,
    PDS_HDR_ROLLBACK,      /* security_version < SEC_MIN */
} pds_hdr_result_t;

/**
 * Kiểm tra header (magic, version, header_crc, sig_algo, image_size,
 * security_version >= sec_min). Header phải đã được copy vào RAM.
 * @param app_end  địa chỉ kết thúc vùng app
 */
pds_hdr_result_t pds_app_header_check(const pds_app_header_t *hdr, uint32_t app_end, uint32_t sec_min);

/** Header của app đang nằm trong flash (không kiểm tra hợp lệ). */
static inline const pds_app_header_t *pds_app_header_in_flash(void)
{
    return (const pds_app_header_t *)PDS_APP_HEADER_ADDR;
}

#ifdef __cplusplus
}
#endif

#endif /* PDS_APP_HEADER_H */
