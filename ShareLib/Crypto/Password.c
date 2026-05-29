#include "Password.h"
#include "SHA256.h"
#include "Hex.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int crypto_hash_password_hex(const char *password, const char *salt_hex, char *out_hash_hex)
{
    uint8_t salt_bytes[8];
    if (crypto_hex_decode(salt_hex, salt_bytes, sizeof(salt_bytes)) < 0) {
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
