#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_x25519_generate_keypair(uint8_t private_key[32], uint8_t public_key[32]);
int crypto_x25519_shared_secret(const uint8_t private_key[32], const uint8_t peer_public[32],
                                uint8_t shared_secret[32]);
void crypto_x25519_scalar_mult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);

#ifdef __cplusplus
}
#endif
