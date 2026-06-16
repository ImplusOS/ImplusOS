#include "TLS_Record.h"
#include "../ChaCha20_Poly1305/ChaCha20_Poly1305.h"
#include <string.h>
#include <stdlib.h>

int tls_record_encode(uint8_t content_type, const uint8_t *data, size_t data_len,
                      uint8_t *out, size_t *out_len)
{
    if (out == NULL || out_len == NULL) return -1;
    if (data == NULL && data_len != 0) return -1;
    if (data_len > TLS_MAX_PLAINTEXT) return -1;

    size_t total = TLS_RECORD_HEADER_SIZE + data_len;
    if (*out_len < total) return -1;

    out[0] = content_type;
    out[1] = 0x03;
    out[2] = 0x03;
    out[3] = (uint8_t)((data_len >> 8) & 0xFF);
    out[4] = (uint8_t)(data_len & 0xFF);

    if (data_len > 0) memcpy(out + TLS_RECORD_HEADER_SIZE, data, data_len);

    *out_len = total;
    return 0;
}

int tls_record_decode(const uint8_t *data, size_t data_len, tls_record_t *record)
{
    if (data == NULL || record == NULL) return -1;
    if (data_len < TLS_RECORD_HEADER_SIZE) return -1;

    record->content_type = data[0];
    record->version = (uint16_t)((data[1] << 8) | data[2]);
    record->length = (uint16_t)((data[3] << 8) | data[4]);

    if (TLS_RECORD_HEADER_SIZE + record->length > data_len) return -1;
    record->fragment = (uint8_t *)(data + TLS_RECORD_HEADER_SIZE);
    return 0;
}

int tls_record_encrypt(const uint8_t *key, size_t key_len,
                       const uint8_t *iv, size_t iv_len,
                       uint8_t content_type,
                       const uint8_t *plaintext, size_t plaintext_len,
                       uint8_t *out, size_t *out_len)
{
    if (key == NULL || iv == NULL || plaintext == NULL || out == NULL || out_len == NULL) {
        return -1;
    }

    /* TLS 1.3 encrypted record: encrypt content || type || padding */
    /* Use ChaCha20-Poly1305 for AEAD */
    if (key_len == 32 && iv_len == 12) {
        size_t encrypted_len = plaintext_len + 1;
        size_t total = TLS_RECORD_HEADER_SIZE + encrypted_len + 16;
        if (*out_len < total) return -1;

        uint8_t nonce[12];
        memcpy(nonce, iv, 12);

        uint8_t aad[TLS_RECORD_HEADER_SIZE];
        aad[0] = content_type;
        aad[1] = 0x03; aad[2] = 0x03;
        aad[3] = (uint8_t)(((encrypted_len + 16) >> 8) & 0xFF);
        aad[4] = (uint8_t)((encrypted_len + 16) & 0xFF);

        uint8_t *encrypt_in = (uint8_t *)malloc(encrypted_len);
        if (!encrypt_in) return -1;
        memcpy(encrypt_in, plaintext, plaintext_len);
        encrypt_in[plaintext_len] = content_type;

        uint8_t tag[16];
        if (crypto_chacha20_poly1305_encrypt(key, nonce, aad, TLS_RECORD_HEADER_SIZE,
                                             encrypt_in, encrypted_len,
                                             out + TLS_RECORD_HEADER_SIZE, tag) < 0) {
            free(encrypt_in);
            return -1;
        }
        free(encrypt_in);

        memcpy(out + TLS_RECORD_HEADER_SIZE + encrypted_len, tag, 16);
        out[0] = content_type;
        out[1] = 0x03; out[2] = 0x03;
        out[3] = (uint8_t)(((encrypted_len + 16) >> 8) & 0xFF);
        out[4] = (uint8_t)((encrypted_len + 16) & 0xFF);

        *out_len = total;
        return 0;
    }

    return -1;
}

int tls_record_decrypt(const uint8_t *key, size_t key_len,
                       const uint8_t *iv, size_t iv_len,
                       const uint8_t *ciphertext, size_t ciphertext_len,
                       uint8_t *out, size_t *out_len, uint8_t *content_type)
{
    if (key == NULL || iv == NULL || ciphertext == NULL || out == NULL ||
        out_len == NULL || content_type == NULL) {
        return -1;
    }

    if (ciphertext_len < TLS_RECORD_HEADER_SIZE + 16 + 1) return -1;

    size_t encrypted_len = ciphertext_len - TLS_RECORD_HEADER_SIZE - 16;
    if (encrypted_len < 1) return -1;

    if (key_len == 32 && iv_len == 12) {
        uint8_t nonce[12];
        memcpy(nonce, iv, 12);

        uint8_t aad[TLS_RECORD_HEADER_SIZE];
        memcpy(aad, ciphertext, TLS_RECORD_HEADER_SIZE);

        uint8_t *decrypted = (uint8_t *)malloc(encrypted_len);
        if (!decrypted) return -1;

        uint8_t tag[16];
        memcpy(tag, ciphertext + TLS_RECORD_HEADER_SIZE + encrypted_len, 16);

        if (crypto_chacha20_poly1305_decrypt(key, nonce, aad, TLS_RECORD_HEADER_SIZE,
                                             ciphertext + TLS_RECORD_HEADER_SIZE,
                                             encrypted_len, tag, decrypted) < 0) {
            free(decrypted);
            return -1;
        }

        *content_type = decrypted[encrypted_len - 1];
        size_t data_len = encrypted_len - 1;
        if (*out_len < data_len) { free(decrypted); return -1; }
        memcpy(out, decrypted, data_len);
        *out_len = data_len;
        free(decrypted);
        return 0;
    }

    return -1;
}
