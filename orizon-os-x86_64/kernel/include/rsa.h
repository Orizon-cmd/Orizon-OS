/*
 * Orizon OS x86_64 - minimal RSA helpers for staged SSH.
 */

#ifndef _RSA_H
#define _RSA_H

#include "sha256.h"
#include "types.h"

#define RSA_MAX_BYTES 256U

typedef struct {
  const uint8_t *n;
  size_t n_len;
  const uint8_t *d;
  size_t d_len;
  const uint8_t *p;
  size_t p_len;
  const uint8_t *q;
  size_t q_len;
  const uint8_t *dmp1;
  size_t dmp1_len;
  const uint8_t *dmq1;
  size_t dmq1_len;
  const uint8_t *iqmp;
  size_t iqmp_len;
} rsa_crt_private_key_t;

typedef struct {
  uint8_t n[128];
  uint8_t d[128];
  uint8_t p[64];
  uint8_t q[64];
  uint8_t dmp1[64];
  uint8_t dmq1[64];
} rsa_generated_private_key_t;

int rsa_pkcs1v15_sha256_sign_crt(uint8_t *signature, size_t signature_len,
                                 const uint8_t digest[SHA256_DIGEST_SIZE],
                                 const rsa_crt_private_key_t *key);
int rsa_pkcs1v15_sha256_verify(const uint8_t *signature, size_t signature_len,
                               const uint8_t digest[SHA256_DIGEST_SIZE],
                               const uint8_t *n, size_t n_len);
int rsa_generate_private_key_1024(rsa_generated_private_key_t *key,
                                  const uint8_t seed[SHA256_DIGEST_SIZE],
                                  char *report, size_t report_size);

#endif /* _RSA_H */
