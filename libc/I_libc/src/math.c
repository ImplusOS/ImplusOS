#include <math.h>
#include <stdint.h>

#define PI 3.14159265358979323846

double fabs(double x)
{
    return x < 0 ? -x : x;
}

float fabsf(float x)
{
    return x < 0.0f ? -x : x;
}

long double fabsl(long double x)
{
    return x < 0.0L ? -x : x;
}

double floor(double x)
{
    long long i = (long long)x;
    if (x < 0 && x != (double)i)
        i--;
    return (double)i;
}

float floorf(float x)
{
    long long i = (long long)x;
    if (x < 0.0f && x != (float)i)
        i--;
    return (float)i;
}

long double floorl(long double x)
{
    long long i = (long long)x;
    if (x < 0.0L && x != (long double)i)
        i--;
    return (long double)i;
}

double ceil(double x)
{
    long long i = (long long)x;
    if (x > 0 && x != (double)i)
        i++;
    return (double)i;
}

float ceilf(float x)
{
    long long i = (long long)x;
    if (x > 0.0f && x != (float)i)
        i++;
    return (float)i;
}

long double ceill(long double x)
{
    long long i = (long long)x;
    if (x > 0.0L && x != (long double)i)
        i++;
    return (long double)i;
}

double fmod(double x, double y)
{
    if (y == 0.0)
        return NAN;

    long long n = (long long)(x / y);
    double r = x - (double)n * y;

    if (r < 0)
        r += fabs(y);

    return r;
}

float fmodf(float x, float y)
{
    if (y == 0.0f)
        return (float)NAN;

    long long n = (long long)(x / y);
    float r = x - (float)n * y;

    if (r < 0.0f)
        r += fabsf(y);

    return r;
}

double sqrt(double x)
{
    if (x < 0.0)
        return NAN;
    if (x == 0.0)
        return 0.0;

    double r = x;

    for (int i = 0; i < 32; i++)
        r = 0.5 * (r + x / r);

    return r;
}

float sqrtf(float x)
{
    if (x < 0.0f)
        return (float)NAN;
    if (x == 0.0f)
        return 0.0f;

    float r = x;

    for (int i = 0; i < 32; i++)
        r = 0.5f * (r + x / r);

    return r;
}

long double sqrtl(long double x)
{
    if (x < 0.0L)
        return (long double)NAN;
    if (x == 0.0L)
        return 0.0L;

    long double r = x;

    for (int i = 0; i < 32; i++)
        r = 0.5L * (r + x / r);

    return r;
}

double cos(double x)
{
    if (__builtin_isnan(x) || __builtin_isinf(x)) return NAN;

    x = fmod(x, 2.0 * PI);

    if (x > PI)
        x -= 2.0 * PI;
    if (x < -PI)
        x += 2.0 * PI;

    if (x > PI / 2)
        x = PI - x;
    if (x < -PI / 2)
        x = -PI - x;

    double x2 = x * x;

    return 1.0
        - x2 / 2.0
        + x2 * x2 / 24.0
        - x2 * x2 * x2 / 720.0;
}

float cosf(float x)
{
    return (float)cos((double)x);
}

long double cosl(long double x)
{
    return (long double)cos((double)x);
}

double sin(double x)
{
    return cos(x - PI / 2.0);
}

float sinf(float x)
{
    return (float)sin((double)x);
}

long double sinl(long double x)
{
    return (long double)sin((double)x);
}

double tan(double x)
{
    double c = cos(x);
    if (c == 0.0) return NAN;
    return sin(x) / c;
}

float tanf(float x)
{
    return (float)tan((double)x);
}

long double tanl(long double x)
{
    return (long double)tan((double)x);
}

double sinh(double x)
{
    double ex = exp(x);
    double emx = exp(-x);
    return (ex - emx) / 2.0;
}

float sinhf(float x)
{
    return (float)sinh((double)x);
}

double cosh(double x)
{
    double ex = exp(x);
    double emx = exp(-x);
    return (ex + emx) / 2.0;
}

float coshf(float x)
{
    return (float)cosh((double)x);
}

double tanh(double x)
{
    double ex = exp(x);
    double emx = exp(-x);
    return (ex - emx) / (ex + emx);
}

float tanhf(float x)
{
    return (float)tanh((double)x);
}

double asinh(double x)
{
    return log(x + sqrt(x * x + 1.0));
}

float asinhf(float x)
{
    return (float)asinh((double)x);
}

double acosh(double x)
{
    if (x < 1.0) return NAN;
    return log(x + sqrt(x * x - 1.0));
}

float acoshf(float x)
{
    return (float)acosh((double)x);
}

double atanh(double x)
{
    if (x <= -1.0 || x >= 1.0) return NAN;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

float atanhf(float x)
{
    return (float)atanh((double)x);
}

static double atan_poly(double x)
{
    double x2 = x * x;
    return x * (1.0
        - x2 / 3.0
        + x2 * x2 / 5.0
        - x2 * x2 * x2 / 7.0);
}

double acos(double x)
{
    if (x > 1.0 || x < -1.0)
        return NAN;

    double s = sqrt(1.0 - x * x);

    if (fabs(x) < 0.7)
        return PI / 2.0 - atan_poly(x / s);

    double a = atan_poly(s / fabs(x));
    return x > 0 ? a : PI - a;
}

float acosf(float x)
{
    return (float)acos((double)x);
}

double asin(double x)
{
    if (x > 1.0 || x < -1.0)
        return NAN;
    return PI / 2.0 - acos(x);
}

float asinf(float x)
{
    return (float)asin((double)x);
}

double atan(double x)
{
    return atan_poly(x);
}

float atanf(float x)
{
    return (float)atan((double)x);
}

double atan2(double y, double x)
{
    if (x > 0) return atan(y/x);
    if (x < 0 && y >= 0) return atan(y/x) + PI;
    if (x < 0 && y < 0) return atan(y/x) - PI;
    if (x == 0 && y > 0) return PI / 2.0;
    if (x == 0 && y < 0) return -PI / 2.0;
    return 0;
}

float atan2f(float y, float x)
{
    return (float)atan2((double)y, (double)x);
}

static double ln_simple(double x)
{
    if (x <= 0.0)
        return NAN;

    int e = 0;

    while (x > 2.0) {
        x *= 0.5;
        e++;
    }

    while (x < 1.0) {
        x *= 2.0;
        e--;
    }

    double t = (x - 1.0) / (x + 1.0);
    double t2 = t * t;

    double s = t;
    double p = t;

    for (int k = 1; k < 10; k++) {
        p *= t2;
        s += p / (2 * k + 1);
    }

    return 2 * s + e * 0.6931471805599453;
}

static double exp_simple(double x)
{
    double sum = 1.0;
    double term = 1.0;

    for (int i = 1; i < 20; i++) {
        term *= x / i;
        sum += term;
    }

    return sum;
}

double exp(double x)
{
    return exp_simple(x);
}

float expf(float x)
{
    return (float)exp_simple((double)x);
}

long double expl(long double x)
{
    return (long double)exp_simple((double)x);
}

double log(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    return ln_simple(x);
}

float logf(float x)
{
    return (float)log((double)x);
}

long double logl(long double x)
{
    return (long double)log((double)x);
}

double log10(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    return ln_simple(x) / 2.302585092994046;
}

float log10f(float x)
{
    return (float)log10((double)x);
}

double log2(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    return ln_simple(x) / 0.6931471805599453;
}

float log2f(float x)
{
    return (float)log2((double)x);
}

double logb(double x)
{
    if (x == 0.0) return -INFINITY;
    int e = 0;
    double abs_x = fabs(x);
    while (abs_x >= 2.0) { abs_x *= 0.5; e++; }
    while (abs_x < 1.0) { abs_x *= 2.0; e--; }
    return (double)e;
}

double log1p(double x)
{
    return log(1.0 + x);
}

double expm1(double x)
{
    return exp(x) - 1.0;
}

double pow(double x, double y)
{
    if (y == 0.0)
        return 1.0;

    if (x == 0.0)
        return 0.0;

    long long yi = (long long)y;

    if ((double)yi == y) {
        double r = 1.0;
        int neg = 0;

        if (yi < 0) {
            neg = 1;
            yi = -yi;
        }

        while (yi) {
            if (yi & 1)
                r *= x;
            x *= x;
            yi >>= 1;
        }

        return neg ? 1.0 / r : r;
    }

    if (x < 0.0)
        return NAN;

    return exp_simple(y * ln_simple(x));
}

float powf(float x, float y)
{
    return (float)pow((double)x, (double)y);
}

long double powl(long double x, long double y)
{
    return (long double)pow((double)x, (double)y);
}

double ldexp(double x, int exp)
{
    return x * pow(2.0, (double)exp);
}

float ldexpf(float x, int exp)
{
    return x * powf(2.0f, (float)exp);
}

double hypot(double x, double y)
{
    x = fabs(x);
    y = fabs(y);
    if (x > y) {
        double t = y / x;
        return x * sqrt(1.0 + t * t);
    } else if (y > 0.0) {
        double t = x / y;
        return y * sqrt(1.0 + t * t);
    }
    return x;
}

float hypotf(float x, float y)
{
    return (float)hypot((double)x, (double)y);
}

double cbrt(double x)
{
    int neg = 0;
    if (x < 0.0) { neg = 1; x = -x; }
    double r = x;
    for (int i = 0; i < 32; i++)
        r = (2.0 * r + x / (r * r)) / 3.0;
    return neg ? -r : r;
}

float cbrtf(float x)
{
    return (float)cbrt((double)x);
}

double round(double x)
{
    double r = floor(x + 0.5);
    if (x - floor(x) == 0.5 && x > 0) r = ceil(x);
    else if (ceil(x) - x == 0.5 && x < 0) r = floor(x);
    else r = floor(x + 0.5);
    return r;
}

float roundf(float x)
{
    return (float)round((double)x);
}

double trunc(double x)
{
    if (x < 0) return ceil(x);
    return floor(x);
}

float truncf(float x)
{
    return (float)trunc((double)x);
}

double nearbyint(double x)
{
    return round(x);
}

float nearbyintf(float x)
{
    return roundf(x);
}

double rint(double x)
{
    return round(x);
}

float rintf(float x)
{
    return roundf(x);
}

double fmin(double x, double y)
{
    if (__builtin_isnan(x)) return y;
    if (__builtin_isnan(y)) return x;
    return x < y ? x : y;
}

double fmax(double x, double y)
{
    if (__builtin_isnan(x)) return y;
    if (__builtin_isnan(y)) return x;
    return x > y ? x : y;
}

double fma(double x, double y, double z)
{
    return x * y + z;
}

double copysign(double x, double y)
{
    uint64_t xi = *(uint64_t*)&x;
    uint64_t yi = *(uint64_t*)&y;
    xi = (xi & 0x7FFFFFFFFFFFFFFFULL) | (yi & 0x8000000000000000ULL);
    return *(double*)&xi;
}

double signbit(double x)
{
    uint64_t xi;
    xi = *(uint64_t*)&x;
    return (double)(int)((xi >> 63) & 1);
}

double modf(double x, double *iptr)
{
    if (iptr) {
        double int_part;
        if (x >= 0.0) {
            int_part = floor(x);
        } else {
            int_part = ceil(x);
        }
        *iptr = int_part;
    }
    return x - *iptr;
}

double frexp(double x, int *exp)
{
    if (!exp) return x;
    if (x == 0.0 || __builtin_isnan(x) || __builtin_isinf(x)) {
        *exp = 0;
        return x;
    }
    int e = 0;
    double abs_x = fabs(x);
    while (abs_x >= 1.0) { abs_x *= 0.5; e++; }
    while (abs_x < 0.5) { abs_x *= 2.0; e--; }
    *exp = e;
    return x > 0 ? abs_x : -abs_x;
}
