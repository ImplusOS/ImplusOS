#include "CSPRNG.h"
#include <string.h>

static uint8_t g_key[32];
static uint8_t g_nonce[12];
static uint64_t g_counter;

extern void crypto_chacha20_block(const uint8_t key[32], uint32_t counter,
                                  const uint8_t nonce[12], uint8_t out[64]);

static inline uint32_t rotl32_cs(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

static inline void qr_cs(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = rotl32_cs(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32_cs(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32_cs(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32_cs(*b, 7);
}

static void csprng_generate_block(uint8_t out[64])
{
    uint32_t s[16];
    uint32_t counter_low = (uint32_t)(g_counter & 0xFFFFFFFF);
    uint32_t counter_high = (uint32_t)((g_counter >> 32) & 0xFFFFFFFF);

    s[0] = 0x61707865; s[1] = 0x3320646e;
    s[2] = 0x79622d32; s[3] = 0x6b206574;

    for (int i = 0; i < 8; ++i) {
        s[4 + i] = ((uint32_t)g_key[i * 4 + 0]) |
                   ((uint32_t)g_key[i * 4 + 1] << 8) |
                   ((uint32_t)g_key[i * 4 + 2] << 16) |
                   ((uint32_t)g_key[i * 4 + 3] << 24);
    }

    s[12] = counter_low;
    s[13] = counter_high;

    s[14] = ((uint32_t)g_nonce[0]) |
            ((uint32_t)g_nonce[1] << 8) |
            ((uint32_t)g_nonce[2] << 16) |
            ((uint32_t)g_nonce[3] << 24);
    s[15] = ((uint32_t)g_nonce[4]) |
            ((uint32_t)g_nonce[5] << 8) |
            ((uint32_t)g_nonce[6] << 16) |
            ((uint32_t)g_nonce[7] << 24);

    uint32_t x[16];
    memcpy(x, s, sizeof(x));

    for (int i = 0; i < 10; ++i) {
        qr_cs(&x[0], &x[4], &x[8], &x[12]);
        qr_cs(&x[1], &x[5], &x[9], &x[13]);
        qr_cs(&x[2], &x[6], &x[10], &x[14]);
        qr_cs(&x[3], &x[7], &x[11], &x[15]);
        qr_cs(&x[0], &x[5], &x[10], &x[15]);
        qr_cs(&x[1], &x[6], &x[11], &x[12]);
        qr_cs(&x[2], &x[7], &x[8], &x[13]);
        qr_cs(&x[3], &x[4], &x[9], &x[14]);
    }

    for (int i = 0; i < 16; ++i) {
        x[i] += s[i];
        out[i * 4 + 0] = (uint8_t)(x[i] & 0xFF);
        out[i * 4 + 1] = (uint8_t)((x[i] >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((x[i] >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((x[i] >> 24) & 0xFF);
    }

    ++g_counter;
}

void crypto_csprng_seed(const uint8_t seed[32], const uint8_t nonce[8])
{
    if (seed != NULL) {
        memcpy(g_key, seed, 32);
    }
    if (nonce != NULL) {
        memset(g_nonce, 0, 12);
        memcpy(g_nonce, nonce, 8);
    }
    g_counter = 0;
}

void crypto_csprng_reseed(const uint8_t seed[32], const uint8_t nonce[8])
{
    uint8_t new_key[32];
    csprng_generate_block(new_key);

    for (int i = 0; i < 32; ++i) {
        new_key[i] ^= seed[i];
    }

    crypto_csprng_seed(new_key, nonce);
    memset(new_key, 0, sizeof(new_key));
}

void crypto_csprng_generate(uint8_t *out, size_t len)
{
    if (out == NULL || len == 0) return;

    uint8_t block[64];
    size_t offset = 0;

    while (offset < len) {
        csprng_generate_block(block);
        size_t todo = len - offset;
        if (todo > 64) todo = 64;
        memcpy(out + offset, block, todo);
        offset += todo;
    }

    memset(block, 0, sizeof(block));
}
