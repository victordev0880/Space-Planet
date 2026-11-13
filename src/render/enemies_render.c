#include "raylib.h"
#include "rlgl.h"

#include "render/enemies_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// ---------------------------------------------------------------------------
//  Modelos
// ---------------------------------------------------------------------------

enum { SW_SHELL, SW_PLATE, SW_UNDER, SW_BLADE, SW_EYE, SW_VEIN };
enum { BM_HULL, BM_PLATE, BM_DARK, BM_VENT, BM_CORE, BM_TURRET };

static LowPoly swarm_model;
static LowPoly bomber_model;
static bool    models_built;

// Drone do Enxame: um dardo com duas pinças à frente, asas em lâmina e um
// espinho nas costas. Escala 1: cerca de 2,6 de envergadura.
static void build_swarm(LowPoly *m)
{
    lowpoly_clear(m);
    lowpoly_part(m, SW_SHELL, (Color){ 104, 64, 150, 255 }, false);
    lowpoly_part(m, SW_PLATE, (Color){ 156, 106, 210, 255 }, false);
    lowpoly_part(m, SW_UNDER, (Color){ 56, 36, 80, 255 }, false);
    lowpoly_part(m, SW_BLADE, (Color){ 186, 128, 236, 255 }, false);
    lowpoly_part(m, SW_EYE,   (Color){ 150, 255, 150, 255 }, true);
    lowpoly_part(m, SW_VEIN,  (Color){ 90, 220, 130, 255 }, true);

    Vector3 nose   = { 0.0f, 0.0f, -1.1f };
    Vector3 top    = { 0.0f, 0.28f, -0.1f };
    Vector3 side   = { 0.32f, 0.0f, -0.05f };
    Vector3 bottom = { 0.0f, -0.22f, 0.0f };
    Vector3 tail   = { 0.0f, 0.05f, 0.95f };
    lowpoly_tri(m, nose, top, side, SW_PLATE, true);
    lowpoly_tri(m, nose, side, bottom, SW_UNDER, true);
    lowpoly_tri(m, top, side, tail, SW_SHELL, true);
    lowpoly_tri(m, side, bottom, tail, SW_UNDER, true);

    // Pinças: lâminas que avançam além do nariz, com o fio verde.
    Vector3 pincer_root  = { 0.24f, 0.0f, -0.15f };
    Vector3 pincer_back  = { 0.30f, 0.0f, 0.30f };
    Vector3 pincer_tip   = { 0.62f, -0.06f, -1.25f };
    Vector3 pincer_elbow = { 0.78f, -0.02f, -0.35f };
    lowpoly_tri(m, pincer_root, pincer_elbow, pincer_back, SW_BLADE, true);
    lowpoly_tri(m, pincer_root, pincer_tip, pincer_elbow, SW_SHELL, true);
    lowpoly_tri(m, (Vector3){ 0.60f, -0.05f, -1.05f }, pincer_tip, (Vector3){ 0.70f, -0.04f, -0.75f }, SW_VEIN, true);

    // Asas em lâmina, para trás.
    Vector3 wing_root  = { 0.28f, 0.04f, 0.10f };
    Vector3 wing_tip   = { 1.30f, 0.16f, 0.62f };
    Vector3 wing_trail = { 0.22f, 0.02f, 0.72f };
    lowpoly_tri(m, wing_root, wing_tip, wing_trail, SW_BLADE, true);
    lowpoly_tri(m, wing_root, (Vector3){ 1.10f, 0.15f, 0.48f }, wing_tip, SW_VEIN, true);

    // Espinho nas costas e os olhos.
    lowpoly_tri(m, top, (Vector3){ 0.0f, 0.62f, 0.50f }, (Vector3){ 0.0f, 0.15f, 0.60f }, SW_PLATE, false);
    lowpoly_tri(m, (Vector3){ 0.0f, 0.10f, -0.80f }, (Vector3){ 0.13f, 0.06f, -0.55f },
                (Vector3){ 0.03f, 0.19f, -0.45f }, SW_EYE, true);
}

// Bombardeiro: uma asa-delta larga e grossa, com lombada, casulo de bombas
// embaixo, torreta em cima e o bloco do motor atrás. Escala 1: cerca de 3,2
// de envergadura.
static void build_bomber(LowPoly *m)
{
    lowpoly_clear(m);
    lowpoly_part(m, BM_HULL,   (Color){ 104, 58, 62, 255 }, false);
    lowpoly_part(m, BM_PLATE,  (Color){ 156, 112, 82, 255 }, false);
    lowpoly_part(m, BM_DARK,   (Color){ 42, 30, 34, 255 }, false);
    lowpoly_part(m, BM_VENT,   (Color){ 255, 140, 60, 255 }, true);
    lowpoly_part(m, BM_CORE,   (Color){ 255, 96, 180, 255 }, true);
    lowpoly_part(m, BM_TURRET, (Color){ 118, 112, 118, 255 }, false);

    Vector3 nose        = { 0.0f, 0.05f, -1.35f };
    Vector3 spine_front = { 0.0f, 0.42f, -0.20f };
    Vector3 spine_rear  = { 0.0f, 0.38f, 0.70f };
    Vector3 mid_edge    = { 0.75f, 0.14f, 0.10f };
    Vector3 wing_tip    = { 1.60f, -0.05f, 0.75f };
    Vector3 root_rear   = { 0.45f, 0.15f, 1.00f };
    Vector3 center_rear = { 0.0f, 0.20f, 1.00f };
    Vector3 nose_low    = { 0.0f, -0.15f, -1.20f };
    Vector3 root_low    = { 0.45f, -0.20f, 1.00f };
    Vector3 center_low  = { 0.0f, -0.25f, 1.00f };

    // Em cima.
    lowpoly_tri(m, nose, spine_front, mid_edge, BM_PLATE, true);
    lowpoly_tri(m, spine_front, spine_rear, mid_edge, BM_HULL, true);
    lowpoly_tri(m, mid_edge, spine_rear, root_rear, BM_HULL, true);
    lowpoly_tri(m, nose, mid_edge, wing_tip, BM_HULL, true);
    lowpoly_tri(m, mid_edge, root_rear, wing_tip, BM_PLATE, true);
    lowpoly_tri(m, spine_rear, center_rear, root_rear, BM_HULL, true);
    // Embaixo e as bordas.
    lowpoly_tri(m, nose_low, wing_tip, root_low, BM_DARK, true);
    lowpoly_tri(m, nose_low, root_low, center_low, BM_DARK, true);
    lowpoly_tri(m, nose, nose_low, wing_tip, BM_DARK, true);
    lowpoly_quad(m, root_rear, wing_tip, root_low, root_low, BM_DARK, true);
    lowpoly_quad(m, center_rear, root_rear, root_low, center_low, BM_DARK, true);

    // Aberturas laranja sobre as asas.
    lowpoly_tri(m, (Vector3){ 0.55f, 0.20f, 0.30f }, (Vector3){ 1.05f, 0.08f, 0.52f },
                (Vector3){ 0.62f, 0.19f, 0.52f }, BM_VENT, true);

    // Pontas das asas, viradas para baixo.
    lowpoly_tri(m, wing_tip, (Vector3){ 1.55f, -0.38f, 0.98f }, (Vector3){ 1.40f, -0.06f, 0.98f }, BM_PLATE, true);

    // Casulo de bombas, embaixo.
    Vector3 pod_front = { 0.0f, -0.22f, -0.50f };
    Vector3 pod_side  = { 0.22f, -0.36f, 0.10f };
    Vector3 pod_low   = { 0.0f, -0.52f, 0.15f };
    Vector3 pod_back  = { 0.0f, -0.28f, 0.62f };
    lowpoly_tri(m, pod_front, pod_side, pod_low, BM_PLATE, true);
    lowpoly_tri(m, pod_side, pod_back, pod_low, BM_HULL, true);

    // Torreta em cima.
    lowpoly_tri(m, (Vector3){ 0.13f, 0.40f, -0.24f }, (Vector3){ 0.0f, 0.60f, -0.10f },
                (Vector3){ 0.13f, 0.40f, 0.04f }, BM_TURRET, true);
    lowpoly_tri(m, (Vector3){ 0.0f, 0.55f, -0.12f }, (Vector3){ 0.03f, 0.53f, -0.55f },
                (Vector3){ 0.0f, 0.50f, -0.12f }, BM_DARK, true);

    // Bloco do motor, atrás, com o núcleo exposto: o ponto fraco.
    Vector3 eng_a = { 0.34f, 0.28f, 0.88f }, eng_b = { 0.34f, -0.10f, 0.88f };
    Vector3 eng_c = { 0.28f, -0.06f, 1.16f }, eng_d = { 0.28f, 0.24f, 1.16f };
    lowpoly_quad(m, eng_a, eng_b, eng_c, eng_d, BM_DARK, true);
    lowpoly_quad(m, (Vector3){ 0.0f, 0.30f, 0.88f }, eng_a, eng_d, (Vector3){ 0.0f, 0.26f, 1.16f }, BM_HULL, true);
    lowpoly_quad(m, (Vector3){ 0.0f, 0.24f, 1.17f }, eng_d, eng_c, (Vector3){ 0.0f, -0.06f, 1.17f }, BM_CORE, true);
}

static void build_models(void)
{
    build_swarm(&swarm_model);
    build_bomber(&bomber_model);
    models_built = true;
}

// ---------------------------------------------------------------------------
//  Inimigos
// ---------------------------------------------------------------------------

#define ENEMY_AMBIENT    0.38f
#define ENEMY_SHADOW     0.22f
#define ENEMY_WRAP       0.35f
#define FLASH_TIME       0.12f   // (mesma duração de game/enemies.c)
#define EYE_GLOW         1.5f
#define TAIL_GLOW        1.2f
#define CORE_GLOW        1.0f    // em escalas do Bombardeiro
#define CORE_PULSE       5.0f
#define BAY_GLOW         1.4f    // em escalas: brilho do casulo armando

static const Vector3 SWARM_EYE  = { 0.0f, 0.10f, -0.75f };
static const Vector3 SWARM_TAIL = { 0.0f, 0.05f, 0.95f };
static const Vector3 BOMBER_CORE = { 0.0f, 0.09f, 1.20f };
static const Vector3 BOMBER_BAY  = { 0.0f, -0.40f, 0.15f };
static const Vector3 BOMBER_VENT = { 0.80f, 0.16f, 0.42f };

static const Color EYE_COLOR   = { 120, 255, 140, 255 };
static const Color TAIL_COLOR  = { 200, 110, 255, 255 };
static const Color CORE_COLOR  = { 255, 90, 190, 255 };
static const Color VENT_COLOR  = { 255, 140, 60, 255 };
static const Color ARMING_COLOR = { 255, 60, 40, 255 };

static Vector3 at(Vector3 pos, Quaternion rot, float scale, Vector3 local)
{
    return Vector3Add(pos, Vector3RotateByQuaternion(Vector3Scale(local, scale), rot));
}

static Color faded(Color c, float alpha)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) };
}

void enemies_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    if (!models_built) build_models();
    const Enemies *e = &w->enemies;
    Vector3 sun = tuning_sun_direction(t);
    float time = (float)w->tick / (float)t->sim_hz;

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        float scale = t->enemy[en->type].scale;
        Vector3 pos = Vector3Lerp(en->pos_prev, en->pos, alpha);
        Quaternion rot = QuaternionSlerp(en->rot_prev, en->rot, alpha);

        LowPolyPose pose = {
            .pos = pos, .rot = rot, .scale = scale, .sun = sun,
            .sunlight = lowpoly_sunlight(pos, sun, w->planet.params.radius, ENEMY_SHADOW),
            .ambient = ENEMY_AMBIENT, .wrap = ENEMY_WRAP,
            .whiten = Clamp(en->flash / FLASH_TIME, 0.0f, 1.0f) * 0.8f, .flash = WHITE,
            .eye = camera.position,
        };
        lowpoly_draw(en->type == ENEMY_SWARM ? &swarm_model : &bomber_model, &pose);
    }

    // Brilhos, por cima.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        float scale = t->enemy[en->type].scale;
        Vector3 pos = Vector3Lerp(en->pos_prev, en->pos, alpha);
        Quaternion rot = QuaternionSlerp(en->rot_prev, en->rot, alpha);

        if (en->type == ENEMY_SWARM) {
            DrawBillboard(camera, glow, at(pos, rot, scale, SWARM_EYE), EYE_GLOW * scale, faded(EYE_COLOR, 0.9f));
            DrawBillboard(camera, glow, at(pos, rot, scale, SWARM_TAIL), TAIL_GLOW * scale, faded(TAIL_COLOR, 0.7f));
            continue;
        }

        // Ponto fraco: pulsa, para chamar o olho.
        float pulse = 0.75f + 0.25f * sinf(time * CORE_PULSE + (float)i);
        Vector3 core = at(pos, rot, scale, BOMBER_CORE);
        DrawBillboard(camera, glow, core, CORE_GLOW * scale * pulse * 1.6f, faded(CORE_COLOR, 0.55f));
        DrawBillboard(camera, glow, core, CORE_GLOW * scale * 0.5f, faded(WHITE, 0.8f));
        for (int side = -1; side <= 1; side += 2) {
            Vector3 vent = { BOMBER_VENT.x * (float)side, BOMBER_VENT.y, BOMBER_VENT.z };
            DrawBillboard(camera, glow, at(pos, rot, scale, vent), 0.5f * scale, faded(VENT_COLOR, 0.5f));
        }

        // Armando a bomba: o casulo acende e cresce até soltar.
        if (en->arming > 0.0f) {
            float progress = 1.0f - en->arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
            float blink = 0.6f + 0.4f * sinf(time * (10.0f + 20.0f * progress));
            DrawBillboard(camera, glow, at(pos, rot, scale, BOMBER_BAY), BAY_GLOW * scale * (0.5f + progress),
                          faded(ARMING_COLOR, blink));
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}

// ---------------------------------------------------------------------------
//  Projéteis e marcas de alvo
// ---------------------------------------------------------------------------

#define BOLT_LENGTH     0.028f   // comprimento do risco, em segundos de voo
#define BOLT_LENGTH_MAX 7.0f
#define BOLT_WIDTH      0.32f
#define BOLT_HALO       2.2f     // largura do halo, em larguras do risco
#define BOLT_WIDTH_AT   25.0f    // distância em que o risco tem a largura normal
#define BOLT_NEAR       6.0f     // mais perto da câmera que isto, o tiro quase some...
#define BOLT_CLEAR      18.0f    // ... e a partir daqui aparece inteiro (senão, colado na
                                 // câmera, vira uma faixa clara enorme sobre a nave)
#define PLASMA_SIZE     1.3f
#define PLASMA_LENGTH   0.05f
#define BOMB_SIZE       2.6f
#define MARK_SEGMENTS   40
#define MARK_LIFT       0.6f     // a marca fica um pouco acima do solo
#define MARK_WIDTH      0.16f    // espessura do anel, em frações do raio
#define MARK_START      2.2f     // o anel de aviso começa este tanto maior e se fecha sobre o alvo

static const Color BOLT_CORE       = { 235, 250, 255, 255 };
static const Color BOLT_HALO_COLOR = {  80, 190, 255, 255 };
static const Color PLASMA_CORE = { 255, 210, 255, 255 };
static const Color PLASMA_HALO = { 190,  80, 255, 255 };
static const Color BOMB_COLOR  = { 255,  70,  50, 255 };
static const Color MARK_COLOR  = { 255,  44,  32, 255 };

// Um risco de `a` a `b`, sempre de frente para a câmera. A textura de brilho é
// lida só na linha do meio: borda suave na largura, cheio no comprimento.
static void streak(Vector3 a, Vector3 b, float width, Color color, Vector3 eye)
{
    Vector3 along = Vector3Subtract(b, a);
    Vector3 side = Vector3CrossProduct(along, Vector3Subtract(eye, a));
    if (Vector3LengthSqr(side) < 1e-8f) return;
    side = Vector3Scale(Vector3Normalize(side), width * 0.5f);

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0.0f, 0.5f); rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlTexCoord2f(1.0f, 0.5f); rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlTexCoord2f(1.0f, 0.5f); rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlTexCoord2f(0.0f, 0.5f); rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
}

// Anel deitado no solo em volta de `center` (normal = para fora do planeta).
static void ground_ring(Vector3 center, float radius, float width, Color color)
{
    Vector3 n = Vector3Normalize(center);
    Vector3 hint = (fabsf(n.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(n, hint));
    Vector3 v = Vector3CrossProduct(n, u);
    Vector3 c = Vector3Add(center, Vector3Scale(n, MARK_LIFT));
    float inner = radius * (1.0f - width);

    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < MARK_SEGMENTS; i++) {
        float a0 = 2.0f * PI * (float)i / MARK_SEGMENTS, a1 = 2.0f * PI * (float)(i + 1) / MARK_SEGMENTS;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(a0)), Vector3Scale(v, sinf(a0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(a1)), Vector3Scale(v, sinf(a1)));
        Vector3 p0 = Vector3Add(c, Vector3Scale(d0, inner)), p1 = Vector3Add(c, Vector3Scale(d0, radius));
        Vector3 p2 = Vector3Add(c, Vector3Scale(d1, radius)), p3 = Vector3Add(c, Vector3Scale(d1, inner));
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }
}

// Marca no solo onde uma bomba vai cair. `closing` vai de 0 (acabou de
// marcar) a 1 (a bomba está caindo): o anel de fora se fecha sobre o alvo.
static void target_mark(Vector3 target, float radius, float closing, float time, Vector3 eye)
{
    // Só do lado do planeta voltado para a câmera.
    Vector3 n = Vector3Normalize(target);
    float facing = Vector3DotProduct(n, Vector3Normalize(Vector3Subtract(eye, target)));
    if (facing <= 0.0f) return;
    float fade = Clamp(facing * 4.0f, 0.0f, 1.0f) * 0.9f;
    float blink = 0.6f + 0.4f * sinf(time * (8.0f + 16.0f * closing));

    ground_ring(target, radius, MARK_WIDTH, faded(MARK_COLOR, fade * blink));
    ground_ring(target, radius * Lerp(MARK_START, 1.0f, closing), MARK_WIDTH * 0.6f, faded(MARK_COLOR, fade * 0.7f));
    ground_ring(target, radius * 0.25f, 0.6f, faded(MARK_COLOR, fade * blink));
}

void projectiles_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Projectiles *p = &w->shots;
    float time = (float)w->tick / (float)t->sim_hz;
    Vector3 eye = camera.position;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // Marcas no solo: por cima do terreno (sem teste de profundidade), para o
    // relevo não esconder o aviso, e com transparência comum (somando luz, o
    // vermelho sobre o mar azul viraria rosa).
    rlDisableDepthTest();
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].arming <= 0.0f || e->items[i].hp <= 0.0f) continue;
        float closing = 1.0f - e->items[i].arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
        target_mark(e->items[i].target, t->bomber_crater_radius, closing * 0.85f, time, eye);
    }
    for (int i = 0; i < p->pool.capacity; i++) {
        if (pool_alive(&p->pool, i) && p->items[i].kind == SHOT_BOMB) {
            target_mark(p->items[i].target, p->items[i].crater_radius, 1.0f, time, eye);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();

    // Tiros: riscos e bolas de luz, somando brilho.
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        float speed = Vector3Length(s->vel);
        if (speed < 1e-3f) continue;
        Vector3 dir = Vector3Scale(s->vel, 1.0f / speed);

        if (s->kind == SHOT_BOLT) {
            // O risco não passa de onde o tiro saiu.
            float length = fminf(fminf(speed * BOLT_LENGTH, BOLT_LENGTH_MAX), s->age * speed + 0.5f);
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            // Perto da câmera o risco fica mais fino e mais fraco; longe, um
            // pouco mais grosso, para continuar visível.
            float distance = Vector3Distance(pos, eye);
            float near = Clamp((distance - BOLT_NEAR) / (BOLT_CLEAR - BOLT_NEAR), 0.15f, 1.0f);
            float width = BOLT_WIDTH * Clamp(distance / BOLT_WIDTH_AT, 0.25f, 1.5f);
            streak(tail, pos, width * BOLT_HALO, faded(BOLT_HALO_COLOR, 0.5f * near), eye);
            streak(tail, pos, width, faded(BOLT_CORE, near), eye);
        } else if (s->kind == SHOT_PLASMA) {
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, speed * PLASMA_LENGTH));
            streak(tail, pos, PLASMA_SIZE * 0.6f, faded(PLASMA_HALO, 0.6f), eye);
        }
    }
    rlEnd();
    rlSetTexture(0);

    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        if (s->kind == SHOT_PLASMA) {
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 1.6f, faded(PLASMA_HALO, 0.8f));
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 0.6f, PLASMA_CORE);
        } else if (s->kind == SHOT_BOMB) {
            // Pisca cada vez mais rápido conforme se aproxima do solo.
            float remaining = Vector3Distance(pos, s->target);
            float blink = 0.6f + 0.4f * sinf(time * (6.0f + 60.0f / (remaining + 4.0f)));
            DrawBillboard(camera, glow, pos, BOMB_SIZE * 2.0f, faded(BOMB_COLOR, 0.5f * blink));
            DrawBillboard(camera, glow, pos, BOMB_SIZE, faded(BOMB_COLOR, blink));
            DrawBillboard(camera, glow, pos, BOMB_SIZE * 0.35f, WHITE);
        }
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
