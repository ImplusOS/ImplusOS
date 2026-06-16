#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_rsa_pkcs1_v15_verify(const uint8_t *n, size_t n_len,
                                const uint8_t *e, size_t e_len,
                                const uint8_t *hash, size_t hash_len,
                                const uint8_t *signature, size_t sig_len,
                                int hash_nid);

#define RSA_HASH_NID_SHA256 4
#define RSA_HASH_NID_SHA384 5
#define RSA_HASH_NID_SHA512 6

#ifdef __cplusplus
}
#endif
