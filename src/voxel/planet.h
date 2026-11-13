// O planeta como campo de densidade em uma grade 3D de voxels.
//
// Cada ponto da grade guarda uma DENSIDADE de 0 a 255 e um MATERIAL. Onde a
// densidade é 128 ou mais há matéria; abaixo disso é vazio. A superfície do
// planeta é a fronteira entre os dois, e a malha desenhada na tela é gerada a
// partir dela (ver mesher.h). Como a densidade varia aos poucos perto da
// superfície, a fronteira pode ficar em qualquer ponto entre dois voxels, e
// é isso que deixa o relevo e as crateras suaves em vez de "em blocos".
//
// Destruir o planeta é só diminuir densidades. A soma de todas elas é o
// volume, e o volume é o HP.
//
// Unidades: 1 voxel = 1 unidade do mundo. O centro do planeta fica na origem.
#ifndef VOXEL_PLANET_H
#define VOXEL_PLANET_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/vmath.h"

#define VOXEL_SOLID            128      // densidade a partir da qual há matéria
#define VOXEL_ISO              127.5f   // valor exato da superfície entre duas amostras
#define VOXEL_MAX_CHUNK        16       // maior tamanho de chunk aceito pelo mesher

// Variação da densidade por unidade de distância, perto da superfície. Com
// 32, a densidade vai de 0 a 255 em uma faixa de 8 voxels centrada na
// superfície. A faixa precisa ser larga assim para a normal sair certa: ela é
// calculada comparando densidades a até uns 3 voxels do ponto, e se alguma
// delas já estivesse travada em 0 ou 255 a normal sairia torta (aparecem
// anéis de sombra na superfície).
#define VOXEL_DENSITY_PER_UNIT 32.0f

// Folga mínima, em voxels, entre o ponto mais alto do relevo e a borda da grade.
#define VOXEL_EDGE_MARGIN      2.0f

// A malha de um chunk depende de pontos da grade um pouco além dele: a camada
// extra de células (1 voxel) mais os vizinhos usados para calcular a normal e
// a cor de cada vértice (outro voxel). Ao mudar um ponto, todo chunk a esta
// distância precisa ser refeito.
#define VOXEL_REMESH_MARGIN    2

typedef enum {
    VMAT_NONE,         // vazio
    VMAT_WATER_SHALLOW,
    VMAT_WATER,
    VMAT_WATER_DEEP,
    VMAT_ICE,
    VMAT_SAND,
    VMAT_GRASS,
    VMAT_FOREST,
    VMAT_DESERT,
    VMAT_ROCK,
    VMAT_SNOW,
    VMAT_CRUST,        // rocha escura sob a superfície
    VMAT_MANTLE,       // camada incandescente
    VMAT_CORE,         // núcleo
    VMAT_COUNT
} VoxelMaterial;

// Receita do planeta. Mesmos parâmetros, mesmo planeta.
typedef struct {
    int      grid;             // pontos da grade por eixo
    int      chunk;            // tamanho do chunk, em voxels por eixo
    uint32_t seed;
    float    radius;           // raio do nível do mar
    float    land_height;      // altura máxima do relevo acima do mar
    float    ocean_depth;      // profundidade máxima do mar
    float    land_bias;        // desloca o nível do mar: positivo = mais terra
    float    noise_freq;       // maior = continentes menores e mais numerosos
    int      noise_octaves;
    float    relief_span;      // valor do ruído em que o relevo chega ao máximo
    float    moisture_freq;    // tamanho das manchas de floresta e deserto
    float    ice_latitude;     // a partir daqui há gelo (0 = equador, 1 = polo)
    float    sand_height;      // abaixo desta altura (em voxels) a costa é areia
    float    rock_height;      // fração da altura máxima onde começa a rocha
    float    snow_height;      // ... e a neve (desce perto dos polos)
    float    desert_moisture;  // umidade abaixo da qual vira deserto
    float    forest_moisture;  // umidade acima da qual vira floresta
    float    surface_depth;    // espessura da camada de cima (bioma, água ou gelo) sobre a crosta
    float    mantle_radius;    // fração do raio onde termina a crosta
    float    core_radius;      // fração do raio onde começa o núcleo
} PlanetParams;

typedef struct {
    PlanetParams params;
    int      size;             // pontos por eixo (= params.grid)
    int      chunk_size;
    int      chunks_per_axis;
    float    center;           // coordenada de grade do centro do planeta
    uint8_t *density;          // size^3 valores
    uint8_t *material;         // size^3 valores (VoxelMaterial)
    // Cópia da grade logo após a geração: o conserto (terraform.h) devolve os
    // voxels a ela. Gerar de novo a partir do ruído seria caro demais.
    uint8_t *original_density;
    uint8_t *original_material;
    int64_t  volume;           // soma atual das densidades
    int64_t  initial_volume;   // soma logo após a geração
    int64_t  core_volume;          // parte do volume que é núcleo
    int64_t  initial_core_volume;

    // Um contador por chunk, incrementado sempre que algo muda a malha dele.
    // Quem desenha guarda a versão que já transformou em malha; se a daqui
    // for diferente, o chunk precisa ser remalhado.
    uint32_t *chunk_version;   // chunks_per_axis^3 valores
} Planet;

// O relevo em uma direção a partir do centro do planeta.
typedef struct {
    float         elevation;   // altura do terreno relativa ao mar (negativa = fundo do mar)
    bool          land;        // terreno acima do nível do mar
    VoxelMaterial surface;     // o que se vê de cima: água, gelo, grama...
} Terrain;

// Reserva a grade na arena e gera o planeta. Retorna false se os parâmetros
// forem inválidos ou a arena não tiver espaço.
bool planet_init(Planet *p, const PlanetParams *params, Arena *arena);

// Gera de novo sobre a grade já reservada (grid e chunk precisam ser os mesmos).
void planet_generate(Planet *p, const PlanetParams *params);

Terrain planet_terrain(const PlanetParams *params, Vector3 dir);

// Duas receitas são idênticas?
bool planet_params_equal(const PlanetParams *a, const PlanetParams *b);

// Fração do volume inicial que ainda existe: 1.0 = planeta intacto.
float planet_hp(const Planet *p);

// Marca para remalhar os chunks cujas malhas dependem dos pontos da grade na
// caixa [min, max] (coordenadas de grade). Devolve quantos chunks marcou.
int planet_touch(Planet *p, int min_x, int min_y, int min_z, int max_x, int max_y, int max_z);

// O mesmo, só para o núcleo.
float planet_core_hp(const Planet *p);

static inline int planet_chunk_count(const Planet *p)
{
    return p->chunks_per_axis * p->chunks_per_axis * p->chunks_per_axis;
}

static inline int planet_chunk_index(const Planet *p, int cx, int cy, int cz)
{
    return (cz * p->chunks_per_axis + cy) * p->chunks_per_axis + cx;
}

// Densidade (0..255) em uma posição qualquer do mundo, interpolando entre os
// 8 voxels vizinhos. Fora da grade vale 0.
float planet_sample(const Planet *p, Vector3 world_pos);

// Direção em que a densidade mais cresce (aponta para dentro da matéria).
// A normal da superfície é o oposto dela.
Vector3 planet_gradient(const Planet *p, Vector3 world_pos);

// Acesso direto à grade. Coordenadas fora dela contam como vazio.
static inline bool planet_in_bounds(const Planet *p, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < p->size && y < p->size && z < p->size;
}

static inline int planet_index(const Planet *p, int x, int y, int z)
{
    return (z * p->size + y) * p->size + x;
}

static inline uint8_t planet_density(const Planet *p, int x, int y, int z)
{
    return planet_in_bounds(p, x, y, z) ? p->density[planet_index(p, x, y, z)] : 0;
}

static inline uint8_t planet_material(const Planet *p, int x, int y, int z)
{
    return planet_in_bounds(p, x, y, z) ? p->material[planet_index(p, x, y, z)] : (uint8_t)VMAT_NONE;
}

#endif
