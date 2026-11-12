#include "raylib.h"
#include "rlgl.h"

#include "render/stars.h"

#include <math.h>

#include "core/rng.h"
#include "core/vmath.h"

#define SPRITE_SIZE 32

// 4 vértices por estrela e índices de 16 bits: 16000 * 4 cabe em 65536.
#define STARS_MAX 16000

// Camadas internas do Sol, como fração das camadas definidas no tuning.
#define SUN_INNER_GLOW_SIZE 0.35f   // segundo brilho, dentro do brilho largo
#define SUN_CORE_SIZE       0.6f    // miolo branco, dentro do disco

// Textura de brilho: branca, com a transparência caindo do centro para a borda.
static Texture2D make_sprite(void)
{
    static unsigned char pixels[SPRITE_SIZE * SPRITE_SIZE * 4];

    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) {
            float u = ((float)x + 0.5f) / SPRITE_SIZE * 2.0f - 1.0f;
            float v = ((float)y + 0.5f) / SPRITE_SIZE * 2.0f - 1.0f;
            float falloff = 1.0f - (u * u + v * v);
            if (falloff < 0.0f) falloff = 0.0f;
            float alpha = falloff * falloff;

            unsigned char *p = &pixels[(y * SPRITE_SIZE + x) * 4];
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = (unsigned char)(alpha * 255.0f + 0.5f);
        }
    }

    Image image = { pixels, SPRITE_SIZE, SPRITE_SIZE, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D texture = LoadTextureFromImage(image);
    // Mipmaps: versões reduzidas da textura. Sem elas, uma estrela de 3 pixels
    // amostraria a textura de 32 "pulando" pontos e cintilaria ao girar a câmera.
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    return texture;
}

// Direção perto de um grande círculo inclinado: a faixa da "Via Láctea".
static Vector3 band_direction(Rng *rng, const Tuning *t)
{
    // Somar três sorteios dá uma distribuição em forma de sino, concentrada no meio.
    float bell = (rng_float(rng) + rng_float(rng) + rng_float(rng)) * (2.0f / 3.0f) - 1.0f;
    float lat = bell * t->stars_band_width * (PI * 0.5f);
    float lon = rng_range(rng, 0.0f, 2.0f * PI);
    Vector3 dir = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon) };
    return Vector3RotateByAxisAngle(dir, (Vector3){ 1.0f, 0.0f, 0.0f }, t->stars_band_tilt * DEG2RAD);
}

static Color star_color(Rng *rng, float brightness)
{
    static const Color PALETTE[5] = {
        { 170, 195, 255, 255 },   // azul
        { 208, 222, 255, 255 },   // branco-azulado
        { 255, 255, 255, 255 },   // branco
        { 255, 242, 216, 255 },   // amarelado
        { 255, 212, 168, 255 },   // alaranjado
    };
    float pick = rng_float(rng);
    int index = (pick < 0.10f) ? 0 : (pick < 0.40f) ? 1 : (pick < 0.72f) ? 2 : (pick < 0.92f) ? 3 : 4;
    Color c = PALETTE[index];
    c.a = (unsigned char)(Clamp(brightness, 0.0f, 1.0f) * 255.0f + 0.5f);
    return c;
}

void stars_build(Stars *s, const Tuning *t)
{
    if (!s->has_material) {
        s->material = LoadMaterialDefault();
        s->material.maps[MATERIAL_MAP_DIFFUSE].texture = make_sprite();
        s->has_material = true;
    }
    if (s->has_mesh) {
        UnloadMesh(s->mesh);
        s->has_mesh = false;
    }

    int count = t->stars_count;
    if (count > STARS_MAX) count = STARS_MAX;
    s->count = count;
    if (count <= 0) return;

    // A malha é criada aqui (ao iniciar ou no F5), nunca durante a simulação.
    Mesh mesh = { 0 };
    mesh.vertexCount = count * 4;
    mesh.triangleCount = count * 2;
    mesh.vertices = MemAlloc((unsigned int)(count * 4 * 3) * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)(count * 4 * 2) * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)(count * 4 * 4));
    mesh.indices = MemAlloc((unsigned int)(count * 6) * sizeof(unsigned short));

    static const float CORNERS[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    static const float UVS[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };

    Rng rng;
    rng_seed(&rng, (uint64_t)t->stars_seed, 2u);

    for (int i = 0; i < count; i++) {
        Vector3 dir = (rng_float(&rng) < t->stars_band_fraction) ? band_direction(&rng, t)
                                                                : rng_unit_vec3(&rng);

        // "Magnitude" de 0 a 1. Elevar a uma potência alta empurra quase todas
        // para perto de zero: muitas estrelas pequenas e poucas grandes.
        float magnitude = powf(rng_float(&rng), t->stars_size_power);
        float size = Lerp(t->stars_size_min, t->stars_size_max, magnitude);
        float dim = rng_float(&rng);
        float glow = fmaxf(sqrtf(magnitude), dim * dim * dim);
        float brightness = Lerp(t->stars_brightness_min, 1.0f, glow);
        Color color = star_color(&rng, brightness);

        // Dois eixos perpendiculares à direção, para deitar o quadrado na esfera.
        Vector3 hint = (fabsf(dir.y) > 0.99f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
        Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, hint));
        Vector3 up = Vector3CrossProduct(right, dir);
        float half = size * 0.5f;

        for (int corner = 0; corner < 4; corner++) {
            int v = i * 4 + corner;
            Vector3 p = dir;
            p = Vector3Add(p, Vector3Scale(right, CORNERS[corner][0] * half));
            p = Vector3Add(p, Vector3Scale(up, CORNERS[corner][1] * half));
            mesh.vertices[v * 3 + 0] = p.x;
            mesh.vertices[v * 3 + 1] = p.y;
            mesh.vertices[v * 3 + 2] = p.z;
            mesh.texcoords[v * 2 + 0] = UVS[corner][0];
            mesh.texcoords[v * 2 + 1] = UVS[corner][1];
            mesh.colors[v * 4 + 0] = color.r;
            mesh.colors[v * 4 + 1] = color.g;
            mesh.colors[v * 4 + 2] = color.b;
            mesh.colors[v * 4 + 3] = color.a;
        }

        unsigned short base = (unsigned short)(i * 4);
        unsigned short *idx = &mesh.indices[i * 6];
        idx[0] = base;
        idx[1] = (unsigned short)(base + 1);
        idx[2] = (unsigned short)(base + 2);
        idx[3] = base;
        idx[4] = (unsigned short)(base + 2);
        idx[5] = (unsigned short)(base + 3);
    }

    UploadMesh(&mesh, false);
    s->mesh = mesh;
    s->has_mesh = true;
}

void stars_draw(const Stars *s, Vector3 camera_pos, float radius)
{
    if (!s->has_mesh || s->count <= 0) return;

    // As estrelas são o fundo: não testam nem escrevem profundidade, então
    // qualquer coisa desenhada depois fica na frente delas. A mistura aditiva
    // soma o brilho ao que já está na tela.
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    Matrix transform = MatrixMultiply(MatrixScale(radius, radius, radius),
                                      MatrixTranslate(camera_pos.x, camera_pos.y, camera_pos.z));
    DrawMesh(s->mesh, s->material, transform);

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    rlEnableDepthTest();
}

void stars_draw_sun(const Stars *s, Camera3D camera, const Tuning *t, float radius)
{
    if (!s->has_material) return;

    Texture2D glow = s->material.maps[MATERIAL_MAP_DIFFUSE].texture;
    Vector3 position = Vector3Add(camera.position, Vector3Scale(tuning_sun_direction(t), radius));
    Color core = { t->sun_color.r, t->sun_color.g, t->sun_color.b, 255 };
    Color halo = core;
    halo.a = (unsigned char)(Clamp(t->sun_glow_alpha, 0.0f, 1.0f) * 255.0f + 0.5f);

    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    // Os tamanhos do tuning são ângulos; multiplicados pela distância viram
    // o tamanho do quadrado no mundo. Várias camadas, da mais larga e fraca
    // à mais estreita e forte, somam um brilho que cresce em direção ao centro.
    DrawBillboard(camera, glow, position, t->sun_glow_size * radius, halo);
    DrawBillboard(camera, glow, position, t->sun_glow_size * radius * SUN_INNER_GLOW_SIZE, halo);
    DrawBillboard(camera, glow, position, t->sun_size * radius, core);
    DrawBillboard(camera, glow, position, t->sun_size * radius * SUN_CORE_SIZE, WHITE);

    EndBlendMode();
    rlEnableDepthMask();
    rlEnableDepthTest();
}

Texture2D stars_glow_texture(const Stars *s)
{
    return s->material.maps[MATERIAL_MAP_DIFFUSE].texture;
}

void stars_unload(Stars *s)
{
    if (s->has_mesh) UnloadMesh(s->mesh);
    if (s->has_material) UnloadMaterial(s->material);   // libera também a textura
    s->has_mesh = false;
    s->has_material = false;
    s->count = 0;
}
