// Matemática determinística (core/detmath.c): precisa ser precisa como a da
// biblioteca C e dar os mesmos bits em qualquer máquina.
#include "test.h"

#include <stdint.h>

#include "core/detmath.h"

// Os nomes entre parênteses chamam a biblioteca C de verdade: a macro de
// detmath.h só troca "sinf(" escrito sem parênteses em volta do nome.
static void detmath_matches_libc(void)
{
    double worst = 0.0;
    for (int i = -20000; i <= 20000; i++) {
        float x = (float)i * 0.0031f;   // de -62 a 62 radianos
        double err = 0.0;
        err = fmax(err, fabs((double)det_sinf(x) - (double)(sinf)(x)));
        err = fmax(err, fabs((double)det_cosf(x) - (double)(cosf)(x)));
        err = fmax(err, fabs(det_sin((double)x) - (sin)((double)x)));
        float u = (float)i / 20000.0f;  // de -1 a 1
        err = fmax(err, fabs((double)det_asinf(u) - (double)(asinf)(u)));
        err = fmax(err, fabs((double)det_acosf(u) - (double)(acosf)(u)));
        err = fmax(err, fabs((double)det_atanf(x) - (double)(atanf)(x)));
        err = fmax(err, fabs((double)det_atan2f(u, x * 0.01f) - (double)(atan2f)(u, x * 0.01f)));
        float t = (float)i * 0.00007f;  // tan longe dos polos
        err = fmax(err, fabs((double)det_tanf(t) - (double)(tanf)(t)) / fmax(1.0, fabs((double)(tanf)(t))));
        worst = fmax(worst, err);
    }
    CHECK(worst < 1e-6);
}

static void detmath_edges(void)
{
    CHECK(det_atan2f(0.0f, 0.0f) == 0.0f);
    CHECK_NEAR(det_atan2f(1.0f, 0.0f), 1.5707963f, 2e-7);
    CHECK_NEAR(det_atan2f(-1.0f, 0.0f), -1.5707963f, 2e-7);
    CHECK_NEAR(det_atan2f(0.0f, -1.0f), 3.1415927f, 1e-6);
    CHECK_NEAR(det_atan2f(-1e-9f, -1.0f), -3.1415927f, 1e-6);
    CHECK(det_acosf(1.0f) == 0.0f);
    CHECK_NEAR(det_acosf(-1.0f), 3.1415927f, 1e-6);
    CHECK_NEAR(det_asinf(1.0f), 1.5707963f, 2e-7);
    CHECK(det_sinf(0.0f) == 0.0f);
    CHECK(det_cosf(0.0f) == 1.0f);
    float nan = det_sinf(INFINITY);
    CHECK(nan != nan);
    float big = det_sinf(3.0e7f);   // ângulo absurdo: não trava, fica em [-1, 1]
    CHECK(big >= -1.0f && big <= 1.0f);
    // A troca pegou: sinf escrito normalmente é o nosso.
    CHECK(sinf(0.7f) == det_sinf(0.7f));
}

// Uma tabela de resultados resumida num hash. O valor esperado foi gravado no
// Windows; a CI roda o mesmo teste no Linux. Só muda se detmath.c mudar.
uint64_t detmath_table_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = -5000; i <= 5000; i++) {
        float x = (float)i * 0.0137f;
        float u = (float)i / 5000.0f;
        float v[6] = { det_sinf(x), det_cosf(x), det_tanf(x * 0.1f), det_asinf(u), det_acosf(u), det_atan2f(u, x) };
        for (int k = 0; k < 6; k++) {
            uint32_t bits;
            memcpy(&bits, &v[k], sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
    }
    return h;
}

#define DETMATH_EXPECTED 0x49ae17662e38bf50ull

static void detmath_same_bits_everywhere(void)
{
    CHECK(detmath_table_hash() == DETMATH_EXPECTED);
}

void suite_detmath(void)
{
    RUN(detmath_matches_libc);
    RUN(detmath_edges);
    RUN(detmath_same_bits_everywhere);
}
