#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_sha384(const uint8_t *data, size_t len, uint8_t out_hash[48]);

#ifdef __cplusplus
}
#endif
