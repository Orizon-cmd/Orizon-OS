/*
 * Orizon OS x86_64 - X25519 key agreement
 */

#ifndef _X25519_H
#define _X25519_H

#include "types.h"

#define X25519_KEY_SIZE 32U

void x25519_clamp_private(uint8_t scalar[X25519_KEY_SIZE]);
void x25519_public_from_private(uint8_t out[X25519_KEY_SIZE],
                                const uint8_t scalar[X25519_KEY_SIZE]);
void x25519_shared_secret(uint8_t out[X25519_KEY_SIZE],
                          const uint8_t scalar[X25519_KEY_SIZE],
                          const uint8_t peer_public[X25519_KEY_SIZE]);

#endif /* _X25519_H */
