#include "raylib.h"
#include "rlgl.h"

#include "render/hive_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelos ------------------------------------------------------------------
// O corpo é montado em raios da Lua (frente -Z para o planeta); o olho e as
// pálpebras, em raios do coração; bolsa e propulsor, nos raios deles.

enum { H_FLESH, H_DARK, H_GLOW, H_BONE, H_EMBER };

static LowPoly body_model, eye_model, lid_model, sac_model, thruster_model;
static bool    built;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void palette(LowPoly *m)
{
    lowpoly_part(m, H_FLESH, (Color){ 74, 34, 82, 255 }, false);
    lowpoly_part(m, H_DARK, (Color){ 30, 14, 36, 255 }, false);
    lowpoly_part_glow(m, H_GLOW, (Color){ 220, 80, 255, 255 });
    lowpoly_part(m, H_BONE, (Color){ 170, 140, 150, 255 }, false);
    lowpoly_part(m, H_EMBER, (Color){ 255, 150, 60, 255 }, true);
}

// Direção a `theta` radianos do eixo -Z (a frente), girada `phi` em volta dele.
static Vector3 around_front(float theta, float phi)
{
    return V(sinf(theta) * cosf(phi), sinf(theta) * sinf(phi), -cosf(theta));
}

static void build(void)
{
    LowPoly *m = &body_model;
    lowpoly_clear(m);
    palette(m);

    // Seis costelas saindo da moldura do coração e abraçando a Lua até as costas.
    for (int r = 0; r < 6; r++) {
        float phi = (float)r * PI / 3.0f + 0.2f;
        for (int s = 0; s < 8; s++) {
            float u0 = (float)s / 8.0f, u1 = (float)(s + 1) / 8.0f;
            float t0 = Lerp(0.34f, 2.75f, u0), t1 = Lerp(0.34f, 2.75f, u1);
            float w0 = 0.16f * sinf(t0 * 2.0f + (float)r), w1 = 0.16f * sinf(t1 * 2.0f + (float)r);
            Vector3 a = Vector3Scale(around_front(t0, phi + w0), 1.02f + 0.04f * sinf(t0 * 3.0f + (float)r));
            Vector3 b = Vector3Scale(around_front(t1, phi + w1), 1.02f + 0.04f * sinf(t1 * 3.0f + (float)r));
            lp_tube(m, a, b, Lerp(0.11f, 0.035f, u0), Lerp(0.11f, 0.035f, u1), 5, (s % 3 == 1) ? H_GLOW : H_FLESH, -1, false);
        }
        // Espinhos de osso entre as costelas, em volta da moldura.
        float between = phi + PI / 6.0f;
        lp_spike(m, Vector3Scale(around_front(0.52f, between), 0.98f), Vector3Scale(around_front(0.66f, between), 1.42f), 0.07f,
                 4, H_BONE, false);
    }
    // A moldura do coração.
    lp_ring(m, V(0.0f, 0.0f, -0.96f), V(0.0f, 0.0f, 1.0f), 0.29f, 0.50f, 0.16f, 10, H_FLESH, H_DARK, false);
    // Os pés das bolsas.
    for (int k = 0; k < HIVE_SACS; k++) {
        Vector3 d = Vector3Normalize(HIVE_SAC[k]);
        lp_tube(m, Vector3Scale(d, 0.95f), Vector3Scale(d, 1.05f), 0.13f, 0.10f, 5, H_FLESH, -1, false);
    }

    // O olho (raio 1): esfera acesa com uma pupila em fenda.
    m = &eye_model;
    lowpoly_clear(m);
    palette(m);
    const LpRing ball[] = { { -1.0f, 0.0f, 0.0f, 0.0f }, { -0.7f, 0.72f, 0.72f, 0.0f }, { 0.0f, 1.0f, 1.0f, 0.0f },
                            { 0.7f, 0.72f, 0.72f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, ball, 5, 10, 0.0f, H_GLOW, H_GLOW, -1, 0.0f, false);
    lp_box(m, V(0.0f, 0.0f, -0.93f), V(0.11f, 0.62f, 0.1f), H_DARK, false);

    // Pálpebra (em raios do coração), com a dobradiça na origem: seis delas
    // fecham o olho; abertas, giram para fora.
    m = &lid_model;
    lowpoly_clear(m);
    palette(m);
    lp_plate(m, V(-0.55f, 0.0f, 0.0f), V(-0.09f, -1.0f, -0.82f), V(0.09f, -1.0f, -0.82f), V(0.55f, 0.0f, 0.0f),
             V(0.0f, 0.6f, -0.8f), 0.1f, 0.04f, H_FLESH, H_DARK, H_GLOW, false);

    // Bolsa (raio 1): pele translúcida (iluminada) com veias acesas por cima.
    m = &sac_model;
    lowpoly_clear(m);
    palette(m);
    lowpoly_part(m, H_BONE, (Color){ 96, 150, 84, 255 }, false);   // aqui, a pele da bolsa
    lp_loft(m, ball, 5, 8, 0.0f, H_BONE, H_BONE, -1, 0.0f, false);
    for (int k = 0; k < 4; k++) {
        float a = (float)k * PI * 0.5f;
        lp_plate(m, V(cosf(a) * 1.0f, sinf(a) * 1.0f, -0.35f), V(cosf(a) * 0.74f, sinf(a) * 0.74f, -0.78f),
                 V(cosf(a) * 0.77f, sinf(a) * 0.77f, 0.72f), V(cosf(a) * 1.02f, sinf(a) * 1.02f, 0.28f), V(cosf(a), sinf(a), 0.0f),
                 0.07f, 0.03f, H_GLOW, H_GLOW, H_GLOW, false);
    }

    // Propulsor (raio 1): um cone de carne com a boca em brasa, para trás (+Z).
    m = &thruster_model;
    lowpoly_clear(m);
    palette(m);
    lp_tube(m, V(0.0f, 0.0f, -0.6f), V(0.0f, 0.0f, 0.7f), 0.95f, 0.6f, 7, H_FLESH, H_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.74f), V(0.0f, 0.0f, 1.0f), 0.28f, 0.62f, 0.1f, 7, H_EMBER, H_DARK, false);
    lp_spike(m, V(0.0f, 0.62f, 0.2f), V(0.0f, 1.25f, 0.65f), 0.12f, 4, H_BONE, false);
    built = true;
}

// --- desenho ------------------------------------------------------------------

#define LID_OPEN     1.2f    // radianos que cada pálpebra abre

static const Color FLESH_GLOW  = { 210, 70, 240, 255 };
static const Color CORE_SHUT   = { 150, 40, 120, 255 };
static const Color CORE_OPEN   = { 255, 110, 230, 255 };
static const Color SAC_GLOW    = { 140, 255, 110, 255 };
static const Color SAC_HOT     = { 255, 80, 60, 255 };
static const Color ACID        = { 150, 255, 90, 255 };
static const Color FLAME       = { 255, 140, 50, 255 };
static const Color DYING_FIRE  = { 255, 140, 50, 255 };
static const Color TENDRIL_OUT = { 150, 50, 190, 255 };
static const Color TENDRIL_IN  = { 170, 255, 120, 255 };

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
    pose.ambient = 0.32f;
    pose.wrap = 0.45f;
    pose.specular = 0.6f;     // carne molhada: brilha
    pose.shininess = 14.0f;
    pose.rim = 0.4f;
    pose.rim_color = FLESH_GLOW;
    pose.bounce = 0.15f;
    pose.bounce_color = (Color){ 110, 160, 230, 255 };
    return pose;
}

typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      scale;   // raio da Lua
    float      grow;    // 0 a 1: a Colmeia se abrindo em volta da Lua (na entrada)
} HivePose;

static HivePose hive_pose(const Boss *b, const Tuning *t, float alpha)
{
    HivePose p = { Vector3Lerp(b->pos_prev, b->pos, alpha), QuaternionSlerp(b->rot_prev, b->rot, alpha), b->scale, 1.0f };
    if (b->state == BOSS_ENTER) {
        float u = 1.0f - Clamp(b->timer / fmaxf(t->hive_enter_time, 1e-3f), 0.0f, 1.0f);
        p.grow = 0.35f + 0.65f * u * u;
    }
    return p;
}

static Vector3 at(const HivePose *p, Vector3 local)
{
    return Vector3Add(p->pos, Vector3RotateByQuaternion(Vector3Scale(local, p->scale), p->rot));
}

void hive_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const HiveState *h = &b->hive;
    (void)glow;
    if (!built) build();
    float time = (float)w->tick / (float)t->sim_hz;
    HivePose p = hive_pose(b, t, alpha);
    bool fighting = b->state == BOSS_FIGHT;
    bool dying = b->state == BOSS_DYING;
    bool open = fighting && b->action == HIVE_OPEN;
    float pulse = 0.5f + 0.5f * sinf(time * 3.0f);

    // Corpo.
    LowPolyPose pose = base_pose(w, t, p.pos, p.rot, p.scale * p.grow, camera);
    pose.glow = mix(FLESH_GLOW, WHITE, 0.15f * pulse);
    pose.whiten = Clamp(b->flash / 0.08f, 0.0f, 1.0f) * 0.25f;
    if (dying) {
        float flicker = 0.5f + 0.5f * sinf(time * 23.0f);
        pose.tint = (Color){ 120, 96, 96, 255 };
        pose.glow = mix(FLESH_GLOW, DYING_FIRE, flicker);
    }
    lowpoly_draw(&body_model, &pose);

    // O coração: olho aceso (aberto, brilha forte; preparando um cuspe, fica
    // verde) e as pálpebras.
    float core_r = HIVE_CORE_RADIUS * p.scale * p.grow;
    Vector3 core = at(&p, Vector3Scale(HIVE_CORE, p.grow));
    float lids = open ? 1.0f : (b->phase >= 3 && fighting) ? 0.4f : 0.0f;
    LowPolyPose ep = base_pose(w, t, core, p.rot, core_r, camera);
    Color iris = mix(CORE_SHUT, CORE_OPEN, lids * (0.7f + 0.3f * pulse));
    if (h->spit_charge > 0.0f) iris = mix(iris, ACID, 1.0f - h->spit_charge / fmaxf(t->hive_spit_warn, 1e-3f));
    ep.glow = iris;
    ep.whiten = Clamp(b->core_flash / 0.12f, 0.0f, 1.0f) * 0.8f;
    lowpoly_draw(&eye_model, &ep);
    for (int k = 0; k < 6; k++) {
        Quaternion around = QuaternionFromAxisAngle(V(0.0f, 0.0f, 1.0f), (float)k * PI / 3.0f);
        Quaternion tilt = QuaternionFromAxisAngle(V(1.0f, 0.0f, 0.0f), lids * LID_OPEN);
        Vector3 hinge = Vector3RotateByQuaternion(V(0.0f, 1.1f, -0.15f), around);
        Vector3 hinge_world = Vector3Add(core, Vector3RotateByQuaternion(Vector3Scale(hinge, core_r), p.rot));
        LowPolyPose lp = base_pose(w, t, hinge_world, QuaternionMultiply(p.rot, QuaternionMultiply(around, tilt)), core_r, camera);
        lp.glow = iris;
        lp.whiten = pose.whiten;
        if (dying) lp.tint = pose.tint;
        lowpoly_draw(&lid_model, &lp);
    }

    // Bolsas: incham e esquentam antes de soltar a ninhada.
    if (!dying) {
        for (int k = 0; k < HIVE_SACS; k++) {
            if (h->sac_hp[k] <= 0.0f || h->sac_grow[k] <= 0.02f) continue;
            float size = hive_sac_size(b, t, k);
            Vector3 pos = Vector3Lerp(p.pos, hive_sac_point(b, k), p.grow);
            LowPolyPose sp = base_pose(w, t, pos, p.rot, HIVE_SAC_RADIUS * p.scale * size * p.grow, camera);
            float heat = (h->inflating == k) ? 1.0f - h->inflate / fmaxf(t->hive_inflate_time, 1e-3f) : 0.0f;
            float beat = 0.5f + 0.5f * sinf(time * (4.0f + 18.0f * heat) + (float)k);
            sp.glow = mix(mix(SAC_GLOW, SAC_HOT, heat), WHITE, 0.25f * beat * (0.3f + heat));
            sp.whiten = Clamp(h->sac_flash[k] / 0.12f, 0.0f, 1.0f) * 0.8f;
            lowpoly_draw(&sac_model, &sp);
        }
        if (h->thr_born) {
            for (int k = 0; k < HIVE_THRUSTERS; k++) {
                if (h->thr_hp[k] <= 0.0f) continue;
                LowPolyPose tp = base_pose(w, t, hive_thruster_point(b, k), p.rot,
                                           HIVE_THRUSTER_RADIUS * p.scale * fmaxf(h->thr_grow[k], 0.05f), camera);
                tp.whiten = Clamp(h->thr_flash[k] / 0.12f, 0.0f, 1.0f) * 0.8f;
                lowpoly_draw(&thruster_model, &tp);
            }
        }
    }
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

#define TENDRIL_STEPS 28

// O tentáculo da maré: da frente da Lua até onde ele morde a crosta, ondulando.
static void tendril(const Boss *b, const HivePose *p, float time, Vector3 eye)
{
    const HiveState *h = &b->hive;
    Vector3 from = at(p, V(0.0f, -0.4f, -1.0f));
    Vector3 to = h->tide_point;
    Vector3 along = Vector3Subtract(to, from);
    float length = Vector3Length(along);
    if (length < 1.0f) return;
    Vector3 side = Vector3Normalize(Vector3CrossProduct(along, V(0.0f, 1.0f, 0.0f)));
    Vector3 lift = Vector3Normalize(Vector3CrossProduct(side, along));
    float reach = 0.35f + 0.65f * h->tide;
    Vector3 prev = from;
    for (int i = 1; i <= TENDRIL_STEPS; i++) {
        float u = (float)i / (float)TENDRIL_STEPS;
        float bend = sinf(u * PI) * length * 0.08f;
        float wave = sinf(time * 3.0f + u * 9.0f) * bend;
        Vector3 next = Vector3Add(Vector3Add(from, Vector3Scale(along, u * reach)),
                                  Vector3Add(Vector3Scale(side, wave), Vector3Scale(lift, bend * 0.6f)));
        float width = Lerp(4.5f, 1.6f, u);
        // Perto da câmera ele some: senão vira uma faixa enorme lavando a tela.
        float near = Clamp((Vector3Distance(next, eye) - 8.0f) / 30.0f, 0.05f, 1.0f);
        streak(prev, next, width * 2.2f, faded(TENDRIL_OUT, 0.35f * near), eye);
        streak(prev, next, width * 0.8f, faded(TENDRIL_IN, (0.7f + 0.3f * sinf(time * 8.0f - u * 12.0f)) * near), eye);
        prev = next;
    }
}

void hive_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Boss *b = &w->boss;
    const HiveState *h = &b->hive;
    if (!boss_active(b) || b->type != BOSS_HIVE) return;
    float time = (float)w->tick / (float)t->sim_hz;
    HivePose p = hive_pose(b, t, alpha);
    bool fighting = b->state == BOSS_FIGHT;
    bool dying = b->state == BOSS_DYING;
    Vector3 eye = camera.position;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    if (fighting && h->tide > 0.0f) {
        rlDisableBackfaceCulling();
        rlSetTexture(glow.id);
        rlBegin(RL_QUADS);
        tendril(b, &p, time, eye);
        rlEnd();
        rlSetTexture(0);
        rlEnableBackfaceCulling();
        float flicker = 0.7f + 0.3f * sinf(time * 13.0f);
        DrawBillboard(camera, glow, h->tide_point, 10.0f * (0.6f + 0.4f * h->tide) * flicker, faded(TENDRIL_IN, 0.7f));
    }

    if (!dying) {
        // Bolsas: um halo; a que está inchando pulsa em vermelho.
        for (int k = 0; k < HIVE_SACS; k++) {
            if (h->sac_hp[k] <= 0.0f || h->sac_grow[k] <= 0.02f) continue;
            float size = hive_sac_size(b, t, k);
            Vector3 pos = Vector3Lerp(p.pos, hive_sac_point(b, k), p.grow);
            float r = HIVE_SAC_RADIUS * p.scale * size * p.grow;
            bool hot = h->inflating == k;
            float beat = 0.5f + 0.5f * sinf(time * (hot ? 16.0f : 3.0f) + (float)k);
            DrawBillboard(camera, glow, pos, r * (hot ? 4.5f : 3.0f) * (0.85f + 0.15f * beat),
                          faded(hot ? SAC_HOT : SAC_GLOW, hot ? 0.55f + 0.3f * beat : 0.25f));
        }
        // Coração aberto (ou preparando o cuspe).
        Vector3 core = at(&p, Vector3Scale(HIVE_CORE, p.grow));
        float core_r = HIVE_CORE_RADIUS * p.scale * p.grow;
        float pulse = 0.5f + 0.5f * sinf(time * 8.0f);
        if (fighting && b->action == HIVE_OPEN) {
            DrawBillboard(camera, glow, core, core_r * (3.0f + pulse), faded(CORE_OPEN, 0.6f));
        } else if (h->spit_charge > 0.0f) {
            float u = 1.0f - h->spit_charge / fmaxf(t->hive_spit_warn, 1e-3f);
            DrawBillboard(camera, glow, core, core_r * (1.5f + 2.5f * u), faded(ACID, 0.4f + 0.5f * u));
        } else {
            DrawBillboard(camera, glow, core, core_r * 1.8f, faded(CORE_SHUT, 0.3f));
        }
        // Chamas dos propulsores empurrando a Lua.
        if (h->thr_born && h->pushing) {
            Vector3 back = Vector3RotateByQuaternion(V(0.0f, 0.0f, 1.0f), p.rot);
            for (int k = 0; k < HIVE_THRUSTERS; k++) {
                if (h->thr_hp[k] <= 0.0f || h->thr_grow[k] < 1.0f) continue;
                Vector3 mouth = Vector3Add(hive_thruster_point(b, k), Vector3Scale(back, HIVE_THRUSTER_RADIUS * p.scale * 0.8f));
                float r = HIVE_THRUSTER_RADIUS * p.scale;
                for (int i = 0; i < 4; i++) {
                    float flicker = 0.75f + 0.25f * sinf(time * 31.0f + (float)(i * 3 + k));
                    Vector3 spot = Vector3Add(mouth, Vector3Scale(back, r * (0.6f + 1.3f * (float)i)));
                    DrawBillboard(camera, glow, spot, r * (2.4f - 0.45f * (float)i) * flicker, faded(FLAME, 0.75f - 0.15f * (float)i));
                }
            }
        }
    } else {
        // Morrendo: fogo saindo das costelas.
        for (int k = 0; k < 10; k++) {
            float u = 0.5f + 0.5f * sinf(time * (9.0f + (float)k * 2.1f) + (float)k);
            Vector3 dir = around_front(0.5f + 0.24f * (float)k, (float)k * 1.9f);
            DrawBillboard(camera, glow, at(&p, Vector3Scale(dir, 1.05f)), p.scale * (0.25f + 0.2f * u),
                          faded(DYING_FIRE, 0.5f + 0.4f * u));
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}
