#include "Hex.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void crypto_hex_encode(const uint8_t *bytes, size_t len, char *out_hex)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out_hex[i * 2] = hex[(bytes[i] >> 4) & 0xF];
        out_hex[i * 2 + 1] = hex[bytes[i] & 0xF];
    }
    out_hex[len * 2] = '\0';
}

int crypto_hex_decode(const char *hex, uint8_t *out_bytes, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) {
        return -1;
    }

    for (size_t i = 0; i < out_len; ++i) {
        uint8_t high;
        uint8_t low;
        char c = hex[i * 2];
        if (c >= '0' && c <= '9') high = c - '0';
        else if (c >= 'a' && c <= 'f') high = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') high = c - 'A' + 10;
        else return -1;

        c = hex[i * 2 + 1];
        if (c >= '0' && c <= '9') low = c - '0';
        else if (c >= 'a' && c <= 'f') low = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') low = c - 'A' + 10;
        else return -1;

        out_bytes[i] = (uint8_t)((high << 4) | low);
    }

    return 0;
}
