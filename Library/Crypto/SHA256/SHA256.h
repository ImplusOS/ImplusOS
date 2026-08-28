#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_sha256(const uint8_t *data, size_t len, uint8_t out_hash[32]);

#ifdef __cplusplus
}
#endif
