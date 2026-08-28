#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS13_MAX_DIGEST_SIZE 64

int crypto_tls13_derive_early_secret(const uint8_t *psk, size_t psk_len,
                                     uint8_t early_secret[32]);

int crypto_tls13_derive_handshake_secret(const uint8_t early_secret[32],
                                         const uint8_t *shared_secret, size_t shared_secret_len,
                                         uint8_t handshake_secret[32]);

int crypto_tls13_derive_master_secret(const uint8_t handshake_secret[32],
                                      uint8_t master_secret[32]);

int crypto_tls13_hkdf_expand_label(const uint8_t *secret, size_t secret_len,
                                   const char *label,
                                   const uint8_t *context, size_t context_len,
                                   uint8_t *out, size_t out_len);

int crypto_tls13_derive_traffic_keys(const uint8_t *secret, size_t secret_len,
                                     const char *label,
                                     uint8_t *key, size_t key_len,
                                     uint8_t *iv, size_t iv_len);

#ifdef __cplusplus
}
#endif
