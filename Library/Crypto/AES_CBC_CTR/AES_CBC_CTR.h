#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                              const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext);
int crypto_aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                              const uint8_t *ciphertext, size_t ciphertext_len,
                              uint8_t *plaintext);
int crypto_aes256_cbc_encrypt(const uint8_t key[32], const uint8_t iv[16],
                              const uint8_t *plaintext, size_t plaintext_len,
                              uint8_t *ciphertext);
int crypto_aes256_cbc_decrypt(const uint8_t key[32], const uint8_t iv[16],
                              const uint8_t *ciphertext, size_t ciphertext_len,
                              uint8_t *plaintext);

int crypto_aes128_ctr_encrypt(const uint8_t key[16], const uint8_t nonce[16],
                              const uint8_t *data, size_t data_len, uint8_t *out);
int crypto_aes256_ctr_encrypt(const uint8_t key[32], const uint8_t nonce[16],
                              const uint8_t *data, size_t data_len, uint8_t *out);

#ifdef __cplusplus
}
#endif
