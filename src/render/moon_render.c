#include "raylib.h"

#include "render/moon_render.h"

#include <math.h>
#include <string.h>

#include "core/noise.h"
#include "core/rng.h"
#include "core/vmath.h"

// A malha é uma esfera de raio 1 em "paralelos e meridianos".
#define RINGS    96    // faixas de polo a polo
#define SEGMENTS 192   // divisões em volta

#define MAX_CRATERS 400

// --- relevo (tudo em frações do raio da Lua) --------------------------------
#define CRATER_SIZE_MIN   0.05f    // raio da menor cratera, em radianos sobre a esfera
#define CRATER_SIZE_MAX   0.30f
#define CRATER_SIZE_POWER 2.6f     // maior = crateras grandes mais raras
#define CRATER_DEPTH      0.16f    // profundidade, em raios da própria cratera
#define CRATER_RIM        0.30f    // altura da borda, em frações da profundidade
#define CRATER_RIM_WIDTH  0.22f    // largura da borda, em raios da cratera
#define CRATER_REACH      1.7f     // além desta distância (em raios dela) a cratera não muda nada
#define GRAIN_HEIGHT      0.006f   // altura do grão fino
#define GRAIN_FREQ        9.0f
#define BUMP_STEP         0.012f   // passo usado para medir a inclinação do relevo
#define BUMP_STRENGTH     1.0f     // quanto o relevo inclina a normal (1 = fiel ao relevo)

// --- aparência --------------------------------------------------------------
#define MARIA_FREQ      1.25f   // tamanho dos "mares" escuros
#define MARIA_DARKNESS  0.38f
#define ALBEDO_VARIATION 0.14f  // manchas claras e escuras no solo
#define FLOOR_DARKNESS  0.22f   // o fundo das crateras é um pouco mais escuro
#define EARTHSHINE      0.035f  // luz mínima no lado escuro
#define ECLIPSE_LIGHT   0.10f   // quanto de luz sobra na sombra do planeta

typedef struct {
    Vector3 dir;      // centro, como direção a partir do centro da Lua
    float   size;     // raio, em radianos
} Crater;

typedef struct {
    Crater   craters[MAX_CRATERS];
    int      count;
    uint32_t seed;
} Relief;

static float smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Altura do relevo na direção `dir`, em raios da Lua. `floor` recebe o quanto
// o ponto está no fundo de alguma cratera (0 a 1), para escurecer o solo ali.
static float relief_height(const Relief *r, Vector3 dir, float *floor)
{
    float height = GRAIN_HEIGHT * noise_fbm3(dir.x * GRAIN_FREQ, dir.y * GRAIN_FREQ, dir.z * GRAIN_FREQ, 3, 2.0f,
                                             0.5f, r->seed);
    float deepest = 0.0f;

    for (int i = 0; i < r->count; i++) {
        const Crater *c = &r->craters[i];
        float cosine = Vector3DotProduct(dir, c->dir);
        // Descarte rápido: longe demais para esta cratera importar.
        if (cosine < cosf(c->size * CRATER_REACH)) continue;

        float x = acosf(Clamp(cosine, -1.0f, 1.0f)) / c->size;   // 0 no centro, 1 na borda
        float depth = c->size * CRATER_DEPTH;
        // Tigela por dentro, e uma borda levantada em volta.
        float bowl = (x < 1.0f) ? -(1.0f - x * x) : 0.0f;
        float rim = CRATER_RIM * expf(-((x - 1.0f) * (x - 1.0f)) / (CRATER_RIM_WIDTH * CRATER_RIM_WIDTH));
        height += depth * (bowl + rim);
        if (-bowl > deepest) deepest = -bowl;
    }

    if (floor != NULL) *floor = deepest;
    return height;
}

static void make_relief(Relief *r, const Tuning *t)
{
    Rng rng;
    rng_seed(&rng, (uint64_t)t->moon_seed, 3u);
    r->seed = (uint32_t)t->moon_seed;
    r->count = (t->moon_craters < MAX_CRATERS) ? t->moon_craters : MAX_CRATERS;
    for (int i = 0; i < r->count; i++) {
        r->craters[i].dir = rng_unit_vec3(&rng);
        float pick = powf(rng_float(&rng), CRATER_SIZE_POWER);   // quase sempre perto de zero
        r->craters[i].size = Lerp(CRATER_SIZE_MIN, CRATER_SIZE_MAX, pick);
    }
}

void moon_render_build(MoonRender *m, const Tuning *t)
{
    moon_render_unload(m);

    // static: a lista de crateras é grande demais para a pilha.
    static Relief relief;
    make_relief(&relief, t);

    int vertex_count = (RINGS + 1) * (SEGMENTS + 1);
    int index_count = RINGS * SEGMENTS * 6;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertex_count;
    mesh.triangleCount = index_count / 3;
    mesh.vertices = MemAlloc((unsigned int)(vertex_count * 3) * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)(vertex_count * 4));
    mesh.indices = MemAlloc((unsigned int)index_count * sizeof(unsigned short));

    Vector3 sun = tuning_sun_direction(t);
    uint32_t seed = (uint32_t)t->moon_seed;

    int v = 0;
    for (int ring = 0; ring <= RINGS; ring++) {
        float polar = PI * (float)ring / RINGS;   // 0 no polo norte, PI no polo sul
        for (int seg = 0; seg <= SEGMENTS; seg++) {
            float around = 2.0f * PI * (float)seg / SEGMENTS;
            Vector3 dir = { sinf(polar) * cosf(around), cosf(polar), sinf(polar) * sinf(around) };

            float floor = 0.0f;
            float height = relief_height(&relief, dir, &floor);

            // Normal: mede o relevo em dois pontos vizinhos, um para cada
            // lado, e inclina a normal da esfera contra a subida.
            Vector3 hint = (fabsf(dir.y) > 0.9f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
            Vector3 east = Vector3Normalize(Vector3CrossProduct(hint, dir));
            Vector3 north = Vector3CrossProduct(dir, east);
            Vector3 dir_east = Vector3Normalize(Vector3Add(dir, Vector3Scale(east, BUMP_STEP)));
            Vector3 dir_north = Vector3Normalize(Vector3Add(dir, Vector3Scale(north, BUMP_STEP)));
            float slope_east = (relief_height(&relief, dir_east, NULL) - height) / BUMP_STEP;
            float slope_north = (relief_height(&relief, dir_north, NULL) - height) / BUMP_STEP;
            Vector3 normal = Vector3Subtract(dir, Vector3Add(Vector3Scale(east, slope_east * BUMP_STRENGTH),
                                                             Vector3Scale(north, slope_north * BUMP_STRENGTH)));
            normal = Vector3Normalize(normal);

            // Cor do solo: manchas, "mares" e o fundo escuro das crateras.
            float patches = noise_fbm3(dir.x * 4.0f, dir.y * 4.0f, dir.z * 4.0f, 4, 2.0f, 0.5f, seed + 11u);
            float maria = smoothstep(0.05f, 0.30f, noise_fbm3(dir.x * MARIA_FREQ, dir.y * MARIA_FREQ,
                                                              dir.z * MARIA_FREQ, 3, 2.0f, 0.5f, seed + 23u));
            float albedo = (1.0f + ALBEDO_VARIATION * patches) * (1.0f - MARIA_DARKNESS * maria)
                         * (1.0f - FLOOR_DARKNESS * floor);

            // Luz do Sol, já embutida.
            float light = EARTHSHINE + fmaxf(Vector3DotProduct(normal, sun), 0.0f);
            float shade = albedo * light;

            Vector3 pos = Vector3Scale(dir, 1.0f + height);
            mesh.vertices[v * 3 + 0] = pos.x;
            mesh.vertices[v * 3 + 1] = pos.y;
            mesh.vertices[v * 3 + 2] = pos.z;
            mesh.colors[v * 4 + 0] = (unsigned char)Clamp((float)t->moon_color.r * shade, 0.0f, 255.0f);
            mesh.colors[v * 4 + 1] = (unsigned char)Clamp((float)t->moon_color.g * shade, 0.0f, 255.0f);
            mesh.colors[v * 4 + 2] = (unsigned char)Clamp((float)t->moon_color.b * shade, 0.0f, 255.0f);
            mesh.colors[v * 4 + 3] = 255;
            v++;
        }
    }

    int i = 0;
    for (int ring = 0; ring < RINGS; ring++) {
        for (int seg = 0; seg < SEGMENTS; seg++) {
            int a = ring * (SEGMENTS + 1) + seg;   // este paralelo
            int b = a + SEGMENTS + 1;              // o paralelo de baixo
            // Sentido anti-horário visto de fora.
            mesh.indices[i++] = (unsigned short)a;
            mesh.indices[i++] = (unsigned short)(a + 1);
            mesh.indices[i++] = (unsigned short)b;
            mesh.indices[i++] = (unsigned short)(a + 1);
            mesh.indices[i++] = (unsigned short)(b + 1);
            mesh.indices[i++] = (unsigned short)b;
        }
    }

    UploadMesh(&mesh, false);
    m->mesh = mesh;
    m->material = LoadMaterialDefault();
    m->has_mesh = true;
}

void moon_render_draw(const MoonRender *m, const Moon *moon, const Tuning *t, float planet_radius, float alpha)
{
    if (!m->has_mesh) return;

    Vector3 pos = Vector3Lerp(moon->pos_prev, moon->pos, alpha);

    // Eclipse: a Lua escurece ao passar pela sombra do planeta, um "cilindro"
    // do lado oposto ao Sol. A borda da sombra é suave, do tamanho da Lua.
    Vector3 sun = tuning_sun_direction(t);
    float along = Vector3DotProduct(pos, sun);
    float off_axis = Vector3Length(Vector3Subtract(pos, Vector3Scale(sun, along)));
    float in_shadow = (along < 0.0f) ? 1.0f - smoothstep(planet_radius - moon->radius, planet_radius + moon->radius,
                                                         off_axis)
                                     : 0.0f;
    unsigned char tint = (unsigned char)(255.0f * Lerp(1.0f, ECLIPSE_LIGHT, in_shadow));

    Material material = m->material;
    material.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ tint, tint, tint, 255 };

    Matrix transform = MatrixMultiply(MatrixScale(moon->radius, moon->radius, moon->radius),
                                      MatrixTranslate(pos.x, pos.y, pos.z));
    DrawMesh(m->mesh, material, transform);
}

void moon_render_unload(MoonRender *m)
{
    if (m->has_mesh) {
        UnloadMesh(m->mesh);
        UnloadMaterial(m->material);
    }
    memset(m, 0, sizeof *m);
}
