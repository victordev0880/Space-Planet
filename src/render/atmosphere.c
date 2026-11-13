#include "raylib.h"
#include "rlgl.h"

#include "render/atmosphere.h"

#include <math.h>

#include "core/vmath.h"

#define SEGMENTS 96   // divisões de cada anel, em volta do eixo de visão

// Cada anel reúne as linhas de visão que passam a uma mesma distância do
// centro do planeta. Os anéis ficam mais juntos perto da borda do planeta,
// onde o brilho muda mais rápido.
//
// Anéis sobre o disco do planeta: distância ao centro, em raios do planeta.
static const float INNER_RINGS[] = { 0.0f, 0.45f, 0.70f, 0.85f, 0.93f, 0.975f, 1.0f };
// Anéis acima da borda: altura, de 0 (borda do planeta) a 1 (topo da atmosfera).
static const float OUTER_RINGS[] = { 0.0f, 0.05f, 0.12f, 0.22f, 0.35f, 0.50f, 0.68f, 0.85f, 1.0f };

#define INNER_COUNT ((int)(sizeof INNER_RINGS / sizeof INNER_RINGS[0]))
#define OUTER_COUNT ((int)(sizeof OUTER_RINGS / sizeof OUTER_RINGS[0]))
#define RING_COUNT  (INNER_COUNT + OUTER_COUNT)

// Até onde a luz do Sol alcança a atmosfera, medido pelo cosseno do ângulo
// com o Sol: começa um pouco depois do terminador (crepúsculo) e chega ao
// máximo já do lado do dia.
#define TWILIGHT_START (-0.30f)
#define TWILIGHT_END   0.35f

// Com o planeta morrendo, a atmosfera tende a esta cor e perde espessura.
static const Color DYING_COLOR = { 255, 96, 60, 255 };
#define DYING_THINNING 0.6f

// --- câmera dentro da atmosfera ---
#define NEAR_SHEET   0.3f   // distância mínima entre a câmera e a "folha" de névoa
#define RELIEF_COVER 0.5f   // a névoa sobre o chão fica esta altura acima das montanhas mais altas
#define BURIED_FADE  3.0f   // abaixo do nível do mar a névoa some ao longo desta profundidade

static float smoothstep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void atmosphere_init(Atmosphere *a)
{
    *a = (Atmosphere){ 0 };

    // Entre o último anel de dentro e o primeiro de fora não há faixa: os
    // dois ficam no mesmo lugar (a borda do planeta) com brilhos diferentes.
    int vertex_count = RING_COUNT * SEGMENTS;
    int band_count = RING_COUNT - 2;
    int index_count = band_count * SEGMENTS * 6;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertex_count;
    mesh.triangleCount = index_count / 3;
    mesh.vertices = MemAlloc((unsigned int)(vertex_count * 3) * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)(vertex_count * 4));
    mesh.indices = MemAlloc((unsigned int)index_count * sizeof(unsigned short));

    int i = 0;
    for (int ring = 0; ring < RING_COUNT - 1; ring++) {
        if (ring == INNER_COUNT - 1) continue;
        for (int seg = 0; seg < SEGMENTS; seg++) {
            int next = (seg + 1) % SEGMENTS;
            int a0 = ring * SEGMENTS + seg;           // anel de dentro
            int b0 = (ring + 1) * SEGMENTS + seg;     // anel de fora
            int c0 = (ring + 1) * SEGMENTS + next;
            int d0 = ring * SEGMENTS + next;
            mesh.indices[i++] = (unsigned short)a0;
            mesh.indices[i++] = (unsigned short)b0;
            mesh.indices[i++] = (unsigned short)c0;
            mesh.indices[i++] = (unsigned short)a0;
            mesh.indices[i++] = (unsigned short)c0;
            mesh.indices[i++] = (unsigned short)d0;
        }
    }

    UploadMesh(&mesh, true);   // dinâmica: posições e cores mudam a cada quadro
    a->mesh = mesh;
    a->material = LoadMaterialDefault();
    a->has_mesh = true;
}

void atmosphere_draw(Atmosphere *a, const Tuning *t, float planet_radius, Vector3 camera_pos, float hp)
{
    if (!a->has_mesh) return;

    float damage = Clamp(1.0f - hp, 0.0f, 1.0f);
    float height = t->atmo_height * (1.0f - DYING_THINNING * damage);
    float top = planet_radius * (1.0f + height);        // raio do topo da atmosfera
    float distance = Vector3Length(camera_pos);
    if (distance < 1e-3f) return;                       // no centro do planeta não há "para baixo"

    // Dentro da atmosfera a conta muda (ver o cabeçalho). Abaixo do nível do
    // mar, em crateras e túneis, a névoa vai sumindo.
    bool inside = distance <= top;
    float fade = inside ? Clamp(1.0f + (distance - planet_radius) / BURIED_FADE, 0.0f, 1.0f) : 1.0f;
    if (fade <= 0.0f) return;
    // Para as contas, a câmera nunca está abaixo do chão.
    float d = inside ? fmaxf(distance, planet_radius * 1.0005f) : distance;
    float horizon = asinf(Clamp(planet_radius / d, 0.0f, 1.0f));   // ângulo entre "para baixo" e o horizonte
    float shell = planet_radius + t->planet_land_height + RELIEF_COVER;

    Vector3 sun = tuning_sun_direction(t);
    Color tint = {
        (unsigned char)Lerp((float)t->atmo_color.r, (float)DYING_COLOR.r, damage),
        (unsigned char)Lerp((float)t->atmo_color.g, (float)DYING_COLOR.g, damage),
        (unsigned char)Lerp((float)t->atmo_color.b, (float)DYING_COLOR.b, damage),
        255,
    };

    // Eixos da "tela": para a frente (da câmera ao centro do planeta), para
    // a direita e para cima.
    Vector3 forward = Vector3Scale(camera_pos, -1.0f / distance);
    Vector3 hint = (fabsf(forward.y) > 0.99f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, hint));
    Vector3 up = Vector3CrossProduct(right, forward);

    // O caminho mais longo possível pelo ar: o da linha que raspa o planeta.
    float longest = 2.0f * sqrtf(top * top - planet_radius * planet_radius);

    Mesh *mesh = &a->mesh;
    for (int ring = 0; ring < RING_COUNT; ring++) {
        bool over_planet = ring < INNER_COUNT;

        // Cada anel é um cone de linhas de visão em volta do eixo que vai da
        // câmera ao centro do planeta. De cada um saem quatro números:
        float sin_a, cos_a;  // o ângulo entre as linhas e esse eixo
        float strength;      // o brilho, antes de considerar o Sol
        float to_vertex;     // a que distância da câmera fica o vértice
        float to_lit_point;  // o ponto da linha usado para decidir se é dia ou noite

        if (over_planet) {
            // A linha bate no chão. `miss` é a distância a que ela passaria do
            // centro do planeta, se o atravessasse.
            float miss = INNER_RINGS[ring] * planet_radius;
            sin_a = miss / d;
            cos_a = sqrtf(1.0f - sin_a * sin_a);
            float half_in_ground = sqrtf(fmaxf(planet_radius * planet_radius - miss * miss, 0.0f));
            float half_in_air = sqrtf(fmaxf(top * top - miss * miss, 0.0f));
            float to_ground = d * cos_a - half_in_ground;
            float to_entry = d * cos_a - half_in_air;         // da câmera até entrar na atmosfera

            // O caminho pelo ar vai da entrada na atmosfera (ou da câmera, se
            // ela já está dentro) até o solo.
            float path = inside ? to_ground : to_ground - to_entry;
            strength = t->atmo_haze + t->atmo_strength * (path / longest);
            to_lit_point = to_ground;

            if (!inside) {
                to_vertex = to_entry;
            } else {
                // A folha de névoa fica logo acima do relevo, para as
                // montanhas não a furarem; se a câmera está mais baixa que
                // isso, fica colada na frente dela.
                float half_in_shell = sqrtf(fmaxf(shell * shell - miss * miss, 0.0f));
                to_vertex = (d > shell) ? fmaxf(d * cos_a - half_in_shell, NEAR_SHEET) : NEAR_SHEET;
            }
        } else if (!inside) {
            // De fora, passando por cima do planeta: a linha atravessa a
            // atmosfera inteira.
            float miss = fminf(planet_radius + OUTER_RINGS[ring - INNER_COUNT] * (top - planet_radius), top);
            sin_a = miss / d;
            cos_a = sqrtf(1.0f - sin_a * sin_a);
            float half_in_air = sqrtf(fmaxf(top * top - miss * miss, 0.0f));
            strength = t->atmo_strength * powf(2.0f * half_in_air / longest, t->atmo_falloff);
            to_lit_point = d * cos_a;                         // o ponto da linha mais perto do centro
            to_vertex = d * cos_a - half_in_air;
        } else {
            // De dentro, olhando acima do horizonte: os anéis vão do horizonte
            // até o zênite, e o caminho pelo ar vai da câmera até a linha
            // sair pelo topo da atmosfera. Esse caminho é o mais longo no
            // horizonte e o mais curto olhando reto para cima.
            int step = ring - INNER_COUNT;
            float longest_here = d * cosf(horizon) + sqrtf(fmaxf(top * top - planet_radius * planet_radius, 0.0f));
            float shortest_here = top - d;

            // Os anéis ficam a passos iguais de brilho, e não de ângulo: o
            // brilho cai depressa logo acima do horizonte e devagar no resto
            // do céu, e anéis mal distribuídos deixariam uma borda visível.
            float glow = 1.0f - (float)step / (float)(OUTER_COUNT - 1);
            float to_exit = shortest_here
                          + (longest_here - shortest_here) * powf(glow, 1.0f / fmaxf(t->atmo_falloff, 0.1f));

            // Ângulo da linha que percorre exatamente `to_exit` até o topo
            // (lei dos cossenos no triângulo centro, câmera, ponto de saída).
            float angle = PI;
            if (step == 0) {
                angle = horizon;
            } else if (step < OUTER_COUNT - 1 && to_exit > 1e-3f) {
                float cos_up = (top * top - d * d - to_exit * to_exit) / (2.0f * d * to_exit);
                angle = PI - acosf(Clamp(cos_up, -1.0f, 1.0f));
            }
            sin_a = sinf(angle);
            cos_a = cosf(angle);
            strength = t->atmo_strength * powf(Clamp(to_exit / longest, 0.0f, 1.0f), t->atmo_falloff);
            to_lit_point = fmaxf(d * cos_a, 0.0f);            // olhando para cima, a própria câmera
            to_vertex = fmaxf(to_exit, NEAR_SHEET);
        }
        strength *= fade;

        for (int seg = 0; seg < SEGMENTS; seg++) {
            float angle = 2.0f * PI * (float)seg / SEGMENTS;
            Vector3 radial = Vector3Add(Vector3Scale(right, cosf(angle)), Vector3Scale(up, sinf(angle)));
            Vector3 dir = Vector3Add(Vector3Scale(forward, cos_a), Vector3Scale(radial, sin_a));

            Vector3 pos = Vector3Add(camera_pos, Vector3Scale(dir, to_vertex));
            Vector3 lit_point = Vector3Add(camera_pos, Vector3Scale(dir, to_lit_point));
            float day = smoothstep(TWILIGHT_START, TWILIGHT_END,
                                   Vector3DotProduct(Vector3Normalize(lit_point), sun));
            float alpha = Clamp(strength * day, 0.0f, 1.0f);

            int v = ring * SEGMENTS + seg;
            mesh->vertices[v * 3 + 0] = pos.x;
            mesh->vertices[v * 3 + 1] = pos.y;
            mesh->vertices[v * 3 + 2] = pos.z;
            mesh->colors[v * 4 + 0] = tint.r;
            mesh->colors[v * 4 + 1] = tint.g;
            mesh->colors[v * 4 + 2] = tint.b;
            mesh->colors[v * 4 + 3] = (unsigned char)(alpha * 255.0f + 0.5f);
        }
    }

    UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, mesh->vertices,
                     mesh->vertexCount * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, mesh->colors, mesh->vertexCount * 4, 0);

    // Testa a profundidade (o que estiver na frente esconde o ar), mas não a
    // escreve, e soma o brilho ao que já está na tela.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    DrawMesh(*mesh, a->material, MatrixIdentity());

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void atmosphere_unload(Atmosphere *a)
{
    if (a->has_mesh) {
        UnloadMesh(a->mesh);
        UnloadMaterial(a->material);
    }
    *a = (Atmosphere){ 0 };
}
