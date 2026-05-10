/*
 * Orizon OS x86_64 - SHA-256
 */

#ifndef _SHA256_H
#define _SHA256_H

#include "types.h"

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65

typedef struct {
  uint32_t state[8];
  uint64_t bit_len;
  uint8_t data[64];
  size_t data_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[SHA256_HEX_SIZE]);
void sha256_buffer(const void *data, size_t len,
                   uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_buffer_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]);
void hmac_sha256(const void *key, size_t key_len, const void *data,
                 size_t data_len, uint8_t digest[SHA256_DIGEST_SIZE]);

#endif /* _SHA256_H */
