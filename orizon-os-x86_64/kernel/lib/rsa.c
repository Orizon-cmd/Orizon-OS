/*
 * Orizon OS x86_64 - tiny big-endian RSA/PKCS#1 v1.5 signer.
 *
 * This is intentionally narrow: it only signs SHA-256 digests for the staged
 * SSH host-key exchange. Wider key formats should grow here, not in ssh.c.
 */

#include "../include/rsa.h"
#include "../include/string.h"

static int rsa_ge_bytes(const uint8_t *a, const uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (a[i] > b[i]) {
      return 1;
    }
    if (a[i] < b[i]) {
      return 0;
    }
  }
  return 1;
}

static void rsa_sub_inplace(uint8_t *a, const uint8_t *b, size_t len) {
  uint16_t borrow = 0;

  for (size_t i = len; i > 0; i--) {
    uint16_t av = a[i - 1];
    uint16_t bv = (uint16_t)b[i - 1] + borrow;
    if (av < bv) {
      a[i - 1] = (uint8_t)(av + 256U - bv);
      borrow = 1;
    } else {
      a[i - 1] = (uint8_t)(av - bv);
      borrow = 0;
    }
  }
}

static int rsa_ge_ext(const uint8_t *tmp, const uint8_t *mod, size_t len) {
  if (tmp[0] != 0) {
    return 1;
  }
  return rsa_ge_bytes(tmp + 1, mod, len);
}

static void rsa_sub_ext(uint8_t *tmp, const uint8_t *mod, size_t len) {
  uint16_t borrow = 0;

  for (size_t i = len + 1; i > 1; i--) {
    uint16_t av = tmp[i - 1];
    uint16_t bv = (uint16_t)mod[i - 2] + borrow;
    if (av < bv) {
      tmp[i - 1] = (uint8_t)(av + 256U - bv);
      borrow = 1;
    } else {
      tmp[i - 1] = (uint8_t)(av - bv);
      borrow = 0;
    }
  }
  if (borrow) {
    tmp[0]--;
  }
}

static void rsa_add_mod(uint8_t *out, const uint8_t *a, const uint8_t *b,
                        const uint8_t *mod, size_t len) {
  uint8_t tmp[RSA_MAX_BYTES + 1];
  uint16_t carry = 0;

  memset(tmp, 0, sizeof(tmp));
  for (size_t i = len; i > 0; i--) {
    uint16_t sum = (uint16_t)a[i - 1] + b[i - 1] + carry;
    tmp[i] = (uint8_t)sum;
    carry = sum >> 8;
  }
  tmp[0] = (uint8_t)carry;

  if (rsa_ge_ext(tmp, mod, len)) {
    rsa_sub_ext(tmp, mod, len);
  }
  memcpy(out, tmp + 1, len);
}

static void rsa_double_mod(uint8_t *out, const uint8_t *a,
                           const uint8_t *mod, size_t len) {
  rsa_add_mod(out, a, a, mod, len);
}

static void rsa_mul_mod(uint8_t *out, const uint8_t *a, const uint8_t *b,
                        const uint8_t *mod, size_t len) {
  uint8_t result[RSA_MAX_BYTES];
  uint8_t temp[RSA_MAX_BYTES];

  memset(result, 0, len);
  memcpy(temp, a, len);
  if (rsa_ge_bytes(temp, mod, len)) {
    rsa_sub_inplace(temp, mod, len);
  }

  for (size_t i = len; i > 0; i--) {
    uint8_t byte = b[i - 1];
    for (int bit = 0; bit < 8; bit++) {
      if (byte & (uint8_t)(1U << bit)) {
        rsa_add_mod(result, result, temp, mod, len);
      }
      rsa_double_mod(temp, temp, mod, len);
    }
  }
  memcpy(out, result, len);
}

static void rsa_add_one_mod(uint8_t *out, const uint8_t *a,
                            const uint8_t *mod, size_t len) {
  uint8_t one[RSA_MAX_BYTES];

  memset(one, 0, len);
  one[len - 1] = 1;
  rsa_add_mod(out, a, one, mod, len);
}

static void rsa_reduce(uint8_t *out, const uint8_t *in, size_t in_len,
                       const uint8_t *mod, size_t mod_len) {
  uint8_t tmp[RSA_MAX_BYTES];

  memset(out, 0, mod_len);
  for (size_t i = 0; i < in_len; i++) {
    uint8_t byte = in[i];
    for (int bit = 7; bit >= 0; bit--) {
      rsa_double_mod(tmp, out, mod, mod_len);
      memcpy(out, tmp, mod_len);
      if (byte & (uint8_t)(1U << bit)) {
        rsa_add_one_mod(tmp, out, mod, mod_len);
        memcpy(out, tmp, mod_len);
      }
    }
  }
}

static int rsa_modexp(uint8_t *out, const uint8_t *base, size_t base_len,
                      const uint8_t *exp, size_t exp_len, const uint8_t *mod,
                      size_t mod_len) {
  uint8_t b[RSA_MAX_BYTES];
  uint8_t result[RSA_MAX_BYTES];
  uint8_t scratch[RSA_MAX_BYTES];

  if (!out || !base || !exp || !mod || mod_len == 0 ||
      mod_len > RSA_MAX_BYTES || exp_len > RSA_MAX_BYTES) {
    return -1;
  }

  rsa_reduce(b, base, base_len, mod, mod_len);
  memset(result, 0, mod_len);
  result[mod_len - 1] = 1;

  for (size_t i = 0; i < exp_len; i++) {
    uint8_t byte = exp[i];
    for (int bit = 7; bit >= 0; bit--) {
      rsa_mul_mod(scratch, result, result, mod, mod_len);
      memcpy(result, scratch, mod_len);
      if (byte & (uint8_t)(1U << bit)) {
        rsa_mul_mod(scratch, result, b, mod, mod_len);
        memcpy(result, scratch, mod_len);
      }
    }
  }
  memcpy(out, result, mod_len);
  return 0;
}

static int rsa_modexp65537(uint8_t *out, const uint8_t *base, size_t base_len,
                           const uint8_t *mod, size_t mod_len) {
  static const uint8_t exp65537[3] = {0x01, 0x00, 0x01};

  return rsa_modexp(out, base, base_len, exp65537, sizeof(exp65537), mod,
                    mod_len);
}

static int rsa_encode_pkcs1_sha256(uint8_t *out, size_t out_len,
                                   const uint8_t digest[SHA256_DIGEST_SIZE]) {
  static const uint8_t sha256_digest_info_prefix[] = {
      0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
      0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
  size_t tail_len = sizeof(sha256_digest_info_prefix) + SHA256_DIGEST_SIZE;
  size_t ps_len;
  size_t off;

  if (!out || !digest || out_len < 3 + 8 + tail_len ||
      out_len > RSA_MAX_BYTES) {
    return -1;
  }

  ps_len = out_len - 3 - tail_len;
  off = 0;
  out[off++] = 0x00;
  out[off++] = 0x01;
  memset(out + off, 0xff, ps_len);
  off += ps_len;
  out[off++] = 0x00;
  memcpy(out + off, sha256_digest_info_prefix,
         sizeof(sha256_digest_info_prefix));
  off += sizeof(sha256_digest_info_prefix);
  memcpy(out + off, digest, SHA256_DIGEST_SIZE);
  return 0;
}

int rsa_pkcs1v15_sha256_verify(const uint8_t *signature, size_t signature_len,
                               const uint8_t digest[SHA256_DIGEST_SIZE],
                               const uint8_t *n, size_t n_len) {
  uint8_t decoded[RSA_MAX_BYTES];
  uint8_t expected[RSA_MAX_BYTES];
  unsigned char diff = 0;

  if (!signature || !digest || !n || signature_len == 0 ||
      signature_len != n_len || n_len > RSA_MAX_BYTES) {
    return -1;
  }
  if (rsa_modexp65537(decoded, signature, signature_len, n, n_len) != 0 ||
      rsa_encode_pkcs1_sha256(expected, n_len, digest) != 0) {
    return -1;
  }
  for (size_t i = 0; i < n_len; i++) {
    diff |= decoded[i] ^ expected[i];
  }
  return diff == 0 ? 0 : -1;
}

static void rsa_mod_sub(uint8_t *out, const uint8_t *a, const uint8_t *b,
                        const uint8_t *mod, size_t len) {
  uint8_t tmp[RSA_MAX_BYTES];

  if (rsa_ge_bytes(a, b, len)) {
    memcpy(out, a, len);
    rsa_sub_inplace(out, b, len);
    return;
  }

  memcpy(tmp, b, len);
  rsa_sub_inplace(tmp, a, len);
  memcpy(out, mod, len);
  rsa_sub_inplace(out, tmp, len);
}

static void rsa_plain_mul(uint8_t *out, size_t out_len, const uint8_t *a,
                          size_t a_len, const uint8_t *b, size_t b_len) {
  memset(out, 0, out_len);

  for (size_t i = a_len; i > 0; i--) {
    uint32_t carry = 0;
    for (size_t j = b_len; j > 0; j--) {
      size_t k = i + j - 1;
      uint32_t product = (uint32_t)out[k] + (uint32_t)a[i - 1] * b[j - 1] +
                         carry;
      out[k] = (uint8_t)product;
      carry = product >> 8;
    }

    size_t k = i - 1;
    while (carry > 0 && k < out_len) {
      uint32_t sum = (uint32_t)out[k] + carry;
      out[k] = (uint8_t)sum;
      carry = sum >> 8;
      if (k == 0) {
        break;
      }
      k--;
    }
  }
}

static void rsa_add_right_aligned(uint8_t *out, size_t out_len,
                                  const uint8_t *in, size_t in_len) {
  uint16_t carry = 0;
  size_t i = out_len;
  size_t j = in_len;

  while (i > 0) {
    uint16_t sum = out[i - 1] + carry;
    if (j > 0) {
      sum += in[j - 1];
      j--;
    }
    out[i - 1] = (uint8_t)sum;
    carry = sum >> 8;
    i--;
  }
}

static int rsa_is_zero(const uint8_t *a, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (a[i] != 0) {
      return 0;
    }
  }
  return 1;
}

static int rsa_is_one(const uint8_t *a, size_t len) {
  if (len == 0 || a[len - 1] != 1) {
    return 0;
  }
  for (size_t i = 0; i + 1 < len; i++) {
    if (a[i] != 0) {
      return 0;
    }
  }
  return 1;
}

static int rsa_is_even(const uint8_t *a, size_t len) {
  return len == 0 || (a[len - 1] & 1U) == 0;
}

static void rsa_sub_small(uint8_t *a, size_t len, uint32_t value) {
  uint32_t borrow = value;

  for (size_t i = len; i > 0 && borrow > 0; i--) {
    uint32_t byte_borrow = borrow & 0xffU;
    uint32_t av = a[i - 1];
    borrow >>= 8;
    if (av < byte_borrow) {
      a[i - 1] = (uint8_t)(av + 256U - byte_borrow);
      borrow++;
    } else {
      a[i - 1] = (uint8_t)(av - byte_borrow);
    }
  }
}

static void rsa_shift_right_one(uint8_t *a, size_t len) {
  uint8_t carry = 0;

  for (size_t i = 0; i < len; i++) {
    uint8_t next_carry = (uint8_t)(a[i] & 1U);
    a[i] = (uint8_t)((a[i] >> 1) | (carry << 7));
    carry = next_carry;
  }
}

static void rsa_set_u32(uint8_t *out, size_t len, uint32_t value) {
  memset(out, 0, len);
  for (size_t i = 0; i < 4 && i < len; i++) {
    out[len - 1 - i] = (uint8_t)(value >> (i * 8));
  }
}

static uint32_t rsa_mod_u32(const uint8_t *a, size_t len, uint32_t mod) {
  uint32_t rem = 0;

  if (mod == 0) {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    rem = (uint32_t)((((uint64_t)rem << 8) | a[i]) % mod);
  }
  return rem;
}

static uint32_t rsa_inv_mod_u32(uint32_t a, uint32_t mod) {
  int64_t t = 0;
  int64_t new_t = 1;
  int64_t r = (int64_t)mod;
  int64_t new_r = (int64_t)(a % mod);

  while (new_r != 0) {
    int64_t q = r / new_r;
    int64_t tmp = t - q * new_t;
    t = new_t;
    new_t = tmp;
    tmp = r - q * new_r;
    r = new_r;
    new_r = tmp;
  }
  if (r != 1) {
    return 0;
  }
  if (t < 0) {
    t += mod;
  }
  return (uint32_t)t;
}

static int rsa_mul_small_add_one_div_small(uint8_t *out, size_t len,
                                           const uint8_t *m, uint32_t mul,
                                           uint32_t div) {
  uint8_t product[RSA_MAX_BYTES + 2];
  uint8_t quotient[RSA_MAX_BYTES + 2];
  size_t wide_len = len + 2;
  uint32_t carry = 0;
  uint32_t rem = 0;

  if (!out || !m || len == 0 || len > RSA_MAX_BYTES || wide_len > sizeof(product) ||
      div == 0) {
    return -1;
  }
  memset(product, 0, sizeof(product));
  memset(quotient, 0, sizeof(quotient));

  for (size_t i = len; i > 0; i--) {
    uint32_t v = (uint32_t)m[i - 1] * mul + carry;
    product[i + 1] = (uint8_t)(v & 0xffU);
    carry = v >> 8;
  }
  product[0] = (uint8_t)((carry >> 8) & 0xffU);
  product[1] = (uint8_t)(carry & 0xffU);

  for (size_t i = wide_len; i > 0; i--) {
    product[i - 1]++;
    if (product[i - 1] != 0) {
      break;
    }
  }

  for (size_t i = 0; i < wide_len; i++) {
    uint32_t acc = (uint32_t)((rem << 8) | product[i]);
    quotient[i] = (uint8_t)(acc / div);
    rem = acc % div;
  }
  if (rem != 0 || quotient[0] != 0 || quotient[1] != 0) {
    return -1;
  }
  memcpy(out, quotient + 2, len);
  return 0;
}

static int rsa_inverse_of_65537(uint8_t *out, size_t len, const uint8_t *mod) {
  const uint32_t e = 65537U;
  uint32_t mod_e;
  uint32_t inv;
  uint32_t k;

  if (!out || !mod || len == 0 || len > RSA_MAX_BYTES) {
    return -1;
  }
  mod_e = rsa_mod_u32(mod, len, e);
  if (mod_e == 0) {
    return -1;
  }
  inv = rsa_inv_mod_u32(mod_e, e);
  if (inv == 0) {
    return -1;
  }
  k = e - inv;
  if (k == e) {
    k = 0;
  }
  return rsa_mul_small_add_one_div_small(out, len, mod, k, e);
}

static void rsa_seed_expand(const uint8_t seed[SHA256_DIGEST_SIZE],
                            const char *label, uint32_t attempt,
                            uint8_t *out, size_t out_len) {
  uint32_t block = 0;
  size_t off = 0;

  while (off < out_len) {
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t take;

    sha256_init(&ctx);
    sha256_update(&ctx, "orizon-rsa-keygen-v1", 20);
    sha256_update(&ctx, seed, SHA256_DIGEST_SIZE);
    sha256_update(&ctx, label, strlen(label));
    sha256_update(&ctx, &attempt, sizeof(attempt));
    sha256_update(&ctx, &block, sizeof(block));
    sha256_final(&ctx, digest);

    take = out_len - off;
    if (take > sizeof(digest)) {
      take = sizeof(digest);
    }
    memcpy(out + off, digest, take);
    off += take;
    block++;
  }
}

static void rsa_make_prime_candidate(const uint8_t seed[SHA256_DIGEST_SIZE],
                                     const char *label, uint32_t attempt,
                                     uint8_t out[64]) {
  rsa_seed_expand(seed, label, attempt, out, 64);
  out[0] |= 0xc0U;
  out[63] |= 1U;
}

static int rsa_probable_prime_512(const uint8_t n[64]) {
  static const uint16_t small_primes[] = {
      3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,
      47,  53,  59,  61,  67,  71,  73,  79,  83,  89,  97,  101, 103,
      107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
      179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233, 239, 241,
      251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317,
      331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401,
      409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479,
      487, 491, 499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571,
      577, 587, 593, 599, 601, 607, 613, 617, 619, 631, 641, 643, 647,
      653, 659, 661, 673, 677, 683, 691, 701, 709, 719, 727, 733, 739,
      743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827,
      829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919,
      929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997};
  static const uint32_t bases[] = {2, 3, 5, 17, 257, 65537};
  uint8_t n_minus_one[64];
  uint8_t d[64];
  uint8_t base[64];
  uint8_t x[64];
  uint32_t s = 0;

  if (!n || rsa_is_even(n, 64) || rsa_is_zero(n, 64) || rsa_is_one(n, 64)) {
    return 0;
  }
  for (size_t i = 0; i < sizeof(small_primes) / sizeof(small_primes[0]); i++) {
    if (rsa_mod_u32(n, 64, small_primes[i]) == 0) {
      return 0;
    }
  }

  memcpy(n_minus_one, n, sizeof(n_minus_one));
  rsa_sub_small(n_minus_one, sizeof(n_minus_one), 1);
  memcpy(d, n_minus_one, sizeof(d));
  while (rsa_is_even(d, sizeof(d))) {
    rsa_shift_right_one(d, sizeof(d));
    s++;
  }

  for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
    int witness_ok = 0;

    rsa_set_u32(base, sizeof(base), bases[i]);
    if (rsa_modexp(x, base, sizeof(base), d, sizeof(d), n, 64) != 0) {
      return 0;
    }
    if (rsa_is_one(x, sizeof(x)) ||
        memcmp(x, n_minus_one, sizeof(n_minus_one)) == 0) {
      continue;
    }
    for (uint32_t r = 1; r < s; r++) {
      rsa_mul_mod(x, x, x, n, 64);
      if (memcmp(x, n_minus_one, sizeof(n_minus_one)) == 0) {
        witness_ok = 1;
        break;
      }
    }
    if (!witness_ok) {
      return 0;
    }
  }
  return 1;
}

static int rsa_generate_prime_512(uint8_t out[64],
                                  const uint8_t seed[SHA256_DIGEST_SIZE],
                                  const char *label, uint32_t *attempts) {
  const uint32_t max_attempts = 8192U;

  for (uint32_t attempt = 0; attempt < max_attempts; attempt++) {
    rsa_make_prime_candidate(seed, label, attempt, out);
    if (rsa_mod_u32(out, 64, 65537U) == 1U) {
      continue;
    }
    if (rsa_probable_prime_512(out)) {
      if (attempts) {
        *attempts = attempt + 1;
      }
      return 0;
    }
  }
  return -1;
}

int rsa_generate_private_key_1024(rsa_generated_private_key_t *key,
                                  const uint8_t seed[SHA256_DIGEST_SIZE],
                                  char *report, size_t report_size) {
  uint8_t p_minus_one[64];
  uint8_t q_minus_one[64];
  uint8_t phi[128];
  uint32_t p_attempts = 0;
  uint32_t q_attempts = 0;

  if (!key || !seed) {
    return -1;
  }
  memset(key, 0, sizeof(*key));
  if (rsa_generate_prime_512(key->p, seed, "p", &p_attempts) != 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size, "rsa: p prime generation failed");
    }
    return -1;
  }
  if (rsa_generate_prime_512(key->q, seed, "q", &q_attempts) != 0 ||
      memcmp(key->p, key->q, 64) == 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size, "rsa: q prime generation failed");
    }
    return -1;
  }

  memcpy(p_minus_one, key->p, sizeof(p_minus_one));
  memcpy(q_minus_one, key->q, sizeof(q_minus_one));
  rsa_sub_small(p_minus_one, sizeof(p_minus_one), 1);
  rsa_sub_small(q_minus_one, sizeof(q_minus_one), 1);

  rsa_plain_mul(key->n, sizeof(key->n), key->p, sizeof(key->p), key->q,
                sizeof(key->q));
  rsa_plain_mul(phi, sizeof(phi), p_minus_one, sizeof(p_minus_one),
                q_minus_one, sizeof(q_minus_one));
  if ((key->n[0] & 0x80U) == 0 ||
      rsa_inverse_of_65537(key->d, sizeof(key->d), phi) != 0 ||
      rsa_inverse_of_65537(key->dmp1, sizeof(key->dmp1), p_minus_one) != 0 ||
      rsa_inverse_of_65537(key->dmq1, sizeof(key->dmq1), q_minus_one) != 0) {
    if (report && report_size > 0) {
      snprintf(report, report_size, "rsa: private exponent derivation failed");
    }
    return -1;
  }

  if (report && report_size > 0) {
    snprintf(report, report_size, "rsa: generated 1024-bit key p-attempts=%lu q-attempts=%lu",
             (unsigned long)p_attempts, (unsigned long)q_attempts);
  }
  return 0;
}

int rsa_pkcs1v15_sha256_sign_crt(uint8_t *signature, size_t signature_len,
                                 const uint8_t digest[SHA256_DIGEST_SIZE],
                                 const rsa_crt_private_key_t *key) {
  uint8_t em[RSA_MAX_BYTES];
  uint8_t m1[RSA_MAX_BYTES];
  uint8_t m2[RSA_MAX_BYTES];
  uint8_t diff[RSA_MAX_BYTES];
  uint8_t h[RSA_MAX_BYTES];
  uint8_t qh[RSA_MAX_BYTES];
  uint8_t verified[RSA_MAX_BYTES];

  if (!signature || !digest || !key || !key->n || key->n_len == 0 ||
      key->n_len > RSA_MAX_BYTES || signature_len != key->n_len) {
    return -1;
  }
  if (rsa_encode_pkcs1_sha256(em, key->n_len, digest) != 0) {
    return -1;
  }

  if (key->d && key->d_len == key->n_len) {
    if (rsa_modexp(signature, em, key->n_len, key->d, key->d_len, key->n,
                   key->n_len) != 0 ||
        rsa_modexp65537(verified, signature, signature_len, key->n,
                        key->n_len) != 0 ||
        memcmp(verified, em, key->n_len) != 0) {
      return -1;
    }
    return 0;
  }

  if (!key->p || !key->q || !key->dmp1 || !key->dmq1 || !key->iqmp ||
      key->p_len == 0 || key->p_len > RSA_MAX_BYTES || key->q_len == 0 ||
      key->q_len > RSA_MAX_BYTES || key->p_len != key->q_len ||
      key->n_len != key->p_len + key->q_len) {
    return -1;
  }

  if (rsa_modexp(m1, em, key->n_len, key->dmp1, key->dmp1_len, key->p,
                 key->p_len) != 0 ||
      rsa_modexp(m2, em, key->n_len, key->dmq1, key->dmq1_len, key->q,
                 key->q_len) != 0) {
    return -1;
  }

  rsa_mod_sub(diff, m1, m2, key->p, key->p_len);
  rsa_mul_mod(h, diff, key->iqmp, key->p, key->p_len);

  rsa_plain_mul(qh, key->n_len, key->q, key->q_len, h, key->p_len);
  rsa_add_right_aligned(qh, key->n_len, m2, key->q_len);
  if (rsa_ge_bytes(qh, key->n, key->n_len)) {
    rsa_sub_inplace(qh, key->n, key->n_len);
  }

  if (rsa_modexp65537(verified, qh, key->n_len, key->n, key->n_len) != 0 ||
      memcmp(verified, em, key->n_len) != 0) {
    return -1;
  }

  memcpy(signature, qh, key->n_len);
  return 0;
}
