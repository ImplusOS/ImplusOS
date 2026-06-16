#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_hkdf_extract(const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len,
                        uint8_t prk[32]);

int crypto_hkdf_expand(const uint8_t *prk, size_t prk_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
