#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *plaintext, size_t plaintext_len,
                                     uint8_t *ciphertext, uint8_t tag[16]);

int crypto_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *ciphertext, size_t ciphertext_len,
                                     const uint8_t tag[16], uint8_t *plaintext);

void crypto_chacha20_block(const uint8_t key[32], uint32_t counter,
                           const uint8_t nonce[12], uint8_t out[64]);

void crypto_chacha20_xor(const uint8_t key[32], uint32_t counter,
                         const uint8_t nonce[12],
                         const uint8_t *in, size_t in_len, uint8_t *out);

#ifdef __cplusplus
}
#endif
