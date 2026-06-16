#include "HKDF.h"
#include "../HMAC_SHA256/HMAC_SHA256.h"
#include <string.h>

int crypto_hkdf_extract(const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len,
                        uint8_t prk[32])
{
    if (prk == NULL || (ikm == NULL && ikm_len != 0)) {
        return -1;
    }

    const uint8_t *actual_salt = salt;
    uint8_t zero_salt[32];
    size_t actual_salt_len = salt_len;

    if (salt == NULL || salt_len == 0) {
        memset(zero_salt, 0, 32);
        actual_salt = zero_salt;
        actual_salt_len = 32;
    }

    crypto_hmac_sha256(actual_salt, actual_salt_len, ikm, ikm_len, prk);

    if (actual_salt == zero_salt) {
        memset(zero_salt, 0, sizeof(zero_salt));
    }

    return 0;
}

int crypto_hkdf_expand(const uint8_t *prk, size_t prk_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *out, size_t out_len)
{
    if (prk == NULL || prk_len == 0 || out == NULL) {
        return -1;
    }
    if (out_len == 0) {
        return 0;
    }
    if (out_len > 255 * 32) {
        return -1;
    }

    uint8_t last_block[32];
    size_t generated = 0;
    uint8_t counter = 1;

    while (generated < out_len) {
        uint8_t input[32 + info_len + 1];
        size_t input_len = 0;

        if (counter > 1) {
            memcpy(input, last_block, 32);
            input_len = 32;
        }
        if (info_len > 0) {
            memcpy(input + input_len, info, info_len);
            input_len += info_len;
        }
        input[input_len++] = counter;

        crypto_hmac_sha256(prk, prk_len, input, input_len, last_block);

        size_t to_copy = out_len - generated;
        if (to_copy > 32) to_copy = 32;
        memcpy(out + generated, last_block, to_copy);
        generated += to_copy;
        ++counter;
    }

    memset(last_block, 0, sizeof(last_block));
    return 0;
}
