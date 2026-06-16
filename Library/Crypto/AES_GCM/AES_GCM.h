#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext, uint8_t tag[16]);

int crypto_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ciphertext_len,
                              const uint8_t tag[16], uint8_t *plaintext);

int crypto_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t iv[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext, uint8_t tag[16]);

int crypto_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t iv[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ciphertext, size_t ciphertext_len,
                              const uint8_t tag[16], uint8_t *plaintext);

void crypto_aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void crypto_aes256_encrypt_block(const uint8_t key[32], const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif
