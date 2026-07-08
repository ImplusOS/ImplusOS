#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CP932_OK = 0,
    CP932_ERR_NULL = -1,
    CP932_ERR_INVALID = -2,
    CP932_ERR_INCOMPLETE = -3,
    CP932_ERR_NOSPACE = -4,
} cp932_status_t;

cp932_status_t cp932_decode_next(const uint8_t *src, size_t src_len,
                                  uint32_t *cp, size_t *used);
cp932_status_t cp932_encode(uint32_t cp, uint8_t *dst, size_t dst_len,
                            size_t *written);
