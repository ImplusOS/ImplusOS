#include "ECDSA.h"
#include "../ECDHE/ECDHE.h"
#include <string.h>

#define P256_LIMBS 4

typedef uint64_t p256_t[P256_LIMBS];

/* Order n of P-256 */
static const p256_t N = {
    0xF3B9CAC2FC632551, 0xBCE6FAADA7179E84,
    0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF00000000
};

static const p256_t ZERO = {0, 0, 0, 0};
static const p256_t ONE  = {1, 0, 0, 0};

static inline void set(p256_t r, const p256_t a)
{
    r[0] = a[0]; r[1] = a[1]; r[2] = a[2]; r[3] = a[3];
}

static inline int is_zero(const p256_t a)
{
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

static int cmp(const p256_t a, const p256_t b)
{
    for (int i = P256_LIMBS - 1; i >= 0; --i) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

static inline uint64_t addc_u64(uint64_t *r, uint64_t a, uint64_t b)
{
    *r = a + b;
    return (*r < a) ? 1 : 0;
}

static void add_mod_n(p256_t r, const p256_t a, const p256_t b)
{
    uint64_t t[4];
    uint64_t carry = 0;
    for (int i = 0; i < P256_LIMBS; ++i) {
        carry = addc_u64(&t[i], a[i], b[i] + carry);
    }
    if (carry) {
        uint64_t borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = t[i] - N[i] - borrow;
            borrow = (d > t[i]) ? 1 : 0;
            t[i] = d;
        }
    }
    while (cmp(t, N) >= 0) {
        uint64_t borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = t[i] - N[i] - borrow;
            borrow = (d > t[i]) ? 1 : 0;
            t[i] = d;
        }
    }
    set(r, t);
}

static void sub_mod_n(p256_t r, const p256_t a, const p256_t b)
{
    uint64_t t[4];
    uint64_t borrow = 0;
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t d = a[i] - b[i] - borrow;
        borrow = (d > a[i]) ? 1 : 0;
        t[i] = d;
    }
    if (borrow) {
        uint64_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry = addc_u64(&t[i], t[i], N[i] + carry);
        }
    }
    set(r, t);
}

static void mul_mod_n(p256_t r, const p256_t a, const p256_t b)
{
    uint64_t temp[8] = {0};
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < P256_LIMBS; ++j) {
            __uint128_t prod = (__uint128_t)a[i] * b[j] + temp[i + j] + carry;
            temp[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        temp[i + P256_LIMBS] += carry;
    }

    /* Reduce mod n */
    for (int i = 7; i >= P256_LIMBS; --i) {
        if (temp[i] == 0) continue;
        /* Approximate quotient = temp[i] (since n is ~2^256, and temp[i] is at position i*64) */
        /* Subtract q * n where q = temp[i] */
        for (int shift = i - P256_LIMBS; shift >= 0; --shift) {
            int n_shift = shift * 64;
            uint64_t q_approx = temp[shift + P256_LIMBS];
            if (q_approx == 0) continue;
            /* Subtract q_approx * (n << (shift*64)) */
            uint64_t borrow = 0;
            for (int j = 0; j < P256_LIMBS; ++j) {
                __uint128_t prod = (__uint128_t)q_approx * N[j];
                uint64_t sub_val = (uint64_t)prod + borrow;
                borrow = (uint64_t)(prod >> 64) + (sub_val < (uint64_t)prod ? 1 : 0);
                /* Subtract from temp[shift + j] */
                uint64_t new_val = temp[shift + j] - sub_val;
                if (new_val > temp[shift + j]) {
                    /* Need to borrow from higher limb */
                    for (int k = shift + j + 1; k < 8; ++k) {
                        if (temp[k] > 0) {
                            --temp[k];
                            break;
                        }
                        temp[k] = 0xFFFFFFFFFFFFFFFF;
                    }
                }
                temp[shift + j] = new_val;
            }
        }
    }

    for (int i = 0; i < P256_LIMBS; ++i) r[i] = temp[i];
    while (cmp(r, N) >= 0) {
        uint64_t borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = r[i] - N[i] - borrow;
            borrow = (d > r[i]) ? 1 : 0;
            r[i] = d;
        }
    }
}

static void sqr_mod_n(p256_t r, const p256_t a)
{
    mul_mod_n(r, a, a);
}

static void inv_mod_n(p256_t r, const p256_t a)
{
    p256_t result;
    set(result, ONE);
    p256_t base;
    set(base, a);

    uint64_t exp[4] = {
        0xF3B9CAC2FC63254F, 0xBCE6FAADA7179E84,
        0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF00000000
    };

    for (int bit = 255; bit >= 0; --bit) {
        int limb = bit / 64;
        int bit_in_limb = bit % 64;
        if (bit < 255) {
            sqr_mod_n(result, result);
        }
        if ((exp[limb] >> bit_in_limb) & 1) {
            mul_mod_n(result, result, base);
        }
    }

    set(r, result);
}

static void bytes_to_int(p256_t r, const uint8_t b[32])
{
    for (int i = 0; i < P256_LIMBS; ++i) {
        r[i] = 0;
        for (int j = 0; j < 8; ++j) {
            r[i] = (r[i] << 8) | b[i * 8 + j];
        }
    }
}

static void int_to_bytes(const p256_t a, uint8_t b[32])
{
    for (int i = P256_LIMBS - 1; i >= 0; --i) {
        for (int j = 8; j > 0; --j) {
            b[i * 8 + j - 1] = (uint8_t)(a[i] & 0xFF);
        }
    }
}

/* We need different byte conversion: big-endian */
static void be_bytes_to_int(p256_t r, const uint8_t b[32])
{
    for (int i = 0; i < P256_LIMBS; ++i) {
        r[P256_LIMBS - 1 - i] = 0;
        for (int j = 0; j < 8; ++j) {
            r[P256_LIMBS - 1 - i] = (r[P256_LIMBS - 1 - i] << 8) | b[i * 8 + j];
        }
    }
}

static void int_to_be_bytes(const p256_t a, uint8_t b[32])
{
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t limb = a[P256_LIMBS - 1 - i];
        for (int j = 8; j > 0; --j) {
            b[i * 8 + j - 1] = (uint8_t)(limb & 0xFF);
            limb >>= 8;
        }
    }
}

extern void crypto_csprng_generate(uint8_t *out, size_t len);

int crypto_ecdsa_sign(const uint8_t private_key[32], const uint8_t hash[32],
                      uint8_t signature[64])
{
    if (private_key == NULL || hash == NULL || signature == NULL) return -1;

    p256_t d, e, k, r, s, k_inv, tmp;
    be_bytes_to_int(d, private_key);
    be_bytes_to_int(e, hash);

    if (cmp(e, N) >= 0) {
        sub_mod_n(e, e, N);
    }

    uint8_t k_bytes[32];

    while (1) {
        crypto_csprng_generate(k_bytes, 32);
        be_bytes_to_int(k, k_bytes);

        if (is_zero(k)) continue;
        if (cmp(k, N) >= 0) continue;

        uint8_t r_point[64];
        if (crypto_ec_p256_scalar_mult_g(k_bytes, r_point) < 0) continue;

        be_bytes_to_int(r, r_point);
        if (cmp(r, N) >= 0) {
            sub_mod_n(r, r, N);
        }
        if (is_zero(r)) continue;

        inv_mod_n(k_inv, k);

        mul_mod_n(tmp, r, d);
        add_mod_n(tmp, e, tmp);
        mul_mod_n(s, k_inv, tmp);

        if (is_zero(s)) continue;

        break;
    }

    memset(k_bytes, 0, sizeof(k_bytes));

    int_to_be_bytes(r, signature);
    int_to_be_bytes(s, signature + 32);

    return 0;
}

int crypto_ecdsa_verify(const uint8_t public_key[64], const uint8_t hash[32],
                        const uint8_t signature[64])
{
    if (public_key == NULL || hash == NULL || signature == NULL) return -1;

    p256_t r, s, e, w, u1, u2;
    be_bytes_to_int(r, signature);
    be_bytes_to_int(s, signature + 32);
    be_bytes_to_int(e, hash);

    if (cmp(e, N) >= 0) {
        sub_mod_n(e, e, N);
    }

    if (is_zero(r) || cmp(r, N) >= 0) return -1;
    if (is_zero(s) || cmp(s, N) >= 0) return -1;

    inv_mod_n(w, s);

    mul_mod_n(u1, e, w);
    mul_mod_n(u2, r, w);

    uint8_t u1_bytes[32], u2_bytes[32];
    int_to_be_bytes(u1, u1_bytes);
    int_to_be_bytes(u2, u2_bytes);

    uint8_t r1[64], r2[64];
    if (crypto_ec_p256_scalar_mult_g(u1_bytes, r1) < 0) return -1;
    if (crypto_ec_p256_scalar_mult(u2_bytes, public_key, r2) < 0) return -1;

    uint8_t sum[64];
    if (crypto_ec_p256_add_points(r1, r2, sum) < 0) return -1;

    p256_t rx;
    be_bytes_to_int(rx, sum);

    if (cmp(rx, N) >= 0) {
        sub_mod_n(rx, rx, N);
    }

    return (cmp(rx, r) == 0) ? 0 : -1;
}

int crypto_ecdsa_sign_der(const uint8_t private_key[32], const uint8_t hash[32],
                          uint8_t *signature, size_t *sig_len)
{
    if (private_key == NULL || hash == NULL || signature == NULL || sig_len == NULL) {
        return -1;
    }

    uint8_t sig[64];
    if (crypto_ecdsa_sign(private_key, hash, sig) < 0) return -1;

    /* Encode (r, s) as DER SEQUENCE { INTEGER r, INTEGER s } */
    uint8_t r_der[36], s_der[36];
    size_t r_len, s_len;

    /* Remove leading zeros from r and s */
    size_t r_start = 0;
    while (r_start < 32 && sig[r_start] == 0) ++r_start;
    size_t r_bytes = 32 - r_start;
    if (r_bytes == 0) r_bytes = 1;

    size_t s_start = 0;
    while (s_start < 32 && sig[32 + s_start] == 0) ++s_start;
    size_t s_bytes = 32 - s_start;
    if (s_bytes == 0) s_bytes = 1;

    /* INTEGER tag + length + (possibly 0x00 padding if high bit set) */
    r_der[0] = 0x02;
    size_t r_val_bytes = (sig[r_start] & 0x80) ? r_bytes + 1 : r_bytes;
    r_der[1] = (uint8_t)r_val_bytes;
    size_t r_pos = 2;
    if (sig[r_start] & 0x80) r_der[r_pos++] = 0x00;
    for (size_t i = 0; i < r_bytes; ++i) r_der[r_pos++] = sig[r_start + i];
    r_len = r_pos;

    s_der[0] = 0x02;
    size_t s_val_bytes = (sig[32 + s_start] & 0x80) ? s_bytes + 1 : s_bytes;
    s_der[1] = (uint8_t)s_val_bytes;
    size_t s_pos = 2;
    if (sig[32 + s_start] & 0x80) s_der[s_pos++] = 0x00;
    for (size_t i = 0; i < s_bytes; ++i) s_der[s_pos++] = sig[32 + s_start + i];
    s_len = s_pos;

    uint8_t seq[72];
    size_t seq_pos = 0;
    for (size_t i = 0; i < r_len; ++i) seq[seq_pos++] = r_der[i];
    for (size_t i = 0; i < s_len; ++i) seq[seq_pos++] = s_der[i];

    /* SEQUENCE tag */
    signature[0] = 0x30;
    if (seq_pos < 0x80) {
        signature[1] = (uint8_t)seq_pos;
        for (size_t i = 0; i < seq_pos; ++i) signature[2 + i] = seq[i];
        *sig_len = 2 + seq_pos;
    } else {
        return -1;
    }

    return 0;
}

int crypto_ecdsa_verify_der(const uint8_t public_key[64], const uint8_t hash[32],
                            const uint8_t *signature, size_t sig_len)
{
    if (public_key == NULL || hash == NULL || signature == NULL) return -1;
    if (sig_len < 8) return -1;

    /* Parse DER SEQUENCE { INTEGER r, INTEGER s } */
    if (signature[0] != 0x30) return -1;
    size_t seq_len = signature[1];
    if (seq_len + 2 != sig_len) return -1;

    const uint8_t *p = signature + 2;
    size_t remaining = seq_len;

    if (remaining < 2 || p[0] != 0x02) return -1;
    size_t r_len = p[1];
    if (r_len > 33 || r_len == 0) return -1;
    p += 2; remaining -= 2;
    if (remaining < r_len) return -1;

    const uint8_t *r_val = p;
    size_t r_skip = 0;
    if (r_len == 33) { r_val++; r_skip = 1; }
    size_t r_bytes = r_len - r_skip;
    if (r_bytes > 32) return -1;

    p += r_len; remaining -= r_len;

    if (remaining < 2 || p[0] != 0x02) return -1;
    size_t s_len = p[1];
    if (s_len > 33 || s_len == 0) return -1;
    p += 2; remaining -= 2;
    if (remaining < s_len) return -1;

    const uint8_t *s_val = p;
    size_t s_skip = 0;
    if (s_len == 33) { s_val++; s_skip = 1; }
    size_t s_bytes = s_len - s_skip;
    if (s_bytes > 32) return -1;

    uint8_t sig[64];
    memset(sig, 0, 64);
    memcpy(sig + 32 - r_bytes, r_val, r_bytes);
    memcpy(sig + 64 - s_bytes, s_val, s_bytes);

    return crypto_ecdsa_verify(public_key, hash, sig);
}
