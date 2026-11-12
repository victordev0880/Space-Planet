#include "voxel/mesher.h"

#include <math.h>

// Bloco local de trabalho: uma cópia das densidades do chunk e de duas
// camadas de pontos em volta dele. A primeira camada serve às células
// vizinhas de que o chunk precisa; a segunda, ao cálculo das normais, que
// compara cada ponto com os seus vizinhos. Trabalhar na cópia é bem mais
// rápido que consultar a grade do planeta ponto a ponto.
#define BLOCK_PAD 2
#define S_MAX (VOXEL_MAX_CHUNK + 2 * BLOCK_PAD)   // pontos da grade por eixo
#define K_MAX (VOXEL_MAX_CHUNK + 1)               // células por eixo

#define NO_VERTEX   (-1)   // a superfície não passa por esta célula
#define NOT_EMITTED (-2)   // tem vértice, mas ele ainda não foi para a saída

// A cor de um vértice é lida a esta profundidade (em voxels) abaixo da
// superfície, para pegar o material do chão e não o vazio logo acima.
#define COLOR_SAMPLE_DEPTH 0.6f

// Profundidade abaixo do nível do mar, em voxels, em que o interior de uma
// cratera chega ao escurecimento máximo. Quanto mais fundo, menos luz do céu
// alcança o lugar; sem isso uma cratera vista de cima parece uma bola.
#define CAVITY_FULL_DEPTH 14.0f

// Memória de trabalho, reutilizada a cada chunk (por isso é estática).
static uint8_t block_density[S_MAX * S_MAX * S_MAX];
static uint8_t block_material[S_MAX * S_MAX * S_MAX];
static float   cell_offset[K_MAX * K_MAX * K_MAX][3];   // posição do vértice dentro da célula, de 0 a 1
static int16_t cell_vertex[K_MAX * K_MAX * K_MAX];

// As 12 arestas de um cubo, como pares de cantos. O canto c fica em
// (c & 1, (c >> 1) & 1, (c >> 2) & 1).
static const int EDGES[12][2] = {
    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },   // paralelas a X
    { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },   // paralelas a Y
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },   // paralelas a Z
};

// Estado de uma chamada de mesher_build_chunk, para não repassar dez
// argumentos a cada função auxiliar.
typedef struct {
    const Planet    *planet;
    const MeshStyle *style;
    MeshData        *out;
    int size_s;       // pontos por eixo no bloco local
    int size_k;       // células por eixo no bloco local
    int base[3];      // ponto global da célula (0, 0, 0): um antes do chunk
} Build;

bool mesh_data_init(MeshData *m, Arena *arena)
{
    *m = (MeshData){ 0 };
    m->positions = ARENA_ARRAY(arena, float, MESHER_MAX_VERTICES * 3);
    m->normals = ARENA_ARRAY(arena, float, MESHER_MAX_VERTICES * 3);
    m->colors = ARENA_ARRAY(arena, uint8_t, MESHER_MAX_VERTICES * 4);
    m->params = ARENA_ARRAY(arena, float, MESHER_MAX_VERTICES * 2);
    m->indices = ARENA_ARRAY(arena, uint16_t, MESHER_MAX_INDICES);
    if (m->positions == NULL || m->normals == NULL || m->colors == NULL || m->params == NULL
        || m->indices == NULL) {
        return false;
    }
    m->vertex_capacity = MESHER_MAX_VERTICES;
    m->index_capacity = MESHER_MAX_INDICES;
    return true;
}

// Posição, no bloco local, do ponto da grade de coordenadas (x, y, z). As
// coordenadas são as das células: a célula (k) fica entre os pontos (k) e
// (k + 1). Valem de -1 a chunk + 2 em cada eixo.
static int sample_index(const Build *b, int x, int y, int z)
{
    return ((z + 1) * b->size_s + (y + 1)) * b->size_s + (x + 1);
}

// Gradiente da densidade em um ponto da grade, por diferença central.
static void corner_gradient(const Build *b, int x, int y, int z, float out[3])
{
    out[0] = (float)block_density[sample_index(b, x + 1, y, z)] - (float)block_density[sample_index(b, x - 1, y, z)];
    out[1] = (float)block_density[sample_index(b, x, y + 1, z)] - (float)block_density[sample_index(b, x, y - 1, z)];
    out[2] = (float)block_density[sample_index(b, x, y, z + 1)] - (float)block_density[sample_index(b, x, y, z - 1)];
}

static int cell_index(const Build *b, int x, int y, int z)
{
    return (z * b->size_k + y) * b->size_k + x;
}

// Número pseudoaleatório em [-1, 1] a partir de coordenadas inteiras: dá a
// cada vértice uma variação de brilho fixa, igual em qualquer chunk.
static float cell_noise(int x, int y, int z)
{
    uint32_t h = ((uint32_t)x * 0x8DA6B343u) ^ ((uint32_t)y * 0xD8163841u) ^ ((uint32_t)z * 0xCB1AB31Fu);
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFu) / 32767.5f - 1.0f;
}

// Passo 1 do algoritmo: decide se a célula tem vértice e onde ele fica.
static void place_cell_vertex(const Build *b, int kx, int ky, int kz)
{
    float density[8];
    int solid_mask = 0;
    for (int c = 0; c < 8; c++) {
        int d = block_density[sample_index(b, kx + (c & 1), ky + ((c >> 1) & 1), kz + ((c >> 2) & 1))];
        density[c] = (float)d;
        if (d >= VOXEL_SOLID) solid_mask |= 1 << c;
    }

    int cell = cell_index(b, kx, ky, kz);
    if (solid_mask == 0 || solid_mask == 0xFF) {
        cell_vertex[cell] = NO_VERTEX;
        return;
    }

    // Média dos pontos em que a superfície cruza as arestas da célula.
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    int crossings = 0;
    for (int e = 0; e < 12; e++) {
        int a = EDGES[e][0], c = EDGES[e][1];
        bool solid_a = ((solid_mask >> a) & 1) != 0;
        bool solid_c = ((solid_mask >> c) & 1) != 0;
        if (solid_a == solid_c) continue;

        // Fração do caminho de `a` até `c` em que a densidade vale VOXEL_ISO.
        float t = (VOXEL_ISO - density[a]) / (density[c] - density[a]);
        for (int axis = 0; axis < 3; axis++) {
            float pa = (float)((a >> axis) & 1);
            float pc = (float)((c >> axis) & 1);
            sum[axis] += pa + (pc - pa) * t;
        }
        crossings++;
    }

    for (int axis = 0; axis < 3; axis++) cell_offset[cell][axis] = sum[axis] / (float)crossings;
    cell_vertex[cell] = NOT_EMITTED;
}

// Devolve o índice do vértice da célula na malha de saída, criando-o na
// primeira vez em que é pedido. Assim só entram na malha os vértices que
// algum quadrado deste chunk usa.
static int vertex_of_cell(const Build *b, int kx, int ky, int kz)
{
    int cell = cell_index(b, kx, ky, kz);
    if (cell_vertex[cell] >= 0) return cell_vertex[cell];

    MeshData *out = b->out;
    if (cell_vertex[cell] == NO_VERTEX || out->vertex_count >= out->vertex_capacity) return -1;

    int v = out->vertex_count++;
    cell_vertex[cell] = (int16_t)v;

    // Posição: célula global + deslocamento dentro dela. Usar a coordenada
    // global faz chunks vizinhos chegarem exatamente ao mesmo número.
    const Planet *p = b->planet;
    int gx = b->base[0] + kx, gy = b->base[1] + ky, gz = b->base[2] + kz;
    Vector3 pos = {
        (float)gx + cell_offset[cell][0] - p->center,
        (float)gy + cell_offset[cell][1] - p->center,
        (float)gz + cell_offset[cell][2] - p->center,
    };
    out->positions[v * 3 + 0] = pos.x;
    out->positions[v * 3 + 1] = pos.y;
    out->positions[v * 3 + 2] = pos.z;

    // Normal: o gradiente da densidade aponta para dentro da matéria; a normal
    // é o oposto. O gradiente no vértice é a mistura dos gradientes nos 8
    // cantos da célula, com peso pela posição do vértice dentro dela. Dá o
    // mesmo resultado que planet_gradient(), só que lendo a cópia local.
    float ox = cell_offset[cell][0], oy = cell_offset[cell][1], oz = cell_offset[cell][2];
    Vector3 gradient = { 0.0f, 0.0f, 0.0f };
    for (int c = 0; c < 8; c++) {
        int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        float weight = (dx ? ox : 1.0f - ox) * (dy ? oy : 1.0f - oy) * (dz ? oz : 1.0f - oz);
        float g[3];
        corner_gradient(b, kx + dx, ky + dy, kz + dz, g);
        gradient.x += weight * g[0];
        gradient.y += weight * g[1];
        gradient.z += weight * g[2];
    }
    float length = Vector3Length(gradient);
    Vector3 normal = (length > 1e-4f) ? Vector3Scale(gradient, -1.0f / length) : Vector3Normalize(pos);
    out->normals[v * 3 + 0] = normal.x;
    out->normals[v * 3 + 1] = normal.y;
    out->normals[v * 3 + 2] = normal.z;

    // Cor: o material que está um pouco abaixo da superfície, neste ponto.
    // Em vez de olhar um único voxel, mistura os 8 em volta do ponto, com o
    // mesmo peso suave usado para interpolar a densidade. O resultado muda
    // aos poucos ao longo da superfície (na costa, areia e água se misturam
    // em vez de fazer um degrau) e não depende de como a superfície está
    // alinhada com a grade, o que evitaria faixas e anéis de cor.
    Vector3 probe = Vector3Subtract(pos, Vector3Scale(normal, COLOR_SAMPLE_DEPTH));
    float px = probe.x + p->center, py = probe.y + p->center, pz = probe.z + p->center;
    float fx = floorf(px), fy = floorf(py), fz = floorf(pz);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = px - fx, ty = py - fy, tz = pz - fz;

    float r = 0.0f, g = 0.0f, bl = 0.0f, emission = 0.0f, gloss = 0.0f, total = 0.0f;
    for (int c = 0; c < 8; c++) {
        int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        int material = planet_material(p, ix + dx, iy + dy, iz + dz);
        if (material == VMAT_NONE || material >= VMAT_COUNT) continue;   // vazio não tem cor

        float weight = (dx ? tx : 1.0f - tx) * (dy ? ty : 1.0f - ty) * (dz ? tz : 1.0f - tz);
        r += weight * b->style->palette[material].r;
        g += weight * b->style->palette[material].g;
        bl += weight * b->style->palette[material].b;
        emission += weight * b->style->emission[material];
        gloss += weight * b->style->gloss[material];
        total += weight;
    }

    if (total < 1e-4f) {
        // Nenhum voxel com material em volta do ponto de amostra (superfície
        // muito fina): usa o primeiro canto sólido da própria célula.
        for (int c = 0; c < 8; c++) {
            int si = sample_index(b, kx + (c & 1), ky + ((c >> 1) & 1), kz + ((c >> 2) & 1));
            if (block_density[si] < VOXEL_SOLID) continue;
            int material = block_material[si];
            if (material >= VMAT_COUNT) material = VMAT_NONE;
            r = b->style->palette[material].r;
            g = b->style->palette[material].g;
            bl = b->style->palette[material].b;
            emission = b->style->emission[material];
            gloss = b->style->gloss[material];
            total = 1.0f;
            break;
        }
    }

    float scale = (1.0f + b->style->jitter * cell_noise(gx, gy, gz)) / total;
    out->colors[v * 4 + 0] = (uint8_t)Clamp(r * scale + 0.5f, 0.0f, 255.0f);
    out->colors[v * 4 + 1] = (uint8_t)Clamp(g * scale + 0.5f, 0.0f, 255.0f);
    out->colors[v * 4 + 2] = (uint8_t)Clamp(bl * scale + 0.5f, 0.0f, 255.0f);
    out->colors[v * 4 + 3] = (uint8_t)Clamp(emission / total + 0.5f, 0.0f, 255.0f);
    out->params[v * 2 + 0] = Clamp(gloss / (total * 255.0f), 0.0f, 1.0f);

    // Quão fundo o vértice está em relação ao nível do mar, de 0 a 1.
    float depth = p->params.radius - Vector3Length(pos);
    out->params[v * 2 + 1] = Clamp(depth / CAVITY_FULL_DEPTH, 0.0f, 1.0f);
    return v;
}

static float distance_sq(const MeshData *m, int a, int c)
{
    float dx = m->positions[a * 3 + 0] - m->positions[c * 3 + 0];
    float dy = m->positions[a * 3 + 1] - m->positions[c * 3 + 1];
    float dz = m->positions[a * 3 + 2] - m->positions[c * 3 + 2];
    return dx * dx + dy * dy + dz * dz;
}

// Passo 2: um quadrado ligando as 4 células em volta de uma aresta. As
// células chegam em sentido anti-horário para quem olha no sentido positivo
// do eixo da aresta vindo de fora; `flip` inverte quando a matéria está do
// outro lado.
static void emit_quad(const Build *b, const int cells[4][3], bool flip)
{
    MeshData *out = b->out;
    if (out->index_count + 6 > out->index_capacity) return;

    int v[4];
    for (int i = 0; i < 4; i++) {
        const int *cell = cells[flip ? 3 - i : i];
        v[i] = vertex_of_cell(b, cell[0], cell[1], cell[2]);
        if (v[i] < 0) return;
    }

    // Divide o quadrado em dois triângulos pela diagonal mais curta, o que
    // evita triângulos muito finos.
    uint16_t *idx = &out->indices[out->index_count];
    if (distance_sq(out, v[0], v[2]) <= distance_sq(out, v[1], v[3])) {
        idx[0] = (uint16_t)v[0]; idx[1] = (uint16_t)v[1]; idx[2] = (uint16_t)v[2];
        idx[3] = (uint16_t)v[0]; idx[4] = (uint16_t)v[2]; idx[5] = (uint16_t)v[3];
    } else {
        idx[0] = (uint16_t)v[0]; idx[1] = (uint16_t)v[1]; idx[2] = (uint16_t)v[3];
        idx[3] = (uint16_t)v[1]; idx[4] = (uint16_t)v[2]; idx[5] = (uint16_t)v[3];
    }
    out->index_count += 6;
}

void mesher_build_chunk(const Planet *p, int cx, int cy, int cz, const MeshStyle *style, MeshData *out)
{
    out->vertex_count = 0;
    out->index_count = 0;

    const int chunk = p->chunk_size;
    Build b;
    b.planet = p;
    b.style = style;
    b.out = out;
    b.size_s = chunk + 2 * BLOCK_PAD;
    b.size_k = chunk + 1;
    b.base[0] = cx * chunk - 1;
    b.base[1] = cy * chunk - 1;
    b.base[2] = cz * chunk - 1;

    // Copia as densidades do chunk (e das camadas em volta) para o bloco
    // local. Em coordenadas de célula, o bloco vai de -1 a chunk + 2.
    const int first = -1, last = chunk + 2;
    int lowest = 255, highest = 0;
    for (int z = first; z <= last; z++) {
        for (int y = first; y <= last; y++) {
            for (int x = first; x <= last; x++) {
                int d = planet_density(p, b.base[0] + x, b.base[1] + y, b.base[2] + z);
                block_density[sample_index(&b, x, y, z)] = (uint8_t)d;
                if (d < lowest) lowest = d;
                if (d > highest) highest = d;
            }
        }
    }

    // Tudo vazio ou tudo sólido: não há superfície, não há malha.
    if (highest < VOXEL_SOLID || lowest >= VOXEL_SOLID) return;

    // Os materiais só são consultados nos cantos das células (de 0 a chunk + 1).
    for (int z = 0; z <= chunk + 1; z++) {
        for (int y = 0; y <= chunk + 1; y++) {
            for (int x = 0; x <= chunk + 1; x++) {
                block_material[sample_index(&b, x, y, z)] =
                    planet_material(p, b.base[0] + x, b.base[1] + y, b.base[2] + z);
            }
        }
    }

    for (int z = 0; z < b.size_k; z++) {
        for (int y = 0; y < b.size_k; y++) {
            for (int x = 0; x < b.size_k; x++) place_cell_vertex(&b, x, y, z);
        }
    }

    // Arestas que pertencem a este chunk: as que saem dos pontos locais 1..chunk
    // no sentido positivo de cada eixo. Cada aresta da grade pertence a um
    // único chunk, então nenhum quadrado é gerado duas vezes.
    for (int z = 1; z <= chunk; z++) {
        for (int y = 1; y <= chunk; y++) {
            for (int x = 1; x <= chunk; x++) {
                bool solid = block_density[sample_index(&b, x, y, z)] >= VOXEL_SOLID;

                if ((block_density[sample_index(&b, x + 1, y, z)] >= VOXEL_SOLID) != solid) {
                    const int cells[4][3] = {
                        { x, y - 1, z - 1 }, { x, y, z - 1 }, { x, y, z }, { x, y - 1, z },
                    };
                    emit_quad(&b, cells, !solid);
                }
                if ((block_density[sample_index(&b, x, y + 1, z)] >= VOXEL_SOLID) != solid) {
                    const int cells[4][3] = {
                        { x - 1, y, z - 1 }, { x - 1, y, z }, { x, y, z }, { x, y, z - 1 },
                    };
                    emit_quad(&b, cells, !solid);
                }
                if ((block_density[sample_index(&b, x, y, z + 1)] >= VOXEL_SOLID) != solid) {
                    const int cells[4][3] = {
                        { x - 1, y - 1, z }, { x, y - 1, z }, { x, y, z }, { x - 1, y, z },
                    };
                    emit_quad(&b, cells, !solid);
                }
            }
        }
    }
}
