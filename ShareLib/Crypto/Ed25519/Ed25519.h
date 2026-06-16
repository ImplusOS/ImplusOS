#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_ed25519_generate_keypair(uint8_t private_key[32], uint8_t public_key[32]);
int crypto_ed25519_sign(const uint8_t private_key[32], const uint8_t *message, size_t message_len,
                        uint8_t signature[64]);
int crypto_ed25519_verify(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                          const uint8_t signature[64]);

#ifdef __cplusplus
}
#endif
