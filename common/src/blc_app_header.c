#include "blc/blc_app_header.h"

#include "blc/blc_crc.h"

blc_hdr_result_t blc_app_header_check(const blc_app_header_t *hdr, uint32_t app_end, uint32_t sec_min)
{
    if (hdr->magic != BLC_APP_HEADER_MAGIC) {
        return BLC_HDR_BAD_MAGIC;
    }
    if (hdr->header_version != BLC_APP_HEADER_VERSION) {
        return BLC_HDR_BAD_VERSION;
    }
    if (blc_crc32(hdr, BLC_APP_HEADER_CRC_LEN) != hdr->header_crc) {
        return BLC_HDR_BAD_CRC;
    }
    if (blc_sig_len(hdr->sig_algo) == 0u) {
        return BLC_HDR_BAD_SIG_ALGO;
    }
    uint32_t size = hdr->image_size;
    uint32_t cap = app_end - BLC_APP_START;
    if ((size & 3u) != 0u || size < BLC_APP_HEADER_OFFSET + sizeof(blc_app_header_t) || size > cap ||
        blc_app_flag_addr(size) + 8u > app_end) {
        return BLC_HDR_BAD_SIZE;
    }
    if (hdr->security_version == 0xFFFFFFFFu) {
        return BLC_HDR_BAD_VERSION; /* không biểu diễn được trong OTP */
    }
    if (hdr->security_version < sec_min) {
        return BLC_HDR_ROLLBACK;
    }
    return BLC_HDR_OK;
}
