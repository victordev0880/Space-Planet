#include "core/rng.h"

#include <math.h>

// Algoritmo PCG-XSH-RR 64/32 (pcg-random.org), implementação de referência.

void rng_seed(Rng *r, uint64_t seed, uint64_t stream)
{
    r->state = 0u;
    r->inc = (stream << 1u) | 1u;
    rng_u32(r);
    r->state += seed;
    rng_u32(r);
}

uint32_t rng_u32(Rng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    if (n == 0u) return 0u;
    // Rejeita a faixa inicial que causaria viés no resto da divisão.
    uint32_t threshold = (0u - n) % n;
    for (;;) {
        uint32_t x = rng_u32(r);
        if (x >= threshold) return x % n;
    }
}

int rng_int(Rng *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)hi - (uint32_t)lo + 1u;
    if (span == 0u) return (int)rng_u32(r);   // faixa inteira de int
    return (int)((uint32_t)lo + rng_below(r, span));
}

float rng_float(Rng *r)
{
    // 24 bits cabem exatamente em um float, então o resultado nunca chega a 1.
    return (float)(rng_u32(r) >> 8u) * (1.0f / 16777216.0f);
}

float rng_range(Rng *r, float lo, float hi)
{
    return lo + (hi - lo) * rng_float(r);
}

Vector3 rng_unit_vec3(Rng *r)
{
    // Altura uniforme + ângulo uniforme dá distribuição uniforme na esfera.
    float y = rng_range(r, -1.0f, 1.0f);
    float angle = rng_range(r, 0.0f, 2.0f * PI);
    float ring = sqrtf(1.0f - y * y);
    return (Vector3){ ring * cosf(angle), y, ring * sinf(angle) };
}
