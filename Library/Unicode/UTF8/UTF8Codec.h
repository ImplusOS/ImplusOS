#pragma once

#include <stddef.h>
#include <stdint.h>

#define UNICODE_UTF8_MAX ((uint32_t)0x10FFFFu)

typedef enum {
    UNICODE_UTF8_OK = 0,
    UNICODE_UTF8_ERR_NULL = -1,
    UNICODE_UTF8_ERR_INVALID = -2,
    UNICODE_UTF8_ERR_INCOMPLETE = -3,
    UNICODE_UTF8_ERR_NOSPACE = -4,
} unicode_utf8_status_t;

int unicode_utf8_seq_len(uint8_t lead);
int unicode_utf8_is_valid_cp(uint32_t cp);
unicode_utf8_status_t unicode_utf8_decode_next(const uint8_t *src,
                                               size_t src_len,
                                               uint32_t *cp,
                                               size_t *used);
unicode_utf8_status_t unicode_utf8_encode(uint32_t cp,
                                          uint8_t *dst,
                                          size_t dst_len,
                                          size_t *written);
