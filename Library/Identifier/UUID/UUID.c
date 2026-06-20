#include "UUID.h"
#include <string.h>
#include <ctype.h>

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int uuid_generate_v4(uuid_t *uuid)
{
    if (uuid == NULL) return -1;

    extern void crypto_csprng_generate(uint8_t *out, size_t len);
    crypto_csprng_generate(uuid->data, UUID_SIZE);

    uuid->data[6] = (uuid->data[6] & 0x0F) | UUID_VERSION_4;
    uuid->data[8] = (uuid->data[8] & 0x3F) | UUID_VARIANT_RFC;

    return 0;
}

int uuid_generate_v4_from_csprng(uuid_t *uuid, void (*rng)(uint8_t *, size_t))
{
    if (uuid == NULL || rng == NULL) return -1;

    rng(uuid->data, UUID_SIZE);

    uuid->data[6] = (uuid->data[6] & 0x0F) | UUID_VERSION_4;
    uuid->data[8] = (uuid->data[8] & 0x3F) | UUID_VARIANT_RFC;

    return 0;
}

int uuid_from_string(const char *str, uuid_t *uuid)
{
    if (str == NULL || uuid == NULL) return -1;

    const char *p = str;
    int byte_idx = 0;

    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (*p != '-') return -1;
            p++;
            continue;
        }
        if (byte_idx >= UUID_SIZE) return -1;

        int hi = hex_digit(*p++);
        if (hi < 0) return -1;
        int lo = hex_digit(*p++);
        if (lo < 0) return -1;

        uuid->data[byte_idx++] = (uint8_t)((hi << 4) | lo);
    }

    if (*p != '\0') return -1;
    if (byte_idx != UUID_SIZE) return -1;

    return 0;
}

void uuid_to_string(const uuid_t *uuid, char buf[UUID_STR_BUF_SIZE])
{
    if (uuid == NULL || buf == NULL) return;

    static const char hex[] = "0123456789abcdef";
    int pos = 0;

    for (int i = 0; i < UUID_SIZE; i++) {
        buf[pos++] = hex[(uuid->data[i] >> 4) & 0x0F];
        buf[pos++] = hex[uuid->data[i] & 0x0F];

        if (i == 3 || i == 5 || i == 7 || i == 9) {
            buf[pos++] = '-';
        }
    }

    buf[pos] = '\0';
}

int uuid_compare(const uuid_t *a, const uuid_t *b)
{
    if (a == NULL || b == NULL) return (a == b) ? 0 : (a == NULL ? -1 : 1);

    for (int i = 0; i < UUID_SIZE; i++) {
        if (a->data[i] < b->data[i]) return -1;
        if (a->data[i] > b->data[i]) return 1;
    }

    return 0;
}

int uuid_is_nil(const uuid_t *uuid)
{
    if (uuid == NULL) return 1;

    for (int i = 0; i < UUID_SIZE; i++) {
        if (uuid->data[i] != 0) return 0;
    }

    return 1;
}

void uuid_copy(uuid_t *dst, const uuid_t *src)
{
    if (dst == NULL || src == NULL) return;
    memcpy(dst->data, src->data, UUID_SIZE);
}

int uuid_version(const uuid_t *uuid)
{
    if (uuid == NULL) return -1;
    return uuid->data[6] & 0xF0;
}

int uuid_variant(const uuid_t *uuid)
{
    if (uuid == NULL) return -1;

    if ((uuid->data[8] & 0x80) == 0) return UUID_VARIANT_NCS;
    if ((uuid->data[8] & 0xC0) == 0x80) return UUID_VARIANT_RFC;
    if ((uuid->data[8] & 0xE0) == 0xC0) return UUID_VARIANT_MICRO;

    return UUID_VARIANT_FUTURE;
}
