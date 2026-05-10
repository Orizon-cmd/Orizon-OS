/*
 * Orizon OS x86_64 - AES-128-GCM
 */

#ifndef _AES_GCM_H
#define _AES_GCM_H

#include "types.h"

int aes128_gcm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *plaintext, size_t plaintext_len,
                       uint8_t *ciphertext, uint8_t tag[16]);
int aes128_gcm_decrypt(const uint8_t key[16], const uint8_t nonce[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *ciphertext, size_t ciphertext_len,
                       const uint8_t tag[16], uint8_t *plaintext);
int aes128_key_unwrap(const uint8_t kek[16], const uint8_t *wrapped,
                      size_t wrapped_len, uint8_t *plaintext,
                      size_t *plaintext_len);
int aes128_key_unwrap_selftest(void);

#endif /* _AES_GCM_H */
