#include "image.h"

#include "boot_jump.h"

#include <string.h>

#include "clock.h"
#include "crypto/sha256.h"
#include "blc/blc_crc.h"
#include "blc/blc_flash.h"
#include "blc/blc_layout.h"
#include "pubkey.h"

static img_result_t load_header(blc_app_header_t *hdr, uint32_t sec_min)
{
    uint32_t ecc = blc_flash_ecc_events();
    memcpy(hdr, (const void *)BLC_APP_HEADER_ADDR, sizeof *hdr);
    if (blc_flash_ecc_events() != ecc) {
        return IMG_ECC_ERROR;
    }
    switch (blc_app_header_check(hdr, blc_layout_app_end(), sec_min)) {
    case BLC_HDR_OK:
        return IMG_OK;
    case BLC_HDR_ROLLBACK:
        return IMG_ROLLBACK;
    default:
        return IMG_BAD_HEADER;
    }
}

static img_result_t check_crc(const blc_app_header_t *hdr)
{
    uint32_t ecc = blc_flash_ecc_events();
    uint32_t crc = blc_crc32((const void *)BLC_APP_START, hdr->image_size);
    uint32_t stored = *(const volatile uint32_t *)(BLC_APP_START + hdr->image_size);
    if (blc_flash_ecc_events() != ecc) {
        return IMG_ECC_ERROR;
    }
    return (crc == stored) ? IMG_OK : IMG_BAD_CRC;
}

img_result_t image_check_boot(uint32_t sec_min)
{
    blc_app_header_t hdr;
    img_result_t r = load_header(&hdr, sec_min);
    if (r != IMG_OK) {
        return r;
    }
    /* Flag trước CRC: chỉ đọc 8 byte, image chưa có flag bị loại mà không cần quét cả image. */
    uint64_t flag;
    if (!blc_flash_read_dw(blc_app_flag_addr(hdr.image_size), &flag)) {
        return IMG_ECC_ERROR;
    }
    if (flag != BLC_APP_VALID_FLAG) {
        return IMG_NOT_VALID;
    }
    if ((r = check_crc(&hdr)) != IMG_OK) {
        return r;
    }
    return boot_vectors_ok() ? IMG_OK : IMG_BAD_VECTORS;
}

img_result_t image_verify_download(uint32_t received, const uint8_t *sig, uint32_t sec_min,
                                   blc_app_header_t *hdr_out)
{
    img_result_t r = load_header(hdr_out, sec_min);
    if (r != IMG_OK) {
        return r;
    }
    uint32_t size = hdr_out->image_size;
    if (received != size + 4u + blc_sig_len(hdr_out->sig_algo)) {
        return IMG_BAD_LENGTH;
    }
    if ((r = check_crc(hdr_out)) != IMG_OK) {
        return r;
    }
    if (!boot_vectors_ok()) {
        return IMG_BAD_VECTORS;
    }

    /* SHA-256 trên [APP_START, APP_START + image_size + 4), đọc lại từ flash. */
    uint32_t ecc = blc_flash_ecc_events();
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    const uint8_t *p = (const uint8_t *)BLC_APP_START;
    uint32_t left = size + 4u;
    while (left != 0u) {
        uint32_t n = left > 4096u ? 4096u : left;
        sha256_update(&ctx, p, n);
        p += n;
        left -= n;
        watchdog_kick();
    }
    uint8_t hash[32];
    sha256_final(&ctx, hash);
    if (blc_flash_ecc_events() != ecc) {
        return IMG_ECC_ERROR;
    }

    switch (hdr_out->sig_algo) {
    case BLC_SIG_ALGO_RSA2048_PKCS1V15:
        return rsa2048_verify_pkcs1v15_sha256(&g_bl_pubkey, sig, hash) ? IMG_OK : IMG_BAD_SIGNATURE;
    default:
        return IMG_BAD_SIGNATURE;
    }
}

bool image_write_flag(const blc_app_header_t *hdr)
{
    uint32_t addr = blc_app_flag_addr(hdr->image_size);
    uint64_t v;
    return blc_flash_program_dw(addr, BLC_APP_VALID_FLAG) && blc_flash_read_dw(addr, &v) &&
           v == BLC_APP_VALID_FLAG;
}
