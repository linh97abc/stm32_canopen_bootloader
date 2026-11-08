#include "pds/pds_app_header.h"

#include "pds/pds_crc.h"

pds_hdr_result_t pds_app_header_check(const pds_app_header_t *hdr, uint32_t app_end, uint32_t sec_min)
{
    if (hdr->magic != PDS_APP_HEADER_MAGIC) {
        return PDS_HDR_BAD_MAGIC;
    }
    if (hdr->header_version != PDS_APP_HEADER_VERSION) {
        return PDS_HDR_BAD_VERSION;
    }
    if (pds_crc32(hdr, PDS_APP_HEADER_CRC_LEN) != hdr->header_crc) {
        return PDS_HDR_BAD_CRC;
    }
    if (pds_sig_len(hdr->sig_algo) == 0u) {
        return PDS_HDR_BAD_SIG_ALGO;
    }
    uint32_t size = hdr->image_size;
    uint32_t cap = app_end - PDS_APP_START;
    if ((size & 3u) != 0u || size < PDS_APP_HEADER_OFFSET + sizeof(pds_app_header_t) || size > cap ||
        pds_app_flag_addr(size) + 8u > app_end) {
        return PDS_HDR_BAD_SIZE;
    }
    if (hdr->security_version == 0xFFFFFFFFu) {
        return PDS_HDR_BAD_VERSION; /* không biểu diễn được trong OTP */
    }
    if (hdr->security_version < sec_min) {
        return PDS_HDR_ROLLBACK;
    }
    return PDS_HDR_OK;
}
