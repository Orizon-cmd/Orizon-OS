/*
 * Orizon OS x86_64 - SHA-1, HMAC-SHA1, PBKDF2-HMAC-SHA1
 */

#include "../include/sha1.h"
#include "../include/string.h"

static uint32_t sha1_rol(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32U - bits));
}

static uint32_t sha1_read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void sha1_write_be32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)((value >> 16) & 0xffU);
  p[2] = (uint8_t)((value >> 8) & 0xffU);
  p[3] = (uint8_t)(value & 0xffU);
}

static void sha1_write_be64(uint8_t *p, uint64_t value) {
  for (int i = 7; i >= 0; i--) {
    p[7 - i] = (uint8_t)(value >> (i * 8));
  }
}

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t block[64]) {
  uint32_t w[80];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;

  for (int i = 0; i < 16; i++) {
    w[i] = sha1_read_be32(block + i * 4);
  }
  for (int i = 16; i < 80; i++) {
    w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];

  for (int i = 0; i < 80; i++) {
    uint32_t f;
    uint32_t k;
    uint32_t temp;

    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5a827999U;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1U;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdcU;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6U;
    }

    temp = sha1_rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = sha1_rol(b, 30);
    b = a;
    a = temp;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->state[0] = 0x67452301U;
  ctx->state[1] = 0xefcdab89U;
  ctx->state[2] = 0x98badcfeU;
  ctx->state[3] = 0x10325476U;
  ctx->state[4] = 0xc3d2e1f0U;
}

void sha1_update(sha1_ctx_t *ctx, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;

  while (len > 0) {
    size_t space = SHA1_BLOCK_SIZE - ctx->data_len;
    size_t take = len < space ? len : space;
    memcpy(ctx->data + ctx->data_len, p, take);
    ctx->data_len += take;
    p += take;
    len -= take;

    if (ctx->data_len == SHA1_BLOCK_SIZE) {
      sha1_transform(ctx, ctx->data);
      ctx->bit_len += SHA1_BLOCK_SIZE * 8U;
      ctx->data_len = 0;
    }
  }
}

void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE]) {
  uint64_t total_bits = ctx->bit_len + ctx->data_len * 8U;

  ctx->data[ctx->data_len++] = 0x80U;
  if (ctx->data_len > 56U) {
    while (ctx->data_len < SHA1_BLOCK_SIZE) {
      ctx->data[ctx->data_len++] = 0;
    }
    sha1_transform(ctx, ctx->data);
    ctx->data_len = 0;
  }
  while (ctx->data_len < 56U) {
    ctx->data[ctx->data_len++] = 0;
  }
  sha1_write_be64(ctx->data + 56U, total_bits);
  sha1_transform(ctx, ctx->data);

  for (int i = 0; i < 5; i++) {
    sha1_write_be32(digest + i * 4, ctx->state[i]);
  }
}

void hmac_sha1(const void *key, size_t key_len, const void *data,
               size_t data_len, uint8_t digest[SHA1_DIGEST_SIZE]) {
  uint8_t key_block[SHA1_BLOCK_SIZE];
  uint8_t inner_pad[SHA1_BLOCK_SIZE];
  uint8_t outer_pad[SHA1_BLOCK_SIZE];
  uint8_t key_digest[SHA1_DIGEST_SIZE];
  uint8_t inner_digest[SHA1_DIGEST_SIZE];
  sha1_ctx_t ctx;

  memset(key_block, 0, sizeof(key_block));
  if (key_len > SHA1_BLOCK_SIZE) {
    sha1_init(&ctx);
    sha1_update(&ctx, key, key_len);
    sha1_final(&ctx, key_digest);
    memcpy(key_block, key_digest, SHA1_DIGEST_SIZE);
  } else if (key && key_len > 0) {
    memcpy(key_block, key, key_len);
  }

  for (uint32_t i = 0; i < SHA1_BLOCK_SIZE; i++) {
    inner_pad[i] = key_block[i] ^ 0x36U;
    outer_pad[i] = key_block[i] ^ 0x5cU;
  }

  sha1_init(&ctx);
  sha1_update(&ctx, inner_pad, sizeof(inner_pad));
  sha1_update(&ctx, data, data_len);
  sha1_final(&ctx, inner_digest);

  sha1_init(&ctx);
  sha1_update(&ctx, outer_pad, sizeof(outer_pad));
  sha1_update(&ctx, inner_digest, sizeof(inner_digest));
  sha1_final(&ctx, digest);

  memset(key_block, 0, sizeof(key_block));
  memset(key_digest, 0, sizeof(key_digest));
  memset(inner_digest, 0, sizeof(inner_digest));
}

int pbkdf2_hmac_sha1(const void *password, size_t password_len,
                     const void *salt, size_t salt_len, uint32_t iterations,
                     uint8_t *out, size_t out_len) {
  uint8_t salt_block[128];
  uint8_t u[SHA1_DIGEST_SIZE];
  uint8_t t[SHA1_DIGEST_SIZE];
  uint32_t block_index = 1;
  size_t produced = 0;

  if (!out || out_len == 0 || !password || !salt || iterations == 0 ||
      salt_len + 4U > sizeof(salt_block)) {
    return -1;
  }

  while (produced < out_len) {
    size_t take;

    memcpy(salt_block, salt, salt_len);
    sha1_write_be32(salt_block + salt_len, block_index);
    hmac_sha1(password, password_len, salt_block, salt_len + 4U, u);
    memcpy(t, u, sizeof(t));

    for (uint32_t i = 1; i < iterations; i++) {
      hmac_sha1(password, password_len, u, sizeof(u), u);
      for (uint32_t j = 0; j < SHA1_DIGEST_SIZE; j++) {
        t[j] ^= u[j];
      }
    }

    take = out_len - produced;
    if (take > SHA1_DIGEST_SIZE) {
      take = SHA1_DIGEST_SIZE;
    }
    memcpy(out + produced, t, take);
    produced += take;
    block_index++;
  }

  memset(salt_block, 0, sizeof(salt_block));
  memset(u, 0, sizeof(u));
  memset(t, 0, sizeof(t));
  return 0;
}

int sha1_selftest(void) {
  static const uint8_t sha1_abc[SHA1_DIGEST_SIZE] = {
      0xa9U, 0x99U, 0x3eU, 0x36U, 0x47U, 0x06U, 0x81U,
      0x6aU, 0xbaU, 0x3eU, 0x25U, 0x71U, 0x78U, 0x50U,
      0xc2U, 0x6cU, 0x9cU, 0xd0U, 0xd8U, 0x9dU};
  static const uint8_t pbkdf2_password_salt_1[SHA1_DIGEST_SIZE] = {
      0x0cU, 0x60U, 0xc8U, 0x0fU, 0x96U, 0x1fU, 0x0eU,
      0x71U, 0xf3U, 0xa9U, 0xb5U, 0x24U, 0xafU, 0x60U,
      0x12U, 0x06U, 0x2fU, 0xe0U, 0x37U, 0xa6U};
  static const uint8_t pbkdf2_password_salt_2[SHA1_DIGEST_SIZE] = {
      0xeaU, 0x6cU, 0x01U, 0x4dU, 0xc7U, 0x2dU, 0x6fU,
      0x8cU, 0xcdU, 0x1eU, 0xd9U, 0x2aU, 0xceU, 0x1dU,
      0x41U, 0xf0U, 0xd8U, 0xdeU, 0x89U, 0x57U};
  uint8_t digest[SHA1_DIGEST_SIZE];
  sha1_ctx_t ctx;

  sha1_init(&ctx);
  sha1_update(&ctx, "abc", 3);
  sha1_final(&ctx, digest);
  if (memcmp(digest, sha1_abc, sizeof(digest)) != 0) {
    return -1;
  }

  if (pbkdf2_hmac_sha1("password", 8, "salt", 4, 1, digest,
                       sizeof(digest)) != 0 ||
      memcmp(digest, pbkdf2_password_salt_1, sizeof(digest)) != 0) {
    return -2;
  }

  if (pbkdf2_hmac_sha1("password", 8, "salt", 4, 2, digest,
                       sizeof(digest)) != 0 ||
      memcmp(digest, pbkdf2_password_salt_2, sizeof(digest)) != 0) {
    return -3;
  }

  memset(digest, 0, sizeof(digest));
  return 0;
}
