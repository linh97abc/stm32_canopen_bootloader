#ifndef BL_RSA2048_H
#define BL_RSA2048_H

#include <stdbool.h>
#include <stdint.h>

#define RSA2048_WORDS 64u

/**
 * Public key RSA-2048, e = 65537 cố định. Limb little-endian (n[0] = word thấp).
 * rr = R^2 mod n (R = 2^2048), n0inv = -n^-1 mod 2^32. Tool tính sẵn.
 */
typedef struct {
    uint32_t n[RSA2048_WORDS];
    uint32_t rr[RSA2048_WORDS];
    uint32_t n0inv;
} rsa2048_pubkey_t;

/**
 * Verify RSASSA-PKCS1-v1_5 với SHA-256. So nguyên khối 256 byte với chuỗi
 * EM tự dựng (không parse padding/ASN.1 từ chữ ký).
 */
bool rsa2048_verify_pkcs1v15_sha256(const rsa2048_pubkey_t *key, const uint8_t sig[256], const uint8_t hash[32]);

#endif
