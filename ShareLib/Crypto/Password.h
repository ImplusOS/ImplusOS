#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int crypto_hash_password_hex(const char *password, const char *salt_hex, char *out_hash_hex);

#ifdef __cplusplus
}
#endif
