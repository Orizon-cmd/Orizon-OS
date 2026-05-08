/*
 * Orizon OS x86_64 - AES-128-GCM implementation
 *
 * Small freestanding implementation for the TLS 1.2 updater path. It is not
 * constant-time yet, so it is a protocol bring-up block rather than hardened
 * production crypto.
 */

#include "../include/aes_gcm.h"
#include "../include/string.h"

#define AES_BLOCK_SIZE 16
#define AES128_ROUND_KEYS 176

static const uint8_t aes_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

static const uint8_t aes_rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                     0x20, 0x40, 0x80, 0x1b, 0x36};

static uint8_t xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static void aes_key_expand(const uint8_t key[16],
                           uint8_t round_keys[AES128_ROUND_KEYS]) {
  memcpy(round_keys, key, 16);

  uint8_t temp[4];
  size_t generated = 16;
  size_t rcon = 0;
  while (generated < AES128_ROUND_KEYS) {
    for (int i = 0; i < 4; i++) {
      temp[i] = round_keys[generated - 4 + i];
    }
    if ((generated % 16) == 0) {
      uint8_t t = temp[0];
      temp[0] = aes_sbox[temp[1]] ^ aes_rcon[rcon++];
      temp[1] = aes_sbox[temp[2]];
      temp[2] = aes_sbox[temp[3]];
      temp[3] = aes_sbox[t];
    }
    for (int i = 0; i < 4; i++) {
      round_keys[generated] = round_keys[generated - 16] ^ temp[i];
      generated++;
    }
  }
}

static void aes_add_round_key(uint8_t state[16], const uint8_t *round_key) {
  for (int i = 0; i < 16; i++) {
    state[i] ^= round_key[i];
  }
}

static void aes_sub_bytes(uint8_t state[16]) {
  for (int i = 0; i < 16; i++) {
    state[i] = aes_sbox[state[i]];
  }
}

static void aes_shift_rows(uint8_t state[16]) {
  uint8_t t;
  t = state[1];
  state[1] = state[5];
  state[5] = state[9];
  state[9] = state[13];
  state[13] = t;

  t = state[2];
  state[2] = state[10];
  state[10] = t;
  t = state[6];
  state[6] = state[14];
  state[14] = t;

  t = state[15];
  state[15] = state[11];
  state[11] = state[7];
  state[7] = state[3];
  state[3] = t;
}

static void aes_mix_columns(uint8_t state[16]) {
  for (int c = 0; c < 4; c++) {
    uint8_t *col = state + c * 4;
    uint8_t a0 = col[0];
    uint8_t a1 = col[1];
    uint8_t a2 = col[2];
    uint8_t a3 = col[3];
    uint8_t t = a0 ^ a1 ^ a2 ^ a3;
    col[0] ^= t ^ xtime(a0 ^ a1);
    col[1] ^= t ^ xtime(a1 ^ a2);
    col[2] ^= t ^ xtime(a2 ^ a3);
    col[3] ^= t ^ xtime(a3 ^ a0);
  }
}

static void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16],
                                 uint8_t out[16]) {
  uint8_t round_keys[AES128_ROUND_KEYS];
  uint8_t state[16];

  aes_key_expand(key, round_keys);
  memcpy(state, in, 16);
  aes_add_round_key(state, round_keys);
  for (int round = 1; round < 10; round++) {
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_mix_columns(state);
    aes_add_round_key(state, round_keys + round * 16);
  }
  aes_sub_bytes(state);
  aes_shift_rows(state);
  aes_add_round_key(state, round_keys + 160);
  memcpy(out, state, 16);
}

static void xor_block(uint8_t out[16], const uint8_t in[16]) {
  for (int i = 0; i < 16; i++) {
    out[i] ^= in[i];
  }
}

static void shift_right_one(uint8_t v[16]) {
  uint8_t carry = 0;
  for (int i = 0; i < 16; i++) {
    uint8_t next_carry = (uint8_t)(v[i] & 1);
    v[i] = (uint8_t)((v[i] >> 1) | (carry << 7));
    carry = next_carry;
  }
}

static void gcm_multiply(const uint8_t x[16], const uint8_t h[16],
                         uint8_t out[16]) {
  uint8_t z[16] = {0};
  uint8_t v[16];
  memcpy(v, h, 16);

  for (int bit = 0; bit < 128; bit++) {
    if (x[bit / 8] & (uint8_t)(0x80 >> (bit % 8))) {
      xor_block(z, v);
    }
    uint8_t lsb = v[15] & 1;
    shift_right_one(v);
    if (lsb) {
      v[0] ^= 0xe1;
    }
  }
  memcpy(out, z, 16);
}

static void ghash_block(uint8_t y[16], const uint8_t h[16],
                        const uint8_t block[16]) {
  xor_block(y, block);
  gcm_multiply(y, h, y);
}

static void put_be64(uint8_t *p, uint64_t v) {
  for (int i = 7; i >= 0; i--) {
    p[7 - i] = (uint8_t)(v >> (i * 8));
  }
}

static void ghash(const uint8_t h[16], const uint8_t *aad, size_t aad_len,
                  const uint8_t *ciphertext, size_t ciphertext_len,
                  uint8_t out[16]) {
  uint8_t y[16] = {0};
  uint8_t block[16];
  size_t off = 0;

  while (off < aad_len) {
    size_t take = aad_len - off;
    if (take > 16) {
      take = 16;
    }
    memset(block, 0, sizeof(block));
    memcpy(block, aad + off, take);
    ghash_block(y, h, block);
    off += take;
  }

  off = 0;
  while (off < ciphertext_len) {
    size_t take = ciphertext_len - off;
    if (take > 16) {
      take = 16;
    }
    memset(block, 0, sizeof(block));
    memcpy(block, ciphertext + off, take);
    ghash_block(y, h, block);
    off += take;
  }

  memset(block, 0, sizeof(block));
  put_be64(block, (uint64_t)aad_len * 8ULL);
  put_be64(block + 8, (uint64_t)ciphertext_len * 8ULL);
  ghash_block(y, h, block);
  memcpy(out, y, 16);
}

static void increment_counter(uint8_t counter[16]) {
  for (int i = 15; i >= 12; i--) {
    counter[i]++;
    if (counter[i] != 0) {
      break;
    }
  }
}

static void aes_ctr_crypt(const uint8_t key[16], const uint8_t icb[16],
                          const uint8_t *in, size_t len, uint8_t *out) {
  uint8_t counter[16];
  uint8_t pad[16];
  size_t off = 0;

  memcpy(counter, icb, 16);
  while (off < len) {
    size_t take = len - off;
    if (take > 16) {
      take = 16;
    }
    aes128_encrypt_block(key, counter, pad);
    for (size_t i = 0; i < take; i++) {
      out[off + i] = in[off + i] ^ pad[i];
    }
    off += take;
    increment_counter(counter);
  }
}

int aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *plaintext, size_t plaintext_len,
                       uint8_t *ciphertext, uint8_t tag[16]) {
  uint8_t h[16] = {0};
  uint8_t j0[16];
  uint8_t ctr[16];
  uint8_t s[16];
  uint8_t tag_mask[16];

  if (!key || !nonce || (!aad && aad_len > 0) ||
      (!plaintext && plaintext_len > 0) || !ciphertext || !tag) {
    return -1;
  }

  aes128_encrypt_block(key, h, h);
  memcpy(j0, nonce, 12);
  j0[12] = 0;
  j0[13] = 0;
  j0[14] = 0;
  j0[15] = 1;
  memcpy(ctr, j0, 16);
  increment_counter(ctr);
  aes_ctr_crypt(key, ctr, plaintext, plaintext_len, ciphertext);
  ghash(h, aad, aad_len, ciphertext, plaintext_len, s);
  aes128_encrypt_block(key, j0, tag_mask);
  for (int i = 0; i < 16; i++) {
    tag[i] = tag_mask[i] ^ s[i];
  }
  return 0;
}

int aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *ciphertext, size_t ciphertext_len,
                       const uint8_t tag[16], uint8_t *plaintext) {
  uint8_t h[16] = {0};
  uint8_t j0[16];
  uint8_t ctr[16];
  uint8_t s[16];
  uint8_t tag_mask[16];
  uint8_t expected[16];
  uint8_t diff = 0;

  if (!key || !nonce || (!aad && aad_len > 0) ||
      (!ciphertext && ciphertext_len > 0) || !tag || !plaintext) {
    return -1;
  }

  aes128_encrypt_block(key, h, h);
  memcpy(j0, nonce, 12);
  j0[12] = 0;
  j0[13] = 0;
  j0[14] = 0;
  j0[15] = 1;
  ghash(h, aad, aad_len, ciphertext, ciphertext_len, s);
  aes128_encrypt_block(key, j0, tag_mask);
  for (int i = 0; i < 16; i++) {
    expected[i] = tag_mask[i] ^ s[i];
    diff |= expected[i] ^ tag[i];
  }
  if (diff != 0) {
    return -2;
  }

  memcpy(ctr, j0, 16);
  increment_counter(ctr);
  aes_ctr_crypt(key, ctr, ciphertext, ciphertext_len, plaintext);
  return 0;
}
