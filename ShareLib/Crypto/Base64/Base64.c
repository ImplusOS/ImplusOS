#include "Base64.h"
#include <string.h>

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char b64_table_url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64_char_val(char c)
{
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (int)(c - '0' + 52);
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

size_t crypto_base64_encode(const uint8_t *data, size_t data_len, char *out, size_t out_size)
{
    size_t out_len = (data_len + 2) / 3 * 4;
    if (out == NULL) {
        return out_len + 1;
    }
    if (out_size < out_len + 1) {
        return 0;
    }

    size_t i, j;
    for (i = 0, j = 0; i + 2 < data_len; i += 3, j += 4) {
        uint32_t v = ((uint32_t)data[i] << 16) |
                     ((uint32_t)data[i + 1] << 8) |
                     (uint32_t)data[i + 2];
        out[j]     = b64_table[(v >> 18) & 0x3F];
        out[j + 1] = b64_table[(v >> 12) & 0x3F];
        out[j + 2] = b64_table[(v >> 6) & 0x3F];
        out[j + 3] = b64_table[v & 0x3F];
    }

    size_t remaining = data_len - i;
    if (remaining == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        out[j]     = b64_table[(v >> 18) & 0x3F];
        out[j + 1] = b64_table[(v >> 12) & 0x3F];
        out[j + 2] = '=';
        out[j + 3] = '=';
    } else if (remaining == 2) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[j]     = b64_table[(v >> 18) & 0x3F];
        out[j + 1] = b64_table[(v >> 12) & 0x3F];
        out[j + 2] = b64_table[(v >> 6) & 0x3F];
        out[j + 3] = '=';
    }

    out[out_len] = '\0';
    return out_len;
}

int crypto_base64_decode(const char *in, uint8_t *out, size_t out_size, size_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL) {
        return -1;
    }

    size_t in_len = strlen(in);
    while (in_len > 0 && in[in_len - 1] == '=') {
        --in_len;
    }

    size_t expected = in_len / 4 * 3 + ((in_len % 4) == 3 ? 1 : 0) + ((in_len % 4) == 2 ? 1 : 0);
    if (expected == 0) {
        *out_len = 0;
        return 0;
    }
    if (out_size < expected) {
        return -1;
    }

    size_t i, j;
    for (i = 0, j = 0; i + 4 <= in_len; i += 4, j += 3) {
        int a = b64_char_val(in[i]);
        int b = b64_char_val(in[i + 1]);
        int c = b64_char_val(in[i + 2]);
        int d = b64_char_val(in[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) {
            return -1;
        }
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)c << 6) | (uint32_t)d;
        out[j]     = (uint8_t)(v >> 16);
        out[j + 1] = (uint8_t)(v >> 8);
        out[j + 2] = (uint8_t)(v);
    }

    size_t remaining = in_len - i;
    if (remaining == 2) {
        int a = b64_char_val(in[i]);
        int b = b64_char_val(in[i + 1]);
        if (a < 0 || b < 0) return -1;
        out[j] = (uint8_t)(((uint32_t)a << 2) | ((uint32_t)b >> 4));
    } else if (remaining == 3) {
        int a = b64_char_val(in[i]);
        int b = b64_char_val(in[i + 1]);
        int c = b64_char_val(in[i + 2]);
        if (a < 0 || b < 0 || c < 0) return -1;
        uint32_t v = ((uint32_t)a << 10) | ((uint32_t)b << 4) | ((uint32_t)c >> 2);
        out[j]     = (uint8_t)(v >> 8);
        out[j + 1] = (uint8_t)(v);
    }

    *out_len = expected;
    return 0;
}
