#ifndef BL_IMAGE_H
#define BL_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "pds/pds_app_header.h"

typedef enum {
    IMG_OK = 0,
    IMG_ECC_ERROR,      /* đọc flash gặp lỗi ECC 2-bit */
    IMG_BAD_HEADER,     /* magic / version / header_crc / sig_algo / size */
    IMG_ROLLBACK,       /* security_version < SEC_MIN */
    IMG_BAD_LENGTH,     /* số byte nhận được không khớp header */
    IMG_BAD_CRC,
    IMG_BAD_SIGNATURE,
    IMG_NOT_VALID,      /* thiếu valid flag */
    IMG_BAD_VECTORS,    /* SP/reset vector không hợp lệ */
} img_result_t;

/** Mỗi lần boot: bước 1–3 mục 5.5 + valid flag + vector table. Không verify chữ ký. */
img_result_t image_check_boot(uint32_t sec_min);

/**
 * Sau khi nạp (Start program): bước 1–4 mục 5.5. Chữ ký tính trên dữ liệu đọc
 * lại từ flash. hdr_out nhận header đã kiểm tra.
 */
img_result_t image_verify_download(uint32_t received, const uint8_t *sig, uint32_t sec_min,
                                   pds_app_header_t *hdr_out);

/** Ghi valid flag tại FLAG_ADDR (bước 5). */
bool image_write_flag(const pds_app_header_t *hdr);

#endif
