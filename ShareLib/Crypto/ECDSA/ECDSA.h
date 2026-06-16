#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_ecdsa_sign(const uint8_t private_key[32], const uint8_t hash[32],
                      uint8_t signature[64]);

int crypto_ecdsa_verify(const uint8_t public_key[64], const uint8_t hash[32],
                        const uint8_t signature[64]);

int crypto_ecdsa_sign_der(const uint8_t private_key[32], const uint8_t hash[32],
                          uint8_t *signature, size_t *sig_len);

int crypto_ecdsa_verify_der(const uint8_t public_key[64], const uint8_t hash[32],
                            const uint8_t *signature, size_t sig_len);

#ifdef __cplusplus
}
#endif
