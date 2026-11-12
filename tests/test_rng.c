#include "test.h"

#include "core/rng.h"

// Valores publicados pela implementação de referência do PCG32
// (pcg32-demo com seed 42 e stream 54).
static void rng_matches_reference(void)
{
    static const uint32_t expected[6] = {
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu,
    };
    Rng r;
    rng_seed(&r, 42u, 54u);
    for (int i = 0; i < 6; i++) CHECK(rng_u32(&r) == expected[i]);
}

static void rng_same_seed_same_sequence(void)
{
    Rng a, b, c;
    rng_seed(&a, 123u, 1u);
    rng_seed(&b, 123u, 1u);
    rng_seed(&c, 124u, 1u);
    int different = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t va = rng_u32(&a);
        CHECK(va == rng_u32(&b));
        if (va != rng_u32(&c)) different++;
    }
    CHECK(different > 90);
}

static void rng_float_stays_in_range(void)
{
    Rng r;
    rng_seed(&r, 7u, 1u);
    double sum = 0.0;
    for (int i = 0; i < 20000; i++) {
        float f = rng_float(&r);
        CHECK(f >= 0.0f && f < 1.0f);
        sum += f;
    }
    CHECK_NEAR(sum / 20000.0, 0.5, 0.02);

    for (int i = 0; i < 1000; i++) {
        float f = rng_range(&r, -3.0f, 5.0f);
        CHECK(f >= -3.0f && f < 5.0f);
    }
}

static void rng_below_is_uniform(void)
{
    Rng r;
    rng_seed(&r, 99u, 1u);
    int counts[6] = { 0 };
    for (int i = 0; i < 60000; i++) {
        uint32_t v = rng_below(&r, 6u);
        CHECK(v < 6u);
        if (v < 6u) counts[v]++;
    }
    for (int i = 0; i < 6; i++) CHECK(counts[i] > 9500 && counts[i] < 10500);
    CHECK(rng_below(&r, 0u) == 0u);
    CHECK(rng_below(&r, 1u) == 0u);
}

static void rng_int_covers_inclusive_range(void)
{
    Rng r;
    rng_seed(&r, 5u, 1u);
    bool saw_lo = false, saw_hi = false;
    for (int i = 0; i < 2000; i++) {
        int v = rng_int(&r, -2, 2);
        CHECK(v >= -2 && v <= 2);
        if (v == -2) saw_lo = true;
        if (v == 2) saw_hi = true;
    }
    CHECK(saw_lo && saw_hi);
    CHECK(rng_int(&r, 9, 9) == 9);
    CHECK(rng_int(&r, 9, 3) == 9);
}

static void rng_unit_vectors_cover_the_sphere(void)
{
    Rng r;
    rng_seed(&r, 11u, 1u);
    Vector3 sum = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 20000; i++) {
        Vector3 v = rng_unit_vec3(&r);
        CHECK_NEAR(Vector3Length(v), 1.0f, 1e-4);
        sum = Vector3Add(sum, v);
    }
    // Direções uniformes se cancelam: a média fica perto de zero.
    CHECK(Vector3Length(sum) / 20000.0f < 0.03f);
}

void suite_rng(void)
{
    RUN(rng_matches_reference);
    RUN(rng_same_seed_same_sequence);
    RUN(rng_float_stays_in_range);
    RUN(rng_below_is_uniform);
    RUN(rng_int_covers_inclusive_range);
    RUN(rng_unit_vectors_cover_the_sphere);
}
