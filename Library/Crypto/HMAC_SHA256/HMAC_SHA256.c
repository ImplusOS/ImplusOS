#include "HMAC_SHA256.h"
#include "../SHA256/SHA256.h"
#include <string.h>

#define HMAC_BLOCK_SIZE 64u

void crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32])
{
    uint8_t key_block[HMAC_BLOCK_SIZE];
    uint8_t inner[HMAC_BLOCK_SIZE + data_len + 4u];
    uint8_t outer[HMAC_BLOCK_SIZE + 32u];
    uint8_t inner_hash[32];

    if (out == NULL) {
        return;
    }
    if ((key == NULL && key_len != 0u) || (data == NULL && data_len != 0u)) {
        return;
    }

    memset(key_block, 0, sizeof(key_block));
    if (key_len > HMAC_BLOCK_SIZE) {
        crypto_sha256(key, key_len, key_block);
    } else if (key_len != 0u) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < HMAC_BLOCK_SIZE; ++i) {
        inner[i] = (uint8_t)(key_block[i] ^ 0x36u);
        outer[i] = (uint8_t)(key_block[i] ^ 0x5cu);
    }

    memcpy(inner + HMAC_BLOCK_SIZE, data, data_len);
    crypto_sha256(inner, HMAC_BLOCK_SIZE + data_len, inner_hash);

    memcpy(outer + HMAC_BLOCK_SIZE, inner_hash, 32u);
    crypto_sha256(outer, HMAC_BLOCK_SIZE + 32u, out);

    memset(key_block, 0, sizeof(key_block));
    memset(inner_hash, 0, sizeof(inner_hash));
}
