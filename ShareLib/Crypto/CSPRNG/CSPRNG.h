#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_csprng_seed(const uint8_t seed[32], const uint8_t nonce[8]);
void crypto_csprng_generate(uint8_t *out, size_t len);
void crypto_csprng_reseed(const uint8_t seed[32], const uint8_t nonce[8]);

#ifdef __cplusplus
}
#endif
