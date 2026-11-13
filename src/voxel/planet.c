#include "voxel/planet.h"

#include <math.h>
#include <string.h>

#include "core/noise.h"

// Deslocamentos de seed, para cada mapa de ruído ter um desenho independente.
#define SEED_MOISTURE 1013u
#define SEED_ICE_EDGE 2027u

#define ICE_EDGE_FREQ   5.0f     // recorte da borda das calotas
#define ICE_EDGE_AMOUNT 0.035f
#define DESERT_MAX_LATITUDE 0.55f   // desertos não chegam perto dos polos

#define SNOW_POLAR_DROP 0.45f       // quanto a linha de neve desce do equador até a calota

// Faixas de profundidade do mar, como fração da profundidade máxima.
#define SHALLOW_WATER_LIMIT 0.22f
#define DEEP_WATER_LIMIT    0.60f

// Distância da superfície, em voxels, além da qual a densidade já está
// travada em 0 (fora) ou 255 (dentro), com meio voxel de folga.
#define SHELL_MARGIN (VOXEL_ISO / VOXEL_DENSITY_PER_UNIT + 0.5f)

// Abaixo deste raio a direção a partir do centro deixa de fazer sentido.
#define MIN_SHELL_RADIUS 0.001f

// A comparação abaixo olha os bytes da struct, o que só é correto se não
// houver espaços de preenchimento entre os campos. Todos têm 4 bytes; se
// alguém acrescentar um campo de outro tamanho, a compilação falha aqui.
_Static_assert(sizeof(PlanetParams) == 20 * 4, "PlanetParams mudou: revise planet_params_equal");

bool planet_params_equal(const PlanetParams *a, const PlanetParams *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

Terrain planet_terrain(const PlanetParams *pp, Vector3 dir)
{
    Terrain t;

    // Ruído avaliado na direção (um ponto da esfera unitária): não tem emenda
    // nem aperta nos polos, como aconteceria com um mapa 2D enrolado.
    float f = pp->noise_freq;
    float h = noise_fbm3(dir.x * f, dir.y * f, dir.z * f, pp->noise_octaves, 2.0f, 0.5f, pp->seed);
    h += pp->land_bias;

    if (h > 0.0f) {
        float e = h / pp->relief_span;
        if (e > 1.0f) e = 1.0f;
        t.elevation = pp->land_height * e * sqrtf(e);   // costa plana, montanhas no interior
        t.land = true;
    } else {
        float d = -h / pp->relief_span;
        if (d > 1.0f) d = 1.0f;
        t.elevation = -pp->ocean_depth * d;
        t.land = false;
    }

    float latitude = fabsf(dir.y);
    float ice_edge = noise3(dir.x * ICE_EDGE_FREQ, dir.y * ICE_EDGE_FREQ, dir.z * ICE_EDGE_FREQ,
                            pp->seed + SEED_ICE_EDGE) * ICE_EDGE_AMOUNT;

    if (latitude > pp->ice_latitude + ice_edge) {
        t.surface = VMAT_ICE;
    } else if (!t.land) {
        float depth = (pp->ocean_depth > 0.0f) ? -t.elevation / pp->ocean_depth : 0.0f;   // 0 a 1
        t.surface = (depth < SHALLOW_WATER_LIMIT) ? VMAT_WATER_SHALLOW
                  : (depth < DEEP_WATER_LIMIT)    ? VMAT_WATER
                  :                                  VMAT_WATER_DEEP;
    } else {
        float height = t.elevation / pp->land_height;   // 0 a 1
        float cold = fminf(latitude / pp->ice_latitude, 1.0f);
        float snow_line = pp->snow_height - SNOW_POLAR_DROP * cold * cold;   // a neve desce perto dos polos

        if (height > snow_line) {
            t.surface = VMAT_SNOW;
        } else if (height > pp->rock_height) {
            t.surface = VMAT_ROCK;
        } else if (t.elevation < pp->sand_height) {
            t.surface = VMAT_SAND;
        } else {
            float m = pp->moisture_freq;
            float moisture = noise_fbm3(dir.x * m, dir.y * m, dir.z * m, 3, 2.0f, 0.5f,
                                        pp->seed + SEED_MOISTURE);
            if (moisture < pp->desert_moisture && latitude < DESERT_MAX_LATITUDE) t.surface = VMAT_DESERT;
            else if (moisture > pp->forest_moisture)                               t.surface = VMAT_FOREST;
            else                                                                   t.surface = VMAT_GRASS;
        }
    }
    return t;
}

// Camadas internas, só pelo raio: crosta, manto e núcleo.
static VoxelMaterial interior_material(const PlanetParams *pp, float r)
{
    if (r < pp->core_radius * pp->radius) return VMAT_CORE;
    if (r < pp->mantle_radius * pp->radius) return VMAT_MANTLE;
    return VMAT_CRUST;
}

void planet_generate(Planet *p, const PlanetParams *params)
{
    p->params = *params;
    p->params.grid = p->size;         // as dimensões da grade não mudam depois de reservada
    p->params.chunk = p->chunk_size;

    const PlanetParams *pp = &p->params;
    const int n = p->size;
    const float radius = pp->radius;

    // Só uma casca em volta da superfície precisa do ruído (que é caro).
    // Acima dela é tudo vazio. Abaixo, a densidade já chegou a 255 e o
    // material já é o do interior em qualquer direção, porque a superfície
    // nunca fica abaixo do nível do mar.
    const float outer = radius + pp->land_height + SHELL_MARGIN;
    const float inner = radius - fmaxf(SHELL_MARGIN, pp->surface_depth + 0.5f);

    int64_t volume = 0;
    int64_t core_volume = 0;
    int index = 0;
    for (int z = 0; z < n; z++) {
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++, index++) {
                Vector3 pos = { (float)x - p->center, (float)y - p->center, (float)z - p->center };
                float r = Vector3Length(pos);
                uint8_t density;
                VoxelMaterial material;

                if (r >= outer) {
                    density = 0;
                    material = VMAT_NONE;
                } else if (r <= inner || r < MIN_SHELL_RADIUS) {
                    density = 255;
                    material = interior_material(pp, r);
                } else {
                    Vector3 dir = Vector3Scale(pos, 1.0f / r);   // r >= MIN_SHELL_RADIUS: divisão segura
                    Terrain t = planet_terrain(pp, dir);
                    // Superfície: o terreno, ou o nível do mar onde ele fica abaixo d'água.
                    float top = radius + (t.land ? t.elevation : 0.0f);

                    // Densidade proporcional à profundidade abaixo da superfície,
                    // passando por VOXEL_ISO exatamente em cima dela.
                    float value = VOXEL_ISO + (top - r) * VOXEL_DENSITY_PER_UNIT;
                    density = (uint8_t)(Clamp(value, 0.0f, 255.0f) + 0.5f);

                    // A camada de cima (bioma, água ou gelo) tem a mesma espessura
                    // em terra e no mar. A profundidade do mar só escolhe o tom da
                    // água: uma camada de água mais fina que um ou dois voxels,
                    // com outro material logo abaixo, apareceria como faixas de
                    // cor na superfície.
                    if (density == 0) {
                        material = VMAT_NONE;
                    } else if (top - r <= pp->surface_depth) {
                        material = t.surface;
                    } else {
                        material = interior_material(pp, r);
                    }
                }

                p->density[index] = density;
                p->material[index] = (uint8_t)material;
                volume += density;
                if (material == VMAT_CORE) core_volume += density;
            }
        }
    }

    p->volume = volume;
    p->initial_volume = volume;
    p->core_volume = core_volume;
    p->initial_core_volume = core_volume;
    size_t count = (size_t)n * (size_t)n * (size_t)n;
    memcpy(p->original_density, p->density, count);
    memcpy(p->original_material, p->material, count);

    // Tudo mudou: todas as malhas ficam desatualizadas.
    int chunks = planet_chunk_count(p);
    for (int i = 0; i < chunks; i++) p->chunk_version[i]++;
}

bool planet_init(Planet *p, const PlanetParams *params, Arena *arena)
{
    *p = (Planet){ 0 };

    if (params->chunk < 2 || params->chunk > VOXEL_MAX_CHUNK) return false;
    if (params->grid < params->chunk || params->grid % params->chunk != 0) return false;
    if (params->grid > 512) return false;

    // O relevo mais alto precisa ficar dentro da grade, com folga. Senão a
    // superfície seria cortada na borda e a malha ficaria aberta.
    float reach = params->radius + params->land_height + VOXEL_EDGE_MARGIN;
    if (reach > (float)(params->grid / 2 - 1)) return false;

    size_t count = (size_t)params->grid * (size_t)params->grid * (size_t)params->grid;
    p->size = params->grid;
    p->chunk_size = params->chunk;
    p->chunks_per_axis = params->grid / params->chunk;
    p->center = (float)params->grid * 0.5f;
    p->density = ARENA_ARRAY(arena, uint8_t, count);
    p->material = ARENA_ARRAY(arena, uint8_t, count);
    p->original_density = ARENA_ARRAY(arena, uint8_t, count);
    p->original_material = ARENA_ARRAY(arena, uint8_t, count);
    p->chunk_version = ARENA_ARRAY(arena, uint32_t, planet_chunk_count(p));
    if (p->density == NULL || p->material == NULL || p->chunk_version == NULL || p->original_density == NULL
        || p->original_material == NULL) {
        return false;
    }

    planet_generate(p, params);
    return true;
}

float planet_hp(const Planet *p)
{
    if (p->initial_volume <= 0) return 0.0f;
    return (float)((double)p->volume / (double)p->initial_volume);
}

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

int planet_touch(Planet *p, int min_x, int min_y, int min_z, int max_x, int max_y, int max_z)
{
    if (max_x < min_x || max_y < min_y || max_z < min_z) return 0;
    const int chunk = p->chunk_size;
    const int last_chunk = p->chunks_per_axis - 1;
    int cx0 = clamp_int((min_x - VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int cy0 = clamp_int((min_y - VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int cz0 = clamp_int((min_z - VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int cx1 = clamp_int((max_x + VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int cy1 = clamp_int((max_y + VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int cz1 = clamp_int((max_z + VOXEL_REMESH_MARGIN) / chunk, 0, last_chunk);
    int touched = 0;
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cy = cy0; cy <= cy1; cy++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                p->chunk_version[planet_chunk_index(p, cx, cy, cz)]++;
                touched++;
            }
        }
    }
    return touched;
}

float planet_core_hp(const Planet *p)
{
    if (p->initial_core_volume <= 0) return 1.0f;   // planeta sem núcleo não morre por ele
    return (float)((double)p->core_volume / (double)p->initial_core_volume);
}

float planet_sample(const Planet *p, Vector3 world_pos)
{
    float gx = world_pos.x + p->center;
    float gy = world_pos.y + p->center;
    float gz = world_pos.z + p->center;

    // Longe da grade não há nada; o teste também evita converter números
    // enormes para inteiro.
    float limit = (float)p->size;
    if (!(gx > -1.0f && gy > -1.0f && gz > -1.0f && gx < limit && gy < limit && gz < limit)) return 0.0f;

    // Parte inteira (arredondando para baixo) sem chamar floorf(): converter
    // para int corta em direção ao zero, então os negativos descem um. Esta
    // função roda dezenas de milhares de vezes por quadro (partículas, raios).
    int x = (int)gx, y = (int)gy, z = (int)gz;
    if (gx < (float)x) x--;
    if (gy < (float)y) y--;
    if (gz < (float)z) z--;
    float tx = gx - (float)x, ty = gy - (float)y, tz = gz - (float)z;

    float d000 = planet_density(p, x,     y,     z);
    float d100 = planet_density(p, x + 1, y,     z);
    float d010 = planet_density(p, x,     y + 1, z);
    float d110 = planet_density(p, x + 1, y + 1, z);
    float d001 = planet_density(p, x,     y,     z + 1);
    float d101 = planet_density(p, x + 1, y,     z + 1);
    float d011 = planet_density(p, x,     y + 1, z + 1);
    float d111 = planet_density(p, x + 1, y + 1, z + 1);

    float x00 = Lerp(d000, d100, tx);
    float x10 = Lerp(d010, d110, tx);
    float x01 = Lerp(d001, d101, tx);
    float x11 = Lerp(d011, d111, tx);
    return Lerp(Lerp(x00, x10, ty), Lerp(x01, x11, ty), tz);
}

Vector3 planet_gradient(const Planet *p, Vector3 q)
{
    // Diferença central: compara a densidade um voxel para cada lado.
    Vector3 g;
    g.x = planet_sample(p, (Vector3){ q.x + 1.0f, q.y, q.z }) - planet_sample(p, (Vector3){ q.x - 1.0f, q.y, q.z });
    g.y = planet_sample(p, (Vector3){ q.x, q.y + 1.0f, q.z }) - planet_sample(p, (Vector3){ q.x, q.y - 1.0f, q.z });
    g.z = planet_sample(p, (Vector3){ q.x, q.y, q.z + 1.0f }) - planet_sample(p, (Vector3){ q.x, q.y, q.z - 1.0f });
    return Vector3Scale(g, 0.5f);
}
