#include "ChaCha20_Poly1305.h"
#include <string.h>

static inline uint32_t rotl32(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32 - n));
}

static inline void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

static void chacha20_block(const uint8_t key[32], uint32_t counter,
                           const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t s[16];
    const uint32_t constants[4] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574 };

    s[0] = constants[0];
    s[1] = constants[1];
    s[2] = constants[2];
    s[3] = constants[3];

    for (int i = 0; i < 8; ++i) {
        s[4 + i] = ((uint32_t)key[i * 4 + 0]) |
                   ((uint32_t)key[i * 4 + 1] << 8) |
                   ((uint32_t)key[i * 4 + 2] << 16) |
                   ((uint32_t)key[i * 4 + 3] << 24);
    }

    s[12] = counter;

    s[13] = ((uint32_t)nonce[0]) |
            ((uint32_t)nonce[1] << 8) |
            ((uint32_t)nonce[2] << 16) |
            ((uint32_t)nonce[3] << 24);
    s[14] = ((uint32_t)nonce[4]) |
            ((uint32_t)nonce[5] << 8) |
            ((uint32_t)nonce[6] << 16) |
            ((uint32_t)nonce[7] << 24);
    s[15] = ((uint32_t)nonce[8]) |
            ((uint32_t)nonce[9] << 8) |
            ((uint32_t)nonce[10] << 16) |
            ((uint32_t)nonce[11] << 24);

    uint32_t x[16];
    memcpy(x, s, sizeof(x));

    for (int i = 0; i < 10; ++i) {
        quarter_round(&x[0], &x[4], &x[8], &x[12]);
        quarter_round(&x[1], &x[5], &x[9], &x[13]);
        quarter_round(&x[2], &x[6], &x[10], &x[14]);
        quarter_round(&x[3], &x[7], &x[11], &x[15]);
        quarter_round(&x[0], &x[5], &x[10], &x[15]);
        quarter_round(&x[1], &x[6], &x[11], &x[12]);
        quarter_round(&x[2], &x[7], &x[8], &x[13]);
        quarter_round(&x[3], &x[4], &x[9], &x[14]);
    }

    for (int i = 0; i < 16; ++i) {
        x[i] += s[i];
        out[i * 4 + 0] = (uint8_t)(x[i] & 0xFF);
        out[i * 4 + 1] = (uint8_t)((x[i] >> 8) & 0xFF);
        out[i * 4 + 2] = (uint8_t)((x[i] >> 16) & 0xFF);
        out[i * 4 + 3] = (uint8_t)((x[i] >> 24) & 0xFF);
    }
}

void crypto_chacha20_block(const uint8_t key[32], uint32_t counter,
                           const uint8_t nonce[12], uint8_t out[64])
{
    chacha20_block(key, counter, nonce, out);
}

void crypto_chacha20_xor(const uint8_t key[32], uint32_t counter,
                         const uint8_t nonce[12],
                         const uint8_t *in, size_t in_len, uint8_t *out)
{
    uint8_t block[64];
    size_t offset = 0;

    while (offset < in_len) {
        chacha20_block(key, counter + (uint32_t)(offset / 64), nonce, block);
        size_t todo = in_len - offset;
        if (todo > 64) todo = 64;
        for (size_t i = 0; i < todo; ++i) {
            out[offset + i] = in[offset + i] ^ block[i];
        }
        offset += todo;
    }
}

/* Poly1305 */

static void poly1305_clamp(uint8_t r[16])
{
    r[3] &= 15;
    r[7] &= 15;
    r[11] &= 15;
    r[15] &= 15;
    r[4] &= 252;
    r[8] &= 252;
    r[12] &= 252;
}

static void poly1305_init(uint64_t acc[2], const uint8_t r[16])
{
    acc[0] = (uint64_t)r[0] | ((uint64_t)r[1] << 8) | ((uint64_t)r[2] << 16) | ((uint64_t)r[3] << 24) |
             ((uint64_t)r[4] << 32) | ((uint64_t)r[5] << 40) | ((uint64_t)r[6] << 48) | ((uint64_t)r[7] << 56);
    acc[1] = (uint64_t)r[8] | ((uint64_t)r[9] << 8) | ((uint64_t)r[10] << 16) | ((uint64_t)r[11] << 24) |
             ((uint64_t)r[12] << 32) | ((uint64_t)r[13] << 40) | ((uint64_t)r[14] << 48) | ((uint64_t)r[15] << 56);
}

static void poly1305_process_block(uint64_t acc[2], const uint8_t block[16],
                                   const uint64_t r[2], const uint64_t p[2])
{
    uint64_t n0 = (uint64_t)block[0] | ((uint64_t)block[1] << 8) | ((uint64_t)block[2] << 16) | ((uint64_t)block[3] << 24) |
                  ((uint64_t)block[4] << 32) | ((uint64_t)block[5] << 40) | ((uint64_t)block[6] << 48) | ((uint64_t)block[7] << 56);
    uint64_t n1 = (uint64_t)block[8] | ((uint64_t)block[9] << 8) | ((uint64_t)block[10] << 16) | ((uint64_t)block[11] << 24) |
                  ((uint64_t)block[12] << 32) | ((uint64_t)block[13] << 40) | ((uint64_t)block[14] << 48) | ((uint64_t)block[15] << 56);

    acc[0] += n0;
    acc[1] += n1;

    __uint128_t a0 = (__uint128_t)acc[0] * (__uint128_t)r[0];
    __uint128_t a1 = (__uint128_t)acc[0] * (__uint128_t)r[1] +
                     (__uint128_t)acc[1] * (__uint128_t)r[0];

    acc[0] = (uint64_t)a0;
    acc[1] = (uint64_t)a1;

    uint64_t carry = (uint64_t)(a0 >> 64);
    acc[1] += carry;
    carry = (uint64_t)(a1 >> 64);
    acc[0] = (acc[0] + carry * 5u) & 0xFFFFFFFFFFFFFFFCull;

    uint64_t d0 = acc[0] & 0xFFFFFFFFFFFFFFFCull;
    uint64_t d1 = acc[1] & 0xFFFFFFFFFFFFFFFCull;
    acc[0] = d0;
    acc[1] = d1;
}

static void poly1305_finish(uint64_t acc[2], const uint8_t s[16], uint8_t tag[16])
{
    uint64_t s0 = (uint64_t)s[0] | ((uint64_t)s[1] << 8) | ((uint64_t)s[2] << 16) | ((uint64_t)s[3] << 24) |
                  ((uint64_t)s[4] << 32) | ((uint64_t)s[5] << 40) | ((uint64_t)s[6] << 48) | ((uint64_t)s[7] << 56);
    uint64_t s1 = (uint64_t)s[8] | ((uint64_t)s[9] << 8) | ((uint64_t)s[10] << 16) | ((uint64_t)s[11] << 24) |
                  ((uint64_t)s[12] << 32) | ((uint64_t)s[13] << 40) | ((uint64_t)s[14] << 48) | ((uint64_t)s[15] << 56);

    acc[0] += s0;
    acc[1] += s1;

    tag[0] = (uint8_t)(acc[0] & 0xFF);
    tag[1] = (uint8_t)((acc[0] >> 8) & 0xFF);
    tag[2] = (uint8_t)((acc[0] >> 16) & 0xFF);
    tag[3] = (uint8_t)((acc[0] >> 24) & 0xFF);
    tag[4] = (uint8_t)((acc[0] >> 32) & 0xFF);
    tag[5] = (uint8_t)((acc[0] >> 40) & 0xFF);
    tag[6] = (uint8_t)((acc[0] >> 48) & 0xFF);
    tag[7] = (uint8_t)((acc[0] >> 56) & 0xFF);
    tag[8] = (uint8_t)(acc[1] & 0xFF);
    tag[9] = (uint8_t)((acc[1] >> 8) & 0xFF);
    tag[10] = (uint8_t)((acc[1] >> 16) & 0xFF);
    tag[11] = (uint8_t)((acc[1] >> 24) & 0xFF);
    tag[12] = (uint8_t)((acc[1] >> 32) & 0xFF);
    tag[13] = (uint8_t)((acc[1] >> 40) & 0xFF);
    tag[14] = (uint8_t)((acc[1] >> 48) & 0xFF);
    tag[15] = (uint8_t)((acc[1] >> 56) & 0xFF);
}

static void poly1305_mac(const uint8_t key[32], const uint8_t *data, size_t data_len, uint8_t tag[16])
{
    uint8_t r[16];
    memcpy(r, key, 16);
    poly1305_clamp(r);

    uint8_t s[16];
    memcpy(s, key + 16, 16);

    uint64_t acc[2] = {0, 0};
    uint64_t r64[2];
    poly1305_init(r64, r);

    size_t offset = 0;
    while (data_len - offset >= 16) {
        poly1305_process_block(acc, data + offset, r64, NULL);
        offset += 16;
    }

    if (offset < data_len) {
        uint8_t last[17];
        size_t rem = data_len - offset;
        memcpy(last, data + offset, rem);
        last[rem] = 1;
        memset(last + rem + 1, 0, 16 - rem);
        poly1305_process_block(acc, last, r64, NULL);
    } else {
        uint8_t last[17];
        memset(last, 0, 16);
        last[16] = 1;
        poly1305_process_block(acc, last, r64, NULL);
    }

    poly1305_finish(acc, s, tag);
}

int crypto_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *plaintext, size_t plaintext_len,
                                     uint8_t *ciphertext, uint8_t tag[16])
{
    if (key == NULL || nonce == NULL || tag == NULL) return -1;
    if ((aad == NULL && aad_len != 0) || (plaintext == NULL && plaintext_len != 0)) return -1;
    if (ciphertext == NULL && plaintext_len != 0) return -1;

    uint8_t poly_key[64];
    chacha20_block(key, 0, nonce, poly_key);

    uint8_t otk[32];
    memcpy(otk, poly_key, 32);

    uint32_t counter = 1;
    size_t offset = 0;
    while (offset < plaintext_len) {
        uint8_t block[64];
        chacha20_block(key, counter, nonce, block);
        size_t todo = plaintext_len - offset;
        if (todo > 64) todo = 64;
        for (size_t i = 0; i < todo; ++i) {
            ciphertext[offset + i] = plaintext[offset + i] ^ block[i];
        }
        ++counter;
        offset += todo;
    }

    size_t aad_pad = (aad_len % 16) ? (16 - aad_len % 16) : 0;
    size_t ct_pad = (plaintext_len % 16) ? (16 - plaintext_len % 16) : 0;
    size_t mac_data_len = aad_len + aad_pad + plaintext_len + ct_pad + 16;
    uint8_t mac_data[mac_data_len + 16];
    size_t pos = 0;

    if (aad_len > 0) {
        memcpy(mac_data + pos, aad, aad_len);
        pos += aad_len;
    }
    memset(mac_data + pos, 0, aad_pad);
    pos += aad_pad;

    if (plaintext_len > 0) {
        memcpy(mac_data + pos, ciphertext, plaintext_len);
        pos += plaintext_len;
    }
    memset(mac_data + pos, 0, ct_pad);
    pos += ct_pad;

    mac_data[pos++] = (uint8_t)(aad_len & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 8) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 16) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 24) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 32) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 40) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 48) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 56) & 0xFF);

    mac_data[pos++] = (uint8_t)(plaintext_len & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 8) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 16) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 24) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 32) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 40) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 48) & 0xFF);
    mac_data[pos++] = (uint8_t)((plaintext_len >> 56) & 0xFF);

    poly1305_mac(otk, mac_data, pos, tag);

    memset(poly_key, 0, sizeof(poly_key));
    memset(otk, 0, sizeof(otk));
    return 0;
}

int crypto_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *ciphertext, size_t ciphertext_len,
                                     const uint8_t tag[16], uint8_t *plaintext)
{
    if (key == NULL || nonce == NULL || tag == NULL) return -1;
    if ((aad == NULL && aad_len != 0) || (ciphertext == NULL && ciphertext_len != 0)) return -1;
    if (plaintext == NULL && ciphertext_len != 0) return -1;

    uint8_t poly_key[64];
    chacha20_block(key, 0, nonce, poly_key);

    uint8_t otk[32];
    memcpy(otk, poly_key, 32);

    size_t aad_pad = (aad_len % 16) ? (16 - aad_len % 16) : 0;
    size_t ct_pad = (ciphertext_len % 16) ? (16 - ciphertext_len % 16) : 0;
    size_t mac_data_len = aad_len + aad_pad + ciphertext_len + ct_pad + 16;
    uint8_t mac_data[mac_data_len + 16];
    size_t pos = 0;

    if (aad_len > 0) {
        memcpy(mac_data + pos, aad, aad_len);
        pos += aad_len;
    }
    memset(mac_data + pos, 0, aad_pad);
    pos += aad_pad;

    if (ciphertext_len > 0) {
        memcpy(mac_data + pos, ciphertext, ciphertext_len);
        pos += ciphertext_len;
    }
    memset(mac_data + pos, 0, ct_pad);
    pos += ct_pad;

    mac_data[pos++] = (uint8_t)(aad_len & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 8) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 16) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 24) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 32) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 40) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 48) & 0xFF);
    mac_data[pos++] = (uint8_t)((aad_len >> 56) & 0xFF);

    mac_data[pos++] = (uint8_t)(ciphertext_len & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 8) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 16) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 24) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 32) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 40) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 48) & 0xFF);
    mac_data[pos++] = (uint8_t)((ciphertext_len >> 56) & 0xFF);

    uint8_t computed_tag[16];
    poly1305_mac(otk, mac_data, pos, computed_tag);

    int ok = 1;
    for (int i = 0; i < 16; ++i) {
        if (computed_tag[i] != tag[i]) ok = 0;
    }
    memset(computed_tag, 0, sizeof(computed_tag));
    memset(mac_data, 0, sizeof(mac_data_len));

    if (!ok) {
        memset(poly_key, 0, sizeof(poly_key));
        memset(otk, 0, sizeof(otk));
        return -1;
    }

    uint32_t counter = 1;
    size_t offset = 0;
    while (offset < ciphertext_len) {
        uint8_t block[64];
        chacha20_block(key, counter, nonce, block);
        size_t todo = ciphertext_len - offset;
        if (todo > 64) todo = 64;
        for (size_t i = 0; i < todo; ++i) {
            plaintext[offset + i] = ciphertext[offset + i] ^ block[i];
        }
        ++counter;
        offset += todo;
    }

    memset(poly_key, 0, sizeof(poly_key));
    memset(otk, 0, sizeof(otk));
    return 0;
}
