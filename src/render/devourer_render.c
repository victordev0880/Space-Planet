#include "raylib.h"
#include "rlgl.h"

#include "render/devourer_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelos (em escalas do Devorador; frente -Z = a boca, topo +Y) -------------------

enum { D_SKIN, D_DARK, D_GLOW, D_TOOTH, D_GUM, D_EYE };

static LowPoly body_model, upper_model, lower_model, eye_model;
static bool    built;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void palette(LowPoly *m)
{
    lowpoly_part(m, D_SKIN, (Color){ 74, 50, 96, 255 }, false);
    lowpoly_part(m, D_DARK, (Color){ 30, 20, 40, 255 }, false);
    lowpoly_part_glow(m, D_GLOW, (Color){ 255, 70, 120, 255 });
    lowpoly_part(m, D_TOOTH, (Color){ 236, 226, 204, 255 }, false);
    // A carne de dentro da boca brilha sozinha: com a boca aberta, de qualquer
    // ângulo, se vê o vermelho lá dentro.
    lowpoly_part(m, D_GUM, (Color){ 190, 36, 70, 255 }, true);
    lowpoly_part(m, D_EYE, (Color){ 255, 230, 120, 255 }, true);
}

// Mandíbula com a dobradiça na origem e a ponta para a frente; `sign` = +1 a
// de cima (dentes para baixo), -1 a de baixo. Uma concha: casca por fora,
// gengiva por dentro, dentes em U na borda.
static void build_jaw(LowPoly *m, float sign)
{
    lowpoly_clear(m);
    palette(m);
    int outside = (sign > 0.0f) ? D_SKIN : D_GUM;
    int inside = (sign > 0.0f) ? D_GUM : D_SKIN;
    const LpRing shell[] = { { 0.15f, 0.62f, 0.15f, 0.0f },          { -0.3f, 0.68f, 0.17f, 0.03f * sign },
                             { -0.7f, 0.52f, 0.13f, 0.0f },          { -1.0f, 0.2f, 0.06f, -0.05f * sign } };
    lp_loft(m, shell, 4, 10, 0.0f, outside, inside, D_DARK, 0.0f, false);
    // Uma crista de osso por cima da concha.
    lp_plate(m, V(0.0f, 0.14f * sign, 0.1f), V(0.0f, 0.12f * sign, -0.8f), V(0.0f, 0.22f * sign, -0.55f),
             V(0.0f, 0.3f * sign, 0.05f), V(1.0f, 0.0f, 0.0f), 0.08f, 0.03f, D_TOOTH, D_TOOTH, D_DARK, false);
    // Dentes em U pela borda, apontando para a outra mandíbula; os da frente
    // são os maiores.
    const float rim[][3] = { { 0.55f, -0.12f, 0.2f }, { 0.6f, -0.38f, 0.24f }, { 0.52f, -0.62f, 0.28f },
                             { 0.36f, -0.82f, 0.32f }, { 0.13f, -0.95f, 0.36f } };
    for (int i = 0; i < 5; i++) {
        float x = rim[i][0], z = rim[i][1], len = rim[i][2];
        lp_spike(m, V(x, -0.08f * sign, z), V(x * 0.85f, -(0.08f + len) * sign, z - 0.04f), 0.07f, 4, D_TOOTH, true);
    }
}

static void build(void)
{
    LowPoly *m = &body_model;
    lowpoly_clear(m);
    palette(m);
    // Corpo: uma massa larga e achatada, mais grossa atrás.
    const LpRing body[] = { { -0.55f, 0.55f, 0.45f, 0.0f }, { -0.2f, 0.9f, 0.75f, 0.05f }, { 0.25f, 0.95f, 0.85f, 0.1f },
                            { 0.7f, 0.75f, 0.7f, 0.15f },  { 1.1f, 0.35f, 0.35f, 0.15f } };
    lp_loft(m, body, 5, 12, 0.0f, D_SKIN, D_DARK, -1, 0.0f, false);
    // Anéis de couraça: o corpo em segmentos, como uma larva.
    const float collar[][3] = { { -0.32f, 0.82f, 0.04f }, { 0.05f, 0.95f, 0.07f }, { 0.45f, 0.9f, 0.12f }, { 0.85f, 0.62f, 0.15f } };
    for (int k = 0; k < 4; k++) {
        lp_ring(m, V(0.0f, collar[k][2], collar[k][0]), V(0.0f, 0.0f, 1.0f), collar[k][1] * 0.92f, collar[k][1] * 1.04f, 0.1f, 12,
                D_DARK, D_DARK, false);
    }
    // Veias acesas pelo dorso.
    for (int k = 0; k < 5; k++) {
        float x = -0.5f + 0.25f * (float)k;
        lp_tube(m, V(x, 0.78f, -0.15f), V(x * 1.2f, 0.7f, 0.55f), 0.035f, 0.02f, 4, D_GLOW, -1, false);
    }
    // Espinhos na corcova, curvados para trás, e tentáculos arrastando atrás.
    for (int k = 0; k < 4; k++) {
        float x = -0.36f + 0.24f * (float)k;
        lp_spike(m, V(x, 0.8f, 0.3f + 0.1f * (float)(k % 2)), V(x * 1.3f, 1.5f, 0.85f), 0.12f, 4, D_TOOTH, false);
    }
    for (int k = 0; k < 3; k++) {
        float x = -0.4f + 0.4f * (float)k;
        lp_tube(m, V(x, -0.1f, 0.9f), V(x * 1.4f, -0.35f, 1.7f), 0.12f, 0.06f, 5, D_SKIN, -1, false);
        lp_tube(m, V(x * 1.4f, -0.35f, 1.7f), V(x * 1.8f, -0.2f, 2.3f), 0.06f, 0.02f, 4, D_GLOW, -1, false);
    }
    // Pinças de osso dos dois lados da boca: a silhueta de bocarra de longe.
    lp_tube(m, V(0.62f, 0.0f, -0.25f), V(0.95f, -0.05f, -0.8f), 0.13f, 0.08f, 5, D_SKIN, -1, true);
    lp_spike(m, V(0.95f, -0.05f, -0.8f), V(0.55f, -0.1f, -1.35f), 0.09f, 5, D_TOOTH, true);
    // Sobrancelhas de osso sobre os olhos.
    lp_spike(m, V(0.2f, 0.74f, -0.48f), V(0.72f, 0.92f, -0.7f), 0.07f, 4, D_TOOTH, true);
    // A goela: um anel de gengiva em volta da garganta, e o fundo escuro.
    lp_ring(m, V(0.0f, 0.0f, -0.62f), V(0.0f, 0.0f, 1.0f), 0.18f, 0.5f, 0.12f, 10, D_GUM, D_DARK, false);
    lp_tube(m, V(0.0f, 0.0f, -0.6f), V(0.0f, 0.0f, -0.2f), 0.2f, 0.04f, 8, D_DARK, -1, false);

    build_jaw(&upper_model, 1.0f);
    build_jaw(&lower_model, -1.0f);

    // Olho: bola acesa (a cor vem da pose) com pupila escura.
    m = &eye_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing ball[] = { { -1.0f, 0.0f, 0.0f, 0.0f }, { -0.6f, 0.8f, 0.8f, 0.0f }, { 0.0f, 1.0f, 1.0f, 0.0f },
                            { 0.6f, 0.8f, 0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, ball, 5, 8, 0.0f, D_GLOW, D_GLOW, -1, 0.0f, false);
    lp_box(m, V(0.0f, 0.0f, -0.95f), V(0.12f, 0.5f, 0.1f), D_DARK, false);
    built = true;
}

// --- desenho ------------------------------------------------------------------

static const Color VEIN       = { 255, 70, 120, 255 };
static const Color EYE_CALM   = { 255, 160, 60, 255 };
static const Color EYE_HOT    = { 255, 240, 140, 255 };
static const Color THROAT     = { 255, 110, 230, 255 };
static const Color SWIRL      = { 190, 120, 255, 255 };
static const Color TARGET     = { 255, 50, 40, 255 };
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
    pose.ambient = 0.28f;
    pose.wrap = 0.4f;
    pose.specular = 0.5f;
    pose.shininess = 16.0f;
    pose.rim = 0.5f;
    pose.rim_color = VEIN;
    pose.bounce = 0.2f;
    pose.bounce_color = (Color){ 110, 160, 230, 255 };
    return pose;
}

typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      scale;
} DevPose;

static Vector3 at(const DevPose *p, Vector3 local)
{
    return Vector3Add(p->pos, Vector3RotateByQuaternion(Vector3Scale(local, p->scale), p->rot));
}

void devourer_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const DevourerState *d = &b->dev;
    (void)glow;
    if (!built) build();
    float time = (float)w->tick / (float)t->sim_hz;
    DevPose p = { Vector3Lerp(b->pos_prev, b->pos, alpha), QuaternionSlerp(b->rot_prev, b->rot, alpha), b->scale };
    bool dying = b->state == BOSS_DYING;
    bool aiming = b->state == BOSS_FIGHT && (b->action == DEV_AIM || b->action == DEV_DIVE);
    float pulse = 0.5f + 0.5f * sinf(time * (b->phase >= DEV_FRENZY ? 7.0f : 3.0f));

    LowPolyPose pose = base_pose(w, t, p.pos, p.rot, p.scale, camera);
    pose.glow = mix(VEIN, WHITE, 0.2f * pulse);
    pose.whiten = Clamp(b->flash / 0.08f, 0.0f, 1.0f) * 0.25f;
    if (dying) {
        float flicker = 0.5f + 0.5f * sinf(time * 23.0f);
        pose.tint = (Color){ 120, 100, 100, 255 };
        pose.glow = mix(VEIN, DYING_FIRE, flicker);
    }
    lowpoly_draw(&body_model, &pose);

    // Mandíbulas, abertas conforme `jaw`.
    for (int k = 0; k < 2; k++) {
        float sign = (k == 0) ? 1.0f : -1.0f;
        Quaternion open = QuaternionFromAxisAngle(V(1.0f, 0.0f, 0.0f), sign * d->jaw * 0.9f);
        LowPolyPose jp = base_pose(w, t, at(&p, V(0.0f, 0.25f * sign, -0.35f)), QuaternionMultiply(p.rot, open), p.scale, camera);
        jp.whiten = pose.whiten;
        if (dying) jp.tint = pose.tint;
        lowpoly_draw((k == 0) ? &upper_model : &lower_model, &jp);
    }

    // Olhos: acesos e grandes na mira (o ponto fraco).
    for (int k = 0; k < 2; k++) {
        LowPolyPose ep = base_pose(w, t, at(&p, DEV_EYE[k]), p.rot, DEV_EYE_RADIUS * p.scale * (aiming ? 1.25f : 1.0f), camera);
        ep.glow = aiming ? mix(EYE_HOT, WHITE, 0.4f * pulse) : EYE_CALM;
        ep.whiten = Clamp(d->eye_flash[k] / 0.12f, 0.0f, 1.0f) * 0.9f;
        lowpoly_draw(&eye_model, &ep);
    }
}

// --- efeitos --------------------------------------------------------------------

#define RING_SIDES 40

// Anel deitado no chão, em volta de `center`.
static void ground_ring(Vector3 center, float radius, float width, Color color)
{
    Vector3 up = Vector3Normalize(center);
    Vector3 a = Vector3Perpendicular(up);
    Vector3 b = Vector3CrossProduct(up, a);
    Vector3 lift = Vector3Add(center, Vector3Scale(up, 0.9f));
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < RING_SIDES; i++) {
        float t0 = (float)i / (float)RING_SIDES * 2.0f * PI, t1 = (float)(i + 1) / (float)RING_SIDES * 2.0f * PI;
        Vector3 d0 = Vector3Add(Vector3Scale(a, cosf(t0)), Vector3Scale(b, sinf(t0)));
        Vector3 d1 = Vector3Add(Vector3Scale(a, cosf(t1)), Vector3Scale(b, sinf(t1)));
        Vector3 p0 = Vector3Add(lift, Vector3Scale(d0, radius - width * 0.5f));
        Vector3 p1 = Vector3Add(lift, Vector3Scale(d0, radius + width * 0.5f));
        Vector3 p2 = Vector3Add(lift, Vector3Scale(d1, radius + width * 0.5f));
        Vector3 p3 = Vector3Add(lift, Vector3Scale(d1, radius - width * 0.5f));
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }
}

void devourer_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const DevourerState *d = &b->dev;
    if (!boss_active(b) || b->type != BOSS_DEVOURER) return;
    float time = (float)w->tick / (float)t->sim_hz;
    DevPose p = { Vector3Lerp(b->pos_prev, b->pos, alpha), QuaternionSlerp(b->rot_prev, b->rot, alpha), b->scale };
    bool fighting = b->state == BOSS_FIGHT;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();

    // O alvo da mordida: anéis no chão fechando sobre o ponto.
    if (fighting && (b->action == DEV_AIM || b->action == DEV_DIVE)) {
        Terrain terrain = planet_terrain(&w->planet.params, d->bite_dir);
        Vector3 ground = Vector3Scale(d->bite_dir, w->planet.params.radius + (terrain.land ? terrain.elevation : 0.0f));
        float u = (b->action == DEV_AIM) ? 1.0f - b->timer / fmaxf(t->dev_aim_time, 1e-3f) : 1.0f;
        float blink = 0.6f + 0.4f * sinf(time * (8.0f + 20.0f * u));
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        rlSetTexture(glow.id);
        rlBegin(RL_QUADS);
        ground_ring(ground, t->dev_bite_radius, 2.6f, faded(TARGET, 0.8f * blink));
        ground_ring(ground, t->dev_bite_radius * (1.0f - 0.85f * u), 1.6f, faded(TARGET, 0.6f));   // fechando
        rlEnd();
        rlSetTexture(0);
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthTest();
    }

    BeginBlendMode(BLEND_ADDITIVE);
    if (b->state == BOSS_DYING) {
        for (int k = 0; k < 10; k++) {
            float u = 0.5f + 0.5f * sinf(time * (9.0f + (float)k * 2.0f) + (float)k);
            Vector3 spot = at(&p, V(sinf((float)k * 2.3f) * 0.7f, cosf((float)k * 1.7f) * 0.5f, sinf((float)k * 0.9f) * 0.6f));
            DrawBillboard(camera, glow, spot, p.scale * (0.3f + 0.3f * u), faded(DYING_FIRE, 0.5f + 0.4f * u));
        }
    } else {
        // O redemoinho: braços de luz girando em espiral até a boca.
        Vector3 mouth = at(&p, V(0.0f, 0.0f, -0.95f));
        Vector3 axis = Vector3RotateByQuaternion(V(0.0f, 0.0f, -1.0f), p.rot);
        Vector3 side = Vector3Perpendicular(axis);
        float range = fminf(t->dev_vortex_range, p.scale * 4.0f);
        for (int arm = 0; arm < 3; arm++) {
            for (int s = 0; s < 10; s++) {
                float u = fmodf((float)s / 10.0f + time * 0.35f, 1.0f);
                float angle = (float)arm * 2.0f * PI / 3.0f + u * 4.0f - time * 1.6f;
                Vector3 dir = Vector3RotateByAxisAngle(side, axis, angle);
                Vector3 spot = Vector3Add(mouth, Vector3Add(Vector3Scale(dir, range * (1.0f - u)), Vector3Scale(axis, p.scale * 0.6f * u)));
                DrawBillboard(camera, glow, spot, p.scale * (0.18f + 0.25f * u), faded(SWIRL, 0.12f + 0.3f * u));
            }
        }
        // A garganta aberta brilha; os olhos, na mira.
        if (fighting && boss_core_factor(b, t) >= 0.99f) {
            float pulse = 0.7f + 0.3f * sinf(time * 11.0f);
            DrawBillboard(camera, glow, at(&p, DEV_THROAT), p.scale * 0.9f * pulse, faded(THROAT, 0.75f));
        }
        if (fighting && (b->action == DEV_AIM || b->action == DEV_DIVE)) {
            for (int k = 0; k < 2; k++) DrawBillboard(camera, glow, at(&p, DEV_EYE[k]), p.scale * 0.6f, faded(EYE_HOT, 0.6f));
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}
