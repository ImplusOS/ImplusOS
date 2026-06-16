#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_rsa_pss_sign(const uint8_t *n, size_t n_len,
                        const uint8_t *e, size_t e_len,
                        const uint8_t *d, size_t d_len,
                        const uint8_t *hash, size_t hash_len,
                        uint8_t *signature, size_t *sig_len);

int crypto_rsa_pss_verify(const uint8_t *n, size_t n_len,
                          const uint8_t *e, size_t e_len,
                          const uint8_t *hash, size_t hash_len,
                          const uint8_t *signature, size_t sig_len);

#ifdef __cplusplus
}
#endif
