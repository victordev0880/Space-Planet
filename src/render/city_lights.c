#include "raylib.h"
#include <stddef.h>
#include "rlgl.h"

#include "render/city_lights.h"

#include <math.h>

#include "core/noise.h"
#include "core/rng.h"
#include "core/vmath.h"

// 4 vértices por luz e índices de 16 bits.
#define LIGHTS_MAX 16000

// Sorteios de centros de cidade para cada luz pedida, antes de desistir
// (protege contra um planeta sem terra habitável).
#define ATTEMPTS_PER_LIGHT 80

// Faixa do terminador em que as luzes se acendem: de totalmente apagadas
// (ainda entardecer) a totalmente acesas (noite fechada), medido pelo cosseno
// do ângulo com o Sol.
#define DUSK_START 0.08f
#define DUSK_END   (-0.14f)

#define SEED_REGIONS 4051u

#define SURFACE_SEARCH_STEP 0.25f

// Ao conferir um impacto: olha as luzes a até IMPACT_REACH raios da cratera
// (mais uma folga), e considera destruída a que perdeu o solo a esta
// profundidade abaixo dela.
#define IMPACT_REACH       2.5f
#define IMPACT_MARGIN      6.0f
#define GROUND_PROBE_DEPTH 0.6f

static bool habitable(const Planet *p, Vector3 dir)
{
    Terrain t = planet_terrain(&p->params, dir);
    if (!t.land) return false;
    return t.surface == VMAT_SAND || t.surface == VMAT_GRASS || t.surface == VMAT_FOREST
        || t.surface == VMAT_DESERT;
}

// Raio em que o solo de verdade (o campo de densidade) começa nesta direção.
static float surface_radius(const Planet *p, Vector3 dir)
{
    float previous = 0.0f;
    float top = p->params.radius + p->params.land_height + 2.0f;
    for (float r = top; r > 0.0f; r -= SURFACE_SEARCH_STEP) {
        float d = planet_sample(p, Vector3Scale(dir, r));
        if (d >= VOXEL_ISO) {
            float t = (d - VOXEL_ISO) / (d - previous + 1e-6f);
            return r + SURFACE_SEARCH_STEP * t;
        }
        previous = d;
    }
    return 0.0f;
}

static float smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Acrescenta uma luz à malha. Retorna false se ela não deve existir (é dia
// ali, ou não há solo).
static bool add_light(Mesh *mesh, float *grounds, int index, const Planet *p, const Tuning *t, Vector3 sun,
                      Vector3 dir, Rng *rng)
{
    float night = smoothstep(DUSK_START, DUSK_END, Vector3DotProduct(dir, sun));
    if (night <= 0.0f) return false;

    float ground = surface_radius(p, dir);
    if (ground <= 0.0f) return false;

    grounds[index * 3 + 0] = dir.x * ground;
    grounds[index * 3 + 1] = dir.y * ground;
    grounds[index * 3 + 2] = dir.z * ground;

    Vector3 center = Vector3Scale(dir, ground + t->city_lift);
    float half = 0.5f * t->city_size * rng_range(rng, 0.6f, 1.4f);
    float brightness = night * t->city_brightness * rng_range(rng, 0.45f, 1.0f);

    // Tom: da cor base até quase branco.
    float whiten = rng_float(rng) * 0.5f;
    Color color = {
        (unsigned char)Lerp((float)t->city_color.r, 255.0f, whiten),
        (unsigned char)Lerp((float)t->city_color.g, 255.0f, whiten),
        (unsigned char)Lerp((float)t->city_color.b, 255.0f, whiten),
        (unsigned char)(Clamp(brightness, 0.0f, 1.0f) * 255.0f + 0.5f),
    };

    Vector3 hint = (fabsf(dir.y) > 0.99f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, hint));
    Vector3 up = Vector3CrossProduct(right, dir);

    static const float CORNERS[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    static const float UVS[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };

    for (int corner = 0; corner < 4; corner++) {
        int v = index * 4 + corner;
        Vector3 pos = center;
        pos = Vector3Add(pos, Vector3Scale(right, CORNERS[corner][0] * half));
        pos = Vector3Add(pos, Vector3Scale(up, CORNERS[corner][1] * half));
        mesh->vertices[v * 3 + 0] = pos.x;
        mesh->vertices[v * 3 + 1] = pos.y;
        mesh->vertices[v * 3 + 2] = pos.z;
        mesh->texcoords[v * 2 + 0] = UVS[corner][0];
        mesh->texcoords[v * 2 + 1] = UVS[corner][1];
        mesh->colors[v * 4 + 0] = color.r;
        mesh->colors[v * 4 + 1] = color.g;
        mesh->colors[v * 4 + 2] = color.b;
        mesh->colors[v * 4 + 3] = color.a;
    }

    unsigned short base = (unsigned short)(index * 4);
    unsigned short *idx = &mesh->indices[index * 6];
    idx[0] = base;
    idx[1] = (unsigned short)(base + 1);
    idx[2] = (unsigned short)(base + 2);
    idx[3] = base;
    idx[4] = (unsigned short)(base + 2);
    idx[5] = (unsigned short)(base + 3);
    return true;
}

void city_lights_build(CityLights *c, const Planet *p, const Tuning *t, Texture2D glow)
{
    if (!c->has_material) {
        c->material = LoadMaterialDefault();
        c->has_material = true;
    }
    c->material.maps[MATERIAL_MAP_DIFFUSE].texture = glow;

    if (c->has_mesh) {
        UnloadMesh(c->mesh);
        c->has_mesh = false;
    }
    if (c->ground != NULL) {
        MemFree(c->ground);
        c->ground = NULL;
    }
    if (c->alpha != NULL) {
        MemFree(c->alpha);
        c->alpha = NULL;
    }
    c->count = 0;
    c->lit = 0;

    int target = t->city_count;
    if (target > LIGHTS_MAX) target = LIGHTS_MAX;
    if (target <= 0) return;

    // Criada ao iniciar ou no F5, nunca durante a simulação.
    float *grounds = MemAlloc((unsigned int)(target * 3) * sizeof(float));
    Mesh mesh = { 0 };
    mesh.vertices = MemAlloc((unsigned int)(target * 4 * 3) * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)(target * 4 * 2) * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)(target * 4 * 4));
    mesh.indices = MemAlloc((unsigned int)(target * 6) * sizeof(unsigned short));

    Vector3 sun = tuning_sun_direction(t);
    Rng rng;
    rng_seed(&rng, (uint64_t)t->city_seed, 3u);

    int made = 0;
    int attempts_left = target * ATTEMPTS_PER_LIGHT;
    float f = t->city_region_freq;

    while (made < target && attempts_left-- > 0) {
        Vector3 center = rng_unit_vec3(&rng);
        if (!habitable(p, center)) continue;

        // Mapa de "população": um ruído suave deixa regiões inteiras cheias de
        // cidades e outras vazias, em vez de luzes espalhadas por igual.
        float population = noise_fbm3(center.x * f, center.y * f, center.z * f, 3, 2.0f, 0.5f,
                                      (uint32_t)t->city_seed + SEED_REGIONS);
        if (population < t->city_region_threshold) continue;

        // Tamanho da cidade: elevar ao quadrado torna as metrópoles raras.
        float scale = rng_float(&rng);
        scale *= scale;
        int lights = 1 + (int)(scale * (float)(t->city_cluster_max - 1));
        float spread = t->city_cluster_spread * (0.25f + 0.75f * sqrtf(scale));

        for (int i = 0; i < lights && made < target; i++) {
            // Desloca a partir do centro; multiplicar dois sorteios concentra
            // as luzes no meio da cidade.
            Vector3 offset = Vector3Scale(rng_unit_vec3(&rng), spread * rng_float(&rng) * rng_float(&rng) * 2.0f);
            Vector3 dir = Vector3Normalize(Vector3Add(center, offset));
            if (!habitable(p, dir)) continue;
            if (add_light(&mesh, grounds, made, p, t, sun, dir, &rng)) made++;
        }
    }

    if (made == 0) {
        MemFree(mesh.vertices);
        MemFree(mesh.texcoords);
        MemFree(mesh.colors);
        MemFree(mesh.indices);
        MemFree(grounds);
        return;
    }

    mesh.vertexCount = made * 4;
    mesh.triangleCount = made * 2;
    UploadMesh(&mesh, true);   // dinâmica: as cores mudam quando luzes se apagam
    c->alpha = MemAlloc((unsigned int)made);
    for (int i = 0; i < made; i++) c->alpha[i] = mesh.colors[i * 16 + 3];
    c->mesh = mesh;
    c->ground = grounds;
    c->has_mesh = true;
    c->count = made;
    c->lit = made;
}

void city_lights_impact(CityLights *c, const Planet *p, Vector3 pos, float radius)
{
    if (!c->has_mesh) return;

    // Só as luzes perto do impacto podem ter perdido o chão.
    float reach = radius * IMPACT_REACH + IMPACT_MARGIN;
    bool changed = false;

    for (int i = 0; i < c->count; i++) {
        unsigned char *alpha = &c->mesh.colors[i * 16 + 3];
        if (*alpha == 0) continue;   // já apagada

        Vector3 ground = { c->ground[i * 3], c->ground[i * 3 + 1], c->ground[i * 3 + 2] };
        if (Vector3DistanceSqr(ground, pos) > reach * reach) continue;

        // O solo logo abaixo da luz ainda existe?
        Vector3 below = Vector3Scale(ground, 1.0f - GROUND_PROBE_DEPTH / Vector3Length(ground));
        if (planet_sample(p, below) >= VOXEL_ISO) continue;

        for (int corner = 0; corner < 4; corner++) c->mesh.colors[(i * 4 + corner) * 4 + 3] = 0;
        c->lit--;
        changed = true;
    }

    if (changed) {
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, c->mesh.colors, c->count * 16, 0);
    }
}

void city_lights_restore(CityLights *c, const Planet *p, Vector3 pos, float radius)
{
    if (!c->has_mesh || c->alpha == NULL) return;
    float reach = radius + IMPACT_MARGIN;
    bool changed = false;
    for (int i = 0; i < c->count; i++) {
        if (c->mesh.colors[i * 16 + 3] != 0) continue;   // já acesa
        Vector3 ground = { c->ground[i * 3], c->ground[i * 3 + 1], c->ground[i * 3 + 2] };
        if (Vector3DistanceSqr(ground, pos) > reach * reach) continue;
        Vector3 below = Vector3Scale(ground, 1.0f - GROUND_PROBE_DEPTH / Vector3Length(ground));
        if (planet_sample(p, below) < VOXEL_ISO) continue;   // o chão ainda não voltou
        for (int corner = 0; corner < 4; corner++) c->mesh.colors[(i * 4 + corner) * 4 + 3] = c->alpha[i];
        c->lit++;
        changed = true;
    }
    if (changed) {
        UpdateMeshBuffer(c->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, c->mesh.colors, c->count * 16, 0);
    }
}

void city_lights_draw(const CityLights *c)
{
    if (!c->has_mesh || c->count <= 0) return;

    // Testa a profundidade (o planeta esconde as luzes do outro lado), mas
    // não a escreve, e soma o brilho ao que já está na tela.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    DrawMesh(c->mesh, c->material, MatrixIdentity());

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void city_lights_unload(CityLights *c)
{
    if (c->has_mesh) UnloadMesh(c->mesh);
    if (c->ground != NULL) MemFree(c->ground);
    if (c->alpha != NULL) MemFree(c->alpha);
    if (c->has_material) {
        // A textura é emprestada: tira do material antes de liberá-lo.
        c->material.maps[MATERIAL_MAP_DIFFUSE].texture = (Texture2D){ 0 };
        UnloadMaterial(c->material);
    }
    *c = (CityLights){ 0 };
}
