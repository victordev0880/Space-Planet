// Gerador de números pseudoaleatórios PCG32.
//
// O estado é explícito (nada de rand() global): cada sistema pode ter o seu
// gerador, e a mesma seed reproduz exatamente a mesma sequência. É a base da
// simulação determinística.
#ifndef CORE_RNG_H
#define CORE_RNG_H

#include <stdint.h>

#include "core/vmath.h"

typedef struct {
    uint64_t state;
    uint64_t inc;   // seleciona a sequência ("stream"); é sempre ímpar
} Rng;

void     rng_seed(Rng *r, uint64_t seed, uint64_t stream);
uint32_t rng_u32(Rng *r);
uint32_t rng_below(Rng *r, uint32_t n);           // inteiro uniforme em [0, n)
int      rng_int(Rng *r, int lo, int hi);         // inteiro uniforme em [lo, hi]
float    rng_float(Rng *r);                       // [0, 1)
float    rng_range(Rng *r, float lo, float hi);   // [lo, hi)
Vector3  rng_unit_vec3(Rng *r);                   // direção uniforme na esfera

#endif
