/*
 * Orizon OS x86_64 - SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1
 */

#ifndef _SHA1_H
#define _SHA1_H

#include "types.h"

#define SHA1_DIGEST_SIZE 20U
#define SHA1_BLOCK_SIZE 64U

typedef struct {
  uint32_t state[5];
  uint64_t bit_len;
  uint8_t data[SHA1_BLOCK_SIZE];
  size_t data_len;
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *ctx);
void sha1_update(sha1_ctx_t *ctx, const void *data, size_t len);
void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE]);
void hmac_sha1(const void *key, size_t key_len, const void *data,
               size_t data_len, uint8_t digest[SHA1_DIGEST_SIZE]);
int pbkdf2_hmac_sha1(const void *password, size_t password_len,
                     const void *salt, size_t salt_len, uint32_t iterations,
                     uint8_t *out, size_t out_len);
int sha1_selftest(void);

#endif /* _SHA1_H */
