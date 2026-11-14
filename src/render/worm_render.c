#include "raylib.h"
#include "rlgl.h"

#include "render/worm_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelos (em raios do segmento; frente -Z, topo +Y) ------------------------------

enum { W_PLATE, W_DARK, W_GLOW, W_BONE, W_FLESH };

static LowPoly head_model, segment_model, jaw_model, nodule_model, stinger_model;
static bool    built;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void palette(LowPoly *m)
{
    lowpoly_part(m, W_PLATE, (Color){ 92, 76, 70, 255 }, false);
    lowpoly_part(m, W_DARK, (Color){ 40, 30, 30, 255 }, false);
    lowpoly_part_glow(m, W_GLOW, (Color){ 255, 120, 40, 255 });
    lowpoly_part(m, W_BONE, (Color){ 196, 180, 160, 255 }, false);
    lowpoly_part(m, W_FLESH, (Color){ 140, 50, 50, 255 }, false);
}

static void build(void)
{
    // Segmento: um anel blindado e grosso, com uma emenda em brasa atrás.
    LowPoly *m = &segment_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing ring[] = { { -0.7f, 0.78f, 0.74f, 0.0f }, { -0.35f, 1.0f, 0.96f, 0.0f }, { 0.25f, 1.0f, 0.96f, 0.0f },
                            { 0.7f, 0.74f, 0.7f, 0.0f } };
    lp_loft(m, ring, 4, 9, 0.0f, W_PLATE, W_DARK, -1, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, 0.72f), V(0.0f, 0.0f, 1.0f), 0.45f, 0.74f, 0.08f, 9, W_GLOW, W_DARK, false);
    // Placas e espinhos no dorso.
    lp_plate(m, V(0.0f, 0.95f, -0.5f), V(0.0f, 1.35f, -0.15f), V(0.0f, 1.3f, 0.25f), V(0.0f, 0.95f, 0.45f), V(1.0f, 0.0f, 0.0f),
             0.14f, 0.06f, W_PLATE, W_PLATE, W_DARK, false);
    lp_spike(m, V(0.62f, 0.62f, 0.0f), V(1.2f, 0.9f, 0.25f), 0.12f, 4, W_BONE, true);

    // Cabeça: um crânio comprido, de olhos em brasa.
    m = &head_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing skull[] = { { -1.15f, 0.55f, 0.45f, 0.0f }, { -0.8f, 0.95f, 0.85f, 0.05f }, { -0.1f, 1.1f, 1.0f, 0.08f },
                             { 0.55f, 0.95f, 0.88f, 0.0f }, { 0.9f, 0.7f, 0.66f, 0.0f } };
    lp_loft(m, skull, 5, 10, 0.0f, W_PLATE, W_DARK, -1, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, -1.12f), V(0.0f, 0.0f, 1.0f), 0.18f, 0.55f, 0.12f, 10, W_FLESH, W_DARK, false);   // a boca
    lp_box(m, V(0.42f, 0.55f, -0.72f), V(0.16f, 0.08f, 0.12f), W_GLOW, true);   // olhos
    lp_plate(m, V(0.0f, 1.05f, -0.7f), V(0.0f, 1.6f, -0.2f), V(0.0f, 1.5f, 0.4f), V(0.0f, 1.05f, 0.6f), V(1.0f, 0.0f, 0.0f),
             0.2f, 0.08f, W_PLATE, W_PLATE, W_GLOW, false);
    lp_spike(m, V(0.7f, 0.5f, 0.1f), V(1.5f, 1.1f, 0.6f), 0.16f, 4, W_BONE, true);

    // Mandíbula (dobradiça na origem, ponta para a frente): quatro em volta da boca.
    m = &jaw_model;
    lowpoly_clear(m);
    palette(m);
    lp_plate(m, V(-0.32f, 0.0f, 0.0f), V(-0.05f, 0.18f, -0.95f), V(0.05f, 0.18f, -0.95f), V(0.32f, 0.0f, 0.0f), V(0.0f, 1.0f, 0.0f),
             0.14f, 0.05f, W_BONE, W_DARK, W_FLESH, false);
    lp_spike(m, V(0.0f, 0.08f, -0.85f), V(0.0f, -0.25f, -1.1f), 0.07f, 3, W_BONE, false);

    // Nódulo: bolha em brasa.
    m = &nodule_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing ball[] = { { -1.0f, 0.0f, 0.0f, 0.0f }, { -0.6f, 0.8f, 0.8f, 0.0f }, { 0.0f, 1.0f, 1.0f, 0.0f },
                            { 0.6f, 0.8f, 0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, ball, 5, 8, 0.0f, W_GLOW, W_GLOW, -1, 0.0f, false);

    // Ferrão: espinho comprido, para trás (+Z).
    m = &stinger_model;
    lowpoly_clear(m);
    palette(m);
    lp_tube(m, V(0.0f, 0.0f, -0.6f), V(0.0f, 0.0f, 0.3f), 0.7f, 0.5f, 6, W_PLATE, W_DARK, false);
    lp_spike(m, V(0.0f, 0.0f, 0.3f), V(0.0f, 0.15f, 2.2f), 0.5f, 6, W_GLOW, false);
    built = true;
}

// --- desenho ------------------------------------------------------------------

#define JAW_OPEN 1.0f   // radianos que cada mandíbula abre no salto

static const Color MAGMA      = { 255, 120, 40, 255 };
static const Color MAGMA_HOT  = { 255, 220, 140, 255 };
static const Color CRACK      = { 255, 50, 30, 255 };
static const Color STINGER    = { 255, 90, 200, 255 };
static const Color DYING_FIRE = { 255, 140, 50, 255 };

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

static LowPolyPose base_pose(const World *w, const Tuning *t, Vector3 pos, Quaternion rot, float scale, Camera3D camera)
{
    Vector3 sun = tuning_sun_direction(t);
    LowPolyPose pose = lowpoly_pose(pos, rot, scale, sun, camera.position);
    pose.sunlight = lowpoly_sunlight(pos, sun, w->planet.params.radius, 0.2f);
    pose.ambient = 0.3f;
    pose.wrap = 0.35f;
    pose.specular = 0.3f;
    pose.shininess = 12.0f;
    pose.rim = 0.35f;
    pose.rim_color = MAGMA;
    pose.bounce = 0.2f;
    pose.bounce_color = (Color){ 255, 120, 60, 255 };   // reflexo do magma
    return pose;
}

static Vector3 seg_pos(const Boss *b, int k, float alpha)
{
    return Vector3Lerp(b->worm.seg_prev[k], b->worm.seg[k], alpha);
}

static Quaternion seg_rot(const Boss *b, int k, float alpha)
{
    Vector3 pos = seg_pos(b, k, alpha);
    Vector3 forward;
    if (k == 0) {
        forward = b->worm.heading;
    } else {
        forward = Vector3Subtract(seg_pos(b, k - 1, alpha), pos);
        if (Vector3LengthSqr(forward) < 1e-6f) forward = b->worm.heading;
    }
    return quat_look(Vector3Normalize(forward), Vector3Normalize(pos));
}

void worm_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const WormState *m = &b->worm;
    (void)glow;
    if (!built) build();
    float time = (float)w->tick / (float)t->sim_hz;
    bool dying = b->state == BOSS_DYING;
    bool breach = b->state == BOSS_FIGHT && b->action == WORM_BREACH;
    float flicker = 0.5f + 0.5f * sinf(time * 23.0f);

    for (int k = WORM_SEGMENTS - 1; k >= 0; k--) {
        Vector3 pos = seg_pos(b, k, alpha);
        Quaternion rot = seg_rot(b, k, alpha);
        float r = worm_segment_radius(t, k);
        LowPolyPose pose = base_pose(w, t, pos, rot, r, camera);
        float pulse = 0.5f + 0.5f * sinf(time * 4.0f - (float)k * 0.6f);   // a brasa corre pelo corpo
        pose.glow = mix(MAGMA, MAGMA_HOT, pulse * (breach ? 0.8f : 0.3f));
        pose.whiten = Clamp(b->flash / 0.08f, 0.0f, 1.0f) * 0.25f;
        if (dying) {
            pose.tint = (Color){ 110, 96, 90, 255 };
            pose.glow = mix(MAGMA, DYING_FIRE, flicker);
        }
        if (k == 0) {
            lowpoly_draw(&head_model, &pose);
            // Mandíbulas: abertas no salto.
            float open = breach ? 0.6f + 0.4f * sinf(time * 9.0f) : 0.05f;
            for (int j = 0; j < 4; j++) {
                Quaternion around = QuaternionFromAxisAngle(V(0.0f, 0.0f, 1.0f), (float)j * PI * 0.5f + PI * 0.25f);
                Vector3 hinge = Vector3RotateByQuaternion(V(0.0f, 0.62f, -1.0f), around);
                Quaternion tilt = QuaternionFromAxisAngle(V(1.0f, 0.0f, 0.0f), open * JAW_OPEN);
                LowPolyPose jp = base_pose(w, t, Vector3Add(pos, Vector3RotateByQuaternion(Vector3Scale(hinge, r), rot)),
                                           QuaternionMultiply(rot, QuaternionMultiply(around, tilt)), r * 1.15f, camera);
                jp.whiten = pose.whiten;
                if (dying) jp.tint = pose.tint;
                lowpoly_draw(&jaw_model, &jp);
            }
        } else if (k == WORM_SEGMENTS - 1) {
            LowPolyPose sp = pose;
            sp.glow = m->tail_open ? mix(STINGER, WHITE, 0.3f * pulse) : MAGMA;
            sp.whiten = Clamp(m->tail_flash / 0.12f, 0.0f, 1.0f) * 0.8f;
            lowpoly_draw(&stinger_model, &sp);
        } else {
            lowpoly_draw(&segment_model, &pose);
        }
    }

    // Nódulos do dorso.
    if (!dying) {
        for (int n = 0; n < WORM_NODULES; n++) {
            if (m->nodule_hp[n] <= 0.0f) continue;
            int k = WORM_NODULE_SEGMENT[n];
            Vector3 pos = Vector3Lerp(m->seg_prev[k], m->seg[k], alpha);
            Vector3 at = Vector3Add(pos, Vector3Scale(Vector3Normalize(pos), worm_segment_radius(t, k) * 0.8f));
            LowPolyPose np = base_pose(w, t, at, seg_rot(b, k, alpha), worm_segment_radius(t, k) * 0.42f, camera);
            float beat = 0.5f + 0.5f * sinf(time * 7.0f + (float)n);
            np.glow = mix(MAGMA, MAGMA_HOT, m->nodule_open[n] ? beat : 0.0f);
            np.whiten = Clamp(m->nodule_flash[n] / 0.12f, 0.0f, 1.0f) * 0.8f;
            lowpoly_draw(&nodule_model, &np);
        }
    }
}

// --- efeitos --------------------------------------------------------------------

#define CRACK_SIDES 28
#define CRACK_LIFT  0.8f

// A rachadura no chão onde ele vai sair: um anel serrilhado, piscando cada
// vez mais rápido, com o centro em brasa.
static void crack_ring(Vector3 center, float radius, float width, Color color)
{
    Vector3 up = Vector3Normalize(center);
    Vector3 a = Vector3Perpendicular(up);
    Vector3 b = Vector3CrossProduct(up, a);
    Vector3 lift = Vector3Scale(up, CRACK_LIFT);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < CRACK_SIDES; i++) {
        float t0 = (float)i / (float)CRACK_SIDES * 2.0f * PI, t1 = (float)(i + 1) / (float)CRACK_SIDES * 2.0f * PI;
        float r0 = radius * (1.0f + 0.18f * sinf(t0 * 7.0f)), r1 = radius * (1.0f + 0.18f * sinf(t1 * 7.0f));
        Vector3 d0 = Vector3Add(Vector3Scale(a, cosf(t0)), Vector3Scale(b, sinf(t0)));
        Vector3 d1 = Vector3Add(Vector3Scale(a, cosf(t1)), Vector3Scale(b, sinf(t1)));
        Vector3 p0 = Vector3Add(Vector3Add(center, lift), Vector3Scale(d0, r0 - width * 0.5f));
        Vector3 p1 = Vector3Add(Vector3Add(center, lift), Vector3Scale(d0, r0 + width * 0.5f));
        Vector3 p2 = Vector3Add(Vector3Add(center, lift), Vector3Scale(d1, r1 + width * 0.5f));
        Vector3 p3 = Vector3Add(Vector3Add(center, lift), Vector3Scale(d1, r1 - width * 0.5f));
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }
}

void worm_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const WormState *m = &b->worm;
    if (!boss_active(b) || b->type != BOSS_WORM) return;
    float time = (float)w->tick / (float)t->sim_hz;
    bool fighting = b->state == BOSS_FIGHT;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();

    // O aviso: a rachadura no chão (por cima do relevo, sem teste de profundidade).
    if (fighting && b->action == WORM_RUMBLE) {
        Terrain ground = planet_terrain(&w->planet.params, m->breach_dir);
        Vector3 at = Vector3Scale(m->breach_dir, w->planet.params.radius + (ground.land ? ground.elevation : 0.0f));
        float blink = 0.55f + 0.45f * sinf(time * (10.0f + 30.0f * (1.0f - b->timer / fmaxf(t->worm_rumble_time, 1e-3f))));
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        rlSetTexture(glow.id);
        rlBegin(RL_QUADS);
        crack_ring(at, t->worm_breach_radius, 2.4f, faded(CRACK, 0.75f * blink));
        crack_ring(at, t->worm_breach_radius * 0.55f, 1.4f, faded(CRACK, 0.5f * blink));
        crack_ring(at, t->worm_breach_blast, 0.8f, faded(CRACK, 0.35f));   // o alcance da erupção
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthTest();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawBillboard(camera, glow, at, t->worm_breach_radius * 1.6f * blink, faded(MAGMA, 0.6f));
        EndBlendMode();
    }

    BeginBlendMode(BLEND_ADDITIVE);
    if (b->state == BOSS_DYING) {
        for (int k = 0; k < WORM_SEGMENTS; k += 2) {
            float u = 0.5f + 0.5f * sinf(time * (9.0f + (float)k * 1.7f) + (float)k);
            DrawBillboard(camera, glow, Vector3Lerp(m->seg_prev[k], m->seg[k], alpha), worm_segment_radius(t, k) * (1.0f + u),
                          faded(DYING_FIRE, 0.5f + 0.4f * u));
        }
    } else {
        // A boca aberta brilha por dentro.
        if (fighting && b->action == WORM_BREACH) {
            Vector3 mouth = Vector3Add(Vector3Lerp(m->seg_prev[0], m->seg[0], alpha), Vector3Scale(m->heading, t->worm_scale * 0.9f));
            float pulse = 0.7f + 0.3f * sinf(time * 13.0f);
            DrawBillboard(camera, glow, mouth, t->worm_scale * 2.4f * pulse, faded((Color){ 255, 110, 230, 255 }, 0.7f));
        }
        for (int n = 0; n < WORM_NODULES; n++) {
            if (m->nodule_hp[n] <= 0.0f || !m->nodule_open[n]) continue;
            int k = WORM_NODULE_SEGMENT[n];
            Vector3 pos = Vector3Lerp(m->seg_prev[k], m->seg[k], alpha);
            Vector3 at = Vector3Add(pos, Vector3Scale(Vector3Normalize(pos), worm_segment_radius(t, k) * 0.8f));
            DrawBillboard(camera, glow, at, worm_segment_radius(t, k) * 1.8f, faded(MAGMA, 0.45f));
        }
        if (m->tail_open) {
            int last = WORM_SEGMENTS - 1;
            DrawBillboard(camera, glow, Vector3Lerp(m->seg_prev[last], m->seg[last], alpha), worm_segment_radius(t, last) * 2.5f,
                          faded(STINGER, 0.5f + 0.3f * sinf(time * 9.0f)));
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}
