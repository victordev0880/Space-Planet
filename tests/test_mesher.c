#include "test.h"
#include "test_util.h"

#include <stdlib.h>

#include "voxel/damage.h"
#include "voxel/mesher.h"
#include "voxel/planet.h"

#define GRID          TEST_GRID
#define SPHERE_RADIUS 18.0f

#define MAX_VERTICES  40000
#define MAX_TRIANGLES 80000
#define TABLE_SIZE    131072   // potência de 2, bem maior que MAX_VERTICES

static Arena    arena;
static Planet   planet;
static MeshData mesh;
static MeshStyle style;

// Malha do planeta inteiro, com os vértices repetidos entre chunks unificados.
static Vector3  vertices[MAX_VERTICES];
static Vector3  normals[MAX_VERTICES];
static int      vertex_count;
static int      triangles[MAX_TRIANGLES][3];
static int      triangle_count;
static int      table[TABLE_SIZE];   // posição -> índice do vértice (+1); 0 = vazio
static int      chunks_with_mesh;
static bool     overflow;

static void setup(void)
{
    arena_reset(&arena);
    PlanetParams pp = test_planet_params(GRID);
    if (!planet_init(&planet, &pp, &arena) || !mesh_data_init(&mesh, &arena)) {
        test_fail(__FILE__, __LINE__, "setup do planeta de teste");
    }
    memset(&style, 0, sizeof style);
    for (int i = 0; i < VMAT_COUNT; i++) style.palette[i] = (Rgb8){ 200, 200, 200 };
}

// Troca o planeta gerado por uma esfera lisa de um único material.
static void fill_sphere(float radius, VoxelMaterial material)
{
    int64_t volume = 0;
    int index = 0;
    for (int z = 0; z < GRID; z++) {
        for (int y = 0; y < GRID; y++) {
            for (int x = 0; x < GRID; x++, index++) {
                Vector3 pos = { (float)x - planet.center, (float)y - planet.center, (float)z - planet.center };
                float value = VOXEL_ISO + (radius - Vector3Length(pos)) * VOXEL_DENSITY_PER_UNIT;
                planet.density[index] = (uint8_t)(Clamp(value, 0.0f, 255.0f) + 0.5f);
                planet.material[index] = (uint8_t)material;
                volume += planet.density[index];
            }
        }
    }
    planet.volume = volume;
}

// Índice único para uma posição: dois chunks que geram o mesmo vértice
// precisam chegar exatamente aos mesmos números.
static int unify_vertex(Vector3 pos, Vector3 normal)
{
    uint32_t bits[3];
    memcpy(bits, &pos, sizeof bits);
    uint32_t h = (bits[0] * 0x9E3779B1u) ^ (bits[1] * 0x85EBCA6Bu) ^ (bits[2] * 0xC2B2AE35u);
    h ^= h >> 15;

    for (uint32_t slot = h & (TABLE_SIZE - 1);; slot = (slot + 1) & (TABLE_SIZE - 1)) {
        if (table[slot] == 0) {
            if (vertex_count >= MAX_VERTICES) { overflow = true; return 0; }
            vertices[vertex_count] = pos;
            normals[vertex_count] = normal;
            table[slot] = ++vertex_count;
            return vertex_count - 1;
        }
        Vector3 other = vertices[table[slot] - 1];
        if (other.x == pos.x && other.y == pos.y && other.z == pos.z) return table[slot] - 1;
    }
}

static void mesh_whole_planet(void)
{
    vertex_count = 0;
    triangle_count = 0;
    chunks_with_mesh = 0;
    overflow = false;
    memset(table, 0, sizeof table);

    int n = planet.chunks_per_axis;
    for (int cz = 0; cz < n; cz++) {
        for (int cy = 0; cy < n; cy++) {
            for (int cx = 0; cx < n; cx++) {
                mesher_build_chunk(&planet, cx, cy, cz, &style, &mesh);
                if (mesh.index_count == 0) continue;
                chunks_with_mesh++;

                CHECK(mesh.index_count % 3 == 0);
                CHECK(mesh.vertex_count <= mesh.vertex_capacity && mesh.index_count <= mesh.index_capacity);

                for (int i = 0; i + 2 < mesh.index_count; i += 3) {
                    if (triangle_count >= MAX_TRIANGLES) { overflow = true; break; }
                    for (int k = 0; k < 3; k++) {
                        int v = mesh.indices[i + k];
                        CHECK(v < mesh.vertex_count);
                        Vector3 pos = { mesh.positions[v * 3], mesh.positions[v * 3 + 1], mesh.positions[v * 3 + 2] };
                        Vector3 nrm = { mesh.normals[v * 3], mesh.normals[v * 3 + 1], mesh.normals[v * 3 + 2] };
                        triangles[triangle_count][k] = unify_vertex(pos, nrm);
                    }
                    triangle_count++;
                }
            }
        }
    }
    CHECK(!overflow);
}

// --- análise de arestas -----------------------------------------------------
// Cada triângulo tem 3 arestas com sentido (a -> b). Em uma superfície fechada
// e bem orientada, toda aresta a -> b tem exatamente uma parceira b -> a, do
// triângulo vizinho.

static uint64_t edge_keys[MAX_TRIANGLES * 3];

static int compare_keys(const void *a, const void *b)
{
    uint64_t ka = *(const uint64_t *)a, kb = *(const uint64_t *)b;
    return (ka > kb) - (ka < kb);
}

static int count_key(uint64_t key, int total)
{
    // Busca binária pela primeira ocorrência, depois conta as repetições.
    int lo = 0, hi = total;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (edge_keys[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    int count = 0;
    while (lo + count < total && edge_keys[lo + count] == key) count++;
    return count;
}

// Devolve quantas arestas não têm o mesmo número de parceiras no sentido
// oposto (buracos) e, em *repeated, quantas aparecem mais de uma vez.
static int count_open_edges(int *repeated)
{
    int total = 0;
    for (int t = 0; t < triangle_count; t++) {
        for (int k = 0; k < 3; k++) {
            uint64_t a = (uint64_t)triangles[t][k], b = (uint64_t)triangles[t][(k + 1) % 3];
            edge_keys[total++] = (a << 32) | b;
        }
    }
    qsort(edge_keys, (size_t)total, sizeof edge_keys[0], compare_keys);

    int open = 0;
    *repeated = 0;
    for (int i = 0; i < total; i++) {
        if (i > 0 && edge_keys[i] == edge_keys[i - 1]) { (*repeated)++; continue; }
        uint64_t reversed = (edge_keys[i] << 32) | (edge_keys[i] >> 32);
        if (count_key(edge_keys[i], total) != count_key(reversed, total)) open++;
    }
    return open;
}

// Volume dentro da malha (soma dos tetraedros entre cada triângulo e a origem).
static double enclosed_volume(void)
{
    double volume = 0.0;
    for (int t = 0; t < triangle_count; t++) {
        Vector3 a = vertices[triangles[t][0]], b = vertices[triangles[t][1]], c = vertices[triangles[t][2]];
        volume += (double)Vector3DotProduct(a, Vector3CrossProduct(b, c)) / 6.0;
    }
    return volume;
}

// ---------------------------------------------------------------------------

static void mesher_sphere_is_closed(void)
{
    setup();
    fill_sphere(SPHERE_RADIUS, VMAT_CRUST);
    mesh_whole_planet();

    CHECK(triangle_count > 1000);
    CHECK(chunks_with_mesh > 8);    // a esfera atravessa vários chunks

    int repeated = 0;
    CHECK(count_open_edges(&repeated) == 0);   // sem buracos nem frestas entre chunks
    CHECK(repeated == 0);                      // nenhum quadrado gerado duas vezes

    // Fórmula de Euler para uma superfície fechada sem furos: V - A + F = 2.
    int edges = triangle_count * 3 / 2;
    CHECK(vertex_count - edges + triangle_count == 2);
}

static void mesher_sphere_has_the_right_size_and_faces_out(void)
{
    setup();
    fill_sphere(SPHERE_RADIUS, VMAT_CRUST);
    mesh_whole_planet();

    double expected = 4.0 / 3.0 * 3.14159265358979 * SPHERE_RADIUS * SPHERE_RADIUS * SPHERE_RADIUS;
    CHECK_NEAR(enclosed_volume() / expected, 1.0, 0.02);

    for (int t = 0; t < triangle_count; t++) {
        Vector3 a = vertices[triangles[t][0]], b = vertices[triangles[t][1]], c = vertices[triangles[t][2]];
        Vector3 face = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        Vector3 middle = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
        CHECK(Vector3DotProduct(face, middle) > 0.0f);   // a frente do triângulo olha para fora
    }

    for (int v = 0; v < vertex_count; v++) {
        CHECK_NEAR(Vector3Length(vertices[v]), SPHERE_RADIUS, 0.25f);
        CHECK_NEAR(Vector3Length(normals[v]), 1.0f, 1e-3);
        CHECK(Vector3DotProduct(normals[v], Vector3Normalize(vertices[v])) > 0.98f);
    }
}

static void mesher_skips_chunks_without_surface(void)
{
    setup();
    fill_sphere(10.0f, VMAT_CRUST);   // pequena o bastante para não alcançar os chunks dos cantos

    mesher_build_chunk(&planet, 0, 0, 0, &style, &mesh);   // canto da grade: só vazio
    CHECK(mesh.vertex_count == 0 && mesh.index_count == 0);

    memset(planet.density, 255, (size_t)GRID * GRID * GRID);
    mesher_build_chunk(&planet, 1, 1, 1, &style, &mesh);   // chunk do meio: só matéria
    CHECK(mesh.vertex_count == 0 && mesh.index_count == 0);
}

static void mesher_colors_come_from_the_palette(void)
{
    setup();
    fill_sphere(SPHERE_RADIUS, VMAT_MANTLE);
    style.palette[VMAT_MANTLE] = (Rgb8){ 10, 20, 30 };
    style.emission[VMAT_MANTLE] = 200;
    style.gloss[VMAT_MANTLE] = 51;   // 0,2 em uma escala de 0 a 255
    style.jitter = 0.0f;

    int checked = 0;
    for (int cz = 0; cz < planet.chunks_per_axis; cz++) {
        mesher_build_chunk(&planet, 1, 1, cz, &style, &mesh);
        for (int v = 0; v < mesh.vertex_count; v++) {
            const uint8_t *c = &mesh.colors[v * 4];
            CHECK(c[0] == 10 && c[1] == 20 && c[2] == 30 && c[3] == 200);
            CHECK_NEAR(mesh.params[v * 2], 0.2f, 1e-4);   // brilho especular do material
            checked++;
        }
    }
    CHECK(checked > 0);

    // Com variação ligada, as cores mudam de vértice para vértice mas ficam por perto.
    style.jitter = 0.2f;
    bool varied = false;
    mesher_build_chunk(&planet, 1, 1, 0, &style, &mesh);
    CHECK(mesh.vertex_count > 1);
    for (int v = 0; v < mesh.vertex_count; v++) {
        const uint8_t *c = &mesh.colors[v * 4];
        CHECK(c[1] >= 15 && c[1] <= 25);
        if (c[1] != mesh.colors[1]) varied = true;
    }
    CHECK(varied);
}

static void mesher_generated_planet_is_watertight(void)
{
    setup();   // planeta com relevo, oceanos e gelo
    mesh_whole_planet();

    CHECK(triangle_count > 1000);
    int repeated = 0;
    CHECK(count_open_edges(&repeated) == 0);

    double radius = planet.params.radius;
    double sphere = 4.0 / 3.0 * 3.14159265358979 * radius * radius * radius;
    double volume = enclosed_volume();
    CHECK(volume > sphere * 0.97 && volume < sphere * 1.12);

    // A malha encerra o volume que o contador de HP enxerga. A soma das
    // densidades inclui a faixa em que a densidade cai aos poucos, de meia
    // largura w em volta da superfície; em uma esfera de raio r essa faixa
    // tem mais volume do lado de fora que do lado de dentro, e a soma passa
    // do volume real por um fator (1 + w^2 / r^2).
    double w = VOXEL_ISO / VOXEL_DENSITY_PER_UNIT;
    double band_factor = 1.0 + (w * w) / (radius * radius);
    CHECK_NEAR(volume * band_factor / ((double)planet.volume / 255.0), 1.0, 0.025);
}

// Depois de crateras de todo tipo (rasas, fundas, sobrepostas, atravessando
// divisas de chunk), a malha continua sem buracos.
static void mesher_cratered_planet_is_still_watertight(void)
{
    setup();
    float r = planet.params.radius;
    planet_carve(&planet, (Vector3){ 0.0f, r, 0.0f }, 5.0f);                 // cratera na superfície
    planet_carve(&planet, (Vector3){ 0.0f, r - 3.0f, 2.0f }, 4.0f);          // outra, sobreposta
    planet_carve(&planet, (Vector3){ r * 0.7f, r * 0.7f, 0.0f }, 3.0f);      // em diagonal
    planet_carve(&planet, (Vector3){ -r, 0.0f, 0.0f }, 2.0f);                // pequena
    for (int i = 0; i < 6; i++) {                                            // túnel até o centro
        planet_carve(&planet, (Vector3){ 0.0f, 0.0f, r - (float)i * 3.0f }, 2.5f);
    }
    mesh_whole_planet();

    CHECK(triangle_count > 1000);
    int repeated = 0;
    CHECK(count_open_edges(&repeated) == 0);

    // A malha encerra menos volume que antes, e ainda bate com o contador.
    double sphere = 4.0 / 3.0 * 3.14159265358979 * (double)r * (double)r * (double)r;
    double volume = enclosed_volume();
    CHECK(volume < sphere * 1.05);
    CHECK(planet.volume < planet.initial_volume);
}

static void mesher_same_chunk_twice_gives_the_same_mesh(void)
{
    static float first[MESHER_MAX_VERTICES * 3];
    setup();
    mesher_build_chunk(&planet, 2, 1, 1, &style, &mesh);
    int vertices_first = mesh.vertex_count, indices_first = mesh.index_count;
    CHECK(vertices_first > 0);
    memcpy(first, mesh.positions, sizeof(float) * 3 * (size_t)vertices_first);

    mesher_build_chunk(&planet, 0, 0, 0, &style, &mesh);   // suja a memória de trabalho
    mesher_build_chunk(&planet, 2, 1, 1, &style, &mesh);
    CHECK(mesh.vertex_count == vertices_first && mesh.index_count == indices_first);
    CHECK(memcmp(first, mesh.positions, sizeof(float) * 3 * (size_t)vertices_first) == 0);
}

void suite_mesher(void)
{
    CHECK(arena_init(&arena, 4u * 1024u * 1024u));
    RUN(mesher_sphere_is_closed);
    RUN(mesher_sphere_has_the_right_size_and_faces_out);
    RUN(mesher_skips_chunks_without_surface);
    RUN(mesher_colors_come_from_the_palette);
    RUN(mesher_generated_planet_is_watertight);
    RUN(mesher_cratered_planet_is_still_watertight);
    RUN(mesher_same_chunk_twice_gives_the_same_mesh);
    arena_free(&arena);
}
