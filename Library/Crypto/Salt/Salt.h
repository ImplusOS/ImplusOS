#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_generate_salt(uint8_t salt[8]);
void crypto_generate_salt_bytes(uint8_t *salt, size_t length);

#ifdef __cplusplus
}
#endif
