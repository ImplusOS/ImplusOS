#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t crypto_base64_encode(const uint8_t *data, size_t data_len, char *out, size_t out_size);
int crypto_base64_decode(const char *in, uint8_t *out, size_t out_size, size_t *out_len);

#ifdef __cplusplus
}
#endif
