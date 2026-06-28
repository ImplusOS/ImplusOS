#include <iconv.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ICONV_ENC_UTF8,
    ICONV_ENC_ASCII,
    ICONV_ENC_LATIN1,
    ICONV_ENC_CP1252,
    ICONV_ENC_UCS4,
} iconv_encoding_t;

typedef struct {
    iconv_encoding_t from;
    iconv_encoding_t to;
    int translit;
} iconv_desc_t;

static const uint16_t cp1252_to_unicode[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static int iconv_ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int iconv_name_has_suffix(const char* name, const char* suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);

    if (name_len < suffix_len) {
        return 0;
    }
    name += name_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (iconv_ascii_tolower((unsigned char)name[i]) !=
            iconv_ascii_tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static void iconv_normalise_name(const char* name, char* out, size_t out_size)
{
    size_t w = 0;

    if (out_size == 0) {
        return;
    }
    if (!name) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; name[i] != '\0' && name[i] != '/'; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c == '-' || c == '_' || c == ' ' || c == '.') {
            continue;
        }
        if (w + 1 < out_size) {
            out[w++] = (char)iconv_ascii_tolower(c);
        }
    }
    out[w] = '\0';
}

static int iconv_parse_encoding(const char* name, iconv_encoding_t* enc)
{
    char normal[48];

    iconv_normalise_name(name, normal, sizeof(normal));
    if (strcmp(normal, "utf8") == 0) {
        *enc = ICONV_ENC_UTF8;
        return 1;
    }
    if (strcmp(normal, "ascii") == 0 ||
        strcmp(normal, "usascii") == 0 ||
        strcmp(normal, "ansi") == 0) {
        *enc = ICONV_ENC_ASCII;
        return 1;
    }
    if (strcmp(normal, "latin1") == 0 ||
        strcmp(normal, "l1") == 0 ||
        strcmp(normal, "iso88591") == 0 ||
        strcmp(normal, "isoir100") == 0 ||
        strcmp(normal, "cp819") == 0 ||
        strcmp(normal, "ibm819") == 0) {
        *enc = ICONV_ENC_LATIN1;
        return 1;
    }
    if (strcmp(normal, "cp1252") == 0 ||
        strcmp(normal, "windows1252") == 0 ||
        strcmp(normal, "msansi") == 0) {
        *enc = ICONV_ENC_CP1252;
        return 1;
    }
    if (strcmp(normal, "ucs4") == 0 ||
        strcmp(normal, "utf32") == 0 ||
        strcmp(normal, "iso10646ucs4") == 0) {
        *enc = ICONV_ENC_UCS4;
        return 1;
    }
    return 0;
}

static int iconv_cp1252_from_unicode(uint32_t cp, unsigned char* out)
{
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
        *out = (unsigned char)cp;
        return 1;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (cp1252_to_unicode[i] == cp) {
            *out = (unsigned char)(0x80u + i);
            return 1;
        }
    }
    return 0;
}

static int iconv_read_utf8(const unsigned char* in, size_t len,
                           uint32_t* cp, size_t* used)
{
    unsigned char c0;

    if (len == 0) {
        errno = EINVAL;
        return 0;
    }
    c0 = in[0];
    if (c0 < 0x80) {
        *cp = c0;
        *used = 1;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0) {
        if (len < 2) {
            errno = EINVAL;
            return 0;
        }
        if ((in[1] & 0xC0) != 0x80) {
            errno = EILSEQ;
            return 0;
        }
        *cp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(in[1] & 0x3F);
        if (*cp < 0x80) {
            errno = EILSEQ;
            return 0;
        }
        *used = 2;
        return 1;
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (len < 3) {
            errno = EINVAL;
            return 0;
        }
        if ((in[1] & 0xC0) != 0x80 || (in[2] & 0xC0) != 0x80) {
            errno = EILSEQ;
            return 0;
        }
        *cp = ((uint32_t)(c0 & 0x0F) << 12) |
              ((uint32_t)(in[1] & 0x3F) << 6) |
              (uint32_t)(in[2] & 0x3F);
        if (*cp < 0x800 || (*cp >= 0xD800 && *cp <= 0xDFFF)) {
            errno = EILSEQ;
            return 0;
        }
        *used = 3;
        return 1;
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (len < 4) {
            errno = EINVAL;
            return 0;
        }
        if ((in[1] & 0xC0) != 0x80 ||
            (in[2] & 0xC0) != 0x80 ||
            (in[3] & 0xC0) != 0x80) {
            errno = EILSEQ;
            return 0;
        }
        *cp = ((uint32_t)(c0 & 0x07) << 18) |
              ((uint32_t)(in[1] & 0x3F) << 12) |
              ((uint32_t)(in[2] & 0x3F) << 6) |
              (uint32_t)(in[3] & 0x3F);
        if (*cp < 0x10000 || *cp > 0x10FFFF) {
            errno = EILSEQ;
            return 0;
        }
        *used = 4;
        return 1;
    }
    errno = EILSEQ;
    return 0;
}

static int iconv_write_utf8(uint32_t cp, unsigned char* out,
                            size_t out_left, size_t* written)
{
    if (cp < 0x80) {
        if (out_left < 1) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)cp;
        *written = 1;
        return 1;
    }
    if (cp < 0x800) {
        if (out_left < 2) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        *written = 2;
        return 1;
    }
    if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            errno = EILSEQ;
            return 0;
        }
        if (out_left < 3) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        *written = 3;
        return 1;
    }
    if (cp <= 0x10FFFF) {
        if (out_left < 4) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)(0xF0 | (cp >> 18));
        out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (cp & 0x3F));
        *written = 4;
        return 1;
    }
    errno = EILSEQ;
    return 0;
}

static int iconv_read_char(iconv_encoding_t enc, const unsigned char* in,
                           size_t len, uint32_t* cp, size_t* used)
{
    if (len == 0) {
        errno = EINVAL;
        return 0;
    }

    switch (enc) {
    case ICONV_ENC_UTF8:
        return iconv_read_utf8(in, len, cp, used);
    case ICONV_ENC_ASCII:
        if (in[0] > 0x7F) {
            errno = EILSEQ;
            return 0;
        }
        *cp = in[0];
        *used = 1;
        return 1;
    case ICONV_ENC_LATIN1:
        *cp = in[0];
        *used = 1;
        return 1;
    case ICONV_ENC_CP1252:
        *cp = (in[0] >= 0x80 && in[0] <= 0x9F) ?
              cp1252_to_unicode[in[0] - 0x80] : in[0];
        *used = 1;
        return 1;
    case ICONV_ENC_UCS4:
        if (len < 4) {
            errno = EINVAL;
            return 0;
        }
        *cp = ((uint32_t)in[0]) |
              ((uint32_t)in[1] << 8) |
              ((uint32_t)in[2] << 16) |
              ((uint32_t)in[3] << 24);
        *used = 4;
        return 1;
    }
    errno = EINVAL;
    return 0;
}

static int iconv_write_char(iconv_encoding_t enc, uint32_t cp, int translit,
                            unsigned char* out, size_t out_left,
                            size_t* written)
{
    unsigned char mapped;

    switch (enc) {
    case ICONV_ENC_UTF8:
        return iconv_write_utf8(cp, out, out_left, written);
    case ICONV_ENC_ASCII:
        if (cp > 0x7F) {
            if (!translit) {
                errno = EILSEQ;
                return 0;
            }
            cp = '?';
        }
        if (out_left < 1) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)cp;
        *written = 1;
        return 1;
    case ICONV_ENC_LATIN1:
        if (cp > 0xFF) {
            if (!translit) {
                errno = EILSEQ;
                return 0;
            }
            cp = '?';
        }
        if (out_left < 1) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)cp;
        *written = 1;
        return 1;
    case ICONV_ENC_CP1252:
        if (!iconv_cp1252_from_unicode(cp, &mapped)) {
            if (!translit) {
                errno = EILSEQ;
                return 0;
            }
            mapped = '?';
        }
        if (out_left < 1) {
            errno = E2BIG;
            return 0;
        }
        out[0] = mapped;
        *written = 1;
        return 1;
    case ICONV_ENC_UCS4:
        if (out_left < 4) {
            errno = E2BIG;
            return 0;
        }
        out[0] = (unsigned char)(cp & 0xFF);
        out[1] = (unsigned char)((cp >> 8) & 0xFF);
        out[2] = (unsigned char)((cp >> 16) & 0xFF);
        out[3] = (unsigned char)((cp >> 24) & 0xFF);
        *written = 4;
        return 1;
    }
    errno = EINVAL;
    return 0;
}

iconv_t iconv_open(const char* tocode, const char* fromcode)
{
    iconv_desc_t* desc;
    iconv_encoding_t from;
    iconv_encoding_t to;

    if (!iconv_parse_encoding(fromcode, &from) ||
        !iconv_parse_encoding(tocode, &to)) {
        errno = EINVAL;
        return (iconv_t)-1;
    }

    desc = (iconv_desc_t*)malloc(sizeof(*desc));
    if (!desc) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    desc->from = from;
    desc->to = to;
    desc->translit = iconv_name_has_suffix(tocode, "//TRANSLIT");
    return (iconv_t)desc;
}

size_t iconv(iconv_t cd, char** inbuf, size_t* inbytesleft,
             char** outbuf, size_t* outbytesleft)
{
    iconv_desc_t* desc = (iconv_desc_t*)cd;
    unsigned char* in;
    unsigned char* out;
    size_t in_left;
    size_t out_left;
    size_t substitutions = 0;

    if (cd == (iconv_t)-1 || !desc) {
        errno = EBADF;
        return (size_t)-1;
    }
    if (!inbuf || !*inbuf) {
        return 0;
    }
    if (!inbytesleft || !outbuf || !*outbuf || !outbytesleft) {
        errno = EINVAL;
        return (size_t)-1;
    }

    in = (unsigned char*)*inbuf;
    out = (unsigned char*)*outbuf;
    in_left = *inbytesleft;
    out_left = *outbytesleft;

    while (in_left > 0) {
        uint32_t cp;
        size_t used = 0;
        size_t written = 0;
        int translit_before = substitutions;

        if (!iconv_read_char(desc->from, in, in_left, &cp, &used)) {
            break;
        }
        if (!iconv_write_char(desc->to, cp, desc->translit,
                              out, out_left, &written)) {
            if (errno == EILSEQ && desc->translit) {
                substitutions++;
            }
            break;
        }
        if (desc->translit && cp == '?' && desc->from != desc->to) {
            substitutions += (translit_before == (int)substitutions) ? 0u : 1u;
        }
        in += used;
        in_left -= used;
        out += written;
        out_left -= written;
    }

    *inbuf = (char*)in;
    *inbytesleft = in_left;
    *outbuf = (char*)out;
    *outbytesleft = out_left;

    if (in_left != 0) {
        return (size_t)-1;
    }
    return substitutions;
}

int iconv_close(iconv_t cd)
{
    if (cd == (iconv_t)-1 || !cd) {
        errno = EBADF;
        return -1;
    }
    free((void*)cd);
    return 0;
}

