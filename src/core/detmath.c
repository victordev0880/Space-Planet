// Matemática determinística (ver detmath.h). Tudo em double, só com + - * /,
// sqrt e floor, que o IEEE 754 obriga a darem o mesmo resultado em qualquer
// máquina; o resultado em float é o double arredondado. A precisão sobra
// para o jogo (erro relativo perto de 1e-15 antes de virar float).
//
// O Makefile compila com -ffp-contract=off: sem isso o compilador poderia
// fundir a*b+c numa instrução FMA (um arredondamento só) em uma máquina e
// não em outra.
#include "core/detmath.h"

#undef sin
#undef cos
#undef tan
#undef atan
#undef atan2
#undef asin
#undef acos
#undef sinf
#undef cosf
#undef tanf
#undef atanf
#undef atan2f
#undef asinf
#undef acosf

static const double PI_D      = 3.14159265358979311600e+00;
static const double HALF_PI_D = 1.57079632679489655800e+00;
static const double TWO_PI_D  = 6.28318530717958623200e+00;
static const double SIXTH_PI  = 5.23598775598298815658e-01;
static const double SQRT3     = 1.73205080756887719318e+00;
static const double INV_PIO2  = 6.36619772367581382433e-01;

// pi/2 em duas partes (Cody-Waite, como no fdlibm): a primeira tem só 33 bits,
// então n * PIO2_HI é exato para n de até 2^20 (ângulos de até ~1,6 milhão).
static const double PIO2_HI = 1.57079632673412561417e+00;
static const double PIO2_LO = 6.07710050650619224932e-11;

// Polinômios de seno e cosseno em [-pi/4, pi/4] (coeficientes do fdlibm).
static double kernel_sin(double r)
{
    double z = r * r;
    double p = 8.33333333332248946124e-03 + z * (-1.98412698298579493134e-04 + z * (2.75573137070700676789e-06
             + z * (-2.50507602534068634195e-08 + z * 1.58969099521155010221e-10)));
    return r + r * z * (-1.66666666666666324348e-01 + z * p);
}

static double kernel_cos(double r)
{
    double z = r * r;
    double p = 4.16666666666666019037e-02 + z * (-1.38888888888741095749e-03 + z * (2.48015872894767294178e-05
             + z * (-2.75573143513906633035e-07 + z * (2.08757232129817482790e-09 + z * -1.13596475577881948265e-11))));
    return 1.0 - 0.5 * z + z * z * p;
}

// Reduz x a r em [-pi/4, pi/4] e devolve o quadrante (x = n*pi/2 + r).
static int reduce(double x, double *r)
{
    // Ângulos enormes (não acontecem no jogo) passam antes por fmod, que é
    // exato por definição; perde-se precisão, mas nunca o determinismo.
    if (x > 1.0e6 || x < -1.0e6) x = fmod(x, TWO_PI_D);
    double n = floor(x * INV_PIO2 + 0.5);
    *r = (x - n * PIO2_HI) - n * PIO2_LO;
    return (int)((long long)n & 3);
}

double det_sin(double x)
{
    if (x != x || x - x != 0.0) return (x - x) / (x - x);   // NaN e infinito: NaN
    double r;
    switch (reduce(x, &r)) {
    case 0:  return kernel_sin(r);
    case 1:  return kernel_cos(r);
    case 2:  return -kernel_sin(r);
    default: return -kernel_cos(r);
    }
}

double det_cos(double x)
{
    if (x != x || x - x != 0.0) return (x - x) / (x - x);
    double r;
    switch (reduce(x, &r)) {
    case 0:  return kernel_cos(r);
    case 1:  return -kernel_sin(r);
    case 2:  return -kernel_cos(r);
    default: return kernel_sin(r);
    }
}

double det_tan(double x)
{
    if (x != x || x - x != 0.0) return (x - x) / (x - x);
    double r;
    int q = reduce(x, &r);
    double s = kernel_sin(r), c = kernel_cos(r);
    return (q & 1) ? -c / s : s / c;
}

// atan em [0, tan(pi/12)]: série de Taylor (x^2 < 0,072; 10 termos sobram).
static double atan_small(double x)
{
    double z = x * x;
    double p = 1.0 / 19.0;
    p = 1.0 / 17.0 - z * p;
    p = 1.0 / 15.0 - z * p;
    p = 1.0 / 13.0 - z * p;
    p = 1.0 / 11.0 - z * p;
    p = 1.0 / 9.0 - z * p;
    p = 1.0 / 7.0 - z * p;
    p = 1.0 / 5.0 - z * p;
    p = 1.0 / 3.0 - z * p;
    return x - x * z * p;
}

double det_atan(double x)
{
    if (x != x) return x;
    int negative = x < 0.0;
    double a = negative ? -x : x;
    double base = 0.0;
    int inverted = a > 1.0;
    if (inverted) a = 1.0 / a;                          // atan(a) = pi/2 - atan(1/a)
    if (a > 0.26794919243112270) {                      // tan(pi/12)
        a = (a * SQRT3 - 1.0) / (a + SQRT3);            // atan(a) = pi/6 + atan(...)
        base = SIXTH_PI;
    }
    double result = base + atan_small(a);
    if (inverted) result = HALF_PI_D - result;
    return negative ? -result : result;
}

double det_atan2(double y, double x)
{
    if (x != x || y != y) return x + y;
    if (x == 0.0) {
        if (y > 0.0) return HALF_PI_D;
        if (y < 0.0) return -HALF_PI_D;
        return 0.0;
    }
    double a = det_atan(y / x);
    if (x > 0.0) return a;
    return (y < 0.0) ? a - PI_D : a + PI_D;
}

double det_asin(double x)
{
    if (x != x || x > 1.0 || x < -1.0) return (x - x) / (x - x);
    return det_atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

double det_acos(double x)
{
    if (x != x || x > 1.0 || x < -1.0) return (x - x) / (x - x);
    return det_atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}

float det_sinf(float x)            { return (float)det_sin((double)x); }
float det_cosf(float x)            { return (float)det_cos((double)x); }
float det_tanf(float x)            { return (float)det_tan((double)x); }
float det_atanf(float x)           { return (float)det_atan((double)x); }
float det_atan2f(float y, float x) { return (float)det_atan2((double)y, (double)x); }
float det_asinf(float x)           { return (float)det_asin((double)x); }
float det_acosf(float x)           { return (float)det_acos((double)x); }
