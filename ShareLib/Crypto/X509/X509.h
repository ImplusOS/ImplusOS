#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    X509_KEY_RSA = 0,
    X509_KEY_EC  = 1
} x509_key_type_t;

typedef struct {
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
} x509_time_t;

typedef struct {
    /* Public key */
    x509_key_type_t key_type;
    uint8_t key_data[512];
    size_t key_data_len;
    size_t key_bits;

    /* RSA-specific */
    uint8_t modulus[512];
    size_t modulus_len;
    uint8_t exponent[8];
    size_t exponent_len;

    /* EC-specific */
    uint8_t ec_point[66];
    size_t ec_point_len;

    /* Identity */
    char subject[256];
    char issuer[256];

    /* Validity */
    x509_time_t not_before;
    x509_time_t not_after;

    /* Serial */
    uint8_t serial[32];
    size_t serial_len;

    /* Signature */
    uint8_t signature[512];
    size_t signature_len;
    int sig_algorithm;
} x509_cert_t;

int x509_parse_certificate(const uint8_t *der, size_t len, x509_cert_t *cert);
int x509_parse_certificate_pem(const char *pem, size_t len, x509_cert_t *cert);

#ifdef __cplusplus
}
#endif
