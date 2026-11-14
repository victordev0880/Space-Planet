#include "raylib.h"
#include "rlgl.h"

#include "render/enemies_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/enemy_models.h"
#include "render/lowpoly.h"
#include "render/particles_render.h"

// ---------------------------------------------------------------------------
//  Variações de cor: cada grupo sai em uma das três
// ---------------------------------------------------------------------------

typedef struct { Color tint, glow; } Variant;
static const Variant VARIANTS[ENEMY_TYPE_COUNT][ENEMY_VARIANTS] = {
    [ENEMY_SWARM] = { { { 255, 255, 255, 255 }, { 150, 255, 150, 255 } },
                      { { 255, 175, 195, 255 }, { 255, 210, 90, 255 } },
                      { { 170, 225, 255, 255 }, { 120, 240, 255, 255 } } },
    [ENEMY_BOMBER] = { { { 255, 255, 255, 255 }, { 255, 140, 60, 255 } },
                       { { 170, 190, 225, 255 }, { 255, 90, 90, 255 } },
                       { { 205, 225, 160, 255 }, { 255, 200, 80, 255 } } },
    [ENEMY_KAMIKAZE] = { { { 255, 255, 255, 255 }, { 255, 90, 40, 255 } },
                         { { 255, 210, 170, 255 }, { 255, 170, 40, 255 } },
                         { { 200, 170, 255, 255 }, { 255, 60, 160, 255 } } },
    [ENEMY_DIGGER] = { { { 255, 255, 255, 255 }, { 255, 150, 50, 255 } },
                       { { 190, 200, 220, 255 }, { 255, 90, 40, 255 } },
                       { { 230, 200, 160, 255 }, { 255, 220, 90, 255 } } },
    [ENEMY_SIPHON] = { { { 255, 255, 255, 255 }, { 120, 255, 190, 255 } },
                       { { 200, 170, 255, 255 }, { 190, 140, 255, 255 } },
                       { { 255, 220, 170, 255 }, { 255, 230, 120, 255 } } },
    [ENEMY_SNIPER] = { { { 255, 255, 255, 255 }, { 255, 60, 90, 255 } },
                       { { 220, 200, 180, 255 }, { 255, 120, 60, 255 } },
                       { { 170, 200, 170, 255 }, { 255, 60, 200, 255 } } },
    [ENEMY_SHIELDER] = { { { 255, 255, 255, 255 }, { 90, 230, 255, 255 } },
                         { { 200, 230, 200, 255 }, { 120, 255, 170, 255 } },
                         { { 230, 210, 255, 255 }, { 170, 150, 255, 255 } } },
    [ENEMY_CARRIER] = { { { 255, 255, 255, 255 }, { 200, 120, 255, 255 } },
                        { { 200, 180, 160, 255 }, { 255, 120, 150, 255 } },
                        { { 160, 190, 210, 255 }, { 120, 200, 255, 255 } } },
};

// ---------------------------------------------------------------------------
//  Desenho dos inimigos
// ---------------------------------------------------------------------------

#define ENEMY_AMBIENT  0.38f
#define ENEMY_SHADOW   0.22f
#define ENEMY_WRAP     0.35f
#define ENEMY_SPECULAR 0.35f
#define ENEMY_SHINE    14.0f
#define ENEMY_RIM      0.35f
#define ENEMY_BOUNCE   0.2f
#define FLASH_TIME     0.12f   // (mesma duração de game/enemies.c)
#define DRILL_SPIN     7.0f    // radianos por segundo da broca girando
#define STAR_SPIN      9.0f    // ... e da estrela do Kamikaze
#define DETAIL_DISTANCE 70.0f  // além disto (em tamanhos do modelo), os detalhes pequenos somem

// Boca do cano do Atirador, igual nos três desenhos (a lógica usa (0, 0, -1.6)).
static const Vector3 SNIPER_MUZZLE_DRAW = { 0.0f, 0.0f, -1.7f };
#define SHIELD_BUBBLE  2.6f    // bolha em volta de um aliado protegido, em raios dele
#define SHIELD_TETHER  0.18f   // espessura do cabo de energia do Escudeiro

static const Color BOUNCE_COLOR = { 110, 160, 230, 255 };
static const Color CORE_COLOR   = { 255, 90, 190, 255 };
static const Color ARMING_COLOR = { 255, 60, 40, 255 };
static const Color BEAM_COLOR   = { 120, 255, 190, 255 };
static const Color LASER_COLOR  = { 255, 40, 60, 255 };

static Vector3 at(Vector3 pos, Quaternion rot, float scale, Vector3 local)
{
    return Vector3Add(pos, Vector3RotateByQuaternion(Vector3Scale(local, scale), rot));
}

static Color faded(Color c, float alpha)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) };
}

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

typedef struct {
    Vector3           pos;
    Quaternion        rot;     // já com o giro da broca ou da estrela
    float             scale;
    Color             glow;
    const EnemyModel *model;
} Placed;

// O modelo gira em volta de z? A broca do Escavador, só cavando; a estrela
// do Kamikaze, sempre.
static bool spinning(const Enemy *en, const EnemyModel *model)
{
    if (!model->spins) return false;
    return (en->type == ENEMY_DIGGER) ? en->state == DIGGER_DRILL : true;
}

static Placed place(const Enemy *en, const Tuning *t, float alpha, float time)
{
    Placed p;
    p.pos = Vector3Lerp(en->pos_prev, en->pos, alpha);
    p.rot = QuaternionSlerp(en->rot_prev, en->rot, alpha);
    p.scale = t->enemy[en->type].scale * en->size;
    p.glow = VARIANTS[en->type][en->variant % ENEMY_VARIANTS].glow;
    p.model = enemy_model((EnemyType)en->type, en->variant);
    if (spinning(en, p.model)) {
        float rate = (en->type == ENEMY_KAMIKAZE) ? STAR_SPIN : DRILL_SPIN;
        p.rot = QuaternionMultiply(p.rot, QuaternionFromAxisAngle((Vector3){ 0.0f, 0.0f, 1.0f }, time * rate));
    }
    return p;
}

void enemies_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Enemies *e = &w->enemies;
    Vector3 sun = tuning_sun_direction(t);
    float time = (float)w->tick / (float)t->sim_hz;

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        Placed p = place(en, t, alpha, time);
        const Variant *v = &VARIANTS[en->type][en->variant % ENEMY_VARIANTS];

        LowPolyPose pose = lowpoly_pose(p.pos, p.rot, p.scale, sun, camera.position);
        pose.sunlight = lowpoly_sunlight(p.pos, sun, w->planet.params.radius, ENEMY_SHADOW);
        pose.ambient = ENEMY_AMBIENT;
        pose.wrap = ENEMY_WRAP;
        pose.specular = ENEMY_SPECULAR;
        pose.shininess = ENEMY_SHINE;
        pose.rim = ENEMY_RIM;
        pose.rim_color = v->glow;
        pose.bounce = ENEMY_BOUNCE;
        pose.bounce_color = BOUNCE_COLOR;
        pose.tint = v->tint;
        pose.glow = v->glow;
        pose.whiten = Clamp(en->flash / FLASH_TIME, 0.0f, 1.0f) * 0.8f;
        // Longe, os detalhes pequenos nem apareceriam: ficam de fora.
        pose.far = Vector3Distance(p.pos, camera.position) > DETAIL_DISTANCE * p.scale;
        lowpoly_draw(&p.model->mesh, &pose);
    }

    // Brilhos, por cima: as luzes de cada modelo (motores, olhos, emissores)
    // e os avisos de cada tipo.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        Placed p = place(en, t, alpha, time);
        float pulse = 0.75f + 0.25f * sinf(time * 5.0f + (float)i);

        for (int k = 0; k < p.model->light_count; k++) {
            const ModelLight *l = &p.model->lights[k];
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, l->pos), l->size * p.scale * pulse,
                          faded(p.glow, 0.75f));
        }

        switch ((EnemyType)en->type) {
        case ENEMY_BOMBER: {
            // Ponto fraco: pulsa, para chamar o olho.
            Vector3 core = at(p.pos, p.rot, p.scale, (Vector3){ 0.0f, 0.08f, 1.20f });
            DrawBillboard(camera, glow, core, p.scale * pulse * 1.6f, faded(CORE_COLOR, 0.55f));
            DrawBillboard(camera, glow, core, p.scale * 0.5f, faded(WHITE, 0.8f));
            if (en->arming > 0.0f) {
                float progress = 1.0f - en->arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
                float blink = 0.6f + 0.4f * sinf(time * (10.0f + 20.0f * progress));
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, (Vector3){ 0.0f, -0.40f, 0.15f }),
                              1.4f * p.scale * (0.5f + progress), faded(ARMING_COLOR, blink));
            }
            break;
        }
        case ENEMY_KAMIKAZE: {
            // Travado na nave, o núcleo pisca depressa.
            float rate = (en->state == KAMIKAZE_HUNT) ? 22.0f : 6.0f;
            float beat = 0.6f + 0.4f * sinf(time * rate + (float)i);
            DrawBillboard(camera, glow, p.pos, 2.2f * p.scale * beat, faded(p.glow, 0.6f));
            DrawBillboard(camera, glow, p.pos, 0.7f * p.scale, faded(WHITE, 0.6f * beat));
            break;
        }
        case ENEMY_SNIPER:
            if (en->arming > 0.0f) {
                // Carregando: a boca do cano acende cada vez mais.
                float charge = 1.0f - en->arming / fmaxf(t->sniper_charge_time, 1e-3f);
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, SNIPER_MUZZLE_DRAW),
                              (0.6f + 1.8f * charge) * p.scale, faded(LASER_COLOR, 0.4f + 0.6f * charge));
            }
            break;
        case ENEMY_SHIELDER:
            DrawBillboard(camera, glow, p.pos, 2.2f * p.scale * pulse, faded(p.glow, 0.35f));
            break;
        case ENEMY_CARRIER: {
            // O hangar acende antes de soltar o bando.
            float bay = (en->arming > 0.0f) ? 0.6f + 0.4f * sinf(time * 18.0f) : 0.25f;
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, (Vector3){ 0.0f, -0.32f, 0.15f }),
                          (en->arming > 0.0f ? 2.2f : 1.2f) * p.scale, faded(p.glow, bay));
            break;
        }
        default:
            break;
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}

// Feixes e cabos: o feixe do Sifão, a mira do Atirador e os cabos de energia
// do Escudeiro até cada aliado protegido (com uma bolha em volta dele).
static void draw_links(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera, float time)
{
    const Enemies *e = &w->enemies;
    Vector3 eye = camera.position;

    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        if (en->hp <= 0.0f) continue;
        Placed p = place(en, t, alpha, time);

        if (en->type == ENEMY_SIPHON && en->state == SIPHON_DRAIN) {
            Vector3 from = at(p.pos, p.rot, p.scale, p.model->beam);
            float flicker = 0.75f + 0.25f * sinf(time * 30.0f + (float)i);
            streak(from, en->aux, 1.6f, faded(p.glow, 0.25f * flicker), eye);
            streak(from, en->aux, 0.45f, faded(BEAM_COLOR, 0.8f * flicker), eye);
            // Material subindo pelo feixe: pontos que andam do solo para cima.
            for (int k = 0; k < 4; k++) {
                float u = fmodf(time * 0.9f + (float)k * 0.25f, 1.0f);
                Vector3 dot = Vector3Lerp(en->aux, from, u);
                Vector3 up = Vector3Scale(Vector3Normalize(Vector3Subtract(from, en->aux)), 0.8f);
                streak(dot, Vector3Add(dot, up), 0.5f, faded(WHITE, 0.6f * (1.0f - u)), eye);
            }
        } else if (en->type == ENEMY_SNIPER && en->arming > 0.0f) {
            // A linha da mira: fina e trêmula enquanto segue a nave; firme e
            // forte quando trava.
            Vector3 muzzle = at(p.pos, p.rot, p.scale, SNIPER_MUZZLE_DRAW);
            bool locked = en->arming <= t->sniper_lock_time;
            float charge = 1.0f - en->arming / fmaxf(t->sniper_charge_time, 1e-3f);
            float width = locked ? 0.35f : 0.12f + 0.1f * charge;
            float shimmer = locked ? 1.0f : 0.5f + 0.3f * sinf(time * 40.0f);
            streak(muzzle, en->aux, width, faded(LASER_COLOR, shimmer * (0.35f + 0.65f * charge)), eye);
        } else if (en->type == ENEMY_SHIELDER) {
            for (int k = 0; k < e->pool.capacity; k++) {
                if (k == i || !pool_alive(&e->pool, k) || e->items[k].hp <= 0.0f) continue;
                if (enemies_shielded_by(e, t, k) != i) continue;
                Vector3 ally = Vector3Lerp(e->items[k].pos_prev, e->items[k].pos, alpha);
                streak(p.pos, ally, SHIELD_TETHER, faded(p.glow, 0.35f), eye);
            }
        }
    }
    rlEnd();
    rlSetTexture(0);

    // Bolhas de escudo em volta de quem está protegido.
    rlDrawRenderBatchActive();
    Texture2D ring = particles_ring_texture();
    for (int k = 0; k < e->pool.capacity; k++) {
        if (!pool_alive(&e->pool, k) || e->items[k].hp <= 0.0f) continue;
        int shielder = enemies_shielded_by(e, t, k);
        if (shielder < 0) continue;
        const Enemy *ally = &e->items[k];
        Color c = VARIANTS[ENEMY_SHIELDER][e->items[shielder].variant % ENEMY_VARIANTS].glow;
        Vector3 pos = Vector3Lerp(ally->pos_prev, ally->pos, alpha);
        float size = t->enemy[ally->type].radius * SHIELD_BUBBLE;
        DrawBillboard(camera, ring, pos, size, faded(c, 0.55f));
        DrawBillboard(camera, glow, pos, size * 0.9f, faded(c, 0.12f));
    }
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
#define LANCE_LENGTH    0.06f    // o tiro do Atirador é um risco vermelho comprido
#define BOMB_SIZE       2.6f
#define MARK_SEGMENTS   40
#define MARK_LIFT       0.6f     // a marca fica um pouco acima do solo
#define MARK_WIDTH      0.16f    // espessura do anel, em frações do raio
#define MARK_START      2.2f     // o anel de aviso começa este tanto maior e se fecha sobre o alvo

static const Color BOLT_CORE       = { 235, 250, 255, 255 };
static const Color BOLT_HALO_COLOR = {  80, 190, 255, 255 };
static const Color PLASMA_CORE     = { 255, 210, 255, 255 };
static const Color PLASMA_HALO     = { 190,  80, 255, 255 };
static const Color SUICIDE_CORE    = { 255, 236, 190, 255 };   // drone suicida (sinergia): laranja
static const Color SUICIDE_HALO    = { 255, 130, 50, 255 };
static const Color MISSILE_TRAIL   = { 170, 200, 255, 255 };   // míssil da nave: fumaça azulada
#define MISSILE_TRAIL_MAX 9.0f
static const Color MISSILE_FLAME   = { 255, 190, 110, 255 };
static const Color BOMB_COLOR      = { 255,  70,  50, 255 };
static const Color MARK_COLOR      = { 255,  44,  32, 255 };
static const Color DIG_MARK_COLOR  = { 255, 150,  40, 255 };

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

// Só do lado do planeta voltado para a câmera; some aos poucos perto da borda.
static float facing_fade(Vector3 target, Vector3 eye)
{
    Vector3 n = Vector3Normalize(target);
    float facing = Vector3DotProduct(n, Vector3Normalize(Vector3Subtract(eye, target)));
    return Clamp(facing * 4.0f, 0.0f, 1.0f) * 0.9f;
}

// Marca no solo onde uma bomba vai cair. `closing` vai de 0 (acabou de
// marcar) a 1 (a bomba está caindo): o anel de fora se fecha sobre o alvo.
static void target_mark(Vector3 target, float radius, float closing, float time, Vector3 eye)
{
    float fade = facing_fade(target, eye);
    if (fade <= 0.0f) return;
    float blink = 0.6f + 0.4f * sinf(time * (8.0f + 16.0f * closing));
    ground_ring(target, radius, MARK_WIDTH, faded(MARK_COLOR, fade * blink));
    ground_ring(target, radius * Lerp(MARK_START, 1.0f, closing), MARK_WIDTH * 0.6f, faded(MARK_COLOR, fade * 0.7f));
    ground_ring(target, radius * 0.25f, 0.6f, faded(MARK_COLOR, fade * blink));
}

void projectiles_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Projectiles *p = &w->shots;
    const Enemies *e = &w->enemies;
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
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        if (en->type == ENEMY_BOMBER && en->arming > 0.0f) {
            float closing = 1.0f - en->arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
            target_mark(en->target, t->bomber_crater_radius, closing * 0.85f, time, eye);
        } else if (en->type == ENEMY_DIGGER && en->state == DIGGER_DRILL) {
            // A boca do túnel do Escavador, em laranja: é por ali que se entra.
            // Anel firme (transparência alta: sobre o mar, laranja fraco vira cinza) e
            // um segundo anel que pulsa para fora.
            Vector3 mouth = Vector3Scale(en->target, w->planet.params.radius);
            float fade = facing_fade(mouth, eye);
            float pulse = fmodf(time * 0.8f, 1.0f);
            float r = t->digger_drill_radius * 1.3f;
            if (fade > 0.0f) {
                ground_ring(mouth, r, 0.12f, faded(DIG_MARK_COLOR, fade));
                ground_ring(mouth, r * (1.0f + pulse * 0.8f), 0.08f, faded(DIG_MARK_COLOR, fade * (1.0f - pulse)));
            }
        }
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

    // Daqui em diante, tudo soma brilho.
    BeginBlendMode(BLEND_ADDITIVE);
    draw_links(w, t, alpha, glow, camera, time);

    // Tiros: riscos e bolas de luz.
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        float speed = Vector3Length(s->vel);
        if (speed < 1e-3f) continue;
        Vector3 dir = Vector3Scale(s->vel, 1.0f / speed);

        if (s->kind == SHOT_BOLT && s->source == BOLT_BROOD) {
            // Ferrão da Ninhada: um risco verde, curto e grosso, que ondula.
            float length = fminf(speed * 0.06f, s->age * speed + 0.3f);
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            streak(tail, pos, 2.6f, faded((Color){ 140, 255, 90, 255 }, 0.45f), eye);
            streak(tail, pos, 0.8f, faded((Color){ 230, 255, 200, 255 }, 0.95f), eye);
        } else if (s->kind == SHOT_BOLT && s->source == BOLT_MISSILE) {
            // Míssil: corpo branco curto e um rastro de fumaça azulada. Como o
            // tiro, fica fino e fraco perto da câmera (saindo da nave, viraria
            // um facho largo na frente da tela).
            float length = fminf(MISSILE_TRAIL_MAX, s->age * speed * 0.5f + 0.4f);   // curto: não passa por trás da câmera
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            Vector3 body = Vector3Subtract(pos, Vector3Scale(dir, 0.9f));
            float distance = Vector3Distance(pos, eye);
            float near = Clamp((distance - BOLT_NEAR) / (BOLT_CLEAR - BOLT_NEAR), 0.1f, 1.0f);
            float width = Clamp(distance / BOLT_WIDTH_AT, 0.25f, 1.5f);
            streak(tail, body, 2.4f * width, faded(MISSILE_TRAIL, 0.4f * near), eye);
            streak(body, pos, 0.8f * width, faded(WHITE, 0.95f * near), eye);
        } else if (s->kind == SHOT_BOLT && s->source == BOLT_SUICIDE) {
            // Drone suicida: um rastro laranja grosso (a cabeça acesa vem abaixo).
            float length = fminf(speed * 0.1f, s->age * speed + 0.4f);
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            streak(tail, pos, 3.0f, faded(SUICIDE_HALO, 0.45f), eye);
            streak(tail, pos, 0.9f, faded(SUICIDE_CORE, 0.95f), eye);
        } else if (s->kind == SHOT_BOLT) {
            // O risco não passa de onde o tiro saiu. Perto da câmera ele fica
            // mais fino e mais fraco; longe, um pouco mais grosso, para
            // continuar visível.
            float length = fminf(fminf(speed * BOLT_LENGTH, BOLT_LENGTH_MAX), s->age * speed + 0.5f);
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            float distance = Vector3Distance(pos, eye);
            float near = Clamp((distance - BOLT_NEAR) / (BOLT_CLEAR - BOLT_NEAR), 0.15f, 1.0f);
            float width = BOLT_WIDTH * Clamp(distance / BOLT_WIDTH_AT, 0.25f, 1.5f);
            streak(tail, pos, width * BOLT_HALO, faded(BOLT_HALO_COLOR, 0.5f * near), eye);
            streak(tail, pos, width, faded(BOLT_CORE, near), eye);
        } else if (s->kind == SHOT_PLASMA && s->source == ENEMY_SNIPER) {
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, speed * LANCE_LENGTH));
            streak(tail, pos, 1.4f, faded(LASER_COLOR, 0.6f), eye);
            streak(tail, pos, 0.4f, faded(WHITE, 0.9f), eye);
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
        if (s->kind == SHOT_PLASMA && s->source != ENEMY_SNIPER) {
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 1.6f, faded(PLASMA_HALO, 0.8f));
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 0.6f, PLASMA_CORE);
        } else if (s->kind == SHOT_BOLT && s->source == BOLT_MISSILE) {
            // A chama do motor, atrás.
            float speed = Vector3Length(s->vel);
            Vector3 back = (speed > 1e-3f) ? Vector3Scale(s->vel, -1.0f / speed) : (Vector3){ 0.0f, 0.0f, 0.0f };
            float flicker = 0.75f + 0.25f * sinf(time * 40.0f + (float)i);
            DrawBillboard(camera, glow, Vector3Add(pos, Vector3Scale(back, 1.0f)), 2.2f * flicker, faded(MISSILE_FLAME, 0.8f));
        } else if (s->kind == SHOT_BOLT && s->source == BOLT_SUICIDE) {
            float blink = 0.7f + 0.3f * sinf(time * 30.0f + (float)i);
            DrawBillboard(camera, glow, pos, 3.0f, faded(SUICIDE_HALO, 0.7f * blink));
            DrawBillboard(camera, glow, pos, 1.1f, SUICIDE_CORE);
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
