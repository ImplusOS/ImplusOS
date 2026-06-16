#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCSP_CERT_GOOD  0
#define OCSP_CERT_REVOKED 1
#define OCSP_CERT_UNKNOWN 2

typedef struct {
    uint8_t serial[32];
    size_t serial_len;
    int status;
} ocsp_single_response_t;

typedef struct {
    uint8_t issuer_key_hash[32];
    uint8_t issuer_name_hash[32];
    ocsp_single_response_t responses[8];
    size_t num_responses;
} ocsp_response_t;

typedef struct {
    uint8_t serial[32];
    size_t serial_len;
    uint32_t revocation_date;
} crl_entry_t;

typedef struct {
    crl_entry_t entries[64];
    size_t num_entries;
} crl_t;

int ocsp_parse_response(const uint8_t *der, size_t len, ocsp_response_t *resp);
int ocsp_check_cert_status(const ocsp_response_t *resp, const uint8_t *serial, size_t serial_len);

int crl_parse(const uint8_t *der, size_t len, crl_t *crl);
int crl_is_revoked(const crl_t *crl, const uint8_t *serial, size_t serial_len);

#ifdef __cplusplus
}
#endif
