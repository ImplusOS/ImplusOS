#include "UTF8.h"

static inline int is_continuation(uint8_t b) {
    return (b & 0xC0) == 0x80;
}

static inline int is_overlong(uint32_t cp, int len) {
    if (len == 2 && cp < 0x80) return 1;
    if (len == 3 && cp < 0x800) return 1;
    if (len == 4 && cp < 0x10000) return 1;
    return 0;
}

static int decode_one(const uint8_t *p, int *out_len, uint32_t *out_cp) {
    uint8_t b0 = p[0];
    if (b0 < 0x80) {
        *out_len = 1;
        *out_cp = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = p[1];
        if (!is_continuation(b1)) return 0;
        uint32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        if (is_overlong(cp, 2)) return 0;
        *out_len = 2;
        *out_cp = cp;
        return 1;
    }
    if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        if (!is_continuation(b1) || !is_continuation(b2)) return 0;
        uint32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        if (is_overlong(cp, 3)) return 0;
        if (cp >= UNICODE_SURR_HIGH_START && cp <= UNICODE_SURR_LOW_END) return 0;
        *out_len = 3;
        *out_cp = cp;
        return 1;
    }
    if ((b0 & 0xF8) == 0xF0) {
        uint8_t b1 = p[1];
        uint8_t b2 = p[2];
        uint8_t b3 = p[3];
        if (!is_continuation(b1) || !is_continuation(b2) || !is_continuation(b3)) return 0;
        uint32_t cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) |
                      ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        if (is_overlong(cp, 4)) return 0;
        if (cp > UNICODE_MAX) return 0;
        *out_len = 4;
        *out_cp = cp;
        return 1;
    }
    return 0;
}

int utf8_seq_len(uint8_t lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

int utf8_codepoint_len(utf8_codepoint_t cp) {
    if (cp <= 0x7F) return 1;
    if (cp <= 0x7FF) return 2;
    if (cp <= 0xFFFF) return 3;
    if (cp <= UNICODE_MAX) return 4;
    return 0;
}

int unicode_is_valid_cp(utf8_codepoint_t cp) {
    if (cp > UNICODE_MAX) return 0;
    if (cp >= UNICODE_SURR_HIGH_START && cp <= UNICODE_SURR_LOW_END) return 0;
    return 1;
}

utf8_codepoint_t utf8_decode(const char **s) {
    if (!s || !*s) return 0;
    const uint8_t *p = (const uint8_t *)*s;
    if (*p == 0) return 0;
    int len = 0;
    uint32_t cp = 0;
    if (!decode_one(p, &len, &cp)) {
        *s = (const char *)(p + 1);
        return UTF8_REPLACEMENT_CHAR;
    }
    *s = (const char *)(p + len);
    return (utf8_codepoint_t)cp;
}

utf8_status_t utf8_next(const char **s,
                        const char *end,
                        utf8_codepoint_t *cp)
{
    if (!s || !*s || !cp)
        return UTF8_ERR_NULL;

    const uint8_t *p = (const uint8_t *)*s;

    if (end && p >= (const uint8_t *)end)
        return UTF8_ERR_INVALID;

    int len = 0;
    uint32_t val = 0;

    if (!decode_one(p, &len, &val)) {
        *cp = UTF8_REPLACEMENT_CHAR;
        *s = (const char *)(p + 1);
        return UTF8_ERR_INVALID;
    }

    if (end && (p + len > (const uint8_t *)end)) {
        *cp = UTF8_REPLACEMENT_CHAR;
        *s = (const char *)(p + 1);
        return UTF8_ERR_INVALID;
    }

    *cp = (utf8_codepoint_t)val;
    *s = (const char *)(p + len);

    return UTF8_OK;
}

int utf8_encode(utf8_codepoint_t cp, char *buf) {
    if (!buf) return 0;
    if (!unicode_is_valid_cp(cp)) return 0;
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        buf[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp <= UNICODE_MAX) {
        buf[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

utf8_codepoint_t utf8_prev(const char **s, const char *start) {
    if (!s || !*s || !start) return UTF8_REPLACEMENT_CHAR;
    const char *p = *s;
    if (p <= start) return UTF8_REPLACEMENT_CHAR;
    do {
        p--;
    } while (p > start && is_continuation((uint8_t)*p));
    const uint8_t *ptr = (const uint8_t *)p;
    int len = 0;
    uint32_t cp = 0;
    if (!decode_one(ptr, &len, &cp) || (ptr + len) > (const uint8_t *)*s) {
        *s = p;
        return UTF8_REPLACEMENT_CHAR;
    }
    *s = p;
    return (utf8_codepoint_t)cp;
}

size_t utf8_strlen(const char *s) {
    size_t count = 0;
    const char *p = s;
    while (*p) {
        const uint8_t *u = (const uint8_t *)p;
        int len = utf8_seq_len(*u);
        if (len == 0) len = 1;
        p += len;
        count++;
    }
    return count;
}

size_t utf8_strnlen(const char *s, size_t max_bytes) {
    size_t count = 0;
    const char *p = s;
    size_t remaining = max_bytes;
    while (remaining && *p) {
        const uint8_t *u = (const uint8_t *)p;
        int len = utf8_seq_len(*u);
        if (len == 0 || len > remaining) len = 1;
        p += len;
        remaining -= len;
        count++;
    }
    return count;
}

const char *utf8_index(const char *s, size_t n) {
    size_t i = 0;
    const char *p = s;
    while (*p && i < n) {
        const uint8_t *u = (const uint8_t *)p;
        int len = utf8_seq_len(*u);
        if (len == 0) len = 1;
        p += len;
        i++;
    }
    return (i == n) ? p : NULL;
}

int utf8_valid(const char *s, size_t len) {
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    while (p < end) {
        int seq_len = utf8_seq_len(*p);
        if (seq_len == 0 || p + seq_len > end) return 0;
        for (int i = 1; i < seq_len; ++i) {
            if (!is_continuation(p[i])) return 0;
        }
        uint32_t cp = 0;
        int dummy_len = 0;
        if (!decode_one(p, &dummy_len, &cp)) return 0;
        p += seq_len;
    }
    return 1;
}

int utf8_has_bom(const char *s) {
    if (!s) return 0;
    const uint8_t *u = (const uint8_t *)s;
    return u[0] == UTF8_BOM_B0 && u[1] == UTF8_BOM_B1 && u[2] == UTF8_BOM_B2;
}

const char *utf8_skip_bom(const char *s) {
    return utf8_has_bom(s) ? s + 3 : s;
}

size_t utf8_to_codepoints(const char *s, size_t len, utf8_codepoint_t *cps, size_t max_cps) {
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    size_t out = 0;
    while (p < end && out < max_cps) {
        int seq_len = utf8_seq_len(*p);
        if (seq_len == 0 || p + seq_len > end) {
            cps[out++] = UTF8_REPLACEMENT_CHAR;
            p++;
            continue;
        }
        uint32_t cp = 0;
        int dummy = 0;
        if (!decode_one(p, &dummy, &cp)) {
            cps[out++] = UTF8_REPLACEMENT_CHAR;
            p += seq_len;
            continue;
        }
        cps[out++] = (utf8_codepoint_t)cp;
        p += seq_len;
    }
    return out;
}

size_t utf8_from_codepoints(const utf8_codepoint_t *cps, size_t count, char *buf, size_t size) {
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        char tmp[4];
        int len = utf8_encode(cps[i], tmp);
        if (len == 0) continue;
        if (written + len >= size) return (size_t)-1;
        for (int j = 0; j < len; ++j) buf[written++] = tmp[j];
    }
    if (written < size) buf[written] = '\0';
    return written;
}

utf8_status_t utf16_next(const uint16_t **s, const uint16_t *end, utf8_codepoint_t *cp) {
    if (!s || !*s || !end || !cp) return UTF8_ERR_NULL;
    const uint16_t *p = *s;
    if (p >= end) return UTF8_ERR_INVALID;
    uint16_t w1 = *p++;
    if (w1 >= UNICODE_SURR_HIGH_START && w1 <= UNICODE_SURR_HIGH_END) {
        if (p >= end) return UTF8_ERR_INVALID;
        uint16_t w2 = *p;
        if (w2 < UNICODE_SURR_LOW_START || w2 > UNICODE_SURR_LOW_END) return UTF8_ERR_INVALID;
        *cp = ((w1 - UNICODE_SURR_HIGH_START) << 10) + (w2 - UNICODE_SURR_LOW_START) + 0x10000;
        *s = p + 1;
        return UTF8_OK;
    }
    if (w1 >= UNICODE_SURR_LOW_START && w1 <= UNICODE_SURR_LOW_END) {
        return UTF8_ERR_INVALID;
    }
    *cp = w1;
    *s = p;
    return UTF8_OK;
}

int utf16_encode(utf8_codepoint_t cp, uint16_t *buf) {
    if (!buf) return 0;
    if (!unicode_is_valid_cp(cp)) return 0;
    if (cp < 0x10000) {
        buf[0] = (uint16_t)cp;
        return 1;
    }
    cp -= 0x10000;
    buf[0] = (uint16_t)((cp >> 10) + UNICODE_SURR_HIGH_START);
    buf[1] = (uint16_t)((cp & 0x3FF) + UNICODE_SURR_LOW_START);
    return 2;
}

size_t utf8_to_utf16(const char *src, uint16_t *dst, size_t dst_count) {
    const char *p = src;
    size_t written = 0;
    while (*p && written < dst_count) {
        utf8_codepoint_t cp = (utf8_codepoint_t)utf8_decode(&p);
        int units = utf16_encode(cp, &dst[written]);
        if (units == 0) return (size_t)-1;
        written += units;
    }
    if (written < dst_count) dst[written] = 0;
    return written;
}

size_t utf16_to_utf8(const uint16_t *src, char *dst, size_t dst_size) {
    const uint16_t *p = src;
    size_t written = 0;
    while (*p) {
        utf8_codepoint_t cp = 0;
        if (*p >= UNICODE_SURR_HIGH_START && *p <= UNICODE_SURR_HIGH_END) {
            uint16_t high = *p++;
            uint16_t low = *p;
            if (low < UNICODE_SURR_LOW_START || low > UNICODE_SURR_LOW_END) return (size_t)-1;
            cp = ((high - UNICODE_SURR_HIGH_START) << 10) + (low - UNICODE_SURR_LOW_START) + 0x10000;
            p++;
        } else {
            cp = *p++;
        }
        char tmp[4];
        int len = utf8_encode(cp, tmp);
        if (len == 0) return (size_t)-1;
        if (written + len >= dst_size) return (size_t)-1;
        for (int i = 0; i < len; ++i) dst[written++] = tmp[i];
    }
    if (written < dst_size) dst[written] = '\0';
    return written;
}

int utf8_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        utf8_codepoint_t ca = (utf8_codepoint_t)utf8_decode(&a);
        utf8_codepoint_t cb = (utf8_codepoint_t)utf8_decode(&b);
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int utf8_strncmp(const char *a, const char *b, size_t n) {
    size_t i = 0;
    while (i < n && *a && *b) {
        utf8_codepoint_t ca = (utf8_codepoint_t)utf8_decode(&a);
        utf8_codepoint_t cb = (utf8_codepoint_t)utf8_decode(&b);
        if (ca != cb) return (ca < cb) ? -1 : 1;
        ++i;
    }
    if (i == n) return 0;
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

static utf8_codepoint_t ascii_casefold(utf8_codepoint_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    return cp;
}

int utf8_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        utf8_codepoint_t ca = ascii_casefold((utf8_codepoint_t)utf8_decode(&a));
        utf8_codepoint_t cb = ascii_casefold((utf8_codepoint_t)utf8_decode(&b));
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

const char *utf8_strchr(const char *s, utf8_codepoint_t cp) {
    while (*s) {
        const char *pos = s;
        utf8_codepoint_t cur = (utf8_codepoint_t)utf8_decode(&s);
        if (cur == cp) return pos;
    }
    return NULL;
}

const char *utf8_strrchr(const char *s, utf8_codepoint_t cp) {
    const char *last = NULL;
    while (*s) {
        const char *pos = s;
        utf8_codepoint_t cur = (utf8_codepoint_t)utf8_decode(&s);
        if (cur == cp) last = pos;
    }
    return last;
}

const char *utf8_strstr(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    size_t needle_len = utf8_strlen(needle);
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        size_t i = 0;
        while (i < needle_len && *h && *n) {
            utf8_codepoint_t ch = (utf8_codepoint_t)utf8_decode(&h);
            utf8_codepoint_t cn = (utf8_codepoint_t)utf8_decode(&n);
            if (ch != cn) break;
            ++i;
        }
        if (i == needle_len) return haystack;
        utf8_decode(&haystack);
    }
    return NULL;
}

static int is_ascii_alpha(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}
static int is_ascii_digit(uint32_t cp) {
    return (cp >= '0' && cp <= '9');
}
static int is_ascii_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' || cp == '\v';
}
static int is_ascii_upper(uint32_t cp) { return (cp >= 'A' && cp <= 'Z'); }
static int is_ascii_lower(uint32_t cp) { return (cp >= 'a' && cp <= 'z'); }
static int is_ascii_print(uint32_t cp) { return cp >= 0x20 && cp < 0x7F; }
static int is_ascii_punct(uint32_t cp) {
    return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
           (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E);
}
static int is_ascii_control(uint32_t cp) { return cp < 0x20 || cp == 0x7F; }

int unicode_is_alpha(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_alpha(cp);
    if ((cp >= 0x0410 && cp <= 0x044F) ||
        (cp >= 0x0391 && cp <= 0x03C9) ||
        (cp >= 0x0410 && cp <= 0x042F) ||
        (cp >= 0x0531 && cp <= 0x0587)) return 1;
    return 0;
}

int unicode_is_digit(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_digit(cp);
    if ((cp >= 0x0660 && cp <= 0x0669) ||
        (cp >= 0x06F0 && cp <= 0x06F9) ||
        (cp >= 0x0966 && cp <= 0x096F) ||
        (cp >= 0x0E50 && cp <= 0x0E59))
        return 1;
    return 0;
}

int unicode_is_alnum(utf8_codepoint_t cp) {
    return unicode_is_alpha(cp) || unicode_is_digit(cp);
}

int unicode_is_space(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_space(cp);
    if (cp == 0x00A0 || cp == 0x1680 || cp == 0x2000 || cp == 0x2001 ||
        cp == 0x2002 || cp == 0x2003 || cp == 0x2004 || cp == 0x2005 ||
        cp == 0x2006 || cp == 0x2007 || cp == 0x2008 || cp == 0x2009 ||
        cp == 0x200A || cp == 0x202F || cp == 0x205F || cp == 0x3000)
        return 1;
    return 0;
}

int unicode_is_upper(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_upper(cp);
    if (cp >= 0x0410 && cp <= 0x042F) return 1;
    return 0;
}

int unicode_is_lower(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_lower(cp);
    if (cp >= 0x0430 && cp <= 0x044F) return 1;
    return 0;
}

int unicode_is_print(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_print(cp);
    return (cp >= 0xA0 && cp != 0xAD && cp != 0xFFFE && cp != 0xFFFF);
}

int unicode_is_punct(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_punct(cp);
    if ((cp >= 0x2000 && cp <= 0x206F) ||
        (cp >= 0x2E00 && cp <= 0x2E7F))
        return 1;
    return 0;
}

int unicode_is_control(utf8_codepoint_t cp) {
    if (cp < 0x80) return is_ascii_control(cp);
    return (cp <= 0x1F) || (cp >= 0x7F && cp <= 0x9F);
}

utf8_codepoint_t unicode_to_upper(utf8_codepoint_t cp) {
    if (cp < 0x80) {
        if (is_ascii_lower(cp)) return cp - ('a' - 'A');
        return cp;
    }
    if (cp >= 0x0430 && cp <= 0x044F) return cp - 0x20;
    return cp;
}

utf8_codepoint_t unicode_to_lower(utf8_codepoint_t cp) {
    if (cp < 0x80) {
        if (is_ascii_upper(cp)) return cp + ('a' - 'A');
        return cp;
    }
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    return cp;
}

utf8_codepoint_t unicode_casefold(utf8_codepoint_t cp) {
    return unicode_to_lower(cp);
}

int unicode_digit_value(utf8_codepoint_t cp) {
    if (cp >= '0' && cp <= '9') return cp - '0';
    if (cp >= 0x0660 && cp <= 0x0669) return cp - 0x0660;
    if (cp >= 0x06F0 && cp <= 0x06F9) return cp - 0x06F0;
    if (cp >= 0x0966 && cp <= 0x096F) return cp - 0x0966;
    if (cp >= 0x0E50 && cp <= 0x0E59) return cp - 0x0E50;
    return -1;
}