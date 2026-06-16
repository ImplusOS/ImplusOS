#include "ECDHE.h"
#include <string.h>

#define P256_LIMBS 4

typedef uint64_t p256_t[P256_LIMBS];

/* P-256 prime: p = 2^256 - 2^224 + 2^192 + 2^96 - 1 */
static const p256_t P = {
    0xFFFFFFFFFFFFFFFF, 0x00000000FFFFFFFF,
    0x0000000000000000, 0xFFFFFFFF00000001
};

/* Order n */
static const p256_t N = {
    0xF3B9CAC2FC632551, 0xBCE6FAADA7179E84,
    0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF00000000
};

/* Generator G (uncompressed) */
static const p256_t GX = {
    0xF4A13945D898C296, 0x77037D812DEB33A0,
    0xF8BCE6E563A440F2, 0x6B17D1F2E12C4247
};

static const p256_t GY = {
    0xCBB6406837BF51F5, 0x2BCE33576B315ECE,
    0x8EE7EB4A7C0F9E16, 0x4FE342E2FE1A7F9B
};

static const p256_t ZERO = {0, 0, 0, 0};
static const p256_t ONE  = {1, 0, 0, 0};

static inline void p256_set(p256_t r, const p256_t a)
{
    r[0] = a[0]; r[1] = a[1]; r[2] = a[2]; r[3] = a[3];
}

static inline int p256_is_zero(const p256_t a)
{
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

static inline int p256_is_one(const p256_t a)
{
    return a[0] == 1 && a[1] == 0 && a[2] == 0 && a[3] == 0;
}

static int p256_cmp(const p256_t a, const p256_t b)
{
    for (int i = P256_LIMBS - 1; i >= 0; --i) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

/* add with carry, return carry */
static inline uint64_t addc(uint64_t *r, uint64_t a, uint64_t b)
{
    *r = a + b;
    return (*r < a) ? 1 : 0;
}

/* sub with borrow, return borrow */
static inline uint64_t subb(uint64_t *r, uint64_t a, uint64_t b)
{
    *r = a - b;
    return (*r > a) ? 1 : 0;
}

/* r = a + b (512-bit) */
static void p256_add512(uint64_t r[8], const p256_t a, const p256_t b)
{
    uint64_t carry = 0;
    for (int i = 0; i < P256_LIMBS; ++i) {
        carry = addc(&r[i], a[i] + carry, b[i]);
    }
    r[4] = carry;
    r[5] = 0; r[6] = 0; r[7] = 0;
}

/* r = a - b (return borrow) */
static uint64_t p256_sub512(uint64_t r[8], const uint64_t a[8], const uint64_t b[8])
{
    uint64_t borrow = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t ai = (i < P256_LIMBS) ? a[i] : 0;
        uint64_t bi = (i < P256_LIMBS) ? b[i] : 0;
        borrow = subb(&r[i], ai - borrow, bi);
    }
    return borrow;
}

static void p256_mul(uint64_t r[8], const p256_t a, const p256_t b)
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
    for (int i = 0; i < 8; ++i) r[i] = temp[i];
}

/* Fast reduction modulo P-256 prime */
static void p256_reduce(p256_t r, const uint64_t a[8])
{
    uint64_t s[8];

    /* s1 = a[7], a[6], a[5], a[4], a[3], a[2], a[1], a[0] as 512-bit */
    /* Use NIST fast reduction for P-256 */

    uint64_t t1[4], t2[4], t3[4], t4[4], t5[4], t6[4], t7[4];

    /* t1 = (a[7], a[6], a[5], a[4]) */
    t1[0] = a[4]; t1[1] = a[5]; t1[2] = a[6]; t1[3] = a[7];

    /* t2 = (a[7], a[6], a[5], a[4]) rotated */
    t2[0] = a[5]; t2[1] = a[6]; t2[2] = a[7]; t2[3] = 0;

    /* t3 = (a[7], a[6], a[5], a[4]) rotated differently */
    t3[0] = a[6]; t3[1] = a[7]; t3[2] = 0; t3[3] = 0;

    /* t4 = (a[7], a[6], a[5], a[4]) rotated differently */
    t4[0] = a[7]; t4[1] = 0; t4[2] = 0; t4[3] = 0;

    /* t5 = (0, a[7], a[6], a[5]) */
    t5[0] = 0; t5[1] = a[7]; t5[2] = a[6]; t5[3] = a[5];

    /* t6 = (0, 0, a[7], a[6]) */
    t6[0] = 0; t6[1] = 0; t6[2] = a[7]; t6[3] = a[6];

    /* t7 = (0, 0, 0, a[7]) */
    t7[0] = 0; t7[1] = 0; t7[2] = 0; t7[3] = a[7];

    s[0] = a[0]; s[1] = a[1]; s[2] = a[2]; s[3] = a[3];

    /* s += t1 + t2 + t3 + t4 - t5 - t6 - t7 */
    uint64_t borrow = 0;
    {
        __uint128_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry += (__uint128_t)s[i] + t1[i];
            s[i] = (uint64_t)carry;
            carry >>= 64;
        }
    }
    {
        __uint128_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry += (__uint128_t)s[i] + t2[i];
            s[i] = (uint64_t)carry;
            carry >>= 64;
        }
    }
    {
        __uint128_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry += (__uint128_t)s[i] + t3[i];
            s[i] = (uint64_t)carry;
            carry >>= 64;
        }
    }
    {
        __uint128_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry += (__uint128_t)s[i] + t4[i];
            s[i] = (uint64_t)carry;
            carry >>= 64;
        }
    }

    /* subtract t5, t6, t7 */
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t sub = s[i] - t5[i] - borrow;
        borrow = (sub > s[i]) ? 1 : 0;
        s[i] = sub;
    }
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t sub = s[i] - t6[i] - borrow;
        borrow = (sub > s[i]) ? 1 : 0;
        s[i] = sub;
    }
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t sub = s[i] - t7[i] - borrow;
        borrow = (sub > s[i]) ? 1 : 0;
        s[i] = sub;
    }

    if (borrow) {
        __uint128_t carry = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            carry += (__uint128_t)s[i] + P[i];
            s[i] = (uint64_t)carry;
            carry >>= 64;
        }
    }

    /* Final subtraction if >= p */
    while (1) {
        int ge = 1;
        for (int i = P256_LIMBS - 1; i >= 0; --i) {
            if (s[i] > P[i]) break;
            if (s[i] < P[i]) { ge = 0; break; }
        }
        if (!ge) break;
        borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = s[i] - P[i] - borrow;
            borrow = (d > s[i]) ? 1 : 0;
            s[i] = d;
        }
    }

    r[0] = s[0]; r[1] = s[1]; r[2] = s[2]; r[3] = s[3];
}

/* r = a + b (mod p) */
static void p256_add(p256_t r, const p256_t a, const p256_t b)
{
    uint64_t t[4];
    uint64_t carry = 0;
    for (int i = 0; i < P256_LIMBS; ++i) {
        carry = addc(&t[i], a[i], b[i] + carry);
    }
    if (carry) {
        /* Subtract p */
        uint64_t borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = t[i] - P[i] - borrow;
            borrow = (d > t[i]) ? 1 : 0;
            t[i] = d;
        }
    }
    /* Constant-time conditional subtract */
    for (int i = P256_LIMBS - 1; i >= 0; --i) {
        if (t[i] < P[i]) break;
        if (t[i] > P[i]) {
            uint64_t borrow = 0;
            for (int j = 0; j < P256_LIMBS; ++j) {
                uint64_t d = t[j] - P[j] - borrow;
                borrow = (d > t[j]) ? 1 : 0;
                t[j] = d;
            }
            break;
        }
    }
    p256_set(r, t);
}

/* r = a - b (mod p) */
static void p256_sub(p256_t r, const p256_t a, const p256_t b)
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
            carry = addc(&t[i], t[i], P[i] + carry);
        }
    }
    p256_set(r, t);
}

/* r = a * b (mod p) */
static void p256_mul_mod(p256_t r, const p256_t a, const p256_t b)
{
    uint64_t temp[8];
    p256_mul(temp, a, b);
    p256_reduce(r, temp);
}

/* r = a^2 (mod p) */
static void p256_sqr_mod(p256_t r, const p256_t a)
{
    p256_mul_mod(r, a, a);
}

/* r = -a (mod p) */
static void p256_neg(p256_t r, const p256_t a)
{
    if (p256_is_zero(a)) {
        p256_set(r, ZERO);
        return;
    }
    uint64_t borrow = 0;
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t d = P[i] - a[i] - borrow;
        borrow = (d > P[i]) ? 1 : 0;
        r[i] = d;
    }
}

/* r = a mod n (reduce to order) */
static void p256_mod_order(p256_t r, const p256_t a)
{
    p256_set(r, a);
    while (1) {
        int ge = 1;
        for (int i = P256_LIMBS - 1; i >= 0; --i) {
            if (r[i] > N[i]) break;
            if (r[i] < N[i]) { ge = 0; break; }
        }
        if (!ge) break;
        uint64_t borrow = 0;
        for (int i = 0; i < P256_LIMBS; ++i) {
            uint64_t d = r[i] - N[i] - borrow;
            borrow = (d > r[i]) ? 1 : 0;
            r[i] = d;
        }
    }
}

/* Extended Euclidean Algorithm for modular inverse */
static void p256_inv_mod(p256_t r, const p256_t a)
{
    /* a^(p-2) mod p using Fermat's little theorem */
    /* Exponent p-2 = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFD */
    /* We'll use binary exponentiation (square-and-multiply) */

    p256_t result;
    p256_set(result, ONE);
    p256_t base;
    p256_set(base, a);

    /* p - 2 in little-endian 64-bit limbs */
    uint64_t exp[4] = {
        0xFFFFFFFFFFFFFFFD, 0x00000000FFFFFFFF,
        0x0000000000000000, 0xFFFFFFFF00000001
    };

    for (int bit = 255; bit >= 0; --bit) {
        int limb = bit / 64;
        int bit_in_limb = bit % 64;
        if (bit < 255) {
            p256_sqr_mod(result, result);
        }
        if ((exp[limb] >> bit_in_limb) & 1) {
            p256_mul_mod(result, result, base);
        }
    }

    p256_set(r, result);
}

/* Convert bytes to p256_t (big-endian 32 bytes to little-endian 4 limbs) */
static void bytes_to_p256(p256_t r, const uint8_t b[32])
{
    for (int i = 0; i < P256_LIMBS; ++i) {
        r[i] = 0;
        for (int j = 0; j < 8; ++j) {
            r[i] = (r[i] << 8) | b[i * 8 + j];
        }
    }
}

/* Convert p256_t to bytes (little-endian 4 limbs to big-endian 32 bytes) */
static void p256_to_bytes(const p256_t a, uint8_t b[32])
{
    for (int i = 0; i < P256_LIMBS; ++i) {
        uint64_t limb = a[i];
        for (int j = 0; j < 8; ++j) {
            b[i * 8 + 7 - j] = (uint8_t)(limb & 0xFF);
            limb >>= 8;
        }
    }
}

/* Jacobian point operations */

typedef struct {
    p256_t x, y, z;
} jacobian_t;

static void jacobian_set_inf(jacobian_t *p)
{
    p256_set(p->x, ZERO);
    p256_set(p->y, ZERO);
    p256_set(p->z, ZERO);
}

static int jacobian_is_inf(const jacobian_t *p)
{
    return p256_is_zero(p->z);
}

/* Point doubling: R = 2P in Jacobian coordinates */
static void jacobian_double(jacobian_t *r, const jacobian_t *p)
{
    if (jacobian_is_inf(p)) {
        jacobian_set_inf(r);
        return;
    }

    p256_t t, s, m, tmp;

    /* t = 3 * X^2 + a * Z^4, where a = -3 for P-256 */
    /* For a = -3: t = 3 * (X - Z^2) * (X + Z^2) = 3*(X^2 - Z^4) */
    /* Simplification: t = 3 * X^2 - 3 * Z^4 */

    p256_sqr_mod(t, p->x);        /* X^2 */
    p256_t three_x2;
    p256_add(three_x2, t, t);
    p256_add(three_x2, three_x2, t);  /* 3*X^2 */

    p256_sqr_mod(s, p->z);        /* Z^2 */
    p256_sqr_mod(s, s);           /* Z^4 */
    p256_t three_z4;
    p256_add(three_z4, s, s);
    p256_add(three_z4, three_z4, s);  /* 3*Z^4 */

    p256_sub(t, three_x2, three_z4);  /* t = 3*X^2 - 3*Z^4 = 3*(X^2 - Z^4) */

    /* s = 4 * X * Y^2 */
    p256_sqr_mod(tmp, p->y);       /* Y^2 */
    p256_mul_mod(s, p->x, tmp);    /* X*Y^2 */
    p256_add(s, s, s);             /* 2*X*Y^2 */
    p256_add(s, s, s);             /* 4*X*Y^2 */

    /* m = 8 * Y^4 */
    p256_sqr_mod(m, tmp);          /* Y^4 */
    p256_add(m, m, m);             /* 2*Y^4 */
    p256_add(m, m, m);             /* 4*Y^4 */
    p256_add(m, m, m);             /* 8*Y^4 */

    /* X' = t^2 - 2*s */
    p256_sqr_mod(r->x, t);
    p256_sub(r->x, r->x, s);
    p256_sub(r->x, r->x, s);

    /* Y' = t * (s - X') - m */
    p256_sub(tmp, s, r->x);
    p256_mul_mod(r->y, t, tmp);
    p256_sub(r->y, r->y, m);

    /* Z' = 2 * Y * Z */
    p256_mul_mod(r->z, p->y, p->z);
    p256_add(r->z, r->z, r->z);
}

/* Point addition: R = P + Q in Jacobian coordinates (P != Q, both not inf) */
static void jacobian_add(jacobian_t *r, const jacobian_t *p, const jacobian_t *q)
{
    if (jacobian_is_inf(p)) {
        *r = *q;
        return;
    }
    if (jacobian_is_inf(q)) {
        *r = *p;
        return;
    }

    p256_t z1z1, z2z2, u1, u2, s1, s2, h, hr, i, v, tmp;

    p256_sqr_mod(z1z1, p->z);            /* Z1^2 */
    p256_sqr_mod(z2z2, q->z);            /* Z2^2 */

    p256_mul_mod(u1, p->x, z2z2);         /* U1 = X1 * Z2^2 */
    p256_mul_mod(u2, q->x, z1z1);         /* U2 = X2 * Z1^2 */

    p256_mul_mod(s1, p->y, q->z);        /* S1 = Y1 * Z2^3 */
    p256_mul_mod(s1, s1, z2z2);

    p256_mul_mod(s2, q->y, p->z);        /* S2 = Y2 * Z1^3 */
    p256_mul_mod(s2, s2, z1z1);

    p256_sub(h, u2, u1);                  /* H = U2 - U1 */
    p256_sub(r->z, s2, s1);              /* R = S2 - S1 (store in Z' temporarily) */

    if (p256_is_zero(h)) {
        if (p256_is_zero(r->z)) {
            jacobian_double(r, p);
        } else {
            jacobian_set_inf(r);
        }
        return;
    }

    /* I = (2*H)^2 */
    p256_add(i, h, h);                    /* 2*H */
    p256_sqr_mod(i, i);                   /* (2*H)^2 = 4*H^2 */

    /* J = H * I */
    p256_mul_mod(hr, h, i);              /* H * I = 4*H^3 */

    /* V = U1 * I */
    p256_mul_mod(v, u1, i);              /* V = U1 * I = U1 * 4*H^2 */

    /* X' = R^2 - J - 2*V */
    p256_sqr_mod(r->x, r->z);            /* R^2 */
    p256_sub(r->x, r->x, hr);            /* R^2 - J */
    p256_sub(r->x, r->x, v);             /* R^2 - J - V */
    p256_sub(r->x, r->x, v);             /* R^2 - J - 2*V */

    /* Y' = R * (V - X') - S1 * J */
    p256_sub(tmp, v, r->x);
    p256_mul_mod(r->y, r->z, tmp);
    p256_mul_mod(tmp, s1, hr);
    p256_sub(r->y, r->y, tmp);

    /* Z' = Z1 * Z2 * H */
    p256_mul_mod(r->z, p->z, q->z);
    p256_mul_mod(r->z, r->z, h);
}

/* Convert Jacobian to affine (x, y) */
static void jacobian_to_affine(const jacobian_t *p, p256_t x, p256_t y)
{
    if (jacobian_is_inf(p)) {
        p256_set(x, ZERO);
        p256_set(y, ZERO);
        return;
    }

    p256_t z_inv, z_inv2;
    p256_inv_mod(z_inv, p->z);
    p256_sqr_mod(z_inv2, z_inv);

    p256_mul_mod(x, p->x, z_inv2);       /* x = X / Z^2 */
    p256_mul_mod(y, p->y, z_inv);        /* y = Y / Z^3 */
    p256_mul_mod(y, y, z_inv2);
}

/* Copy Jacobian point */
static void jacobian_set(jacobian_t *r, const jacobian_t *a)
{
    p256_set(r->x, a->x);
    p256_set(r->y, a->y);
    p256_set(r->z, a->z);
}

/* Scalar multiplication: R = k * P */
static void jacobian_scalar_mult(jacobian_t *r, const p256_t k, const jacobian_t *p)
{
    jacobian_t result;
    jacobian_set_inf(&result);
    jacobian_t addend;
    jacobian_set(&addend, p);

    for (int bit = 0; bit < 256; ++bit) {
        int limb = bit / 64;
        int bit_in_limb = bit % 64;
        if ((k[limb] >> bit_in_limb) & 1) {
            jacobian_add(&result, &result, &addend);
        }
        jacobian_double(&addend, &addend);
    }

    *r = result;
}

int crypto_ec_p256_scalar_mult_g(const uint8_t scalar[32], uint8_t out[64])
{
    p256_t k;
    bytes_to_p256(k, scalar);
    p256_mod_order(k, k);

    jacobian_t g;
    p256_set(g.x, GX);
    p256_set(g.y, GY);
    p256_set(g.z, ONE);

    jacobian_t result;
    jacobian_scalar_mult(&result, k, &g);

    p256_t rx, ry;
    jacobian_to_affine(&result, rx, ry);

    p256_to_bytes(rx, out);
    p256_to_bytes(ry, out + 32);

    return 0;
}

int crypto_ec_p256_scalar_mult(const uint8_t scalar[32], const uint8_t point[64], uint8_t out[64])
{
    p256_t k;
    bytes_to_p256(k, scalar);
    p256_mod_order(k, k);

    jacobian_t p;
    bytes_to_p256(p.x, point);
    bytes_to_p256(p.y, point + 32);
    p256_set(p.z, ONE);

    /* Verify point is on curve */
    p256_t lhs, rhs, tmp;
    p256_sqr_mod(lhs, p.y);           /* y^2 */
    p256_sqr_mod(rhs, p.x);           /* x^2 */
    p256_mul_mod(tmp, rhs, p.x);      /* x^3 */
    p256_add(rhs, rhs, rhs);
    p256_add(rhs, rhs, rhs);          /* 3*x^2 (a = -3, so a*x = -3x) */
    p256_sub(rhs, tmp, rhs);          /* x^3 - 3x */
    p256_add(rhs, rhs, (p256_t){0xD898C296, 0xF4A13945, 0x2DEB33A0, 0x77037D81}); /* but properly: add b */

    /* Actually a = -3, so ax = -3x. x^3 + ax + b = x^3 - 3x + b */
    /* b = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B */
    static const p256_t B_COEFF = {
        0x3BCE3C3E27D2604B, 0x651D06B0CC53B0F6,
        0xB3EBBD55769886BC, 0x5AC635D8AA3A93E7
    };
    p256_add(rhs, rhs, B_COEFF);

    if (!p256_is_zero(lhs) && p256_cmp(lhs, rhs) != 0) {
        return -1;
    }

    jacobian_t result;
    jacobian_scalar_mult(&result, k, &p);

    p256_t rx, ry;
    jacobian_to_affine(&result, rx, ry);

    p256_to_bytes(rx, out);
    p256_to_bytes(ry, out + 32);

    return 0;
}

int crypto_ec_p256_add_points(const uint8_t a[64], const uint8_t b[64], uint8_t out[64])
{
    if (a == NULL || b == NULL || out == NULL) return -1;

    jacobian_t p1, p2, sum;

    /* Convert affine to Jacobian */
    bytes_to_p256(p1.x, a);
    bytes_to_p256(p1.y, a + 32);
    p256_set(p1.z, ONE);

    bytes_to_p256(p2.x, b);
    bytes_to_p256(p2.y, b + 32);
    p256_set(p2.z, ONE);

    jacobian_add(&sum, &p1, &p2);

    p256_t rx, ry;
    jacobian_to_affine(&sum, rx, ry);

    p256_to_bytes(rx, out);
    p256_to_bytes(ry, out + 32);

    return 0;
}

extern void crypto_csprng_generate(uint8_t *out, size_t len);

int crypto_ecdhe_generate_keypair(uint8_t private_key[32], uint8_t public_key[64])
{
    if (private_key == NULL || public_key == NULL) return -1;

    crypto_csprng_generate(private_key, 32);

    /* Ensure private key is in [1, n-1] */
    p256_t sk;
    bytes_to_p256(sk, private_key);
    p256_mod_order(sk, sk);

    /* Make sure not zero */
    if (p256_is_zero(sk)) {
        sk[0] = 1;
    }

    p256_to_bytes(sk, private_key);

    return crypto_ec_p256_scalar_mult_g(private_key, public_key);
}

int crypto_ecdhe_shared_secret(const uint8_t private_key[32], const uint8_t peer_public[64],
                               uint8_t shared_secret[32])
{
    if (private_key == NULL || peer_public == NULL || shared_secret == NULL) {
        return -1;
    }

    uint8_t point[64];
    int ret = crypto_ec_p256_scalar_mult(private_key, peer_public, point);
    if (ret < 0) return ret;

    /* Shared secret is x-coordinate */
    memcpy(shared_secret, point, 32);
    memset(point, 0, sizeof(point));
    return 0;
}
