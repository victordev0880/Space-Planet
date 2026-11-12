// Gera a malha de triângulos de um chunk a partir do campo de densidade.
//
// Algoritmo: "surface nets".
//   1. Uma CÉLULA é o cubinho entre 8 pontos vizinhos da grade. Se alguns dos
//      8 cantos são sólidos e outros vazios, a superfície passa por dentro
//      dela, e a célula ganha UM vértice, posicionado na média dos pontos em
//      que a superfície cruza as arestas do cubinho.
//   2. Para cada aresta da grade que liga um ponto sólido a um vazio, as 4
//      células em volta dela são unidas por um quadrado (2 triângulos).
//
// Comparado ao marching cubes, gera cerca de metade dos triângulos, não
// precisa de tabelas de 256 casos e os vértices já saem compartilhados entre
// triângulos. Em troca, arredonda cantos vivos, o que combina com um planeta.
//
// Emendas entre chunks: cada chunk também gera os vértices da camada de
// células vizinha de que precisa. Como a posição de um vértice depende só
// das 8 densidades da sua célula, os dois chunks chegam exatamente ao mesmo
// ponto e não aparece fresta.
#ifndef VOXEL_MESHER_H
#define VOXEL_MESHER_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/color.h"
#include "voxel/planet.h"

// Pior caso de um chunk: um vértice por célula (com a camada extra) e três
// quadrados por ponto da grade.
#define MESHER_MAX_VERTICES ((VOXEL_MAX_CHUNK + 1) * (VOXEL_MAX_CHUNK + 1) * (VOXEL_MAX_CHUNK + 1))
#define MESHER_MAX_INDICES  (VOXEL_MAX_CHUNK * VOXEL_MAX_CHUNK * VOXEL_MAX_CHUNK * 18)

// Malha em arrays simples, no formato que a placa de vídeo recebe.
typedef struct {
    float    *positions;   // 3 por vértice, em coordenadas do mundo
    float    *normals;     // 3 por vértice, apontando para fora da matéria
    uint8_t  *colors;      // 4 por vértice: cor do material (RGB) e emissão (A)
    float    *params;      // 2 por vértice: brilho especular (0 a 1) e um valor reservado
    uint16_t *indices;     // 3 por triângulo, sentido anti-horário visto de fora
    int       vertex_count;
    int       index_count;
    int       vertex_capacity;
    int       index_capacity;
} MeshData;

// Como transformar materiais em cor.
typedef struct {
    Rgb8    palette[VMAT_COUNT];
    uint8_t emission[VMAT_COUNT];   // 0 = só reflete luz; 255 = brilha por conta própria
    uint8_t gloss[VMAT_COUNT];      // 0 = fosco; 255 = espelha o Sol (água, gelo)
    float   jitter;                 // variação aleatória de brilho por vértice (0 = nenhuma)
} MeshStyle;

// Reserva na arena os arrays com capacidade para o pior caso de um chunk.
bool mesh_data_init(MeshData *m, Arena *arena);

// Preenche `out` com a malha do chunk (cx, cy, cz). Chunks sem superfície
// (totalmente vazios ou totalmente sólidos) saem com zero vértices.
void mesher_build_chunk(const Planet *p, int cx, int cy, int cz, const MeshStyle *style, MeshData *out);

#endif
