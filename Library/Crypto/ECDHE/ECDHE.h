#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_ecdhe_generate_keypair(uint8_t private_key[32], uint8_t public_key[64]);
int crypto_ecdhe_shared_secret(const uint8_t private_key[32], const uint8_t peer_public[64],
                               uint8_t shared_secret[32]);

/* P-256 curve operations (needed by ECDSA) */
int crypto_ec_p256_scalar_mult(const uint8_t scalar[32], const uint8_t point[64], uint8_t out[64]);
int crypto_ec_p256_scalar_mult_g(const uint8_t scalar[32], uint8_t out[64]);
int crypto_ec_p256_add_points(const uint8_t a[64], const uint8_t b[64], uint8_t out[64]);

#ifdef __cplusplus
}
#endif
