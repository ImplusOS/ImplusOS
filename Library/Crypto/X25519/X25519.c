#include "X25519.h"
#include <string.h>

#define X25519_LIMBS 5

/* p = 2^255 - 19 */
static const uint64_t P[5] = {
    0xFFFFFFFFFFFFFFED, 0xFFFFFFFFFFFFFFFF,
    0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
    0x7FFFFFFFFFFFFFFF
};

static const uint64_t P_INV[5] = { 19, 0, 0, 0, 0 };

/* u coordinate of base point */
static const uint8_t BASE_POINT[32] = { 9 };

static void fe_copy(uint64_t *r, const uint64_t *a)
{
    for (int i = 0; i < X25519_LIMBS; ++i) r[i] = a[i];
}

static void fe_zero(uint64_t *r)
{
    for (int i = 0; i < X25519_LIMBS; ++i) r[i] = 0;
}

/* r = a + b, then reduce mod p */
static void fe_add(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t carry = 0;
    for (int i = 0; i < X25519_LIMBS; ++i) {
        __uint128_t sum = (__uint128_t)a[i] + b[i] + carry;
        r[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    carry = 19 * carry;
    r[0] += carry;
    carry = r[0] < carry ? 1 : 0;
    for (int i = 1; i < X25519_LIMBS; ++i) {
        r[i] += carry;
        carry = r[i] < carry ? 1 : 0;
    }
}

/* r = a - b, then reduce mod p */
static void fe_sub(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t borrow = 0;
    for (int i = 0; i < X25519_LIMBS; ++i) {
        uint64_t diff = a[i] - b[i] - borrow;
        borrow = (diff > a[i]) ? 1 : 0;
        r[i] = diff;
    }
    /* If borrow, subtract from 0 = -(p) so add p back */
    /* Actually since we compute a - b with borrow, we need p = 2^255 - 19 */
    /* Borrow = 1 means the result is negative, we need to add p */
    __uint128_t carry_adj = (__uint128_t)borrow * 19;
    r[0] += (uint64_t)carry_adj;
    carry_adj >>= 64;
    for (int i = 1; i < X25519_LIMBS; ++i) {
        r[i] += (uint64_t)carry_adj;
        carry_adj >>= 64;
    }
}

/* r = a * b mod p */
static void fe_mul(uint64_t *r, const uint64_t *a, const uint64_t *b)
{
    uint64_t product[10] = {0};

    for (int i = 0; i < X25519_LIMBS; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < X25519_LIMBS; ++j) {
            __uint128_t prod = (__uint128_t)a[i] * b[j] + product[i + j] + carry;
            product[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        product[i + X25519_LIMBS] += carry;
    }

    /* Reduce mod 2^255 - 19 */
    /* product has 10 limbs (512 bits). 2^255 = 19 mod p */
    /* We reduce the top 5 limbs by multiplying by 19 and adding to bottom 5 */

    uint64_t carry = 0;
    for (int i = 0; i < X25519_LIMBS; ++i) {
        __uint128_t sum = (__uint128_t)product[i] + carry;
        for (int j = 5; j < 10; ++j) {
            sum += (__uint128_t)product[j] * (j == i + 5 ? 19ULL : (j == i + 5 + 5 ? 0 : 0));
        }
        /* Actually, let me use a simpler approach */
    }

    /* Simpler: product[5..9] * 19 + product[0..4] */
    uint64_t t[5] = {0};
    uint64_t c = 0;
    for (int i = 0; i < 5; ++i) {
        __uint128_t sum = (__uint128_t)product[i] + product[i + 5] * 19ULL + c;
        t[i] = (uint64_t)sum;
        c = (uint64_t)(sum >> 64);
    }
    /* Handle remaining carry from top 5 */
    if (c) {
        __uint128_t sum = (__uint128_t)t[0] + c * 19;
        t[0] = (uint64_t)sum;
        c = (uint64_t)(sum >> 64);
        for (int i = 1; i < 5 && c; ++i) {
            t[i] += c;
            c = t[i] < (uint64_t)c ? 1 : 0;
        }
    }

    /* Final reduction: if t >= p, subtract p */
    int ge = 1;
    for (int i = 4; i >= 0; --i) {
        if (t[i] > P[i]) break;
        if (t[i] < P[i]) { ge = 0; break; }
    }
    if (ge) {
        uint64_t borrow = 0;
        for (int i = 0; i < 5; ++i) {
            uint64_t d = t[i] - P[i] - borrow;
            borrow = (d > t[i]) ? 1 : 0;
            t[i] = d;
        }
    }

    for (int i = 0; i < X25519_LIMBS; ++i) r[i] = t[i];
}

/* r = a^2 mod p */
static void fe_sqr(uint64_t *r, const uint64_t *a)
{
    fe_mul(r, a, a);
}

/* r = -a mod p */
static void fe_neg(uint64_t *r, const uint64_t *a)
{
    uint64_t borrow = 0;
    for (int i = 0; i < X25519_LIMBS; ++i) {
        uint64_t d = P[i] - a[i] - borrow;
        borrow = (d > P[i]) ? 1 : 0;
        r[i] = d;
    }
}

/* r = 1 */
static void fe_one(uint64_t *r)
{
    fe_zero(r);
    r[0] = 1;
}

/* r = a * 121666 mod p */
static void fe_mul121666(uint64_t *r, const uint64_t *a)
{
    uint64_t product[6] = {0};
    __uint128_t carry = 0;
    for (int i = 0; i < X25519_LIMBS; ++i) {
        __uint128_t prod = (__uint128_t)a[i] * 121666ULL + carry;
        product[i] = (uint64_t)prod;
        carry = prod >> 64;
    }
    product[5] = (uint64_t)carry;

    /* Reduce mod p */
    uint64_t t[5];
    __uint128_t sum = (__uint128_t)product[0] + product[5] * 19ULL;
    t[0] = (uint64_t)sum;
    carry = sum >> 64;
    for (int i = 1; i < 5; ++i) {
        sum = (__uint128_t)product[i] + carry;
        t[i] = (uint64_t)sum;
        carry = sum >> 64;
    }
    /* Propagate carry */
    if (carry) {
        __uint128_t s = (__uint128_t)t[0] + carry * 19;
        t[0] = (uint64_t)s;
        carry = s >> 64;
        for (int i = 1; i < 5 && carry; ++i) {
            t[i] += (uint64_t)carry;
            carry = t[i] < (uint64_t)carry ? 1 : 0;
        }
    }

    for (int i = 0; i < 5; ++i) r[i] = t[i];
}

/* r = a^(-1) mod p using Fermat's little theorem: a^(p-2) mod p */
static void fe_inv(uint64_t *r, const uint64_t *a)
{
    uint64_t t[5], result[5];
    fe_copy(result, a);

    /* Exponent = p - 2 = 2^255 - 21 */
    /* Square-and-multiply chain for exponent 2^255 - 21 */
    /* From RFC 7748: use the standard method */

    /* Compute a^(p-2) = a^(2^255 - 21) */
    /* Use the addition chain approach: */
    /* a^2, a^3, a^6, a^7, a^14, a^15, a^30, a^31, a^62, a^63, a^126, a^127, a^254 */
    /* Then a^(2^255 - 21) = (a^254)^2 * a^(-19) ... actually this is complex */

    /* Let's use the simpler square-and-multiply method */
    /* p - 2 = 0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEB */
    /* That's 254 bits with most being 1s */
    /* For efficiency, use a^(2^255 - 21) = ((...((a^2 * a)^2 * a)^2 ...)^2 * a^(-19)) */

    /* Simpler approach: use the standard 2^255-21 exponentiation */
    /* a^(2^255 - 21) = (((a^2)^2...^2) * a^(-21)) but this is complex */

    /* Let me just do square-and-multiply on each bit */
    /* We need to compute a^(p-2) mod p */
    /* Since p-2 has 255 bits, we do 254 squarings and some multiplications */

    /* For simplicity, implement a^(2^255 - 21) using the addition chain from RFC 8032 */
    fe_sqr(result, a);           /* a^2 */
    fe_mul(result, result, a);   /* a^3 */
    fe_sqr(result, result);      /* a^6 */
    fe_mul(result, result, a);   /* a^7 */
    for (int i = 0; i < 4; ++i) {
        fe_sqr(result, result);  /* a^112 */
    }
    /* ... This is getting complex. Let me use a simple iterative method. */

    /* Simple binary square-and-multiply for a^(p-2) */
    fe_copy(t, a);

    /* p-2 in hex (little-endian limbs): */
    static const uint64_t exp[5] = {
        0xFFFFFFFFFFFFFFEB, 0xFFFFFFFFFFFFFFFF,
        0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
        0x7FFFFFFFFFFFFFFF
    };

    fe_one(result);

    for (int bit = 254; bit >= 0; --bit) {
        fe_sqr(result, result);
        int limb = bit / 64;
        int b = bit % 64;
        if ((exp[limb] >> b) & 1) {
            fe_mul(result, result, t);
        }
    }

    fe_copy(r, result);
}

/* Convert 32 bytes (little-endian) to field element */
static void fe_from_bytes(uint64_t *r, const uint8_t *b)
{
    fe_zero(r);
    for (int i = 0; i < 32; ++i) {
        int limb = i / 8;
        int shift = (i % 8) * 8;
        r[limb] |= (uint64_t)b[i] << shift;
    }
}

/* Convert field element to 32 bytes (little-endian) */
static void fe_to_bytes(uint8_t *b, const uint64_t *a)
{
    for (int i = 0; i < 32; ++i) {
        int limb = i / 8;
        int shift = (i % 8) * 8;
        b[i] = (uint8_t)(a[limb] >> shift);
    }
}

/* Clamp the scalar for X25519 */
static void clamp_scalar(uint8_t scalar[32])
{
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;
}

/* Montgomery ladder step for X25519 */
static void monty_ladder(uint64_t *x2, uint64_t *z2, uint64_t *x3, uint64_t *z3,
                         const uint64_t *xm, const uint64_t *diff)
{
    /* x25519 Montgomery ladder step from RFC 7748 */
    uint64_t a[5], b[5], c[5], d[5], e[5], f[5];

    /* A = X2 + Z2 */
    fe_add(a, x2, z2);
    /* B = X2 - Z2 */
    fe_sub(b, x2, z2);
    /* AA = A^2 */
    fe_sqr(a, a);
    /* BB = B^2 */
    fe_sqr(b, b);
    /* C = X3 + Z3 */
    fe_add(c, x3, z3);
    /* D = X3 - Z3 */
    fe_sub(d, x3, z3);
    /* E = AA - BB */
    fe_sub(e, a, b);
    /* DA = D * A */
    fe_mul(c, d, a);
    /* CB = C * B */
    fe_mul(d, c, b);
    /* X3 = (DA + CB)^2 */
    fe_add(x3, c, d);
    fe_sqr(x3, x3);
    /* Z3 = X1 * (DA - CB)^2 */
    fe_sub(z3, c, d);
    fe_sqr(z3, z3);
    fe_mul(z3, z3, xm);
    /* X2 = AA * BB */
    fe_mul(x2, a, b);
    /* Z2 = E * (AA + a24 * E) where a24 = 121665 */
    /* a24 * E = 121665 * E */
    fe_mul121666(f, e);   /* This gives 121666*E, but we need 121665*E */
    /* Actually a24 = 121665 = 121666 - 1 */
    /* So a24 * E = 121666 * E - E */
    fe_sub(f, f, e);      /* f = 121666*E - E = 121665*E */
    fe_add(f, a, f);      /* AA + a24 * E */
    fe_mul(z2, e, f);
}

void crypto_x25519_scalar_mult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    uint8_t e[32];
    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    uint64_t x1[5], x2[5], z2[5], x3[5], z3[5];
    fe_from_bytes(x1, point);

    fe_one(x2);
    fe_zero(z2);
    fe_copy(x3, x1);
    fe_one(z3);

    int swap = 0;

    for (int i = 254; i >= 0; --i) {
        int bit = (e[i / 8] >> (i % 8)) & 1;
        int swap_this = bit ^ swap;
        swap = bit;

        /* Conditional swap */
        if (swap_this) {
            for (int j = 0; j < 5; ++j) {
                uint64_t tx = x2[j]; x2[j] = x3[j]; x3[j] = tx;
                uint64_t tz = z2[j]; z2[j] = z3[j]; z3[j] = tz;
            }
        }

        monty_ladder(x2, z2, x3, z3, x1, NULL);

        if (swap_this) {
            for (int j = 0; j < 5; ++j) {
                uint64_t tx = x2[j]; x2[j] = x3[j]; x3[j] = tx;
                uint64_t tz = z2[j]; z2[j] = z3[j]; z3[j] = tz;
            }
        }
    }

    /* Final conditional swap */
    if (swap) {
        for (int j = 0; j < 5; ++j) {
            uint64_t tx = x2[j]; x2[j] = x3[j]; x3[j] = tx;
            uint64_t tz = z2[j]; z2[j] = z3[j]; z3[j] = tz;
        }
    }

    /* Result = X2 * Z2^(p-2) mod p */
    uint64_t z2_inv[5];
    fe_inv(z2_inv, z2);

    uint64_t result[5];
    fe_mul(result, x2, z2_inv);

    fe_to_bytes(out, result);
}

extern void crypto_csprng_generate(uint8_t *out, size_t len);

void crypto_x25519_generate_keypair(uint8_t private_key[32], uint8_t public_key[32])
{
    crypto_csprng_generate(private_key, 32);
    clamp_scalar(private_key);
    crypto_x25519_scalar_mult(public_key, private_key, BASE_POINT);
}

int crypto_x25519_shared_secret(const uint8_t private_key[32], const uint8_t peer_public[32],
                                uint8_t shared_secret[32])
{
    uint8_t clamped[32];
    memcpy(clamped, private_key, 32);
    clamp_scalar(clamped);
    crypto_x25519_scalar_mult(shared_secret, clamped, peer_public);
    memset(clamped, 0, 32);

    /* Check for low-order point (all-zero result) */
    int all_zero = 1;
    for (int i = 0; i < 32; ++i) {
        if (shared_secret[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) return -1;

    return 0;
}
