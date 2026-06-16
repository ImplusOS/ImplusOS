#include "PBKDF2.h"
#include "../HMAC_SHA256/HMAC_SHA256.h"
#include <string.h>

int crypto_pbkdf2_hmac_sha256(const uint8_t *password, size_t password_len,
                              const uint8_t *salt, size_t salt_len,
                              uint32_t iterations,
                              uint8_t *out, size_t out_len)
{
    if (password == NULL && password_len != 0) return -1;
    if (salt == NULL) return -1;
    if (out == NULL) return -1;
    if (iterations == 0) return -1;
    if (out_len > 0xFFFFFFFFULL * 32) return -1;

    uint32_t blocks_needed = (uint32_t)((out_len + 31) / 32);
    uint8_t u[32];
    size_t generated = 0;

    for (uint32_t block = 1; block <= blocks_needed; ++block) {
        /* U1 = HMAC(Password, Salt || INT32(i)) */
        uint8_t input[salt_len + 4];
        memcpy(input, salt, salt_len);
        input[salt_len + 0] = (uint8_t)((block >> 24) & 0xFF);
        input[salt_len + 1] = (uint8_t)((block >> 16) & 0xFF);
        input[salt_len + 2] = (uint8_t)((block >> 8) & 0xFF);
        input[salt_len + 3] = (uint8_t)(block & 0xFF);

        crypto_hmac_sha256(password, password_len, input, salt_len + 4, u);

        uint8_t t[32];
        memcpy(t, u, 32);

        for (uint32_t iter = 1; iter < iterations; ++iter) {
            crypto_hmac_sha256(password, password_len, u, 32, u);
            for (int j = 0; j < 32; ++j) t[j] ^= u[j];
        }

        size_t to_copy = out_len - generated;
        if (to_copy > 32) to_copy = 32;
        memcpy(out + generated, t, to_copy);
        generated += to_copy;
        memset(t, 0, sizeof(t));
    }

    memset(u, 0, sizeof(u));
    return 0;
}
