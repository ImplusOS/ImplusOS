#include "RSA_PKCS1_V15.h"
#include <string.h>

#define RSA_MAX_LIMBS 64

typedef struct {
    uint64_t limbs[RSA_MAX_LIMBS];
    size_t count;
} rsa_int_t;

static void rsa_int_zero(rsa_int_t *r)
{
    memset(r->limbs, 0, sizeof(r->limbs));
    r->count = 0;
}

static void rsa_int_one(rsa_int_t *r)
{
    rsa_int_zero(r);
    r->limbs[0] = 1;
    r->count = 1;
}

static void rsa_int_normalize(rsa_int_t *r)
{
    while (r->count > 0 && r->limbs[r->count - 1] == 0) --r->count;
}

static int rsa_int_cmp(const rsa_int_t *a, const rsa_int_t *b)
{
    size_t max = (a->count > b->count) ? a->count : b->count;
    for (size_t i = max; i > 0; --i) {
        uint64_t av = (i - 1 < a->count) ? a->limbs[i - 1] : 0;
        uint64_t bv = (i - 1 < b->count) ? b->limbs[i - 1] : 0;
        if (av > bv) return 1;
        if (av < bv) return -1;
    }
    return 0;
}

static void rsa_int_set(rsa_int_t *r, const rsa_int_t *a)
{
    memcpy(r->limbs, a->limbs, a->count * sizeof(uint64_t));
    r->count = a->count;
}

static void rsa_int_mul(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *b)
{
    rsa_int_t result;
    rsa_int_zero(&result);
    for (size_t i = 0; i < a->count; ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b->count; ++j) {
            __uint128_t prod = (__uint128_t)a->limbs[i] * b->limbs[j] +
                               result.limbs[i + j] + carry;
            result.limbs[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        if (carry) result.limbs[i + b->count] += carry;
    }
    result.count = a->count + b->count;
    rsa_int_normalize(&result);
    *r = result;
}

static void rsa_int_mod(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *m)
{
    rsa_int_t rem;
    rsa_int_set(&rem, a);

    for (int shift = (int)(a->count - m->count) * 64; shift >= 0; --shift) {
        rsa_int_t bs;
        rsa_int_zero(&bs);
        for (size_t i = 0; i < m->count; ++i) {
            bs.limbs[i + shift / 64] |= m->limbs[i] << (shift % 64);
            if (shift % 64 && i + 1 < m->count) {
                bs.limbs[i + shift / 64] |= m->limbs[i + 1] >> (64 - shift % 64);
            }
        }
        bs.count = m->count + shift / 64 + 1;
        rsa_int_normalize(&bs);

        while (rsa_int_cmp(&rem, &bs) >= 0) {
            uint64_t borrow = 0;
            for (size_t i = 0; i < bs.count; ++i) {
                uint64_t d = rem.limbs[i] - bs.limbs[i] - borrow;
                borrow = (d > rem.limbs[i]) ? 1 : 0;
                rem.limbs[i] = d;
            }
            rsa_int_normalize(&rem);
        }
    }
    *r = rem;
}

static void rsa_int_mod_mul(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *b, const rsa_int_t *m)
{
    rsa_int_t prod;
    rsa_int_mul(&prod, a, b);
    rsa_int_mod(r, &prod, m);
}

static void rsa_int_mod_exp(rsa_int_t *r, const rsa_int_t *base, const rsa_int_t *exp, const rsa_int_t *m)
{
    rsa_int_t result, b;
    rsa_int_one(&result);
    rsa_int_set(&b, base);

    for (size_t i = 0; i < exp->count * 64; ++i) {
        size_t limb = i / 64;
        size_t bit = i % 64;
        if ((exp->limbs[limb] >> bit) & 1) rsa_int_mod_mul(&result, &result, &b, m);
        rsa_int_mod_mul(&b, &b, &b, m);
    }
    *r = result;
}

static void rsa_int_from_bytes(rsa_int_t *r, const uint8_t *bytes, size_t len)
{
    rsa_int_zero(r);
    for (size_t i = 0; i < len; ++i) {
        size_t byte_pos = len - 1 - i;
        size_t limb_idx = i / 8;
        size_t bit_shift = (i % 8) * 8;
        if (limb_idx < RSA_MAX_LIMBS) r->limbs[limb_idx] |= (uint64_t)bytes[byte_pos] << bit_shift;
    }
    r->count = (len + 7) / 8;
    rsa_int_normalize(r);
}

static void rsa_int_to_bytes(const rsa_int_t *a, uint8_t *out, size_t out_len)
{
    memset(out, 0, out_len);
    for (size_t i = 0; i < a->count * 8 && i < out_len; ++i) {
        size_t limb_idx = i / 8;
        size_t bit_shift = (i % 8) * 8;
        size_t byte_pos = a->count * 8 - 1 - i;
        if (byte_pos < out_len) out[byte_pos] = (uint8_t)(a->limbs[limb_idx] >> bit_shift);
    }
}

/* DigestInfo prefixes for PKCS#1 v1.5 */
static const uint8_t DIGESTINFO_SHA256[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

static const uint8_t DIGESTINFO_SHA384[] = {
    0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05,
    0x00, 0x04, 0x30
};

static const uint8_t DIGESTINFO_SHA512[] = {
    0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0x04, 0x40
};

int crypto_rsa_pkcs1_v15_verify(const uint8_t *n, size_t n_len,
                                const uint8_t *e, size_t e_len,
                                const uint8_t *hash, size_t hash_len,
                                const uint8_t *signature, size_t sig_len,
                                int hash_nid)
{
    if (n == NULL || e == NULL || hash == NULL || signature == NULL) return -1;
    if (sig_len != n_len) return -1;

    const uint8_t *digest_info = NULL;
    size_t di_len = 0;
    size_t expected_hash_len = 0;

    if (hash_nid == RSA_HASH_NID_SHA256) {
        digest_info = DIGESTINFO_SHA256;
        di_len = sizeof(DIGESTINFO_SHA256);
        expected_hash_len = 32;
    } else if (hash_nid == RSA_HASH_NID_SHA384) {
        digest_info = DIGESTINFO_SHA384;
        di_len = sizeof(DIGESTINFO_SHA384);
        expected_hash_len = 48;
    } else if (hash_nid == RSA_HASH_NID_SHA512) {
        digest_info = DIGESTINFO_SHA512;
        di_len = sizeof(DIGESTINFO_SHA512);
        expected_hash_len = 64;
    } else {
        return -1;
    }
    if (hash_len != expected_hash_len) return -1;

    rsa_int_t modulus, pub_exp, sig_int, m;
    rsa_int_from_bytes(&modulus, n, n_len);
    rsa_int_from_bytes(&pub_exp, e, e_len);
    rsa_int_from_bytes(&sig_int, signature, sig_len);

    rsa_int_mod_exp(&m, &sig_int, &pub_exp, &modulus);

    uint8_t em[512];
    memset(em, 0, sizeof(em));
    rsa_int_to_bytes(&m, em, n_len);

    /* Check PKCS#1 v1.5 format: 0x00 0x01 PS 0x00 DigestInfo */
    if (em[0] != 0x00) return -1;
    if (em[1] != 0x01) return -1;

    size_t pos = 2;
    while (pos < n_len && em[pos] == 0xFF) ++pos;
    if (pos == 2) return -1;
    if (pos >= n_len || em[pos] != 0x00) return -1;
    ++pos;

    /* Check DigestInfo */
    if (pos + di_len > n_len) return -1;
    if (memcmp(em + pos, digest_info, di_len) != 0) return -1;
    pos += di_len;

    /* Check hash */
    if (pos + hash_len != n_len) return -1;
    if (memcmp(em + pos, hash, hash_len) != 0) return -1;

    return 0;
}
