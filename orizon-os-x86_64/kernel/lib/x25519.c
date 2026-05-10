/*
 * Orizon OS x86_64 - X25519 key agreement
 *
 * Small freestanding implementation used by network protocols while Orizon
 * grows its crypto layer. It mirrors the existing TLS diagnostic path but is
 * kept in lib/ so SSH can use it without reaching into netstack internals.
 */

#include "../include/x25519.h"
#include "../include/string.h"

#define FE51_MASK ((uint64_t)((1ULL << 51) - 1ULL))

typedef uint64_t fe51[5];

static uint64_t load_le64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) {
    v = (v << 8) | p[i];
  }
  return v;
}

static void store_le64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    p[i] = (uint8_t)(v >> (8 * i));
  }
}

static void fe51_reduce(fe51 h) {
  for (int round = 0; round < 2; round++) {
    uint64_t c;
    c = h[0] >> 51;
    h[0] &= FE51_MASK;
    h[1] += c;
    c = h[1] >> 51;
    h[1] &= FE51_MASK;
    h[2] += c;
    c = h[2] >> 51;
    h[2] &= FE51_MASK;
    h[3] += c;
    c = h[3] >> 51;
    h[3] &= FE51_MASK;
    h[4] += c;
    c = h[4] >> 51;
    h[4] &= FE51_MASK;
    h[0] += c * 19;
  }
}

static void fe51_copy(fe51 out, const fe51 in) {
  for (int i = 0; i < 5; i++) {
    out[i] = in[i];
  }
}

static void fe51_1(fe51 out) {
  out[0] = 1;
  out[1] = 0;
  out[2] = 0;
  out[3] = 0;
  out[4] = 0;
}

static void fe51_0(fe51 out) {
  memset(out, 0, sizeof(fe51));
}

static void fe51_frombytes(fe51 out, const uint8_t in[32]) {
  out[0] = load_le64(in) & FE51_MASK;
  out[1] = (load_le64(in + 6) >> 3) & FE51_MASK;
  out[2] = (load_le64(in + 12) >> 6) & FE51_MASK;
  out[3] = (load_le64(in + 19) >> 1) & FE51_MASK;
  out[4] = (load_le64(in + 24) >> 12) & FE51_MASK;
}

static int fe51_ge_p(const fe51 h) {
  static const uint64_t p[5] = {FE51_MASK - 18, FE51_MASK, FE51_MASK,
                                FE51_MASK, FE51_MASK};
  for (int i = 4; i >= 0; i--) {
    if (h[i] > p[i]) {
      return 1;
    }
    if (h[i] < p[i]) {
      return 0;
    }
  }
  return 1;
}

static void fe51_sub_p(fe51 h) {
  static const uint64_t p[5] = {FE51_MASK - 18, FE51_MASK, FE51_MASK,
                                FE51_MASK, FE51_MASK};
  uint64_t borrow = 0;
  for (int i = 0; i < 5; i++) {
    uint64_t sub = p[i] + borrow;
    uint64_t old = h[i];
    h[i] = old - sub;
    borrow = old < sub;
  }
}

static void fe51_tobytes(uint8_t out[32], const fe51 in) {
  fe51 h;
  fe51_copy(h, in);
  fe51_reduce(h);
  if (fe51_ge_p(h)) {
    fe51_sub_p(h);
  }

  store_le64(out, h[0] | (h[1] << 51));
  store_le64(out + 8, (h[1] >> 13) | (h[2] << 38));
  store_le64(out + 16, (h[2] >> 26) | (h[3] << 25));
  store_le64(out + 24, (h[3] >> 39) | (h[4] << 12));
  out[31] &= 0x7f;
}

static void fe51_add(fe51 out, const fe51 a, const fe51 b) {
  for (int i = 0; i < 5; i++) {
    out[i] = a[i] + b[i];
  }
  fe51_reduce(out);
}

static void fe51_sub(fe51 out, const fe51 a, const fe51 b) {
  static const uint64_t p4[5] = {(FE51_MASK - 18) * 4, FE51_MASK * 4,
                                 FE51_MASK * 4, FE51_MASK * 4,
                                 FE51_MASK * 4};
  for (int i = 0; i < 5; i++) {
    out[i] = a[i] + p4[i] - b[i];
  }
  fe51_reduce(out);
}

static void fe51_mul(fe51 out, const fe51 f, const fe51 g) {
  unsigned __int128 h0 =
      (unsigned __int128)f[0] * g[0] +
      (unsigned __int128)19 * (f[1] * (unsigned __int128)g[4] +
                               f[2] * (unsigned __int128)g[3] +
                               f[3] * (unsigned __int128)g[2] +
                               f[4] * (unsigned __int128)g[1]);
  unsigned __int128 h1 =
      (unsigned __int128)f[0] * g[1] + (unsigned __int128)f[1] * g[0] +
      (unsigned __int128)19 * (f[2] * (unsigned __int128)g[4] +
                               f[3] * (unsigned __int128)g[3] +
                               f[4] * (unsigned __int128)g[2]);
  unsigned __int128 h2 =
      (unsigned __int128)f[0] * g[2] + (unsigned __int128)f[1] * g[1] +
      (unsigned __int128)f[2] * g[0] +
      (unsigned __int128)19 * (f[3] * (unsigned __int128)g[4] +
                               f[4] * (unsigned __int128)g[3]);
  unsigned __int128 h3 =
      (unsigned __int128)f[0] * g[3] + (unsigned __int128)f[1] * g[2] +
      (unsigned __int128)f[2] * g[1] + (unsigned __int128)f[3] * g[0] +
      (unsigned __int128)19 * f[4] * g[4];
  unsigned __int128 h4 =
      (unsigned __int128)f[0] * g[4] + (unsigned __int128)f[1] * g[3] +
      (unsigned __int128)f[2] * g[2] + (unsigned __int128)f[3] * g[1] +
      (unsigned __int128)f[4] * g[0];

  out[0] = (uint64_t)h0 & FE51_MASK;
  h1 += h0 >> 51;
  out[1] = (uint64_t)h1 & FE51_MASK;
  h2 += h1 >> 51;
  out[2] = (uint64_t)h2 & FE51_MASK;
  h3 += h2 >> 51;
  out[3] = (uint64_t)h3 & FE51_MASK;
  h4 += h3 >> 51;
  out[4] = (uint64_t)h4 & FE51_MASK;
  out[0] += (uint64_t)(h4 >> 51) * 19;
  fe51_reduce(out);
}

static void fe51_sq(fe51 out, const fe51 a) {
  fe51_mul(out, a, a);
}

static int exponent_pminus2_bit(int bit) {
  if (bit == 2 || bit == 4 || bit >= 255) {
    return 0;
  }
  return 1;
}

static void fe51_inv(fe51 out, const fe51 z) {
  fe51 result;
  fe51 base;
  fe51 scratch;
  fe51_1(result);
  fe51_copy(base, z);
  for (int bit = 254; bit >= 0; bit--) {
    fe51_sq(scratch, result);
    fe51_copy(result, scratch);
    if (exponent_pminus2_bit(bit)) {
      fe51_mul(scratch, result, base);
      fe51_copy(result, scratch);
    }
  }
  fe51_copy(out, result);
}

static void fe51_cswap(fe51 a, fe51 b, uint8_t swap) {
  uint64_t mask = 0 - (uint64_t)swap;
  for (int i = 0; i < 5; i++) {
    uint64_t t = mask & (a[i] ^ b[i]);
    a[i] ^= t;
    b[i] ^= t;
  }
}

void x25519_clamp_private(uint8_t scalar[X25519_KEY_SIZE]) {
  scalar[0] &= 248;
  scalar[31] &= 127;
  scalar[31] |= 64;
}

void x25519_shared_secret(uint8_t out[X25519_KEY_SIZE],
                          const uint8_t scalar_in[X25519_KEY_SIZE],
                          const uint8_t point_in[X25519_KEY_SIZE]) {
  static const fe51 a24 = {121665, 0, 0, 0, 0};
  uint8_t scalar[X25519_KEY_SIZE];
  uint8_t point[X25519_KEY_SIZE];
  fe51 x1, x2, z2, x3, z3;
  fe51 a, aa, b, bb, e, c, d, da, cb, tmp0, tmp1;
  uint8_t swap = 0;

  memcpy(scalar, scalar_in, sizeof(scalar));
  memcpy(point, point_in, sizeof(point));
  x25519_clamp_private(scalar);
  point[31] &= 0x7f;

  fe51_frombytes(x1, point);
  fe51_1(x2);
  fe51_0(z2);
  fe51_copy(x3, x1);
  fe51_1(z3);

  for (int t = 254; t >= 0; t--) {
    uint8_t kt = (uint8_t)((scalar[t >> 3] >> (t & 7)) & 1);
    swap ^= kt;
    fe51_cswap(x2, x3, swap);
    fe51_cswap(z2, z3, swap);
    swap = kt;

    fe51_add(a, x2, z2);
    fe51_sq(aa, a);
    fe51_sub(b, x2, z2);
    fe51_sq(bb, b);
    fe51_sub(e, aa, bb);
    fe51_add(c, x3, z3);
    fe51_sub(d, x3, z3);
    fe51_mul(da, d, a);
    fe51_mul(cb, c, b);
    fe51_add(tmp0, da, cb);
    fe51_sq(x3, tmp0);
    fe51_sub(tmp0, da, cb);
    fe51_sq(tmp1, tmp0);
    fe51_mul(z3, tmp1, x1);
    fe51_mul(x2, aa, bb);
    fe51_mul(tmp0, e, a24);
    fe51_add(tmp0, aa, tmp0);
    fe51_mul(z2, e, tmp0);
  }

  fe51_cswap(x2, x3, swap);
  fe51_cswap(z2, z3, swap);
  fe51_inv(z2, z2);
  fe51_mul(x2, x2, z2);
  fe51_tobytes(out, x2);
}

void x25519_public_from_private(uint8_t out[X25519_KEY_SIZE],
                                const uint8_t scalar[X25519_KEY_SIZE]) {
  static const uint8_t basepoint[X25519_KEY_SIZE] = {9};
  x25519_shared_secret(out, scalar, basepoint);
}
