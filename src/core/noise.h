// Ruído 3D com seed (ruído de gradiente, no estilo "Perlin").
//
// "Ruído" aqui é uma função suave e pseudoaleatória do espaço: pontos
// vizinhos dão valores parecidos, e a mesma seed dá sempre o mesmo resultado.
// É a matéria-prima do relevo: avaliado na superfície de uma esfera, vira
// continentes e oceanos sem emenda nem distorção nos polos.
#ifndef CORE_NOISE_H
#define CORE_NOISE_H

#include <stdint.h>

// Valor em [-1, 1]. Vale exatamente 0 em coordenadas inteiras.
float noise3(float x, float y, float z, uint32_t seed);

// fBm ("fractal Brownian motion"): soma várias camadas de ruído, cada uma
// com o dobro de detalhe (lacunarity) e metade da força (gain) da anterior.
// As primeiras dão as formas grandes; as últimas, o recorte fino das bordas.
// Resultado normalizado para [-1, 1].
float noise_fbm3(float x, float y, float z, int octaves, float lacunarity, float gain, uint32_t seed);

#endif
