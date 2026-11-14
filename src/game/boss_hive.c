// Colmeia Lunar: uma massa viva agarrada à Lua.
//
//   NEST    de tempos em tempos uma bolsa incha (o aviso) e solta drones. Se a
//           nave estoura a bolsa antes, a ninhada morre junto. O coração, de
//           frente para o planeta, fica fechado (só passa uma fração do dano);
//   OPEN    todas as bolsas estouradas: o coração abre e leva o dano inteiro;
//   REGROW  as bolsas renascem (só levam dano inteiras) e tudo recomeça.
//
// Da fase 2 em diante nascem três propulsores atrás da Lua, que a empurram
// contra a Terra. Com a Lua perto, um tentáculo morde a crosta embaixo dela,
// mais rápido quanto mais perto. Destruídos os três, a Lua volta devagar até
// eles renascerem. Na fase 3 o coração cospe ácido na nave e fica entreaberto.
// Bolsas estouradas pela nave deixam esporos (uma a cada duas): o mundo os
// solta, e o raio trator pode arremessá-los no coração.
#include "game/boss.h"

#include <math.h>

#include "game/boss_internal.h"

#define SAC_LIFT        1.06f   // a bolsa fica com o centro este tanto (em raios da Lua) do centro dela
#define THRUSTER_LIFT   1.04f
#define INFLATE_SWELL   0.7f    // a bolsa inchando cresce até 1 + isto
#define THRUSTER_GROW   2.0f    // segundos para um propulsor nascer
#define HULL_RADIUS     0.24f   // carapaça em volta do coração
#define FACE_RATE       6.0f
#define SPIT_LIFE       6.0f

const Vector3 HIVE_SAC[HIVE_SACS] = {
    { 0.53f, 0.43f, -0.73f },  { -0.54f, -0.40f, -0.74f }, { 0.84f, -0.36f, 0.40f },
    { -0.78f, 0.47f, 0.41f },  { 0.20f, 0.80f, 0.56f },    { -0.15f, -0.76f, 0.63f },
};
const float   HIVE_SAC_RADIUS = 0.2f;
const Vector3 HIVE_THRUSTER[HIVE_THRUSTERS] = {
    { 0.0f, 0.45f, 0.89f }, { 0.42f, -0.26f, 0.87f }, { -0.42f, -0.26f, 0.87f },
};
const float   HIVE_THRUSTER_RADIUS = 0.17f;
const Vector3 HIVE_CORE = { 0.0f, 0.0f, -1.04f };
const float   HIVE_CORE_RADIUS = 0.3f;

#define HIVE_HULLS 6
static const Vector3 HIVE_HULL[HIVE_HULLS] = {
    { 0.50f, 0.26f, -0.86f }, { -0.50f, 0.26f, -0.86f }, { 0.50f, -0.26f, -0.86f },
    { -0.50f, -0.26f, -0.86f }, { 0.0f, 0.55f, -0.85f }, { 0.0f, -0.55f, -0.85f },
};

// Números das peças para boss_hit: bolsas, propulsores, coração, carapaça.
#define ID_THRUSTER HIVE_SACS
#define ID_CORE     (HIVE_SACS + HIVE_THRUSTERS)
#define ID_HULL     (ID_CORE + 1)

int hive_sacs_left(const Boss *b)
{
    int n = 0;
    for (int i = 0; i < HIVE_SACS; i++) n += (b->hive.sac_hp[i] > 0.0f) ? 1 : 0;
    return n;
}

int hive_thrusters_left(const Boss *b)
{
    if (!b->hive.thr_born) return 0;
    int n = 0;
    for (int i = 0; i < HIVE_THRUSTERS; i++) n += (b->hive.thr_hp[i] > 0.0f) ? 1 : 0;
    return n;
}

Vector3 hive_sac_point(const Boss *b, int k)
{
    return Vector3Add(b->pos, Vector3RotateByQuaternion(Vector3Scale(Vector3Normalize(HIVE_SAC[k]), SAC_LIFT * b->scale), b->rot));
}

Vector3 hive_thruster_point(const Boss *b, int k)
{
    return Vector3Add(b->pos, Vector3RotateByQuaternion(Vector3Scale(Vector3Normalize(HIVE_THRUSTER[k]), THRUSTER_LIFT * b->scale),
                                                       b->rot));
}

float hive_sac_size(const Boss *b, const Tuning *t, int k)
{
    const HiveState *h = &b->hive;
    float swell = 0.0f;
    if (h->inflating == k) swell = INFLATE_SWELL * (1.0f - Clamp(h->inflate / fmaxf(t->hive_inflate_time, 1e-3f), 0.0f, 1.0f));
    return h->sac_grow[k] * (1.0f + swell);
}

float hive_core_factor(const Boss *b, const Tuning *t)
{
    if (b->action == HIVE_OPEN) return 1.0f;
    return (b->phase >= 3) ? t->hive_core_open : t->hive_core_armor;
}

// ---------------------------------------------------------------------------

// Frente (-Z) para o planeta, topo para cima (o eixo Y do mundo: a órbita da
// Lua é quase equatorial).
static Vector3 toward_planet(Vector3 pos)
{
    return Vector3Normalize(Vector3Negate(pos));
}

void hive_spawn(Boss *b, const BossContext *ctx, Vector3 dir)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    b->action = HIVE_REGROW;
    b->scale = ctx->moon->radius;
    b->hp = b->hp_max = t->hive_hp;
    // A entrada é relativa à Lua (que continua andando): enter_from guarda o
    // deslocamento inicial a partir dela.
    b->enter_from = Vector3Scale(dir, ctx->moon->radius * t->hive_spawn_distance);
    b->pos = b->pos_prev = Vector3Add(ctx->moon->pos, b->enter_from);
    b->rot = quat_look(Vector3Negate(dir), (Vector3){ 0.0f, 1.0f, 0.0f });
    b->rot_prev = b->rot;
    b->timer = t->hive_enter_time;
    h->inflating = -1;
    h->moon_scale = ctx->moon->scale;
    for (int k = 0; k < HIVE_SACS; k++) {
        h->sac_hp[k] = t->hive_sac_hp;
        h->sac_grow[k] = 0.0f;
    }
    events_push(ctx->events, ctx->tick, EV_BOSS_ENTER, (uint8_t)b->type, ctx->moon->pos, t->hive_enter_time);
}

void hive_enter(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    b->timer -= dt;
    float u = 1.0f - Clamp(b->timer / t->hive_enter_time, 0.0f, 1.0f);
    float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    Vector3 before = b->pos;
    b->pos = Vector3Add(ctx->moon->pos, Vector3Scale(b->enter_from, 1.0f - ease));
    b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
    boss_face(b, toward_planet(ctx->moon->pos), (Vector3){ 0.0f, 1.0f, 0.0f }, FACE_RATE * ease + 0.5f, dt);
    if (b->timer <= 0.0f) {
        // Agarrou a Lua: as bolsas brotam.
        b->state = BOSS_FIGHT;
        b->action = HIVE_REGROW;
        b->timer = t->hive_sac_regrow;
    }
}

// Distância do centro à superfície ORIGINAL do planeta na direção `dir` (as
// mordidas do tentáculo são fundas em relação a ela: no mesmo ponto, não
// cavam mais fundo a cada mordida).
static float original_ground(const Planet *planet, Vector3 dir)
{
    Terrain t = planet_terrain(&planet->params, dir);
    return planet->params.radius + (t.land ? t.elevation : 0.0f);
}

// O tentáculo morde a crosta perto do ponto embaixo da Lua.
static void tide_bite(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    Vector3 under = Vector3Normalize(ctx->moon->pos);
    Vector3 side = boss_random_perpendicular(ctx->rng, under);
    float radius = ctx->planet->params.radius;
    float spread = t->hive_tide_spread * rng_float(ctx->rng) / fmaxf(radius, 1.0f);
    Vector3 dir = Vector3Normalize(Vector3RotateByAxisAngle(under, side, spread));
    float ground = original_ground(ctx->planet, dir);
    h->tide_point = Vector3Scale(dir, ground);
    if (b->carve_count < BOSS_CARVES) {
        b->carves[b->carve_count++] = (CarveRequest){ Vector3Scale(dir, ground - t->hive_tide_sink), t->hive_tide_radius,
                                                      ENEMY_TYPE_COUNT };
    }
}

static void nest_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    float dt = ctx->dt;
    if (h->inflating < 0) {
        h->inflate_wait -= dt;
        if (h->inflate_wait > 0.0f) return;
        // Uma bolsa inteira, sorteada.
        int ready[HIVE_SACS], n = 0;
        for (int k = 0; k < HIVE_SACS; k++) {
            if (h->sac_hp[k] > 0.0f && h->sac_grow[k] >= 1.0f) ready[n++] = k;
        }
        if (n == 0) return;
        h->inflating = ready[rng_below(ctx->rng, (uint32_t)n)];
        h->inflate = t->hive_inflate_time;
        events_push(ctx->events, ctx->tick, EV_HIVE_INFLATE, b->type, hive_sac_point(b, h->inflating), h->inflate);
        return;
    }
    h->inflate -= dt;
    if (h->inflate > 0.0f) return;
    // Com drones demais em cena, a bolsa fica cheia esperando.
    if (ctx->drones + t->hive_brood > t->hive_brood_cap) {
        h->inflate = 0.0f;
        return;
    }
    Vector3 at = hive_sac_point(b, h->inflating);
    Vector3 out = Vector3Normalize(Vector3Subtract(at, b->pos));
    b->launch_count = t->hive_brood;
    b->launch_type = ENEMY_SWARM;
    b->launch_from = Vector3Add(at, Vector3Scale(out, HIVE_SAC_RADIUS * b->scale * 3.0f));
    events_push(ctx->events, ctx->tick, EV_HIVE_BIRTH, b->type, at, (float)t->hive_brood);
    h->inflating = -1;
    h->inflate_wait = t->hive_inflate[b->phase - 1];
}

static void thrusters_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    float dt = ctx->dt;
    if (b->phase < 2) return;
    if (!h->thr_born) {
        h->thr_born = true;
        for (int k = 0; k < HIVE_THRUSTERS; k++) {
            h->thr_hp[k] = t->hive_thruster_hp;
            h->thr_grow[k] = 0.0f;
        }
        events_push(ctx->events, ctx->tick, EV_HIVE_PUSH, b->type, b->pos, 1.0f);
    }
    int alive = 0;
    for (int k = 0; k < HIVE_THRUSTERS; k++) {
        if (h->thr_hp[k] <= 0.0f) continue;
        h->thr_grow[k] = fminf(h->thr_grow[k] + dt / THRUSTER_GROW, 1.0f);
        if (h->thr_grow[k] >= 1.0f) alive++;
    }
    h->pushing = alive > 0;
    if (h->pushing) {
        h->moon_scale = fmaxf(h->moon_scale - t->hive_push_rate * (float)alive / (float)HIVE_THRUSTERS * dt, t->hive_moon_min);
    } else {
        h->moon_scale = fminf(h->moon_scale + t->hive_recede_rate * dt, 1.0f);
    }
    if (hive_thrusters_left(b) == 0) {
        h->thr_regrow -= dt;
        if (h->thr_regrow <= 0.0f) {
            for (int k = 0; k < HIVE_THRUSTERS; k++) {
                h->thr_hp[k] = t->hive_thruster_hp;
                h->thr_grow[k] = 0.0f;
            }
            events_push(ctx->events, ctx->tick, EV_HIVE_PUSH, b->type, b->pos, 1.0f);
        }
    }
}

static void spit_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    const Ship *s = ctx->ship;
    if (b->phase < 3 || s == NULL || !s->alive) {
        h->spit_charge = 0.0f;
        return;
    }
    float dt = ctx->dt;
    if (h->spit_charge > 0.0f) {
        h->spit_charge -= dt;
        if (h->spit_charge > 0.0f) return;
        h->spit_charge = 0.0f;
        Vector3 from = boss_point(b, t, HIVE_CORE);
        float d = Vector3Distance(from, s->pos);
        Vector3 aim = Vector3Add(s->pos, Vector3Scale(Vector3Add(s->vel, s->fall), d / t->hive_spit_speed));
        Vector3 dir = Vector3Normalize(Vector3Subtract(aim, from));
        Shot *shot = projectiles_spawn(ctx->shots, SHOT_PLASMA, from, Vector3Scale(dir, t->hive_spit_speed), SPIT_LIFE);
        if (shot != NULL) {
            shot->damage = t->hive_spit_damage;
            shot->source = ENEMY_SIPHON;   // verde, como o ácido
        }
        events_push(ctx->events, ctx->tick, EV_ENEMY_SHOT, ENEMY_SIPHON, from, 0.0f);
        h->spit_wait = t->hive_spit_interval;
        return;
    }
    h->spit_wait -= dt;
    if (h->spit_wait <= 0.0f) {
        h->spit_charge = t->hive_spit_warn;
        events_push(ctx->events, ctx->tick, EV_HIVE_SPIT_WARN, b->type, boss_point(b, t, HIVE_CORE), t->hive_spit_warn);
    }
}

void hive_fight(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    HiveState *h = &b->hive;
    float dt = ctx->dt;
    const Moon *moon = ctx->moon;

    // Agarrada à Lua, de frente para o planeta.
    b->vel = Vector3Scale(Vector3Subtract(moon->pos, b->pos), 1.0f / dt);
    b->pos = moon->pos;
    boss_face(b, toward_planet(moon->pos), (Vector3){ 0.0f, 1.0f, 0.0f }, FACE_RATE, dt);

    for (int k = 0; k < HIVE_SACS; k++) {
        if (h->sac_hp[k] > 0.0f) h->sac_grow[k] = fminf(h->sac_grow[k] + dt / t->hive_sac_regrow, 1.0f);
    }

    switch (b->action) {
    case HIVE_REGROW:
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = HIVE_NEST;
            h->inflate_wait = t->hive_inflate[b->phase - 1] * 0.5f;
        }
        break;
    case HIVE_NEST:
        nest_tick(b, ctx);
        break;
    default:   // HIVE_OPEN
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            // O coração fecha e as bolsas brotam de novo.
            b->action = HIVE_REGROW;
            b->timer = t->hive_sac_regrow;
            h->inflating = -1;
            for (int k = 0; k < HIVE_SACS; k++) {
                h->sac_hp[k] = t->hive_sac_hp;
                h->sac_grow[k] = 0.0f;
            }
        }
        break;
    }

    thrusters_tick(b, ctx);

    // A maré: com a Lua perto, o tentáculo morde a crosta.
    h->tide = Clamp((t->hive_tide_start - h->moon_scale) / fmaxf(t->hive_tide_start - t->hive_moon_min, 1e-3f), 0.0f, 1.0f);
    if (h->tide > 0.0f) {
        h->tide_wait -= dt;
        if (h->tide_wait <= 0.0f) {
            tide_bite(b, ctx);
            h->tide_wait = Lerp(t->hive_tide_slow, t->hive_tide_fast, h->tide);
        }
    } else {
        h->tide_wait = t->hive_tide_slow * 0.5f;
    }

    spit_tick(b, ctx);
}

// ---------------------------------------------------------------------------

int hive_parts(const Boss *b, const Tuning *t, BossPart *out)
{
    const HiveState *h = &b->hive;
    bool vulnerable = boss_vulnerable(b);
    int n = 0;
    for (int k = 0; k < HIVE_SACS; k++) {
        if (h->sac_hp[k] <= 0.0f || h->sac_grow[k] <= 0.05f) continue;
        bool open = vulnerable && h->sac_grow[k] >= 1.0f;
        float aim = !open ? 0.0f : (h->inflating == k) ? 0.1f : 0.35f;
        Vector3 facing = Vector3RotateByQuaternion(Vector3Normalize(HIVE_SAC[k]), b->rot);
        out[n++] = (BossPart){ hive_sac_point(b, k), HIVE_SAC_RADIUS * b->scale * hive_sac_size(b, t, k), PART_TARGET,
                               (uint8_t)k, open, false, aim, facing };
    }
    if (h->thr_born) {
        for (int k = 0; k < HIVE_THRUSTERS; k++) {
            if (h->thr_hp[k] <= 0.0f) continue;
            bool open = vulnerable && h->thr_grow[k] >= 1.0f;
            // Quanto mais perto a Lua, mais urgente parar o empurrão.
            float aim = !open ? 0.0f : 0.45f - 0.3f * h->tide;
            Vector3 facing = Vector3RotateByQuaternion(Vector3Normalize(HIVE_THRUSTER[k]), b->rot);
            out[n++] = (BossPart){ hive_thruster_point(b, k), HIVE_THRUSTER_RADIUS * b->scale * fmaxf(h->thr_grow[k], 0.2f),
                                   PART_TARGET, (uint8_t)(ID_THRUSTER + k), open, false, aim, facing };
        }
    }
    float factor = boss_core_factor(b, t);
    float core_aim = !vulnerable ? 0.0f : (factor >= 0.99f) ? 0.12f : (factor >= 0.25f) ? 0.6f : 0.0f;
    out[n++] = (BossPart){ boss_point(b, t, HIVE_CORE), HIVE_CORE_RADIUS * b->scale, PART_CORE, ID_CORE, factor >= 0.99f,
                           true, core_aim, Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, -1.0f }, b->rot) };
    for (int k = 0; k < HIVE_HULLS; k++) {
        out[n++] = (BossPart){ boss_point(b, t, HIVE_HULL[k]), HULL_RADIUS * b->scale, PART_HULL, (uint8_t)(ID_HULL + k), false,
                               true, 0.0f, { 0.0f, 0.0f, 0.0f } };
    }
    return n;
}

static float damage_sac(Boss *b, const Tuning *t, int k, float amount, Events *events, uint32_t tick)
{
    HiveState *h = &b->hive;
    if (h->sac_hp[k] <= 0.0f || h->sac_grow[k] < 1.0f) return 0.0f;
    float applied = fminf(amount, h->sac_hp[k]);
    h->sac_hp[k] -= amount;
    h->sac_flash[k] = 0.12f;
    if (h->sac_hp[k] > 0.0f) return applied;

    h->sac_hp[k] = 0.0f;
    h->popped++;
    Vector3 at = hive_sac_point(b, k);
    int left = hive_sacs_left(b);
    events_push(events, tick, EV_HIVE_POP, b->type, at, (float)left);
    if (h->inflating == k) {
        // Estourou a bolsa cheia: a ninhada morre junto.
        h->inflating = -1;
        h->inflate_wait = t->hive_inflate[b->phase - 1] * 0.6f;
    }
    if (h->popped % 2 == 0 && h->spore_count < 2) h->spore_at[h->spore_count++] = at;
    if (left == 0 && b->action != HIVE_OPEN) {
        b->action = HIVE_OPEN;
        b->timer = t->hive_open_time;
        h->inflating = -1;
        events_push(events, tick, EV_BOSS_STAGGER, b->type, boss_point(b, t, HIVE_CORE), t->hive_open_time);
    }
    return applied;
}

static float damage_thruster(Boss *b, const Tuning *t, int k, float amount, Events *events, uint32_t tick)
{
    HiveState *h = &b->hive;
    if (!h->thr_born || h->thr_hp[k] <= 0.0f || h->thr_grow[k] < 1.0f) return 0.0f;
    float applied = fminf(amount, h->thr_hp[k]);
    h->thr_hp[k] -= amount;
    h->thr_flash[k] = 0.12f;
    if (h->thr_hp[k] > 0.0f) return applied;
    h->thr_hp[k] = 0.0f;
    int left = hive_thrusters_left(b);
    events_push(events, tick, EV_HIVE_THRUSTER, b->type, hive_thruster_point(b, k), (float)left);
    if (left == 0) {
        h->thr_regrow = t->hive_thruster_regrow;
        events_push(events, tick, EV_HIVE_PUSH, b->type, b->pos, 0.0f);
    }
    return applied;
}

float hive_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick)
{
    if (id < HIVE_SACS) return damage_sac(b, t, id, amount, events, tick);
    if (id < ID_CORE) return damage_thruster(b, t, id - ID_THRUSTER, amount, events, tick);
    if (id == ID_CORE) return boss_damage_core(b, t, amount, hive_core_factor(b, t), events, tick);
    return 0.0f;   // carapaça
}
