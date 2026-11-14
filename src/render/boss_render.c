#include "raylib.h"
#include "rlgl.h"

#include "render/boss_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/hive_render.h"
#include "render/worm_render.h"
#include "render/lowpoly.h"

// --- modelos ------------------------------------------------------------------
// Eixos do modelo: frente -Z, direita +X, topo +Y; vezes breaker.scale. Os
// pontos que a lógica usa (capacitores, núcleo, canhão, torretas) vêm de
// game/boss.h.

enum { B_HULL, B_PLATE, B_DARK, B_ACCENT, B_METAL, B_GLASS, B_GLOW, B_HOT };

static LowPoly hull_model, capacitor_model, core_model, petal_model;
static bool    built;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void palette(LowPoly *m)
{
    lowpoly_part(m, B_HULL, (Color){ 76, 78, 92, 255 }, false);
    lowpoly_part(m, B_PLATE, (Color){ 120, 118, 132, 255 }, false);
    lowpoly_part(m, B_DARK, (Color){ 32, 32, 40, 255 }, false);
    lowpoly_part(m, B_ACCENT, (Color){ 196, 58, 46, 255 }, false);
    lowpoly_part(m, B_METAL, (Color){ 150, 150, 160, 255 }, false);
    lowpoly_part(m, B_GLASS, (Color){ 20, 16, 24, 255 }, false);
    lowpoly_part_glow(m, B_GLOW, (Color){ 255, 90, 50, 255 });
    lowpoly_part(m, B_HOT, (Color){ 255, 240, 220, 255 }, true);
}

static void build_hull(void)
{
    const Vector3 AX = { 1.0f, 0.0f, 0.0f }, AY = { 0.0f, 1.0f, 0.0f }, AZ = { 0.0f, 0.0f, 1.0f };
    LowPoly *m = &hull_model;
    lowpoly_clear(m);
    palette(m);

    // Casco: uma cunha longa e pesada.
    const LpRing body[] = {
        { -1.60f, 0.20f, 0.12f, 0.18f }, { -1.20f, 0.38f, 0.26f, 0.16f }, { -0.50f, 0.50f, 0.34f, 0.16f },
        { 0.40f, 0.52f, 0.36f, 0.15f },  { 1.00f, 0.46f, 0.32f, 0.15f },  { 1.35f, 0.36f, 0.24f, 0.14f },
    };
    lp_loft(m, body, 6, 8, 0.0f, B_HULL, B_DARK, B_DARK, 0.0f, false);
    // Crista blindada na frente (o núcleo fica atrás dela, em cima).
    lp_plate(m, V(0.0f, 0.42f, -1.30f), V(0.0f, 0.68f, -0.95f), V(0.0f, 0.64f, -0.38f), V(0.0f, 0.46f, -0.30f), AX, 0.08f,
             0.05f, B_PLATE, B_PLATE, B_ACCENT, false);
    // Encaixe do núcleo.
    lp_ring(m, V(0.0f, 0.48f, 0.15f), AY, 0.24f, 0.36f, 0.14f, 8, B_PLATE, B_DARK, false);

    // O canhão, embaixo, com anéis de resfriamento e a boca acesa.
    lp_tube(m, V(0.0f, -0.25f, 0.60f), V(0.0f, -0.25f, -2.10f), 0.21f, 0.15f, 10, B_METAL, B_DARK, false);
    for (int i = 0; i < 3; i++) {
        lp_ring(m, V(0.0f, -0.25f, -0.6f - 0.5f * (float)i), AZ, 0.17f, 0.25f, 0.08f, 10, B_ACCENT, B_DARK, false);
    }
    lp_ring(m, V(0.0f, -0.25f, -2.15f), AZ, 0.08f, 0.24f, 0.08f, 10, B_GLOW, B_DARK, false);
    lp_plate(m, V(0.0f, -0.05f, 0.4f), V(0.0f, -0.12f, -0.6f), V(0.0f, -0.12f, -1.2f), V(0.0f, -0.02f, -1.2f), AX, 0.10f,
             0.08f, B_DARK, B_DARK, B_PLATE, false);

    // Suportes dos capacitores.
    for (int k = 0; k < BOSS_CAPACITORS; k++) {
        Vector3 c = BREAKER_CAPACITOR[k];
        Vector3 root = V(c.x * 0.6f, c.y, c.z);
        lp_tube(m, root, V(c.x * 0.86f, c.y, c.z), 0.07f, 0.06f, 6, B_METAL, -1, false);
    }

    // Lâminas laterais na frente e asas pesadas atrás.
    lp_plate(m, V(0.40f, 0.0f, -1.20f), V(0.78f, -0.10f, -1.62f), V(0.74f, -0.10f, -1.35f), V(0.46f, 0.0f, -0.80f), AY,
             0.08f, 0.03f, B_ACCENT, B_DARK, B_HULL, true);
    lp_plate(m, V(0.45f, 0.10f, 0.60f), V(1.18f, -0.05f, 1.00f), V(1.12f, -0.05f, 1.35f), V(0.45f, 0.10f, 1.30f), AY, 0.10f,
             0.04f, B_PLATE, B_DARK, B_ACCENT, true);
    lp_plate(m, V(1.12f, -0.05f, 1.05f), V(1.18f, 0.40f, 1.28f), V(1.16f, 0.38f, 1.40f), V(1.12f, -0.05f, 1.33f), AX, 0.05f,
             0.03f, B_HULL, B_HULL, B_ACCENT, true);

    // Torretas.
    for (int k = 0; k < BOSS_TURRETS; k++) {
        Vector3 c = BREAKER_TURRET[k];
        lp_box(m, c, V(0.10f, 0.06f, 0.10f), B_METAL, false);
        lp_tube(m, V(c.x + 0.03f, c.y + 0.02f, c.z - 0.05f), V(c.x + 0.03f, c.y + 0.02f, c.z - 0.42f), 0.022f, 0.02f, 5,
                B_DARK, -1, false);
        lp_tube(m, V(c.x - 0.03f, c.y + 0.02f, c.z - 0.05f), V(c.x - 0.03f, c.y + 0.02f, c.z - 0.42f), 0.022f, 0.02f, 5,
                B_DARK, -1, false);
    }

    // Motores.
    for (int i = -1; i <= 1; i++) {
        float x = (float)i * 0.24f;
        lp_tube(m, V(x, 0.15f, 1.30f), V(x, 0.15f, 1.58f), 0.11f, 0.13f, 8, B_METAL, -1, false);
        lp_ring(m, V(x, 0.15f, 1.59f), AZ, 0.0f, 0.11f, 0.02f, 8, B_GLOW, B_GLOW, false);
    }

    lowpoly_detail(m, true);
    lp_box(m, V(0.25f, 0.47f, 0.75f), V(0.08f, 0.03f, 0.30f), B_ACCENT, true);
    lp_box(m, V(0.30f, 0.45f, -0.55f), V(0.10f, 0.03f, 0.20f), B_PLATE, true);
    lp_box(m, V(0.0f, 0.50f, 0.95f), V(0.12f, 0.04f, 0.18f), B_PLATE, false);
    lp_box(m, V(0.0f, 0.36f, -1.35f), V(0.08f, 0.03f, 0.05f), B_GLOW, false);   // "olho"
    lp_spike(m, V(0.18f, 0.48f, 1.05f), V(0.24f, 0.95f, 1.25f), 0.02f, 3, B_METAL, true);
    for (int i = 0; i < 4; i++) {
        lp_box(m, V(0.50f, -0.02f, -0.3f + 0.25f * (float)i), V(0.02f, 0.05f, 0.08f), B_GLOW, true);   // respiros
    }
    lowpoly_detail(m, false);

    // Capacitor: um cilindro com bobinas acesas.
    m = &capacitor_model;
    lowpoly_clear(m);
    palette(m);
    lp_tube(m, V(0.0f, 0.0f, -0.20f), V(0.0f, 0.0f, 0.20f), 0.18f, 0.18f, 8, B_DARK, B_METAL, false);
    for (int i = 0; i < 3; i++) {
        lp_ring(m, V(0.0f, 0.0f, -0.12f + 0.12f * (float)i), AZ, 0.18f, 0.22f, 0.05f, 8, B_GLOW, B_GLOW, false);
    }
    lp_spike(m, V(0.0f, 0.18f, 0.0f), V(0.0f, 0.36f, 0.0f), 0.04f, 4, B_GLOW, false);

    // Núcleo: uma esfera facetada acesa.
    m = &core_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing ball[] = { { -0.22f, 0.0f, 0.0f, 0.0f }, { -0.15f, 0.16f, 0.16f, 0.0f }, { 0.0f, 0.22f, 0.22f, 0.0f },
                            { 0.15f, 0.16f, 0.16f, 0.0f }, { 0.22f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, ball, 5, 8, 0.0f, B_GLOW, B_GLOW, -1, 0.0f, false);

    // Pétala da blindagem do núcleo: base na borda do encaixe (na origem),
    // ponta sobre o centro. Quatro delas fecham o núcleo; abertas, giram para fora.
    m = &petal_model;
    lowpoly_clear(m);
    palette(m);
    lp_plate(m, V(-0.17f, 0.0f, 0.0f), V(-0.02f, 0.28f, -0.26f), V(0.02f, 0.28f, -0.26f), V(0.17f, 0.0f, 0.0f),
             V(0.0f, 0.7f, 0.7f), 0.04f, 0.02f, B_PLATE, B_DARK, B_ACCENT, false);
    built = true;
}

// --- desenho ------------------------------------------------------------------

#define BOSS_AMBIENT  0.34f
#define BOSS_SHADOW   0.2f
#define BOSS_SPECULAR 0.45f
#define BOSS_SHINE    20.0f
#define DETAIL_DISTANCE 260.0f
#define PETAL_OPEN    1.25f   // radianos que cada pétala abre no atordoamento
#define TRACE_STEPS   48      // segmentos do traço do corte
#define TRACE_LIFT    0.9f
#define TRACE_WIDTH   2.6f

static const Color RIM_COLOR    = { 255, 90, 60, 255 };
static const Color CORE_CLOSED  = { 150, 40, 40, 255 };
static const Color CORE_OPEN    = { 255, 110, 230, 255 };
static const Color CAP_IDLE     = { 70, 70, 90, 255 };
static const Color CAP_ARMED    = { 255, 210, 80, 255 };
static const Color CAP_DEAD     = { 30, 24, 24, 255 };
static const Color LASER_OUTER  = { 255, 60, 40, 255 };
static const Color LASER_INNER  = { 255, 210, 160, 255 };
static const Color TRACE_COLOR  = { 255, 50, 40, 255 };
static const Color TRACE_HOT    = { 255, 190, 90, 255 };
static const Color ABILITY_COLOR = { 120, 220, 255, 255 };
static const Color DYING_FIRE   = { 255, 140, 50, 255 };

static Color faded(Color c, float a)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(a, 0.0f, 1.0f) * 255.0f) };
}

static Color mix(Color a, Color b, float u)
{
    u = Clamp(u, 0.0f, 1.0f);
    return (Color){ (unsigned char)Lerp((float)a.r, (float)b.r, u), (unsigned char)Lerp((float)a.g, (float)b.g, u),
                    (unsigned char)Lerp((float)a.b, (float)b.b, u), 255 };
}

typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      scale;
} BossPose;

static BossPose boss_pose(const Boss *b, const Tuning *t, float alpha)
{
    (void)t;
    BossPose p = { Vector3Lerp(b->pos_prev, b->pos, alpha), QuaternionSlerp(b->rot_prev, b->rot, alpha), b->scale };
    return p;
}

static Vector3 at(const BossPose *p, Vector3 local)
{
    return Vector3Add(p->pos, Vector3RotateByQuaternion(Vector3Scale(local, p->scale), p->rot));
}

static LowPolyPose base_pose(const World *w, const Tuning *t, Vector3 pos, Quaternion rot, float scale, Camera3D camera)
{
    Vector3 sun = tuning_sun_direction(t);
    LowPolyPose pose = lowpoly_pose(pos, rot, scale, sun, camera.position);
    pose.sunlight = lowpoly_sunlight(pos, sun, w->planet.params.radius, BOSS_SHADOW);
    pose.ambient = BOSS_AMBIENT;
    pose.wrap = 0.35f;
    pose.specular = BOSS_SPECULAR;
    pose.shininess = BOSS_SHINE;
    pose.rim = 0.3f;
    pose.rim_color = RIM_COLOR;
    pose.bounce = 0.2f;
    pose.bounce_color = (Color){ 110, 160, 230, 255 };
    return pose;
}

void boss_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    if (!boss_active(b)) return;
    if (b->type == BOSS_HIVE) {
        hive_draw(w, t, alpha, glow, camera);
        return;
    }
    if (b->type == BOSS_WORM) {
        worm_draw(w, t, alpha, glow, camera);
        return;
    }
    if (!built) build_hull();
    float time = (float)w->tick / (float)t->sim_hz;
    BossPose p = boss_pose(b, t, alpha);
    bool charging = b->state == BOSS_FIGHT && b->action == BREAKER_CHARGE;
    bool staggered = b->state == BOSS_FIGHT && b->action == BREAKER_STAGGER;
    float pulse = 0.5f + 0.5f * sinf(time * (charging ? 10.0f : 3.0f));

    LowPolyPose pose = base_pose(w, t, p.pos, p.rot, p.scale, camera);
    pose.glow = charging ? mix(RIM_COLOR, CAP_ARMED, pulse) : RIM_COLOR;
    pose.whiten = Clamp(b->flash / 0.08f, 0.0f, 1.0f) * 0.35f;
    pose.far = Vector3Distance(p.pos, camera.position) > DETAIL_DISTANCE;
    bool dying = b->state == BOSS_DYING;
    if (dying) {
        // Morrendo: o casco escurece, queimado, e as bordas tremem em brasa.
        float flicker = 0.5f + 0.5f * sinf(time * 23.0f);
        pose.tint = (Color){ 120, 100, 92, 255 };
        pose.glow = mix(RIM_COLOR, DYING_FIRE, flicker);
        pose.whiten = 0.1f * flicker;
    }
    lowpoly_draw(&hull_model, &pose);

    // Capacitores: apagados em repouso, acesos e pulsando na carga, escuros
    // depois de destruídos.
    for (int k = 0; k < BOSS_CAPACITORS; k++) {
        LowPolyPose cp = base_pose(w, t, at(&p, BREAKER_CAPACITOR[k]), p.rot, p.scale, camera);
        bool dead = charging && b->breaker.cap_hp[k] <= 0.0f;
        cp.glow = dead ? CAP_DEAD : charging ? mix(CAP_ARMED, WHITE, pulse * 0.4f) : CAP_IDLE;
        cp.whiten = Clamp(b->breaker.cap_flash[k] / 0.12f, 0.0f, 1.0f) * 0.7f;
        if (dead) cp.tint = (Color){ 90, 80, 80, 255 };
        lowpoly_draw(&capacitor_model, &cp);
    }

    // Núcleo e a blindagem em pétalas: fechada, meio aberta na fase 3, toda
    // aberta no atordoamento.
    float open = staggered ? 1.0f : (b->phase >= 3 ? 0.45f : 0.0f);
    Vector3 core = at(&p, BREAKER_CORE);
    LowPolyPose kp = base_pose(w, t, core, p.rot, p.scale, camera);
    kp.glow = mix(CORE_CLOSED, CORE_OPEN, open * (0.7f + 0.3f * pulse));
    kp.whiten = Clamp(b->core_flash / 0.12f, 0.0f, 1.0f) * 0.8f;
    lowpoly_draw(&core_model, &kp);
    for (int k = 0; k < 4; k++) {
        Quaternion around = QuaternionFromAxisAngle((Vector3){ 0.0f, 1.0f, 0.0f }, (float)k * PI * 0.5f);
        Vector3 base = Vector3RotateByQuaternion(V(0.0f, 0.0f, 0.27f), around);
        Quaternion tilt = QuaternionFromAxisAngle((Vector3){ 1.0f, 0.0f, 0.0f }, open * PETAL_OPEN);
        Quaternion local = QuaternionMultiply(around, tilt);
        LowPolyPose pp = base_pose(w, t, at(&p, Vector3Add(BREAKER_CORE, Vector3Add(base, V(0.0f, 0.02f, 0.0f)))),
                                   QuaternionMultiply(p.rot, local), p.scale, camera);
        pp.whiten = pose.whiten;
        lowpoly_draw(&petal_model, &pp);
    }

    // Brilhos: motores, olho, boca do canhão carregando, núcleo aberto.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = -1; i <= 1; i++) {
        DrawBillboard(camera, glow, at(&p, V((float)i * 0.24f, 0.15f, 1.65f)), p.scale * 0.35f, faded(RIM_COLOR, 0.7f));
    }
    if (charging) {
        float charge = 1.0f - b->timer / fmaxf(t->breaker_charge[b->phase - 1], 1e-3f);
        DrawBillboard(camera, glow, at(&p, BREAKER_MUZZLE), p.scale * (0.3f + 0.9f * charge), faded(LASER_OUTER, 0.5f + 0.5f * pulse));
        DrawBillboard(camera, glow, at(&p, BREAKER_MUZZLE), p.scale * 0.25f * (0.5f + charge), faded(WHITE, 0.8f));
        for (int k = 0; k < BOSS_CAPACITORS; k++) {
            if (b->breaker.cap_hp[k] <= 0.0f) continue;
            DrawBillboard(camera, glow, at(&p, BREAKER_CAPACITOR[k]), p.scale * 0.55f * (0.7f + 0.3f * pulse), faded(CAP_ARMED, 0.6f));
        }
    }
    if (open > 0.0f) {
        DrawBillboard(camera, glow, core, p.scale * (0.6f + 0.4f * pulse) * open, faded(CORE_OPEN, 0.7f));
    }
    if (dying) {
        // Fogo saindo do casco em vários pontos, cada um piscando no seu ritmo.
        for (int k = 0; k < BOSS_HULL_SPHERES; k++) {
            const BossSphere *s = &BREAKER_HULL[k];
            float u = 0.5f + 0.5f * sinf(time * (9.0f + (float)k * 2.3f) + (float)k);
            Vector3 spot = Vector3Add(s->center, V(sinf((float)k * 2.7f) * s->radius * 0.8f, s->radius * 0.6f,
                                                   cosf((float)k * 1.9f) * s->radius * 0.5f));
            DrawBillboard(camera, glow, at(&p, spot), p.scale * s->radius * (0.8f + 0.7f * u), faded(DYING_FIRE, 0.5f + 0.4f * u));
        }
    }
    if (staggered) {
        // Faíscas pelo casco: ele está avariado.
        for (int k = 0; k < 6; k++) {
            float u = fmodf(time * 3.7f + (float)k * 0.37f, 1.0f);
            if (u > 0.25f) continue;
            const BossSphere *s = &BREAKER_HULL[(k * 3 + (int)(time * 2.0f)) % BOSS_HULL_SPHERES];
            Vector3 spot = Vector3Add(s->center, V(sinf((float)k * 2.1f) * s->radius, 0.1f, cosf((float)k * 1.3f) * s->radius));
            DrawBillboard(camera, glow, at(&p, spot), p.scale * 0.25f * (1.0f - u * 4.0f), faded(CAP_ARMED, 0.9f));
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}

// --- efeitos --------------------------------------------------------------------

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

// Ponto do solo (como era no começo da carga) na fração `u` do corte, um pouco
// acima dele.
static Vector3 ground(const Boss *b, float u)
{
    Vector3 p = boss_cut_point(b, u);
    return Vector3Add(p, Vector3Scale(boss_cut_direction(b, u), TRACE_LIFT));
}

// Faixa deitada no solo ao longo do corte, de `u0` a `u1`.
static void trace(const Boss *b, float u0, float u1, float width, Color color, Vector3 eye)
{
    if (u1 <= u0) return;
    int steps = (int)ceilf((u1 - u0) * (float)TRACE_STEPS);
    if (steps < 1) steps = 1;
    Vector3 prev = ground(b, u0);
    for (int i = 1; i <= steps; i++) {
        float u = u0 + (u1 - u0) * (float)i / (float)steps;
        Vector3 dir = boss_cut_direction(b, u);
        Vector3 next = ground(b, u);
        // Só do lado voltado para a câmera.
        float facing = Vector3DotProduct(dir, Vector3Normalize(Vector3Subtract(eye, next)));
        float fade = Clamp(facing * 4.0f, 0.0f, 1.0f);
        if (fade > 0.0f) {
            Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(Vector3Subtract(next, prev), dir)), width * 0.5f);
            Color c = faded(color, (float)color.a / 255.0f * fade);
            rlColor4ub(c.r, c.g, c.b, c.a);
            rlTexCoord2f(0.5f, 0.5f);
            rlVertex3f(prev.x - side.x, prev.y - side.y, prev.z - side.z);
            rlVertex3f(prev.x + side.x, prev.y + side.y, prev.z + side.z);
            rlVertex3f(next.x + side.x, next.y + side.y, next.z + side.z);
            rlVertex3f(next.x - side.x, next.y - side.y, next.z - side.z);
        }
        prev = next;
    }
}

void boss_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const Ability *ab = &w->ability;
    float time = (float)w->tick / (float)t->sim_hz;
    Vector3 eye = camera.position;
    hive_effects_draw(w, t, alpha, glow, camera);
    worm_effects_draw(w, t, alpha, glow, camera);
    bool fighting = boss_active(b) && b->state == BOSS_FIGHT && b->type == BOSS_BREAKER;
    bool charging = fighting && b->action == BREAKER_CHARGE;
    bool firing = fighting && b->action == BREAKER_FIRE;
    if (!charging && !firing && ab->beam_left <= 0.0f) return;
    if (!built) build_hull();

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // O traço do corte no chão: por cima do relevo (sem teste de profundidade),
    // com transparência comum (somando luz, o vermelho sobre o mar ficaria rosa).
    if (charging || firing) {
        // A faixa larga tem a largura da vala que o laser vai abrir: o jogador
        // vê exatamente o que vai sumir.
        float r = t->breaker_cut_radius, sink = fminf(t->breaker_cut_sink, r * 0.9f);
        float trench = fmaxf(2.0f * sqrtf(r * r - sink * sink), TRACE_WIDTH);
        rlDisableDepthTest();
        rlSetTexture(glow.id);
        rlBegin(RL_QUADS);
        if (charging) {
            float charge = 1.0f - b->timer / fmaxf(t->breaker_charge[b->phase - 1], 1e-3f);
            float blink = 0.55f + 0.45f * sinf(time * (6.0f + 14.0f * charge));
            trace(b, 0.0f, 1.0f, trench, faded(TRACE_COLOR, 0.22f + 0.12f * blink), eye);
            trace(b, 0.0f, 1.0f, fmaxf(trench * 0.16f, TRACE_WIDTH * 0.5f), faded(TRACE_COLOR, 0.6f + 0.4f * blink), eye);
            trace(b, 0.0f, charge, fmaxf(trench * 0.3f, TRACE_WIDTH), faded(TRACE_HOT, 0.8f), eye);   // a carga enchendo o traço
        } else {
            // Disparando: o que já foi cortado é a própria vala (com o manto à
            // mostra); só o que falta continua marcado.
            trace(b, b->breaker.cut_done, 1.0f, trench, faded(TRACE_COLOR, 0.2f), eye);
            trace(b, b->breaker.cut_done, 1.0f, fmaxf(trench * 0.16f, TRACE_WIDTH * 0.5f), faded(TRACE_COLOR, 0.75f), eye);
        }
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    if (firing) {
        BossPose p = boss_pose(b, t, alpha);
        Vector3 muzzle = at(&p, BREAKER_MUZZLE);
        float flicker = 0.85f + 0.15f * sinf(time * 47.0f);
        streak(muzzle, b->breaker.beam_point, t->breaker_beam_width * 3.4f, faded(LASER_OUTER, 0.35f * flicker), eye);
        streak(muzzle, b->breaker.beam_point, t->breaker_beam_width * 1.4f, faded(LASER_OUTER, 0.8f * flicker), eye);
        streak(muzzle, b->breaker.beam_point, t->breaker_beam_width * 0.45f, faded(LASER_INNER, flicker), eye);
    }
    if (ab->beam_left > 0.0f) {
        float fade = Clamp(ab->beam_left / 0.45f, 0.0f, 1.0f);
        streak(ab->beam_from, ab->beam_to, t->ability_width * 3.0f * fade, faded(ABILITY_COLOR, 0.4f * fade), eye);
        streak(ab->beam_from, ab->beam_to, t->ability_width * 1.0f * fade, faded(ABILITY_COLOR, 0.9f * fade), eye);
        streak(ab->beam_from, ab->beam_to, t->ability_width * 0.3f * fade, faded(WHITE, fade), eye);
    }
    rlEnd();
    rlSetTexture(0);
    if (firing) {
        float flicker = 0.8f + 0.2f * sinf(time * 33.0f);
        DrawBillboard(camera, glow, b->breaker.beam_point, 14.0f * flicker, faded(LASER_OUTER, 0.8f));
        DrawBillboard(camera, glow, b->breaker.beam_point, 6.0f, faded(WHITE, 0.9f));
        BossPose p = boss_pose(b, t, alpha);
        DrawBillboard(camera, glow, at(&p, BREAKER_MUZZLE), p.scale * 1.2f * flicker, faded(LASER_INNER, 0.9f));
    }
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
