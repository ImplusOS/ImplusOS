#include "RSA_PSS.h"
#include "../SHA256/SHA256.h"
#include <string.h>
#include <stdlib.h>

#define RSA_MAX_LIMBS 64
#define RSA_MAX_BYTES 512
#define PSS_SALT_LEN 32

static const uint8_t PSS_DEFAULT_SALT[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

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

static int rsa_int_is_zero(const rsa_int_t *a)
{
    for (size_t i = 0; i < a->count; ++i) {
        if (a->limbs[i] != 0) return 0;
    }
    return 1;
}

static int rsa_int_is_one(const rsa_int_t *a)
{
    return a->count == 1 && a->limbs[0] == 1;
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

static int rsa_int_cmp_limb(const rsa_int_t *a, uint64_t b)
{
    if (a->count > 1) return 1;
    if (a->count == 0) return (b == 0) ? 0 : -1;
    if (a->limbs[0] > b) return 1;
    if (a->limbs[0] < b) return -1;
    return 0;
}

static void rsa_int_normalize(rsa_int_t *r)
{
    while (r->count > 0 && r->limbs[r->count - 1] == 0) {
        --r->count;
    }
}

static void rsa_int_add(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *b)
{
    size_t max = (a->count > b->count) ? a->count : b->count;
    uint64_t carry = 0;
    for (size_t i = 0; i < max; ++i) {
        uint64_t av = (i < a->count) ? a->limbs[i] : 0;
        uint64_t bv = (i < b->count) ? b->limbs[i] : 0;
        uint64_t sum = av + bv + carry;
        carry = (sum < av) ? 1 : (carry && sum == av) ? 1 : 0;
        r->limbs[i] = sum;
    }
    if (carry) {
        r->limbs[max] = carry;
        r->count = max + 1;
    } else {
        r->count = max;
    }
    rsa_int_normalize(r);
}

static void rsa_int_sub(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *b)
{
    if (r != a) {
        memcpy(r->limbs, a->limbs, a->count * sizeof(uint64_t));
        r->count = a->count;
    }
    uint64_t borrow = 0;
    for (size_t i = 0; i < b->count || borrow; ++i) {
        uint64_t bv = (i < b->count) ? b->limbs[i] : 0;
        uint64_t diff = r->limbs[i] - bv - borrow;
        borrow = (diff > r->limbs[i]) ? 1 : 0;
        r->limbs[i] = diff;
    }
    rsa_int_normalize(r);
}

static void rsa_int_shl(rsa_int_t *r, const rsa_int_t *a, size_t bits)
{
    size_t limb_shift = bits / 64;
    size_t bit_shift = bits % 64;
    size_t new_count = a->count + limb_shift + 1;

    if (new_count > RSA_MAX_LIMBS) return;

    rsa_int_zero(r);
    uint64_t carry = 0;
    for (size_t i = 0; i < a->count; ++i) {
        r->limbs[i + limb_shift] = (a->limbs[i] << bit_shift) | carry;
        carry = (bit_shift == 0) ? 0 : (a->limbs[i] >> (64 - bit_shift));
    }
    if (carry) {
        r->limbs[a->count + limb_shift] = carry;
        r->count = a->count + limb_shift + 1;
    } else {
        r->count = a->count + limb_shift;
    }
    rsa_int_normalize(r);
}

static void rsa_int_shr(rsa_int_t *r, const rsa_int_t *a, size_t bits)
{
    size_t limb_shift = bits / 64;
    size_t bit_shift = bits % 64;

    if (limb_shift >= a->count) {
        rsa_int_zero(r);
        return;
    }

    rsa_int_zero(r);
    for (size_t i = 0; i < a->count - limb_shift; ++i) {
        r->limbs[i] = a->limbs[i + limb_shift] >> bit_shift;
        if (bit_shift > 0 && i + limb_shift + 1 < a->count) {
            r->limbs[i] |= a->limbs[i + limb_shift + 1] << (64 - bit_shift);
        }
    }
    r->count = a->count - limb_shift;
    rsa_int_normalize(r);
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
        if (carry) {
            result.limbs[i + b->count] += carry;
        }
    }
    result.count = a->count + b->count;
    rsa_int_normalize(&result);
    *r = result;
}

static void rsa_int_div(rsa_int_t *q, rsa_int_t *rem, const rsa_int_t *a, const rsa_int_t *b)
{
    if (b->count == 0 || rsa_int_is_zero(b)) return;

    rsa_int_t remainder;
    memcpy(&remainder, a, sizeof(rsa_int_t));

    rsa_int_t quotient;
    rsa_int_zero(&quotient);

    if (rsa_int_cmp(&remainder, b) < 0) {
        if (q) rsa_int_zero(q);
        if (rem) *rem = remainder;
        return;
    }

    size_t shift = (remainder.count - b->count) * 64;
    rsa_int_t bs;
    rsa_int_shl(&bs, b, shift);

    while (rsa_int_cmp(&bs, &remainder) > 0 && shift > 0) {
        shift -= 64;
        rsa_int_shl(&bs, b, shift);
    }

    while (1) {
        while (rsa_int_cmp(&bs, &remainder) <= 0) {
            rsa_int_sub(&remainder, &remainder, &bs);
            quotient.limbs[shift / 64] |= (uint64_t)1 << (shift % 64);
        }

        if (shift == 0) break;

        if (shift >= 64) {
            shift -= 64;
        } else {
            shift = 0;
        }
        rsa_int_shl(&bs, b, shift);
    }

    quotient.count = RSA_MAX_LIMBS;
    rsa_int_normalize(&quotient);

    if (q) *q = quotient;
    if (rem) *rem = remainder;
}

static void rsa_int_mod(rsa_int_t *r, const rsa_int_t *a, const rsa_int_t *m)
{
    rsa_int_t rem;
    rsa_int_div(NULL, &rem, a, m);
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
    rsa_int_t result;
    rsa_int_one(&result);
    rsa_int_t b;
    memcpy(&b, base, sizeof(rsa_int_t));

    for (size_t i = 0; i < exp->count * 64; ++i) {
        size_t limb = i / 64;
        size_t bit = i % 64;
        if ((exp->limbs[limb] >> bit) & 1) {
            rsa_int_mod_mul(&result, &result, &b, m);
        }
        rsa_int_mod_mul(&b, &b, &b, m);
    }

    *r = result;
}

static void rsa_int_from_bytes(rsa_int_t *r, const uint8_t *bytes, size_t len)
{
    rsa_int_zero(r);
    if (bytes == NULL || len == 0) return;

    for (size_t i = 0; i < len; ++i) {
        size_t byte_pos = len - 1 - i;
        size_t limb_idx = i / 8;
        size_t bit_shift = (i % 8) * 8;
        if (limb_idx < RSA_MAX_LIMBS) {
            r->limbs[limb_idx] |= (uint64_t)bytes[byte_pos] << bit_shift;
        }
    }
    r->count = (len + 7) / 8;
    rsa_int_normalize(r);
}

static size_t rsa_int_to_bytes(const rsa_int_t *a, uint8_t *out, size_t out_len)
{
    size_t needed = a->count * 8;
    if (out == NULL) return needed;

    if (out_len < needed) return 0;

    memset(out, 0, out_len);
    for (size_t i = 0; i < needed; ++i) {
        size_t limb_idx = i / 8;
        size_t bit_shift = (i % 8) * 8;
        size_t byte_pos = needed - 1 - i;
        if (limb_idx < a->count) {
            out[byte_pos] = (uint8_t)(a->limbs[limb_idx] >> bit_shift);
        }
    }
    return needed;
}

/* MGF1 based on SHA-256 */
static void mgf1(const uint8_t *seed, size_t seed_len, size_t mask_len, uint8_t *mask)
{
    uint32_t counter = 0;
    size_t offset = 0;

    while (offset < mask_len) {
        uint8_t c[4];
        c[0] = (uint8_t)((counter >> 24) & 0xFF);
        c[1] = (uint8_t)((counter >> 16) & 0xFF);
        c[2] = (uint8_t)((counter >> 8) & 0xFF);
        c[3] = (uint8_t)(counter & 0xFF);

        uint8_t hash_input[seed_len + 4];
        memcpy(hash_input, seed, seed_len);
        memcpy(hash_input + seed_len, c, 4);

        uint8_t hash[32];
        crypto_sha256(hash_input, seed_len + 4, hash);

        size_t to_copy = mask_len - offset;
        if (to_copy > 32) to_copy = 32;
        memcpy(mask + offset, hash, to_copy);
        offset += to_copy;
        ++counter;
    }
}

/* EMSA-PSS-ENCODE */
static int emsa_pss_encode(const uint8_t *m_hash, size_t hash_len,
                           size_t em_bits, uint8_t *em, size_t em_len)
{
    if (hash_len != 32) return -1;

    uint8_t salt[PSS_SALT_LEN];
    memset(salt, 0, PSS_SALT_LEN);

    /* M' = padding1 || mHash || salt */
    uint8_t m2_input[8 + 32 + PSS_SALT_LEN];
    memset(m2_input, 0, 8);
    memcpy(m2_input + 8, m_hash, hash_len);
    memcpy(m2_input + 8 + hash_len, salt, PSS_SALT_LEN);

    uint8_t m2[32];
    crypto_sha256(m2_input, 8 + hash_len + PSS_SALT_LEN, m2);

    size_t db_len = em_len - hash_len - 1;
    if (db_len < PSS_SALT_LEN + 1) return -1;

    /* DB = PS || 0x01 || salt */
    uint8_t *db = (uint8_t *)calloc(db_len, 1);
    if (db == NULL) return -1;
    memset(db, 0, db_len - PSS_SALT_LEN - 1);
    db[db_len - PSS_SALT_LEN - 1] = 0x01;
    memcpy(db + db_len - PSS_SALT_LEN, salt, PSS_SALT_LEN);

    /* dbMask = MGF1(M', db_len) */
    uint8_t *db_mask = (uint8_t *)malloc(db_len);
    if (db_mask == NULL) { free(db); return -1; }
    mgf1(m2, hash_len, db_len, db_mask);

    /* maskedDB = DB xor dbMask */
    for (size_t i = 0; i < db_len; ++i) {
        db[i] ^= db_mask[i];
    }

    /* Set leftmost (8*db_len - em_bits) bits of maskedDB to 0 */
    size_t excess_bits = 8 * em_len - em_bits;
    if (excess_bits > 0 && excess_bits <= 8) {
        uint8_t mask = (uint8_t)(0xFF >> excess_bits);
        db[0] &= mask;
    }

    /* EM = maskedDB || H || 0xBC */
    memcpy(em, db, db_len);
    memcpy(em + db_len, m2, hash_len);
    em[em_len - 1] = 0xBC;

    free(db);
    free(db_mask);
    return 0;
}

/* EMSA-PSS-VERIFY */
static int emsa_pss_verify(const uint8_t *m_hash, size_t hash_len,
                           const uint8_t *em, size_t em_len, size_t em_bits)
{
    int ret = -1;

    if (hash_len != 32) return -1;
    if (em_len < hash_len + PSS_SALT_LEN + 2) return -1;
    if (em[em_len - 1] != 0xBC) return -1;

    size_t db_len = em_len - hash_len - 1;

    /* Check leftmost excess bits are 0 */
    size_t excess_bits = 8 * em_len - em_bits;
    if (excess_bits > 0 && excess_bits <= 8) {
        uint8_t mask = (uint8_t)(0xFF << (8 - excess_bits));
        /* Check that masked bits (the top excess_bits bits of first byte) are 0 */
        uint8_t top_bits = em[0] & (uint8_t)(~((1 << (8 - excess_bits)) - 1));
        if (top_bits != 0) return -1;
    }

    /* dbMask = MGF1(H, db_len) */
    uint8_t *db_mask = (uint8_t *)malloc(db_len);
    if (db_mask == NULL) return -1;
    mgf1(em + db_len, hash_len, db_len, db_mask);

    /* DB = maskedDB xor dbMask */
    uint8_t *db = (uint8_t *)malloc(db_len);
    if (db == NULL) { free(db_mask); return -1; }
    for (size_t i = 0; i < db_len; ++i) {
        db[i] = em[i] ^ db_mask[i];
    }

    /* Set leftmost excess bits to 0 */
    if (excess_bits > 0 && excess_bits <= 8) {
        uint8_t mask = (uint8_t)(0xFF >> excess_bits);
        db[0] &= mask;
    }

    /* Check PS: look for 0x01 separator */
    size_t sep_pos = (size_t)-1;
    for (size_t i = 0; i < db_len; ++i) {
        if (db[i] == 0x01) { sep_pos = i; break; }
        if (db[i] != 0x00) { goto cleanup; }
    }

    if (sep_pos == (size_t)-1) goto cleanup;
    size_t salt_len = db_len - sep_pos - 1;
    if (salt_len != PSS_SALT_LEN) goto cleanup;

    /* Reconstruct M' and compare H */
    uint8_t m2_input[8 + 32 + PSS_SALT_LEN];
    memset(m2_input, 0, 8);
    memcpy(m2_input + 8, m_hash, hash_len);
    memcpy(m2_input + 8 + hash_len, db + sep_pos + 1, salt_len);

    uint8_t m2[32];
    crypto_sha256(m2_input, 8 + hash_len + salt_len, m2);

    if (memcmp(m2, em + db_len, hash_len) == 0) {
        ret = 0;
    }

cleanup:
    free(db);
    free(db_mask);
    return ret;
}

extern void crypto_csprng_generate(uint8_t *out, size_t len);

int crypto_rsa_pss_sign(const uint8_t *n, size_t n_len,
                        const uint8_t *e, size_t e_len,
                        const uint8_t *d, size_t d_len,
                        const uint8_t *hash, size_t hash_len,
                        uint8_t *signature, size_t *sig_len)
{
    if (n == NULL || e == NULL || d == NULL || hash == NULL ||
        signature == NULL || sig_len == NULL) {
        return -1;
    }
    if (hash_len != 32) return -1;

    /* Generate salt for PSS */
    uint8_t salt[PSS_SALT_LEN];
    crypto_csprng_generate(salt, PSS_SALT_LEN);

    /* Import RSA key */
    rsa_int_t modulus, pub_exp, priv_exp;
    rsa_int_from_bytes(&modulus, n, n_len);
    rsa_int_from_bytes(&pub_exp, e, e_len);
    rsa_int_from_bytes(&priv_exp, d, d_len);

    size_t mod_bytes = n_len;

    /* EMSA-PSS-ENCODE */
    uint8_t em[RSA_MAX_BYTES];
    memset(em, 0, sizeof(em));
    size_t em_bits = mod_bytes * 8 - 1;

    if (emsa_pss_encode(hash, hash_len, em_bits, em, mod_bytes) < 0) {
        return -1;
    }

    /* OS2IP and RSAVP1 */
    rsa_int_t m, s;
    rsa_int_from_bytes(&m, em, mod_bytes);
    rsa_int_mod_exp(&s, &m, &priv_exp, &modulus);

    /* I2OSP */
    size_t written = rsa_int_to_bytes(&s, signature, *sig_len);
    if (written == 0) return -1;
    *sig_len = written;

    return 0;
}

int crypto_rsa_pss_verify(const uint8_t *n, size_t n_len,
                          const uint8_t *e, size_t e_len,
                          const uint8_t *hash, size_t hash_len,
                          const uint8_t *signature, size_t sig_len)
{
    if (n == NULL || e == NULL || hash == NULL || signature == NULL) {
        return -1;
    }
    if (hash_len != 32) return -1;

    rsa_int_t modulus, pub_exp, sig_int, m;
    rsa_int_from_bytes(&modulus, n, n_len);
    rsa_int_from_bytes(&pub_exp, e, e_len);
    rsa_int_from_bytes(&sig_int, signature, sig_len);

    /* RSAVP1 */
    rsa_int_mod_exp(&m, &sig_int, &pub_exp, &modulus);

    /* I2OSP */
    uint8_t em[RSA_MAX_BYTES];
    memset(em, 0, sizeof(em));
    size_t written = rsa_int_to_bytes(&m, em, n_len);
    if (written == 0) return -1;

    /* EMSA-PSS-VERIFY */
    size_t em_bits = n_len * 8 - 1;
    return emsa_pss_verify(hash, hash_len, em, n_len, em_bits);
}
