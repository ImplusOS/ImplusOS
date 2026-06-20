#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UUID_SIZE           16
#define UUID_STR_LEN        36
#define UUID_STR_BUF_SIZE   37

#define UUID_VERSION_1      0x10
#define UUID_VERSION_2      0x20
#define UUID_VERSION_3      0x30
#define UUID_VERSION_4      0x40
#define UUID_VERSION_5      0x50
#define UUID_VERSION_7      0x70
#define UUID_VERSION_8      0x80

#define UUID_VARIANT_NCS    0x00
#define UUID_VARIANT_RFC    0x80
#define UUID_VARIANT_MICRO  0xC0
#define UUID_VARIANT_FUTURE 0xE0

typedef struct {
    uint8_t data[UUID_SIZE];
} uuid_t;

static const uuid_t UUID_NIL = { { 0 } };

int uuid_generate_v4(uuid_t *uuid);
int uuid_generate_v4_from_csprng(uuid_t *uuid, void (*rng)(uint8_t *, size_t));
int uuid_from_string(const char *str, uuid_t *uuid);
void uuid_to_string(const uuid_t *uuid, char buf[UUID_STR_BUF_SIZE]);
int uuid_compare(const uuid_t *a, const uuid_t *b);
int uuid_is_nil(const uuid_t *uuid);
void uuid_copy(uuid_t *dst, const uuid_t *src);
int uuid_version(const uuid_t *uuid);
int uuid_variant(const uuid_t *uuid);

#ifdef __cplusplus
}
#endif
