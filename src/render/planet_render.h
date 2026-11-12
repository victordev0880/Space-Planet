// Desenho do planeta: uma malha na placa de vídeo para cada chunk com superfície.
//
// Há dois modos de iluminar:
//   com shader   a malha leva a cor do material e a normal; a luz do Sol, o
//                detalhe de perto e o pulso do núcleo são calculados por pixel
//                (assets/shaders/planet.fs).
//   sem shader   a luz é "assada": quando a malha de um chunk é gerada, a cor
//                final de cada vértice já sai calculada (cor do material x luz
//                do Sol naquele ponto). Funciona porque o Sol é fixo. É o modo
//                de --no-shaders, e o usado se o shader não compilar.
//
// Os buffers de cada chunk são criados com folga. Quando um chunk é
// remalhado (crateras), os dados novos são escritos por cima dos antigos, sem
// recriar nada, desde que caibam.
#ifndef RENDER_PLANET_RENDER_H
#define RENDER_PLANET_RENDER_H

#include "raylib.h"

#include "core/arena.h"
#include "game/tuning.h"
#include "render/shaders.h"
#include "voxel/mesher.h"
#include "voxel/planet.h"

typedef struct {
    Mesh mesh;              // buffers na placa de vídeo, com capacidade fixa
    int  vertex_capacity;
    int  index_capacity;
    int  index_count;       // quantos índices desenhar (0 = chunk sem superfície)
    int  vertex_count;
    bool loaded;
} ChunkMesh;

typedef struct {
    ChunkMesh   *chunks;      // um por chunk do planeta
    int          chunk_count;
    Material     material;    // material padrão do raylib, para o modo sem shader
    PlanetShader shader;
    bool         use_shaders; // false com --no-shaders
    bool         shaded;      // true quando o shader está carregado e em uso
    Arena        arena;       // memória de trabalho do mesher
    MeshData     scratch;     // malha do chunk em geração
    uint8_t     *lit_colors;  // cores já iluminadas do chunk em geração (modo sem shader)

    // Versão do planeta que a malha de cada chunk representa. Se for diferente
    // de planet.chunk_version, o chunk mudou e precisa ser remalhado.
    uint32_t *meshed_version;

    // estatísticas para o overlay de debug e o relatório do autoplay
    int    mesh_count;
    int    vertex_total;
    int    triangle_total;
    double rebuild_ms;          // tempo da última geração completa de malhas

    int    remeshed_last_frame; // chunks remalhados no último quadro
    double remesh_ms_last_frame;
    int    backlog;             // chunks que ficaram para os próximos quadros
    int    remeshed_total;
    double remesh_ms_total;     // tempo somado de todos os remalhamentos
    int    remeshed_max_frame;  // máximos desde o início, para o relatório
    int    backlog_max;
    double remesh_ms_max;
} PlanetRender;

bool planet_render_init(PlanetRender *r, const Planet *p, const Tuning *t, bool use_shaders);

// Relê os arquivos do shader (F5). Chamar antes de planet_render_rebuild.
void planet_render_reload_shader(PlanetRender *r);

// Gera de novo a malha de todos os chunks (ao iniciar e depois de um F5).
void planet_render_rebuild(PlanetRender *r, const Planet *p, const Tuning *t);

// Chamar uma vez por quadro: remalha os chunks que mudaram desde a última
// vez (crateras), começando pelos mais próximos da câmera e parando ao
// atingir o orçamento do tuning (número de chunks ou tempo). O que sobrar
// fica para os próximos quadros, para nenhum quadro travar.
void planet_render_update(PlanetRender *r, const Planet *p, const Tuning *t, Vector3 camera_pos);

// Chamar dentro de BeginMode3D. `time` é o tempo de jogo, em segundos.
void planet_render_draw(const PlanetRender *r, const Tuning *t, Vector3 camera_pos, float time, bool wireframe);

void planet_render_unload(PlanetRender *r);

#endif
