#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_PASSWORD_KDF_ITERATIONS 100000u

int crypto_hash_password_hex(const char *password, const char *salt_hex, char *out_hash_hex);
int crypto_hash_password_hex_legacy(const char *password,
                                    const char *salt_hex,
                                    char *out_hash_hex);

#ifdef __cplusplus
}
#endif
