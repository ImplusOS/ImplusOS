#include "CP932.h"

typedef struct {
    uint16_t sjis;
    uint16_t unicode;
} cp932_pair_t;

#include "CP932Table.inc"

static int cp932_is_lead(uint8_t byte)
{
    return (byte >= 0x81u && byte <= 0x9Fu) ||
           (byte >= 0xE0u && byte <= 0xFCu);
}

static int cp932_is_trail(uint8_t byte)
{
    return (byte >= 0x40u && byte <= 0x7Eu) ||
           (byte >= 0x80u && byte <= 0xFCu);
}

static int cp932_lookup_sjis(uint16_t sjis, uint32_t *cp)
{
    size_t low = 0;
    size_t high = cp932_to_unicode_table_count;

    while (low < high) {
        size_t mid = low + ((high - low) / 2u);
        uint16_t value = cp932_to_unicode_table[mid].sjis;

        if (value == sjis) {
            *cp = cp932_to_unicode_table[mid].unicode;
            return 1;
        }
        if (value < sjis) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return 0;
}

static int cp932_lookup_unicode(uint32_t cp, uint16_t *sjis)
{
    for (size_t i = 0; i < cp932_to_unicode_table_count; ++i) {
        if (cp932_to_unicode_table[i].unicode == cp) {
            *sjis = cp932_to_unicode_table[i].sjis;
            return 1;
        }
    }
    return 0;
}

cp932_status_t cp932_decode_next(const uint8_t *src, size_t src_len,
                                  uint32_t *cp, size_t *used)
{
    uint8_t first;

    if (!src || !cp || !used) {
        return CP932_ERR_NULL;
    }
    if (src_len == 0) {
        return CP932_ERR_INCOMPLETE;
    }

    first = src[0];
    if (first < 0x80u) {
        *cp = first;
        *used = 1;
        return CP932_OK;
    }
    if (first == 0x80u) {
        *cp = 0x80u;
        *used = 1;
        return CP932_OK;
    }
    if (first == 0xA0u) {
        *cp = 0xF8F0u;
        *used = 1;
        return CP932_OK;
    }
    if (first >= 0xA1u && first <= 0xDFu) {
        *cp = 0xFF61u + (uint32_t)(first - 0xA1u);
        *used = 1;
        return CP932_OK;
    }
    if (first >= 0xFDu) {
        *cp = 0xF8F1u + (uint32_t)(first - 0xFDu);
        *used = 1;
        return CP932_OK;
    }
    if (cp932_is_lead(first)) {
        uint16_t sjis;

        if (src_len < 2) {
            return CP932_ERR_INCOMPLETE;
        }
        if (!cp932_is_trail(src[1])) {
            return CP932_ERR_INVALID;
        }

        sjis = (uint16_t)(((uint16_t)first << 8u) | (uint16_t)src[1]);
        if (!cp932_lookup_sjis(sjis, cp)) {
            return CP932_ERR_INVALID;
        }
        *used = 2;
        return CP932_OK;
    }
    return CP932_ERR_INVALID;
}

cp932_status_t cp932_encode(uint32_t cp, uint8_t *dst, size_t dst_len,
                            size_t *written)
{
    uint16_t sjis;

    if (!dst || !written) {
        return CP932_ERR_NULL;
    }
    if (cp < 0x80u || cp == 0x80u) {
        if (dst_len < 1) {
            return CP932_ERR_NOSPACE;
        }
        dst[0] = (uint8_t)cp;
        *written = 1;
        return CP932_OK;
    }
    if (cp == 0xF8F0u) {
        if (dst_len < 1) {
            return CP932_ERR_NOSPACE;
        }
        dst[0] = 0xA0u;
        *written = 1;
        return CP932_OK;
    }
    if (cp >= 0xFF61u && cp <= 0xFF9Fu) {
        if (dst_len < 1) {
            return CP932_ERR_NOSPACE;
        }
        dst[0] = (uint8_t)(0xA1u + (cp - 0xFF61u));
        *written = 1;
        return CP932_OK;
    }
    if (cp >= 0xF8F1u && cp <= 0xF8F3u) {
        if (dst_len < 1) {
            return CP932_ERR_NOSPACE;
        }
        dst[0] = (uint8_t)(0xFDu + (cp - 0xF8F1u));
        *written = 1;
        return CP932_OK;
    }
    if (!cp932_lookup_unicode(cp, &sjis)) {
        return CP932_ERR_INVALID;
    }
    if (dst_len < 2) {
        return CP932_ERR_NOSPACE;
    }
    dst[0] = (uint8_t)(sjis >> 8u);
    dst[1] = (uint8_t)(sjis & 0xFFu);
    *written = 2;
    return CP932_OK;
}
