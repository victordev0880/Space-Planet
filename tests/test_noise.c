#include "test.h"

#include "core/noise.h"
#include "core/rng.h"

static void noise_is_deterministic_and_seeded(void)
{
    int different = 0;
    for (int i = 0; i < 200; i++) {
        float x = (float)i * 0.37f, y = (float)i * -0.21f + 3.0f, z = (float)i * 0.11f - 7.0f;
        CHECK(noise3(x, y, z, 5u) == noise3(x, y, z, 5u));
        if (noise3(x, y, z, 5u) != noise3(x, y, z, 6u)) different++;
    }
    CHECK(different > 190);
}

static void noise_stays_in_range_and_is_centered(void)
{
    Rng r;
    rng_seed(&r, 3u, 1u);
    double sum = 0.0;
    float lowest = 0.0f, highest = 0.0f;
    for (int i = 0; i < 50000; i++) {
        float v = noise3(rng_range(&r, -50.0f, 50.0f), rng_range(&r, -50.0f, 50.0f),
                         rng_range(&r, -50.0f, 50.0f), 9u);
        CHECK(v >= -1.0f && v <= 1.0f);
        sum += v;
        if (v < lowest) lowest = v;
        if (v > highest) highest = v;
    }
    CHECK_NEAR(sum / 50000.0, 0.0, 0.02);
    // Usa uma boa parte da faixa, para os dois lados.
    CHECK(lowest < -0.5f && highest > 0.5f);
}

static void noise_is_zero_on_grid_points(void)
{
    for (int i = -5; i <= 5; i++) {
        CHECK(noise3((float)i, (float)(i * 2), (float)(-i), 1u) == 0.0f);
    }
}

static void noise_is_smooth(void)
{
    // Um passo pequeno muda pouco o valor: não há saltos, nem ao cruzar
    // de uma célula da grade para outra.
    Rng r;
    rng_seed(&r, 8u, 1u);
    float step = 0.01f;
    for (int i = 0; i < 5000; i++) {
        float x = rng_range(&r, -20.0f, 20.0f), y = rng_range(&r, -20.0f, 20.0f), z = rng_range(&r, -20.0f, 20.0f);
        float a = noise3(x, y, z, 2u);
        CHECK(fabsf(noise3(x + step, y, z, 2u) - a) < 0.05f);
        CHECK(fabsf(noise3(x, y + step, z, 2u) - a) < 0.05f);
        CHECK(fabsf(noise3(x, y, z + step, 2u) - a) < 0.05f);
    }
}

static void noise_fbm_stays_in_range(void)
{
    Rng r;
    rng_seed(&r, 4u, 1u);
    float lowest = 0.0f, highest = 0.0f;
    for (int i = 0; i < 20000; i++) {
        float v = noise_fbm3(rng_range(&r, -9.0f, 9.0f), rng_range(&r, -9.0f, 9.0f),
                             rng_range(&r, -9.0f, 9.0f), 5, 2.0f, 0.5f, 77u);
        CHECK(v >= -1.0f && v <= 1.0f);
        if (v < lowest) lowest = v;
        if (v > highest) highest = v;
    }
    CHECK(lowest < -0.3f && highest > 0.3f);

    CHECK(noise_fbm3(1.3f, 2.1f, 0.4f, 0, 2.0f, 0.5f, 1u) == 0.0f);          // zero camadas
    CHECK(noise_fbm3(1.3f, 2.1f, 0.4f, 1, 2.0f, 0.5f, 1u) == noise3(1.3f, 2.1f, 0.4f, 1u));
}

void suite_noise(void)
{
    RUN(noise_is_deterministic_and_seeded);
    RUN(noise_stays_in_range_and_is_centered);
    RUN(noise_is_zero_on_grid_points);
    RUN(noise_is_smooth);
    RUN(noise_fbm_stays_in_range);
}
