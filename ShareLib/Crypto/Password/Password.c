#include "Password.h"
#include "../SHA256/SHA256.h"
#include "../Hex/Hex.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HMAC_SHA256_BLOCK_SIZE 64u
#define PASSWORD_SALT_MAX_SIZE 32u

static void hmac_sha256(const uint8_t *key,
                        size_t key_len,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t out[32])
{
    uint8_t key_block[HMAC_SHA256_BLOCK_SIZE];
    uint8_t inner[HMAC_SHA256_BLOCK_SIZE + PASSWORD_SALT_MAX_SIZE + 4u];
    uint8_t outer[HMAC_SHA256_BLOCK_SIZE + 32u];
    uint8_t inner_hash[32];

    memset(key_block, 0, sizeof(key_block));
    if (key_len > sizeof(key_block)) {
        crypto_sha256(key, key_len, key_block);
    } else if (key_len != 0u) {
        memcpy(key_block, key, key_len);
    }

    for (size_t i = 0; i < HMAC_SHA256_BLOCK_SIZE; ++i) {
        inner[i] = (uint8_t)(key_block[i] ^ 0x36u);
        outer[i] = (uint8_t)(key_block[i] ^ 0x5cu);
    }
    memcpy(inner + HMAC_SHA256_BLOCK_SIZE, data, data_len);
    crypto_sha256(inner, HMAC_SHA256_BLOCK_SIZE + data_len, inner_hash);
    memcpy(outer + HMAC_SHA256_BLOCK_SIZE, inner_hash, sizeof(inner_hash));
    crypto_sha256(outer, sizeof(outer), out);

    memset(key_block, 0, sizeof(key_block));
    memset(inner_hash, 0, sizeof(inner_hash));
}

int crypto_hash_password_hex(const char *password,
                             const char *salt_hex,
                             char *out_hash_hex)
{
    uint8_t salt[PASSWORD_SALT_MAX_SIZE];
    uint8_t first_input[PASSWORD_SALT_MAX_SIZE + 4u];
    uint8_t u[32];
    uint8_t derived[32];
    size_t salt_hex_len;
    size_t salt_len;

    if (password == NULL || salt_hex == NULL || out_hash_hex == NULL) {
        return -1;
    }

    salt_hex_len = strlen(salt_hex);
    if (salt_hex_len == 0u || (salt_hex_len & 1u) != 0u) {
        return -1;
    }
    salt_len = salt_hex_len / 2u;
    if (salt_len > sizeof(salt) ||
        crypto_hex_decode(salt_hex, salt, salt_len) < 0) {
        return -1;
    }

    memcpy(first_input, salt, salt_len);
    first_input[salt_len] = 0u;
    first_input[salt_len + 1u] = 0u;
    first_input[salt_len + 2u] = 0u;
    first_input[salt_len + 3u] = 1u;

    size_t password_len = strlen(password);
    hmac_sha256((const uint8_t *)password,
                password_len,
                first_input,
                salt_len + 4u,
                u);
    memcpy(derived, u, sizeof(derived));

    for (uint32_t iteration = 1u;
         iteration < CRYPTO_PASSWORD_KDF_ITERATIONS;
         ++iteration) {
        hmac_sha256((const uint8_t *)password, password_len, u, sizeof(u), u);
        for (size_t i = 0; i < sizeof(derived); ++i) {
            derived[i] ^= u[i];
        }
    }

    crypto_hex_encode(derived, sizeof(derived), out_hash_hex);
    memset(u, 0, sizeof(u));
    memset(derived, 0, sizeof(derived));
    return 0;
}

int crypto_hash_password_hex_legacy(const char *password,
                                    const char *salt_hex,
                                    char *out_hash_hex)
{
    uint8_t salt_bytes[8];
    if (password == NULL || salt_hex == NULL || out_hash_hex == NULL ||
        crypto_hex_decode(salt_hex, salt_bytes, sizeof(salt_bytes)) < 0) {
        return -1;
    }

    size_t password_len = strlen(password);
    size_t input_len = sizeof(salt_bytes) + password_len;
    uint8_t *input = (uint8_t *)malloc(input_len);
    if (!input) {
        return -1;
    }

    memcpy(input, salt_bytes, sizeof(salt_bytes));
    memcpy(input + sizeof(salt_bytes), password, password_len);

    uint8_t hash[32];
    crypto_sha256(input, input_len, hash);
    crypto_hex_encode(hash, sizeof(hash), out_hash_hex);
    free(input);
    return 0;
}
