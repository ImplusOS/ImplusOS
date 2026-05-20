typedef double stbtt_float;

static stbtt_float stbtt_fabs_impl(stbtt_float x)
{
    return (x < 0) ? -x : x;
}

static int stbtt_ifloor_impl(stbtt_float x)
{
    int i = (int)x;
    return (x < (stbtt_float)i) ? (i - 1) : i;
}

static int stbtt_iceil_impl(stbtt_float x)
{
    int i = (int)x;
    return (x > (stbtt_float)i) ? (i + 1) : i;
}

static stbtt_float stbtt_sqrt_impl(stbtt_float x)
{
    if (x <= 0.0)
        return 0.0;

    stbtt_float r = x;

    for (int i = 0; i < 20; ++i)
        r = 0.5 * (r + x / r);

    return r;
}

static stbtt_float stbtt_fmod_impl(stbtt_float x, stbtt_float y)
{
    if (y == 0.0)
        return 0.0;

    int q = (int)(x / y);
    return x - ((stbtt_float)q * y);
}

static stbtt_float stbtt_cos_impl(stbtt_float x)
{
    const stbtt_float PI  = 3.14159265358979323846;
    const stbtt_float TAU = 6.28318530717958647692;
    
    while (x >  PI) x -= TAU;
    while (x < -PI) x += TAU;

    stbtt_float x2 = x * x;

    return 1.0
        - x2 / 2.0
        + (x2 * x2) / 24.0
        - (x2 * x2 * x2) / 720.0
        + (x2 * x2 * x2 * x2) / 40320.0;
}

static stbtt_float stbtt_atan_impl(stbtt_float z)
{
    const stbtt_float PI_OVER_4 = 0.7853981633974483;

    return z / (1.0 + 0.28 * z * z);
}

static stbtt_float stbtt_acos_impl(stbtt_float x)
{
    const stbtt_float PI = 3.14159265358979323846;

    if (x >=  1.0) return 0.0;
    if (x <= -1.0) return PI;

    stbtt_float y = stbtt_sqrt_impl(1.0 - x * x);

    stbtt_float a;

    if (stbtt_fabs_impl(x) < 1e-6)
    {
        a = PI / 2.0;
    }
    else
    {
        stbtt_float z = y / x;
        a = stbtt_atan_impl(z);

        if (x < 0.0)
            a += PI;
    }

    return a;
}

static stbtt_float stbtt_pow_impl(stbtt_float x, int y)
{
    stbtt_float r = 1.0;
    int exp = (y < 0) ? -y : y;

    while (exp)
    {
        if (exp & 1)
            r *= x;

        x *= x;
        exp >>= 1;
    }

    return (y < 0) ? (1.0 / r) : r;
}