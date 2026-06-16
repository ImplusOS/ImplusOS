#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS_CONTENT_ALERT              21
#define TLS_CONTENT_HANDSHAKE          22
#define TLS_CONTENT_APPLICATION_DATA   23

#define TLS_RECORD_HEADER_SIZE  5
#define TLS_MAX_PLAINTEXT       16384
#define TLS_MAX_CIPHERTEXT      TLS_MAX_PLAINTEXT + 256

typedef struct {
    uint8_t content_type;
    uint16_t version;
    uint16_t length;
    uint8_t *fragment;
} tls_record_t;

int tls_record_encode(uint8_t content_type, const uint8_t *data, size_t data_len,
                      uint8_t *out, size_t *out_len);
int tls_record_decode(const uint8_t *data, size_t data_len, tls_record_t *record);

int tls_record_encrypt(const uint8_t *key, size_t key_len,
                       const uint8_t *iv, size_t iv_len,
                       uint8_t content_type,
                       const uint8_t *plaintext, size_t plaintext_len,
                       uint8_t *out, size_t *out_len);

int tls_record_decrypt(const uint8_t *key, size_t key_len,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *ciphertext, size_t ciphertext_len,
                       uint8_t *out, size_t *out_len, uint8_t *content_type);

#ifdef __cplusplus
}
#endif
