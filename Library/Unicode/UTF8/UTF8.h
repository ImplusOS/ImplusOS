#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint32_t utf8_codepoint_t;
#define UTF8_REPLACEMENT_CHAR   ((utf8_codepoint_t)0xFFFDu)
#define UNICODE_MAX             ((utf8_codepoint_t)0x10FFFFu)

#define UNICODE_SURR_HIGH_START ((utf8_codepoint_t)0xD800u)
#define UNICODE_SURR_HIGH_END   ((utf8_codepoint_t)0xDBFFu)

#define UNICODE_SURR_LOW_START  ((utf8_codepoint_t)0xDC00u)
#define UNICODE_SURR_LOW_END    ((utf8_codepoint_t)0xDFFFu)

#define UTF8_BOM_B0  ((uint8_t)0xEFu)
#define UTF8_BOM_B1  ((uint8_t)0xBBu)
#define UTF8_BOM_B2  ((uint8_t)0xBFu)

typedef enum {
    UTF8_OK            =  0,
    UTF8_ERR_NULL      = -1,
    UTF8_ERR_INVALID   = -2,
    UTF8_ERR_OVERFLOW  = -3,
    UTF8_ERR_SURROGATE = -4,
    UTF8_ERR_RANGE     = -5,
} utf8_status_t;

int utf8_seq_len(uint8_t lead);
int utf8_codepoint_len(utf8_codepoint_t cp);
int unicode_is_valid_cp(utf8_codepoint_t cp);
utf8_status_t utf8_next(const char **s, const char *end,
                         utf8_codepoint_t *cp);
int utf8_encode(utf8_codepoint_t cp, char *buf);
utf8_codepoint_t utf8_prev(const char **s, const char *start);
size_t utf8_strlen(const char *s);
size_t utf8_strnlen(const char *s, size_t max_bytes);
const char *utf8_index(const char *s, size_t n);
int utf8_valid(const char *s, size_t len);
int utf8_has_bom(const char *s);
const char *utf8_skip_bom(const char *s);
size_t utf8_to_codepoints(const char *s, size_t len,
                           utf8_codepoint_t *cps, size_t max_cps);
size_t utf8_from_codepoints(const utf8_codepoint_t *cps, size_t count,
                             char *buf, size_t size);
utf8_status_t utf16_next(const uint16_t **s, const uint16_t *end,
                          utf8_codepoint_t *cp);
int utf16_encode(utf8_codepoint_t cp, uint16_t *buf);
size_t utf8_to_utf16(const char *src, uint16_t *dst, size_t dst_count);
size_t utf16_to_utf8(const uint16_t *src, char *dst, size_t dst_size);
int utf8_strcmp(const char *a, const char *b);
int utf8_strncmp(const char *a, const char *b, size_t n);
int utf8_strcasecmp(const char *a, const char *b);
const char *utf8_strchr(const char *s, utf8_codepoint_t cp);
const char *utf8_strrchr(const char *s, utf8_codepoint_t cp);
const char *utf8_strstr(const char *haystack, const char *needle);
int unicode_is_alpha(utf8_codepoint_t cp);
int unicode_is_digit(utf8_codepoint_t cp);
int unicode_is_alnum(utf8_codepoint_t cp);
int unicode_is_space(utf8_codepoint_t cp);
int unicode_is_upper(utf8_codepoint_t cp);
int unicode_is_lower(utf8_codepoint_t cp);
int unicode_is_print(utf8_codepoint_t cp);
int unicode_is_punct(utf8_codepoint_t cp);
int unicode_is_control(utf8_codepoint_t cp);
utf8_codepoint_t unicode_to_upper(utf8_codepoint_t cp);
utf8_codepoint_t unicode_to_lower(utf8_codepoint_t cp);
utf8_codepoint_t unicode_casefold(utf8_codepoint_t cp);
int unicode_digit_value(utf8_codepoint_t cp);