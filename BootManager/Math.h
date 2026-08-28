typedef float stbtt_float;

static float stbtt_fabs_impl(float x)
{
    return (x < 0.0f) ? -x : x;
}

static int stbtt_ifloor_impl(float x)
{
    int i = (int)x;
    return (x < (float)i) ? (i - 1) : i;
}

static int stbtt_iceil_impl(float x)
{
    int i = (int)x;
    return (x > (float)i) ? (i + 1) : i;
}

static float stbtt_sqrt_impl(float x)
{
    if (x <= 0.0f)
        return 0.0f;

    float r = x;

    for (int i = 0; i < 20; ++i)
        r = 0.5f * (r + x / r);

    return r;
}

static float stbtt_fmod_impl(float x, float y)
{
    if (y == 0.0f)
        return 0.0f;

    int q = (int)(x / y);
    return x - ((float)q * y);
}

static float stbtt_cos_impl(float x)
{
    const float PI  = 3.14159265f;
    const float TAU = 6.28318530f;

    while (x >  PI) x -= TAU;
    while (x < -PI) x += TAU;

    float x2 = x * x;

    return 1.0f
        - x2 / 2.0f
        + (x2 * x2) / 24.0f
        - (x2 * x2 * x2) / 720.0f
        + (x2 * x2 * x2 * x2) / 40320.0f;
}

static float stbtt_atan_impl(float z)
{
    return z / (1.0f + 0.28f * z * z);
}

static float stbtt_acos_impl(float x)
{
    const float PI = 3.14159265358979323846f;

    if (x >=  1.0f) return 0.0f;
    if (x <= -1.0f) return PI;

    float y = stbtt_sqrt_impl(1.0f - x * x);

    float a;

    if (stbtt_fabs_impl(x) < 1e-6f)
    {
        a = PI / 2.0f;
    }
    else
    {
        float z = y / x;
        a = stbtt_atan_impl(z);

        if (x < 0.0f)
            a += PI;
    }

    return a;
}

static float stbtt_pow_impl(float x, int y)
{
    float r = 1.0f;
    int exp = (y < 0) ? -y : y;

    while (exp)
    {
        if (exp & 1)
            r *= x;

        x *= x;
        exp >>= 1;
    }

    return (y < 0) ? (1.0f / r) : r;
}