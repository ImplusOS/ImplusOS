#include <math.h>

#define PI 3.14159265358979323846
#define NAN (__builtin_nan(""))
#define INFINITY (__builtin_inf())

double fabs(double x)
{
    return x < 0 ? -x : x;
}

double floor(double x)
{
    long long i = (long long)x;
    if (x < 0 && x != (double)i)
        i--;
    return (double)i;
}

double ceil(double x)
{
    long long i = (long long)x;
    if (x > 0 && x != (double)i)
        i++;
    return (double)i;
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

double sin(double x)
{
    return cos(x - PI / 2.0);
}

double tan(double x)
{
    double c = cos(x);
    if (c == 0.0) return NAN;
    return sin(x) / c;
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

double asin(double x)
{
    if (x > 1.0 || x < -1.0)
        return NAN;
    return PI / 2.0 - acos(x);
}

double atan(double x)
{
    return atan_poly(x);
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

double log(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    return ln_simple(x);
}

double log10(double x)
{
    if (x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    return ln_simple(x) / 2.302585092994046;
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

double ldexp(double x, int exp)
{
    return x * pow(2.0, (double)exp);
}