#include "raylib.h"
#include "rlgl.h"

#include "render/rocks_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelos ------------------------------------------------------------------
// Esfera de raio 1 (vezes o raio da rocha): um icosaedro dividido uma vez (80
// faces), com cada vértice empurrado para dentro ou para fora por uma soma de
// ondas que depende só da direção dele. Vértices compartilhados saem iguais,
// então a rocha não abre frestas.

enum { PART_BODY, PART_BODY2, PART_TOP, PART_CRACK };

#define ROCK_DETAIL_DISTANCE 220.0f
#define TOP_FACES            0.42f    // faces com o centro acima disto (em y) são o "topo" do fragmento
#define TAIL_SPRITES         10
#define BEAM_RINGS           4

static const Color FRAGMENT_BODY  = { 112, 98, 88, 255 };
static const Color FRAGMENT_BODY2 = { 92, 80, 74, 255 };
static const Color METEOR_BODY    = { 58, 52, 56, 255 };
static const Color METEOR_BODY2   = { 78, 68, 64, 255 };
static const Color METEOR_CRACK   = { 255, 120, 40, 255 };
static const Color METEOR_RIM     = { 255, 140, 70, 255 };
static const Color ROCK_RIM       = { 210, 196, 180, 255 };
static const Color TRACTOR_COLOR  = { 110, 255, 210, 255 };
static const Color TAIL_HOT       = { 255, 226, 160, 255 };
static const Color TAIL_COOL      = { 255, 84, 30, 255 };
static const Color REPAIR_COLOR   = { 120, 255, 170, 255 };

static const Color SPORE_SKIN     = { 70, 110, 50, 255 };
static const Color SPORE_SPIKE    = { 180, 200, 140, 255 };
static const Color SPORE_GLOW     = { 150, 255, 90, 255 };

static LowPoly fragment_models[ROCK_SHAPES];
static LowPoly meteor_models[ROCK_SHAPES];
static LowPoly spore_model;
static bool    built = false;

static float bump(Vector3 d, int shape)
{
    float k = (float)shape * 1.7f + 0.4f;
    return 1.0f + 0.20f * sinf(d.x * 2.3f + k) * cosf(d.y * 3.1f - k * 0.7f)
                + 0.13f * sinf(d.z * 4.7f + k * 1.3f + d.x * 2.0f)
                + 0.07f * sinf((d.x + d.y + d.z) * 7.0f + k);
}

static Vector3 shaped(Vector3 dir, int shape)
{
    Vector3 squash = (shape % 2 == 1) ? (Vector3){ 1.18f, 0.78f, 1.0f } : (Vector3){ 1.0f, 0.92f, 1.06f };
    float r = bump(dir, shape);
    return (Vector3){ dir.x * r * squash.x, dir.y * r * squash.y, dir.z * r * squash.z };
}

static void build_rock(LowPoly *m, int shape, bool meteor)
{
    static const float X = 0.525731f, Z = 0.850651f;
    static const Vector3 V[12] = {
        { -X, 0, Z }, { X, 0, Z }, { -X, 0, -Z }, { X, 0, -Z }, { 0, Z, X }, { 0, Z, -X },
        { 0, -Z, X }, { 0, -Z, -X }, { Z, X, 0 }, { -Z, X, 0 }, { Z, -X, 0 }, { -Z, -X, 0 },
    };
    static const int F[20][3] = {
        { 0, 4, 1 }, { 0, 9, 4 }, { 9, 5, 4 }, { 4, 5, 8 }, { 4, 8, 1 }, { 8, 10, 1 }, { 8, 3, 10 },
        { 5, 3, 8 }, { 5, 2, 3 }, { 2, 7, 3 }, { 7, 10, 3 }, { 7, 6, 10 }, { 7, 11, 6 }, { 11, 0, 6 },
        { 0, 1, 6 }, { 6, 1, 10 }, { 9, 0, 11 }, { 9, 11, 2 }, { 9, 2, 5 }, { 7, 2, 11 },
    };
    lowpoly_clear(m);
    lowpoly_part(m, PART_BODY, meteor ? METEOR_BODY : FRAGMENT_BODY, false);
    lowpoly_part(m, PART_BODY2, meteor ? METEOR_BODY2 : FRAGMENT_BODY2, false);
    lowpoly_part(m, PART_TOP, FRAGMENT_BODY, false);   // a cor certa entra na hora de desenhar
    lowpoly_part(m, PART_CRACK, METEOR_CRACK, true);

    int face = 0;
    for (int f = 0; f < 20; f++) {
        Vector3 a = V[F[f][0]], b = V[F[f][1]], c = V[F[f][2]];
        Vector3 ab = Vector3Normalize(Vector3Add(a, b));
        Vector3 bc = Vector3Normalize(Vector3Add(b, c));
        Vector3 ca = Vector3Normalize(Vector3Add(c, a));
        Vector3 tris[4][3] = { { a, ab, ca }, { b, bc, ab }, { c, ca, bc }, { ab, bc, ca } };
        for (int k = 0; k < 4; k++, face++) {
            Vector3 mid = Vector3Normalize(Vector3Add(Vector3Add(tris[k][0], tris[k][1]), tris[k][2]));
            int part = ((face * 7 + shape * 3) % 5 == 0) ? PART_BODY2 : PART_BODY;
            if (meteor) {
                if ((face * 11 + shape * 5) % 6 == 0) part = PART_CRACK;   // rachaduras em brasa
            } else if (mid.y > TOP_FACES) {
                part = PART_TOP;   // o pedaço da superfície do planeta
            }
            lowpoly_tri(m, shaped(tris[k][0], shape), shaped(tris[k][1], shape), shaped(tris[k][2], shape), part,
                        false);
        }
    }
}

// Esporo da Colmeia: uma vagem viva, acesa por dentro, com espinhos.
static void build_spore(LowPoly *m)
{
    lowpoly_clear(m);
    lowpoly_part(m, PART_BODY, SPORE_SKIN, false);
    lowpoly_part(m, PART_BODY2, SPORE_SPIKE, false);
    lowpoly_part_glow(m, PART_CRACK, SPORE_GLOW);
    const LpRing pod[] = { { -1.1f, 0.0f, 0.0f, 0.0f }, { -0.7f, 0.75f, 0.75f, 0.0f }, { 0.0f, 0.95f, 0.95f, 0.0f },
                           { 0.7f, 0.75f, 0.75f, 0.0f }, { 1.1f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, pod, 5, 8, 0.0f, PART_CRACK, PART_BODY, -1, 0.35f, false);
    for (int k = 0; k < 8; k++) {
        float a = (float)k * PI * 0.25f, z = (k % 2 == 0) ? -0.3f : 0.35f;
        Vector3 base = { cosf(a) * 0.85f, sinf(a) * 0.85f, z };
        lp_spike(m, base, Vector3Scale(base, 1.75f), 0.16f, 4, PART_BODY2, false);
    }
}

static void build_models(void)
{
    for (int s = 0; s < ROCK_SHAPES; s++) {
        build_rock(&fragment_models[s], s, false);
        build_rock(&meteor_models[s], s, true);
    }
    build_spore(&spore_model);
    built = true;
}

static Color rgb(Rgb8 c)
{
    return (Color){ c.r, c.g, c.b, 255 };
}

// Cor do topo do fragmento: a da superfície de onde ele saiu.
static Color top_color(const Tuning *t, int material)
{
    switch ((VoxelMaterial)material) {
    case VMAT_WATER_SHALLOW:
    case VMAT_WATER:
    case VMAT_WATER_DEEP: return rgb(t->color_water_shallow);
    case VMAT_ICE:        return rgb(t->color_ice);
    case VMAT_SAND:       return rgb(t->color_sand);
    case VMAT_GRASS:      return rgb(t->color_grass);
    case VMAT_FOREST:     return rgb(t->color_forest);
    case VMAT_DESERT:     return rgb(t->color_desert);
    case VMAT_ROCK:       return rgb(t->color_rock);
    case VMAT_SNOW:       return rgb(t->color_snow);
    default:              return FRAGMENT_BODY2;
    }
}

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

static Vector3 rock_pos(const Rock *k, float alpha)
{
    return Vector3Lerp(k->pos_prev, k->pos, alpha);
}

void rocks_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    (void)glow;
    if (!built) build_models();
    Vector3 sun = tuning_sun_direction(t);
    float time = (float)w->tick / (float)t->sim_hz;
    float radius = w->planet.params.radius;
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive) continue;
        Vector3 pos = rock_pos(k, alpha);
        Quaternion rot = QuaternionSlerp(k->rot_prev, k->rot, alpha);
        LowPolyPose pose = lowpoly_pose(pos, rot, k->radius, sun, camera.position);
        pose.sunlight = lowpoly_sunlight(pos, sun, radius, 0.25f);
        pose.ambient = 0.3f;
        pose.wrap = 0.35f;
        pose.specular = 0.15f;
        pose.shininess = 10.0f;
        pose.rim = 0.25f;
        pose.rim_color = ROCK_RIM;
        pose.bounce = 0.2f;
        pose.bounce_color = (Color){ 110, 160, 230, 255 };
        pose.far = Vector3Distance(pos, camera.position) > ROCK_DETAIL_DISTANCE;
        pose.whiten = k->flash * 4.0f;   // clarão do tiro
        pose.flash = (Color){ 255, 220, 160, 255 };
        bool meteor = k->kind == ROCK_METEOR;
        if (meteor) {
            pose.rim = 0.55f;
            pose.rim_color = METEOR_RIM;
        }
        if (k->state == ROCK_TOWED) {
            // Presa ao raio: borda verde-água pulsando.
            pose.rim = 0.7f;
            pose.rim_color = TRACTOR_COLOR;
            pose.whiten = 0.06f + 0.06f * sinf(time * 9.0f);
            pose.flash = TRACTOR_COLOR;
        }
        if (k->kind == ROCK_SPORE) {
            // O esporo pulsa como um coração; quanto mais velho, mais rápido.
            float beat = 0.5f + 0.5f * sinf(time * (5.0f + k->age * 0.4f) + (float)i);
            pose.glow = (Color){ (unsigned char)(150 + 80 * beat), 255, (unsigned char)(90 + 60 * beat), 255 };
            pose.rim = 0.6f;
            pose.rim_color = SPORE_GLOW;
            pose.scale = k->radius * (1.0f + 0.08f * beat);
            lowpoly_draw(&spore_model, &pose);
            continue;
        }
        LowPoly *model = meteor ? &meteor_models[k->shape % ROCK_SHAPES] : &fragment_models[k->shape % ROCK_SHAPES];
        if (!meteor) model->colors[PART_TOP] = top_color(t, k->material);
        lowpoly_draw(model, &pose);
    }
}

// Faixa de frente para a câmera, de `a` (largura wa) até `b` (largura wb):
// com larguras diferentes, vira um cone visto de lado.
static void taper(Vector3 a, Vector3 b, float wa, float wb, Color color, Vector3 eye)
{
    Vector3 along = Vector3Subtract(b, a);
    Vector3 side = Vector3CrossProduct(along, Vector3Subtract(eye, a));
    if (Vector3LengthSqr(side) < 1e-8f) return;
    side = Vector3Normalize(side);
    Vector3 sa = Vector3Scale(side, wa * 0.5f), sb = Vector3Scale(side, wb * 0.5f);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0.5f, 0.5f);
    rlVertex3f(a.x - sa.x, a.y - sa.y, a.z - sa.z);
    rlVertex3f(a.x + sa.x, a.y + sa.y, a.z + sa.z);
    rlVertex3f(b.x + sb.x, b.y + sb.y, b.z + sb.z);
    rlVertex3f(b.x - sb.x, b.y - sb.y, b.z - sb.z);
}

void rocks_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    float time = ((float)w->tick + alpha) / (float)t->sim_hz;
    const Rock *towed = rocks_towed(&w->rocks);
    bool any_job = repair_active_jobs(&w->repair) > 0;
    bool any = towed != NULL || any_job;
    for (int i = 0; i < ROCKS_MAX && !any; i++) {
        any = w->rocks.items[i].alive && (w->rocks.items[i].kind != ROCK_FRAGMENT || w->rocks.items[i].state == ROCK_EJECT);
    }
    if (!any) return;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive) continue;
        Vector3 pos = rock_pos(k, alpha);
        if (k->kind == ROCK_METEOR && k->state == ROCK_FLYBY) {
            // Cauda de fogo, para trás da direção do voo.
            float speed = Vector3Length(k->vel);
            Vector3 back = (speed > 1e-3f) ? Vector3Scale(k->vel, -1.0f / speed) : (Vector3){ 0.0f, 0.0f, 0.0f };
            DrawBillboard(camera, glow, pos, k->radius * 3.2f, faded(METEOR_RIM, 0.35f));
            for (int s = 0; s < TAIL_SPRITES; s++) {
                float u = (float)s / (float)(TAIL_SPRITES - 1);
                float flicker = 0.85f + 0.15f * sinf(time * 23.0f + (float)s * 1.7f);
                Vector3 at = Vector3Add(pos, Vector3Scale(back, k->radius * (0.6f + (float)s * 0.95f)));
                DrawBillboard(camera, glow, at, k->radius * (2.2f - 1.5f * u) * flicker,
                              faded(mix(TAIL_HOT, TAIL_COOL, u), 0.75f * (1.0f - u)));
            }
        } else if (k->kind == ROCK_METEOR) {
            DrawBillboard(camera, glow, pos, k->radius * 2.4f, faded(METEOR_RIM, 0.25f));   // ainda em brasa
        } else if (k->kind == ROCK_SPORE) {
            // Halo verde; arremessado, deixa um rastro.
            DrawBillboard(camera, glow, pos, k->radius * 4.0f, faded(SPORE_GLOW, 0.3f + 0.15f * sinf(time * 7.0f + (float)i)));
            if (k->state == ROCK_THROWN) {
                Vector3 back = Vector3Scale(Vector3Normalize(k->vel), -1.0f);
                for (int s = 1; s <= 6; s++) {
                    DrawBillboard(camera, glow, Vector3Add(pos, Vector3Scale(back, k->radius * 1.3f * (float)s)),
                                  k->radius * (2.6f - 0.3f * (float)s), faded(SPORE_GLOW, 0.5f - 0.07f * (float)s));
                }
            }
        } else if (k->state == ROCK_EJECT) {
            // Fragmento recém-arrancado: quente, esfriando na subida.
            float u = Clamp(k->age / fmaxf(t->fragment_eject_time, 1e-3f), 0.0f, 1.0f);
            DrawBillboard(camera, glow, pos, k->radius * 2.6f, faded(METEOR_RIM, 0.6f * (1.0f - u)));
        }
    }

    // Raio trator: um cone de luz da barriga da nave até a rocha, largo do
    // lado dela, com anéis subindo em direção à nave (ela está puxando).
    if (towed != NULL && w->ship.alive) {
        const Ship *s = &w->ship;
        Vector3 ship_pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        Quaternion ship_rot = QuaternionSlerp(s->rot_prev, s->rot, alpha);
        Vector3 from = Vector3Add(ship_pos, Vector3RotateByQuaternion((Vector3){ 0.0f, -0.35f, -0.5f }, ship_rot));
        Vector3 to = rock_pos(towed, alpha);
        float pulse = 0.85f + 0.15f * sinf(time * 14.0f);
        float wide = towed->radius * 2.2f;
        rlSetTexture(glow.id);
        rlBegin(RL_QUADS);
        taper(from, to, 0.6f, wide * pulse, faded(TRACTOR_COLOR, 0.16f), camera.position);
        taper(from, to, 0.35f, wide * 0.45f, faded(TRACTOR_COLOR, 0.32f * pulse), camera.position);
        taper(from, to, 0.12f, 0.25f, faded(WHITE, 0.6f), camera.position);
        rlEnd();
        rlSetTexture(0);
        for (int r = 0; r < BEAM_RINGS; r++) {
            float u = fmodf(time * 1.2f + (float)r / (float)BEAM_RINGS, 1.0f);
            float size = Lerp(wide, 0.6f, u);
            DrawBillboard(camera, glow, Vector3Lerp(to, from, u), size, faded(TRACTOR_COLOR, 0.45f * (1.0f - u)));
        }
        DrawBillboard(camera, glow, to, towed->radius * (2.6f + 0.3f * pulse), faded(TRACTOR_COLOR, 0.22f));
        DrawBillboard(camera, glow, from, 1.2f * pulse, faded(TRACTOR_COLOR, 0.7f));
    }

    // Consertos em andamento: um brilho verde que cresce e se apaga.
    for (int i = 0; i < REPAIR_JOBS && any_job; i++) {
        const RepairJob *j = &w->repair.jobs[i];
        if (!j->active) continue;
        float u = Clamp(j->age / fmaxf(t->repair_time, 1e-3f), 0.0f, 1.0f);
        Vector3 up = Vector3Normalize(j->ground);
        Vector3 at = Vector3Add(j->ground, Vector3Scale(up, 1.5f));
        DrawBillboard(camera, glow, at, j->radius * (0.8f + 1.6f * u), faded(REPAIR_COLOR, 0.55f * (1.0f - u * 0.7f)));
        DrawBillboard(camera, glow, at, j->radius * 0.5f, faded(WHITE, 0.5f * (1.0f - u)));
    }

    EndBlendMode();
    rlEnableDepthMask();
}
