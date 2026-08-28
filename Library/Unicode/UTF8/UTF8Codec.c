#include "UTF8Codec.h"

static int unicode_utf8_is_continuation(uint8_t byte)
{
    return (byte & 0xC0u) == 0x80u;
}

int unicode_utf8_seq_len(uint8_t lead)
{
    if (lead < 0x80u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 0;
}

int unicode_utf8_is_valid_cp(uint32_t cp)
{
    if (cp > UNICODE_UTF8_MAX) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    return 1;
}

unicode_utf8_status_t unicode_utf8_decode_next(const uint8_t *src,
                                               size_t src_len,
                                               uint32_t *cp,
                                               size_t *used)
{
    uint8_t first;
    int seq_len;
    uint32_t value;

    if (!src || !cp || !used) {
        return UNICODE_UTF8_ERR_NULL;
    }
    if (src_len == 0) {
        return UNICODE_UTF8_ERR_INCOMPLETE;
    }

    first = src[0];
    seq_len = unicode_utf8_seq_len(first);
    if (seq_len == 0) {
        return UNICODE_UTF8_ERR_INVALID;
    }
    if ((size_t)seq_len > src_len) {
        return UNICODE_UTF8_ERR_INCOMPLETE;
    }

    if (seq_len == 1) {
        *cp = first;
        *used = 1;
        return UNICODE_UTF8_OK;
    }

    for (int i = 1; i < seq_len; ++i) {
        if (!unicode_utf8_is_continuation(src[i])) {
            return UNICODE_UTF8_ERR_INVALID;
        }
    }

    if (seq_len == 2) {
        value = ((uint32_t)(first & 0x1Fu) << 6u) |
                (uint32_t)(src[1] & 0x3Fu);
        if (value < 0x80u) {
            return UNICODE_UTF8_ERR_INVALID;
        }
    } else if (seq_len == 3) {
        value = ((uint32_t)(first & 0x0Fu) << 12u) |
                ((uint32_t)(src[1] & 0x3Fu) << 6u) |
                (uint32_t)(src[2] & 0x3Fu);
        if (value < 0x800u ||
            (value >= 0xD800u && value <= 0xDFFFu)) {
            return UNICODE_UTF8_ERR_INVALID;
        }
    } else {
        value = ((uint32_t)(first & 0x07u) << 18u) |
                ((uint32_t)(src[1] & 0x3Fu) << 12u) |
                ((uint32_t)(src[2] & 0x3Fu) << 6u) |
                (uint32_t)(src[3] & 0x3Fu);
        if (value < 0x10000u || value > UNICODE_UTF8_MAX) {
            return UNICODE_UTF8_ERR_INVALID;
        }
    }

    *cp = value;
    *used = (size_t)seq_len;
    return UNICODE_UTF8_OK;
}

unicode_utf8_status_t unicode_utf8_encode(uint32_t cp,
                                          uint8_t *dst,
                                          size_t dst_len,
                                          size_t *written)
{
    if (!dst || !written) {
        return UNICODE_UTF8_ERR_NULL;
    }
    if (!unicode_utf8_is_valid_cp(cp)) {
        return UNICODE_UTF8_ERR_INVALID;
    }

    if (cp <= 0x7Fu) {
        if (dst_len < 1) return UNICODE_UTF8_ERR_NOSPACE;
        dst[0] = (uint8_t)cp;
        *written = 1;
        return UNICODE_UTF8_OK;
    }
    if (cp <= 0x7FFu) {
        if (dst_len < 2) return UNICODE_UTF8_ERR_NOSPACE;
        dst[0] = (uint8_t)(0xC0u | (cp >> 6u));
        dst[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        *written = 2;
        return UNICODE_UTF8_OK;
    }
    if (cp <= 0xFFFFu) {
        if (dst_len < 3) return UNICODE_UTF8_ERR_NOSPACE;
        dst[0] = (uint8_t)(0xE0u | (cp >> 12u));
        dst[1] = (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu));
        dst[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        *written = 3;
        return UNICODE_UTF8_OK;
    }

    if (dst_len < 4) return UNICODE_UTF8_ERR_NOSPACE;
    dst[0] = (uint8_t)(0xF0u | (cp >> 18u));
    dst[1] = (uint8_t)(0x80u | ((cp >> 12u) & 0x3Fu));
    dst[2] = (uint8_t)(0x80u | ((cp >> 6u) & 0x3Fu));
    dst[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    *written = 4;
    return UNICODE_UTF8_OK;
}
