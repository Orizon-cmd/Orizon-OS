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

  if (!signature || !digest || !key || !key->n || !key->p || !key->q ||
      !key->dmp1 || !key->dmq1 || !key->iqmp || key->n_len == 0 ||
      key->n_len > RSA_MAX_BYTES || key->p_len == 0 ||
      key->p_len > RSA_MAX_BYTES || key->q_len == 0 ||
      key->q_len > RSA_MAX_BYTES || key->p_len != key->q_len ||
      key->n_len != key->p_len + key->q_len ||
      signature_len != key->n_len) {
    return -1;
  }
  if (rsa_encode_pkcs1_sha256(em, key->n_len, digest) != 0) {
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
