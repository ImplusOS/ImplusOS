#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void crypto_generate_salt(uint8_t salt[8]);

#ifdef __cplusplus
}
#endif
