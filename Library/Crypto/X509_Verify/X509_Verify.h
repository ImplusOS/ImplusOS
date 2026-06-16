#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t der_data[2048];
    size_t der_len;
    uint8_t public_key[64];
    size_t public_key_len;
    int key_type;
    char subject[256];
    char issuer[256];
    uint8_t serial[32];
    size_t serial_len;
} x509_chain_cert_t;

int x509_verify_certificate_chain(const uint8_t * const *certs_der, const size_t *certs_len,
                                  size_t cert_count,
                                  const uint8_t *root_der, size_t root_len);

int x509_verify_signature(const uint8_t *data, size_t data_len,
                          const uint8_t *signature, size_t sig_len,
                          const uint8_t *public_key, size_t pubkey_len,
                          int key_type);

#ifdef __cplusplus
}
#endif
