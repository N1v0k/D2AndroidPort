/* Las funciones de math.h que usa la simulación, iguales en todas las máquinas (dmath.h) */
#include "dmath.h"

/* (Taylor hasta r^17 / r^18 con |r| <= pi/4) */
static double d2m_sin_poly(double r)
{
    double r2 = r * r;
    return r + r * r2 * (-1.0 / 6 + r2 * (1.0 / 120 + r2 * (-1.0 / 5040 + r2 * (1.0 / 362880 + r2 * (-1.0 / 39916800
           + r2 * (1.0 / 6227020800.0 + r2 * (-1.0 / 1307674368000.0 + r2 * (1.0 / 355687428096000.0))))))));
}

static double d2m_cos_poly(double r)
{
    double r2 = r * r;
    return 1.0 + r2 * (-1.0 / 2 + r2 * (1.0 / 24 + r2 * (-1.0 / 720 + r2 * (1.0 / 40320 + r2 * (-1.0 / 3628800
           + r2 * (1.0 / 479001600.0 + r2 * (-1.0 / 87178291200.0 + r2 * (1.0 / 20922789888000.0 + r2 * (-1.0 / 6402373705728000.0)))))))));
}

/* x = k * pi/2 + r, con |r| <= pi/4: el cuadrante (k & 3) y r (pi/2 en dos
   partes, la primera de 33 bits: k * esa parte es exacta) */
static int d2m_reduce(float x, double *r)
{
    double xd = x, k = floor(xd * 0.63661977236758134308 + 0.5);
    *r = (xd - k * 1.57079632673412561417e+00) - k * 6.07710050650619224932e-11;
    return (int)((long long)k & 3);
}

float d2_sinf(float x)
{
    if (!(fabsf(x) < 1e15f))
        return isfinite(x) ? 0.0f : x - x;
    double r;
    switch (d2m_reduce(x, &r)) {
    case 0: return (float)d2m_sin_poly(r);
    case 1: return (float)d2m_cos_poly(r);
    case 2: return (float)-d2m_sin_poly(r);
    default: return (float)-d2m_cos_poly(r);
    }
}

float d2_cosf(float x)
{
    if (!(fabsf(x) < 1e15f))
        return isfinite(x) ? 1.0f : x - x;
    double r;
    switch (d2m_reduce(x, &r)) {
    case 0: return (float)d2m_cos_poly(r);
    case 1: return (float)-d2m_sin_poly(r);
    case 2: return (float)-d2m_cos_poly(r);
    default: return (float)d2m_sin_poly(r);
    }
}

/* atan(z) con 0 <= z <= 1: arriba de tan(pi/12), atan(z) = pi/6 +
   atan((z*raíz(3) - 1) / (z + raíz(3))), y la serie hasta z^31 */
static double d2m_atan01(double z)
{
    double off = 0.0;
    if (z > 0.26794919243112270647) {
        z = (z * 1.73205080756887729353 - 1.0) / (z + 1.73205080756887729353);
        off = 0.52359877559829887308;
    }
    double z2 = z * z, s = -1.0 / 31;
    for (int k = 29; k >= 1; k -= 2)
        s = (k & 2 ? -1.0 : 1.0) / k + z2 * s;
    return off + z * s;
}

float d2_atan2f(float y, float x)
{
    if (isnan(x) || isnan(y))
        return x + y;
    double ax = fabs((double)x), ay = fabs((double)y), a;
    if (ax == 0.0 && ay == 0.0)
        a = 0.0;
    else if (isinf(ax) || isinf(ay))
        a = isinf(ax) && isinf(ay) ? 0.78539816339744830962 : isinf(ax) ? 0.0 : 1.57079632679489661923;
    else
        a = ay <= ax ? d2m_atan01(ay / ax) : 1.57079632679489661923 - d2m_atan01(ax / ay);
    if (signbit(x))
        a = 3.14159265358979323846 - a;
    return (float)(signbit(y) ? -a : a);
}

/* e^x = 2^k * e^r, con |r| <= ln(2)/2 (ln 2 en dos partes) y Taylor hasta r^14 */
float d2_expf(float x)
{
    if (isnan(x))
        return x;
    if (x > 89.0f)
        return HUGE_VALF;
    if (x < -104.0f)
        return 0.0f;
    double xd = x, k = floor(xd * 1.44269504088896340736 + 0.5);
    double r = (xd - k * 6.93147180369123816490e-01) - k * 1.90821492927058770002e-10, e = 1.0;
    for (int n = 14; n >= 1; n--)
        e = 1.0 + e * r / n;
    return (float)ldexp(e, (int)k);
}

float d2_hypotf(float x, float y)
{
    double xd = x, yd = y; /* (los cuadrados de float son exactos en double) */
    return (float)sqrt(xd * xd + yd * yd);
}
