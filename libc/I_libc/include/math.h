#pragma once

#define M_E        2.71828182845904523536
#define M_LOG2E    1.44269504088896340736
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

#define HUGE_VAL  (__builtin_huge_val())
#define INFINITY  (__builtin_inff())
#define NAN       (__builtin_nanf(""))
#define FP_NAN    0
#define FP_INFINITE 1
#define FP_ZERO   2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

static inline int __isnan(double x) { return __builtin_isnan(x); }
static inline int __isinf(double x) { return __builtin_isinf(x); }
static inline int __isfinite(double x) { return __builtin_isfinite(x); }
static inline int __isnormal(double x) { return __builtin_isnormal(x); }
static inline int __fpclassify(double x) { return __builtin_fpclassify(0, 1, 2, 3, 4, x); }
#define isnan(x) __isnan(x)
#define isinf(x) __isinf(x)
#define isfinite(x) __isfinite(x)
#define isnormal(x) __isnormal(x)
#define fpclassify(x) __fpclassify(x)

static inline int __isnanf(float x) { return __builtin_isnan(x); }
static inline int __isinff(float x) { return __builtin_isinf(x); }
static inline int __isfinitef(float x) { return __builtin_isfinite(x); }

double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double fabs(double x);
double ldexp(double x, int exp);
double scalbn(double x, int exp);
double log(double x);
double log10(double x);
double log2(double x);
double exp(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double logb(double x);
double log1p(double x);
double expm1(double x);

double fmin(double x, double y);
double fmax(double x, double y);
double fma(double x, double y, double z);
double signbit(double x);
double modf(double x, double *iptr);
double frexp(double x, int *exp);
float frexpf(float x, int *exp);
long double frexpl(long double x, int *exp);

double hypot(double x, double y);
double cbrt(double x);
double round(double x);
double trunc(double x);
double nearbyint(double x);
double rint(double x);
double copysign(double x, double y);
float cbrtf(float x);
float roundf(float x);
float truncf(float x);

/* float variants */
float sinf(float x);
float cosf(float x);
float tanf(float x);
float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fabsf(float x);
float powf(float x, float y);
float logf(float x);
float log10f(float x);
float log2f(float x);
float expf(float x);
float scalbnf(float x, int exp);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float hypotf(float x, float y);
float nearbyintf(float x);
float rintf(float x);
float fminf(float x, float y);
float fmaxf(float x, float y);
float fmodf(float x, float y);

/* Bessel functions */
double j0(double x);
double j1(double x);
double jn(int n, double x);
double y0(double x);
double y1(double x);
double yn(int n, double x);
float ldexpf(float x, int exp);

/* long double variants */
long double sinl(long double x);
long double cosl(long double x);
long double tanl(long double x);
long double sqrtl(long double x);
long double floorl(long double x);
long double ceill(long double x);
long double fabsl(long double x);
long double powl(long double x, long double y);
long double logl(long double x);
long double expl(long double x);
long double scalbnl(long double x, int exp);
