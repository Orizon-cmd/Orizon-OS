/*
 * Orizon OS x86_64 - SHA-256 implementation
 */

#include "../include/sha256.h"
#include "../include/string.h"

static const uint32_t k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32U - n));
}

static uint32_t read_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static void write_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)((v >> 16) & 0xff);
  p[2] = (uint8_t)((v >> 8) & 0xff);
  p[3] = (uint8_t)(v & 0xff);
}

static void write_be64(uint8_t *p, uint64_t v) {
  for (int i = 7; i >= 0; i--) {
    p[7 - i] = (uint8_t)(v >> (i * 8));
  }
}

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[64]) {
  uint32_t m[64];
  uint32_t a, b, c, d, e, f, g, h;

  for (int i = 0; i < 16; i++) {
    m[i] = read_be32(data + i * 4);
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (int i = 0; i < 64; i++) {
    uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + s1 + ch + k[i] + m[i];
    uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->state[0] = 0x6a09e667U;
  ctx->state[1] = 0xbb67ae85U;
  ctx->state[2] = 0x3c6ef372U;
  ctx->state[3] = 0xa54ff53aU;
  ctx->state[4] = 0x510e527fU;
  ctx->state[5] = 0x9b05688cU;
  ctx->state[6] = 0x1f83d9abU;
  ctx->state[7] = 0x5be0cd19U;
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len > 0) {
    size_t space = 64 - ctx->data_len;
    size_t take = len < space ? len : space;
    memcpy(ctx->data + ctx->data_len, p, take);
    ctx->data_len += take;
    p += take;
    len -= take;

    if (ctx->data_len == 64) {
      sha256_transform(ctx, ctx->data);
      ctx->bit_len += 512;
      ctx->data_len = 0;
    }
  }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
  uint64_t total_bits = ctx->bit_len + ctx->data_len * 8;

  ctx->data[ctx->data_len++] = 0x80;
  if (ctx->data_len > 56) {
    while (ctx->data_len < 64) {
      ctx->data[ctx->data_len++] = 0;
    }
    sha256_transform(ctx, ctx->data);
    ctx->data_len = 0;
  }
  while (ctx->data_len < 56) {
    ctx->data[ctx->data_len++] = 0;
  }
  write_be64(ctx->data + 56, total_bits);
  sha256_transform(ctx, ctx->data);

  for (int i = 0; i < 8; i++) {
    write_be32(digest + i * 4, ctx->state[i]);
  }
}

void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out[SHA256_HEX_SIZE]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0f];
  }
  out[SHA256_HEX_SIZE - 1] = '\0';
}

void sha256_buffer(const void *data, size_t len,
                   uint8_t digest[SHA256_DIGEST_SIZE]) {
  sha256_ctx_t ctx;

  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, digest);
}

void sha256_buffer_hex(const void *data, size_t len, char out[SHA256_HEX_SIZE]) {
  uint8_t digest[SHA256_DIGEST_SIZE];

  sha256_buffer(data, len, digest);
  sha256_hex(digest, out);
}

void hmac_sha256(const void *key, size_t key_len, const void *data,
                 size_t data_len, uint8_t digest[SHA256_DIGEST_SIZE]) {
  uint8_t key_block[64];
  uint8_t inner_pad[64];
  uint8_t outer_pad[64];
  uint8_t inner_digest[SHA256_DIGEST_SIZE];
  sha256_ctx_t ctx;

  memset(key_block, 0, sizeof(key_block));
  if (key_len > sizeof(key_block)) {
    sha256_buffer(key, key_len, key_block);
  } else if (key && key_len > 0) {
    memcpy(key_block, key, key_len);
  }

  for (size_t i = 0; i < sizeof(key_block); i++) {
    inner_pad[i] = key_block[i] ^ 0x36U;
    outer_pad[i] = key_block[i] ^ 0x5cU;
  }

  sha256_init(&ctx);
  sha256_update(&ctx, inner_pad, sizeof(inner_pad));
  sha256_update(&ctx, data, data_len);
  sha256_final(&ctx, inner_digest);

  sha256_init(&ctx);
  sha256_update(&ctx, outer_pad, sizeof(outer_pad));
  sha256_update(&ctx, inner_digest, sizeof(inner_digest));
  sha256_final(&ctx, digest);
}
