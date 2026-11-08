#include "rsa2048.h"

#include <string.h>

#define N RSA2048_WORDS

/* r = a * b * R^-1 mod n (CIOS). r có thể trùng a hoặc b. */
static void mont_mul(uint32_t *r, const uint32_t *a, const uint32_t *b, const rsa2048_pubkey_t *k)
{
    uint32_t t[N + 2];
    memset(t, 0, sizeof t);
    for (unsigned i = 0; i < N; i++) {
        uint64_t c = 0;
        uint32_t bi = b[i];
        for (unsigned j = 0; j < N; j++) {
            c += (uint64_t)a[j] * bi + t[j];
            t[j] = (uint32_t)c;
            c >>= 32;
        }
        c += t[N];
        t[N] = (uint32_t)c;
        t[N + 1] = (uint32_t)(c >> 32);

        uint32_t m = t[0] * k->n0inv;
        c = (uint64_t)m * k->n[0] + t[0];
        c >>= 32;
        for (unsigned j = 1; j < N; j++) {
            c += (uint64_t)m * k->n[j] + t[j];
            t[j - 1] = (uint32_t)c;
            c >>= 32;
        }
        c += t[N];
        t[N - 1] = (uint32_t)c;
        t[N] = t[N + 1] + (uint32_t)(c >> 32);
    }
    /* t < 2n: trừ n nếu t >= n. */
    bool ge = (t[N] != 0u);
    if (!ge) {
        ge = true;
        for (int j = N - 1; j >= 0; j--) {
            if (t[j] != k->n[j]) {
                ge = t[j] > k->n[j];
                break;
            }
        }
    }
    if (ge) {
        uint64_t borrow = 0;
        for (unsigned j = 0; j < N; j++) {
            uint64_t d = (uint64_t)t[j] - k->n[j] - borrow;
            t[j] = (uint32_t)d;
            borrow = (d >> 63) & 1u;
        }
    }
    memcpy(r, t, N * sizeof(uint32_t));
}

static bool less_than_n(const uint32_t *a, const uint32_t *n)
{
    for (int j = N - 1; j >= 0; j--) {
        if (a[j] != n[j]) {
            return a[j] < n[j];
        }
    }
    return false;
}

bool rsa2048_verify_pkcs1v15_sha256(const rsa2048_pubkey_t *key, const uint8_t sig[256], const uint8_t hash[32])
{
    static const uint8_t digest_info[19] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                            0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
    uint32_t s[N], ar[N], x[N];

    for (unsigned i = 0; i < N; i++) {
        const uint8_t *p = &sig[256u - 4u * (i + 1u)];
        s[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
    if (!less_than_n(s, key->n)) {
        return false;
    }

    /* m = s^65537 mod n */
    mont_mul(ar, s, key->rr, key);     /* s*R */
    memcpy(x, ar, sizeof x);
    for (int i = 0; i < 16; i++) {
        mont_mul(x, x, x, key);         /* s^(2^i)*R */
    }
    mont_mul(x, x, s, key);             /* s^65536*R * s * R^-1 = s^65537 */

    uint8_t em[256], expect[256];
    for (unsigned i = 0; i < N; i++) {
        uint32_t w = x[N - 1u - i];
        em[4 * i] = (uint8_t)(w >> 24);
        em[4 * i + 1] = (uint8_t)(w >> 16);
        em[4 * i + 2] = (uint8_t)(w >> 8);
        em[4 * i + 3] = (uint8_t)w;
    }
    expect[0] = 0x00;
    expect[1] = 0x01;
    memset(&expect[2], 0xFF, 256u - 3u - sizeof digest_info - 32u);
    expect[256 - 32 - sizeof digest_info - 1] = 0x00;
    memcpy(&expect[256 - 32 - sizeof digest_info], digest_info, sizeof digest_info);
    memcpy(&expect[256 - 32], hash, 32);

    /* So hết 256 byte (không thoát sớm). */
    uint8_t diff = 0;
    for (unsigned i = 0; i < 256u; i++) {
        diff |= (uint8_t)(em[i] ^ expect[i]);
    }
    return diff == 0u;
}
