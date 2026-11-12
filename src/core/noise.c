#include "core/noise.h"

#include <math.h>

// Embaralha as coordenadas inteiras de um ponto da grade (mais a seed) em um
// número de 32 bits. Substitui a tabela de permutação do Perlin clássico:
// não guarda estado, então qualquer seed funciona sem preparação.
static uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed)
{
    uint32_t h = seed ^ 0x9E3779B9u;
    h ^= (uint32_t)x * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h *= 0xC2B2AE35u;
    h ^= (uint32_t)y * 0x27D4EB2Fu;
    h = (h << 15) | (h >> 17);
    h *= 0x165667B1u;
    h ^= (uint32_t)z * 0x9E3779B1u;
    h = (h << 11) | (h >> 21);
    h *= 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// Produto escalar entre o vetor (x, y, z) e um de 12 gradientes fixos (as
// direções das arestas de um cubo), escolhido pelos 4 bits baixos do hash.
// Cada gradiente tem duas coordenadas valendo +1 ou -1 e uma valendo 0, então
// o resultado é sempre "mais ou menos uma coordenada, mais ou menos outra":
//    0..3: x e y     4..7: x e z     8..11: y e z     12..15: repetem alguns
//
// Escrito com escolhas simples em vez de um switch de 16 casos: o hash é
// imprevisível, e um switch faria o processador errar o desvio quase sempre.
// Esta função roda dezenas de milhões de vezes ao gerar o planeta.
static float grad(uint32_t h, float x, float y, float z)
{
    uint32_t low = h & 15u;
    float u = (low < 8u) ? x : y;
    float v = (low < 4u) ? y : ((low == 12u || low == 14u) ? x : z);
    return ((low & 1u) ? -u : u) + ((low & 2u) ? -v : v);
}

// Curva de suavização 6t^5 - 15t^4 + 10t^3: sai de 0 e chega em 1 com
// inclinação zero nas pontas, o que evita "vincos" entre células da grade.
static float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Maior inteiro que não passa de v. Faz o mesmo que floorf() para os valores
// que o jogo usa (bem dentro da faixa de um int), mas sem chamar a biblioteca:
// converter para inteiro corta a parte fracionária em direção ao zero, o que
// para números negativos arredonda para cima; nesse caso desce um.
static int32_t floor_to_int(float v)
{
    int32_t i = (int32_t)v;
    return (v < (float)i) ? i - 1 : i;
}

float noise3(float x, float y, float z, uint32_t seed)
{
    int32_t xi = floor_to_int(x), yi = floor_to_int(y), zi = floor_to_int(z);
    float tx = x - (float)xi, ty = y - (float)yi, tz = z - (float)zi;
    float u = fade(tx), v = fade(ty), w = fade(tz);

    // Contribuição de cada um dos 8 cantos da célula que contém o ponto.
    float n000 = grad(hash3(xi,     yi,     zi,     seed), tx,        ty,        tz);
    float n100 = grad(hash3(xi + 1, yi,     zi,     seed), tx - 1.0f, ty,        tz);
    float n010 = grad(hash3(xi,     yi + 1, zi,     seed), tx,        ty - 1.0f, tz);
    float n110 = grad(hash3(xi + 1, yi + 1, zi,     seed), tx - 1.0f, ty - 1.0f, tz);
    float n001 = grad(hash3(xi,     yi,     zi + 1, seed), tx,        ty,        tz - 1.0f);
    float n101 = grad(hash3(xi + 1, yi,     zi + 1, seed), tx - 1.0f, ty,        tz - 1.0f);
    float n011 = grad(hash3(xi,     yi + 1, zi + 1, seed), tx,        ty - 1.0f, tz - 1.0f);
    float n111 = grad(hash3(xi + 1, yi + 1, zi + 1, seed), tx - 1.0f, ty - 1.0f, tz - 1.0f);

    float x00 = lerpf(n000, n100, u);
    float x10 = lerpf(n010, n110, u);
    float x01 = lerpf(n001, n101, u);
    float x11 = lerpf(n011, n111, u);
    float result = lerpf(lerpf(x00, x10, v), lerpf(x01, x11, v), w);

    if (result > 1.0f) result = 1.0f;
    if (result < -1.0f) result = -1.0f;
    return result;
}

float noise_fbm3(float x, float y, float z, int octaves, float lacunarity, float gain, uint32_t seed)
{
    float sum = 0.0f, total = 0.0f;
    float amplitude = 1.0f, frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        // Cada camada usa uma seed própria para não repetir o desenho da anterior.
        sum += amplitude * noise3(x * frequency, y * frequency, z * frequency, seed + (uint32_t)i * 7919u);
        total += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return (total > 0.0f) ? sum / total : 0.0f;
}
