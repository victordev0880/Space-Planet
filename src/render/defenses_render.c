#include "raylib.h"
#include "rlgl.h"

#include "render/defenses_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelos ------------------------------------------------------------------
// Eixos do modelo: frente -Z, topo +Y. A torre mira com o cano (frente); o
// satélite voa de frente, com os painéis abertos para os lados.

enum { PART_HULL, PART_DARK, PART_PANEL, PART_LIGHT, PART_TRIM };

#define TURRET_SCALE    2.0f
#define SATELLITE_SCALE 1.5f
#define DOME_RINGS      10      // divisões da cúpula do polo até a borda
#define DOME_SEGMENTS   36      // ... e em volta
#define DOME_DIP        0.12f   // a borda desce este tanto abaixo do horizonte (fração de 90°)
#define DOME_FADE_TIME  6.0f    // nos últimos segundos o escudo pisca

static const Color HULL_COLOR    = { 200, 206, 218, 255 };
static const Color DARK_COLOR    = { 70, 76, 92, 255 };
static const Color PANEL_COLOR   = { 50, 90, 170, 255 };
static const Color LIGHT_COLOR   = { 120, 230, 255, 255 };
static const Color TRIM_COLOR    = { 255, 196, 90, 255 };
static const Color SHIELD_COLOR  = { 110, 200, 255, 255 };
static const Color ZAP_COLOR     = { 150, 230, 255, 255 };
static const Color MUZZLE_COLOR  = { 140, 230, 255, 255 };
static const Color ORE_COLOR     = { 255, 206, 90, 255 };

static LowPoly turret_model;
static LowPoly satellite_model;
static bool    built = false;

static Vector3 V(float x, float y, float z)
{
    return (Vector3){ x, y, z };
}

static void build_turret(LowPoly *m)
{
    lowpoly_clear(m);
    lowpoly_part(m, PART_HULL, HULL_COLOR, false);
    lowpoly_part(m, PART_DARK, DARK_COLOR, false);
    lowpoly_part(m, PART_PANEL, PANEL_COLOR, false);
    lowpoly_part(m, PART_LIGHT, LIGHT_COLOR, true);
    lowpoly_part(m, PART_TRIM, TRIM_COLOR, true);
    // Corpo: um disco grosso, com um anel de luz na borda.
    lp_tube(m, V(0.0f, -0.35f, 0.0f), V(0.0f, 0.25f, 0.0f), 0.85f, 0.7f, 8, PART_HULL, PART_DARK, false);
    lp_ring(m, V(0.0f, -0.05f, 0.0f), V(0.0f, 1.0f, 0.0f), 0.86f, 0.98f, 0.12f, 8, PART_LIGHT, PART_DARK, false);
    // Domo e o cano duplo, apontando para a frente (-Z).
    lp_tube(m, V(0.0f, 0.25f, 0.0f), V(0.0f, 0.6f, 0.0f), 0.55f, 0.3f, 6, PART_DARK, PART_HULL, false);
    lp_tube(m, V(0.18f, 0.42f, -0.2f), V(0.18f, 0.42f, -1.45f), 0.11f, 0.09f, 5, PART_HULL, PART_TRIM, true);
    lp_box(m, V(0.0f, 0.42f, -0.35f), V(0.32f, 0.12f, 0.22f), PART_DARK, false);
    // Antenas de baixo, apontando para o planeta.
    lp_spike(m, V(0.45f, -0.35f, 0.0f), V(0.6f, -1.1f, 0.0f), 0.08f, 4, PART_DARK, true);
}

static void build_satellite(LowPoly *m)
{
    lowpoly_clear(m);
    lowpoly_part(m, PART_HULL, HULL_COLOR, false);
    lowpoly_part(m, PART_DARK, DARK_COLOR, false);
    lowpoly_part(m, PART_PANEL, PANEL_COLOR, false);
    lowpoly_part(m, PART_LIGHT, LIGHT_COLOR, true);
    lowpoly_part(m, PART_TRIM, TRIM_COLOR, true);
    lp_box(m, V(0.0f, 0.0f, 0.0f), V(0.35f, 0.35f, 0.55f), PART_HULL, false);
    // Painéis solares, dos dois lados.
    lp_box(m, V(0.5f, 0.0f, 0.0f), V(0.15f, 0.05f, 0.05f), PART_DARK, true);
    lp_plate(m, V(0.65f, 0.0f, -0.4f), V(2.1f, 0.0f, -0.4f), V(2.1f, 0.0f, 0.4f), V(0.65f, 0.0f, 0.4f),
             V(0.0f, 1.0f, 0.0f), 0.04f, 0.04f, PART_PANEL, PART_PANEL, PART_DARK, true);
    // Emissor na frente (o raio sai daqui) e uma antena atrás.
    lp_tube(m, V(0.0f, 0.0f, -0.55f), V(0.0f, 0.0f, -0.9f), 0.22f, 0.1f, 6, PART_DARK, PART_LIGHT, false);
    lp_spike(m, V(0.0f, 0.3f, 0.4f), V(0.0f, 1.0f, 0.6f), 0.05f, 4, PART_TRIM, false);
    lp_box(m, V(0.0f, -0.36f, 0.0f), V(0.2f, 0.05f, 0.3f), PART_LIGHT, false);
}

static void build_models(void)
{
    build_turret(&turret_model);
    build_satellite(&satellite_model);
    built = true;
}

static Color faded(Color c, float a)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(a, 0.0f, 1.0f) * 255.0f) };
}

static LowPolyPose pose_for(const World *w, const Tuning *t, Vector3 pos, Quaternion rot, float scale, Camera3D camera)
{
    Vector3 sun = tuning_sun_direction(t);
    LowPolyPose pose = lowpoly_pose(pos, rot, scale, sun, camera.position);
    pose.sunlight = lowpoly_sunlight(pos, sun, w->planet.params.radius, 0.25f);
    pose.ambient = 0.35f;
    pose.wrap = 0.35f;
    pose.specular = 0.5f;
    pose.shininess = 24.0f;
    pose.rim = 0.3f;
    pose.rim_color = SHIELD_COLOR;
    pose.bounce = 0.2f;
    pose.bounce_color = (Color){ 110, 160, 230, 255 };
    return pose;
}

void defenses_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    (void)glow;
    if (!built) build_models();
    for (int i = 0; i < DEFENSES_MAX; i++) {
        const Defense *f = &w->defenses.items[i];
        if (!f->alive || f->type == DEF_SHIELD) continue;
        Vector3 pos = Vector3Lerp(f->pos_prev, f->pos, alpha);
        Vector3 up = Vector3Normalize(pos);
        if (f->type == DEF_TURRET) {
            // Mira com o cano; a base continua de costas para o planeta.
            Vector3 aim = f->dir;
            if (fabsf(Vector3DotProduct(aim, up)) > 0.98f) aim = Vector3Normalize(Vector3Add(aim, Vector3Scale(up, -0.3f)));
            LowPolyPose pose = pose_for(w, t, pos, quat_look(aim, up), TURRET_SCALE, camera);
            pose.whiten = f->flash * 2.0f;
            pose.flash = MUZZLE_COLOR;
            lowpoly_draw(&turret_model, &pose);
        } else {
            Vector3 ahead = Vector3Normalize(Vector3CrossProduct(f->axis, up));
            LowPolyPose pose = pose_for(w, t, pos, quat_look(ahead, up), SATELLITE_SCALE, camera);
            pose.whiten = f->flash * 2.0f;
            pose.flash = ZAP_COLOR;
            lowpoly_draw(&satellite_model, &pose);
        }
    }
}

// Cúpula: meia esfera sobre o solo, mais transparente no meio e brilhando na
// borda (onde a vista passa de raspão), clareando quando segura um golpe.
static void draw_dome(const Defense *f, const Tuning *t, float radius, Vector3 eye, float time)
{
    Vector3 up = f->dir;
    Vector3 hint = (fabsf(up.y) > 0.9f) ? V(1.0f, 0.0f, 0.0f) : V(0.0f, 1.0f, 0.0f);
    Vector3 a = Vector3Normalize(Vector3CrossProduct(up, hint));
    Vector3 b = Vector3CrossProduct(up, a);
    Vector3 center = f->pos;
    float r = t->shield_radius;
    float strength = Clamp(f->hp / fmaxf(t->shield_hp, 1e-3f), 0.0f, 1.0f);
    float fade = 1.0f;
    if (f->life < DOME_FADE_TIME) fade = 0.5f + 0.5f * ((sinf(time * 12.0f) > 0.0f) ? 1.0f : 0.0f);
    float base = (0.05f + 0.06f * strength) * fade;
    float flash = f->flash;
    (void)radius;

    rlBegin(RL_TRIANGLES);
    for (int ring = 0; ring < DOME_RINGS; ring++) {
        float t0 = (float)ring / (float)DOME_RINGS * (0.5f + DOME_DIP) * PI;
        float t1 = (float)(ring + 1) / (float)DOME_RINGS * (0.5f + DOME_DIP) * PI;
        for (int seg = 0; seg < DOME_SEGMENTS; seg++) {
            float p0 = (float)seg / (float)DOME_SEGMENTS * 2.0f * PI;
            float p1 = (float)(seg + 1) / (float)DOME_SEGMENTS * 2.0f * PI;
            float th[4] = { t0, t0, t1, t1 }, ph[4] = { p0, p1, p1, p0 };
            Vector3 v[4];
            Color c[4];
            for (int k = 0; k < 4; k++) {
                Vector3 n = Vector3Add(Vector3Scale(up, cosf(th[k])),
                                       Vector3Add(Vector3Scale(a, sinf(th[k]) * cosf(ph[k])), Vector3Scale(b, sinf(th[k]) * sinf(ph[k]))));
                v[k] = Vector3Add(center, Vector3Scale(n, r));
                Vector3 view = Vector3Normalize(Vector3Subtract(eye, v[k]));
                float rim = 1.0f - fabsf(Vector3DotProduct(n, view));
                float alpha = base + 0.32f * rim * rim * fade + flash * 0.9f;
                c[k] = faded(SHIELD_COLOR, alpha);
            }
            int order[6] = { 0, 1, 2, 0, 2, 3 };
            for (int k = 0; k < 6; k++) {
                rlColor4ub(c[order[k]].r, c[order[k]].g, c[order[k]].b, c[order[k]].a);
                rlVertex3f(v[order[k]].x, v[order[k]].y, v[order[k]].z);
            }
        }
    }
    rlEnd();
}

static void streak(Vector3 a, Vector3 b, float width, Color color, Vector3 eye)
{
    Vector3 along = Vector3Subtract(b, a);
    Vector3 side = Vector3CrossProduct(along, Vector3Subtract(eye, a));
    if (Vector3LengthSqr(side) < 1e-8f) return;
    side = Vector3Scale(Vector3Normalize(side), width * 0.5f);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0.5f, 0.5f);
    rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
}

void defenses_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    float time = ((float)w->tick + alpha) / (float)t->sim_hz;
    bool any_ore = rocks_ore_alive(&w->rocks) > 0;
    bool any_defense = false;
    for (int i = 0; i < DEFENSES_MAX; i++) any_defense = any_defense || w->defenses.items[i].alive;
    if (!any_ore && !any_defense) return;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);

    for (int i = 0; i < DEFENSES_MAX; i++) {
        const Defense *f = &w->defenses.items[i];
        if (f->alive && f->type == DEF_SHIELD) draw_dome(f, t, t->shield_radius, camera.position, time);
    }
    rlDrawRenderBatchActive();

    for (int i = 0; i < DEFENSES_MAX; i++) {
        const Defense *f = &w->defenses.items[i];
        if (!f->alive || f->type == DEF_SHIELD) continue;
        Vector3 pos = Vector3Lerp(f->pos_prev, f->pos, alpha);
        float pulse = 0.7f + 0.3f * sinf(time * 4.0f + (float)i);
        DrawBillboard(camera, glow, pos, 2.2f * pulse, faded(LIGHT_COLOR, 0.25f));   // luz de posição
        if (f->type == DEF_TURRET && f->flash > 0.0f) {
            DrawBillboard(camera, glow, Vector3Add(pos, Vector3Scale(f->dir, 2.6f)), 2.4f, faded(MUZZLE_COLOR, f->flash * 7.0f));
        }
        if (f->type == DEF_SATELLITE && f->zap_time > 0.0f) {
            float u = f->zap_time / 0.15f;
            rlSetTexture(glow.id);
            rlBegin(RL_QUADS);
            streak(pos, f->zap, 0.9f * u, faded(ZAP_COLOR, 0.6f * u), camera.position);
            streak(pos, f->zap, 0.25f, faded(WHITE, 0.9f * u), camera.position);
            rlEnd();
            rlSetTexture(0);
            DrawBillboard(camera, glow, f->zap, 3.5f * u, faded(ZAP_COLOR, 0.8f * u));
        }
    }

    // Cristais de minério: brilho dourado piscando, com um miolo branco.
    for (int i = 0; i < ORE_MAX && any_ore; i++) {
        const Ore *o = &w->rocks.ore[i];
        if (!o->alive) continue;
        Vector3 pos = Vector3Lerp(o->pos_prev, o->pos, alpha);
        float twinkle = 0.75f + 0.25f * sinf(o->spin * 2.0f);
        float fade = Clamp((t->ore_life - o->age) / 3.0f, 0.0f, 1.0f);   // some aos poucos no fim
        DrawBillboard(camera, glow, pos, 3.6f * twinkle, faded(ORE_COLOR, 0.45f * fade));
        DrawBillboard(camera, glow, pos, 1.5f, faded(ORE_COLOR, 0.95f * fade));
        DrawBillboard(camera, glow, pos, 0.7f, faded(WHITE, fade));
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
