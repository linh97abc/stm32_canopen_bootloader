#ifndef BL_SHA256_H
#define BL_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t total;
    uint8_t buf[64];
    uint32_t buf_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *c);
void sha256_update(sha256_ctx_t *c, const void *data, size_t len);
void sha256_final(sha256_ctx_t *c, uint8_t out[32]);

#endif
