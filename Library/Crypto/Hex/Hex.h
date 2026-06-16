#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_hex_encode(const uint8_t *bytes, size_t len, char *out_hex);
int crypto_hex_decode(const char *hex, uint8_t *out_bytes, size_t out_len);

#ifdef __cplusplus
}
#endif
