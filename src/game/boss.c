// A parte comum dos bosses: nascer, pensar (entrada, luta e morte), peças,
// dano e fases. O que cada tipo faz está em boss_breaker.c, boss_hive.c...
#include "game/boss.h"

#include <math.h>
#include <string.h>

#include "game/boss_internal.h"

#define DYING_BLAST_GAP 0.22f   // segundos entre as explosões da morte

const char *boss_name(BossType type)
{
    switch (type) {
    case BOSS_HIVE: return "COLMEIA LUNAR";
    case BOSS_WORM: return "VERME DO NÚCLEO";
    default:        return "ROMPE-MUNDOS";
    }
}

float boss_enter_time(const Boss *b, const Tuning *t)
{
    switch ((BossType)b->type) {
    case BOSS_HIVE: return t->hive_enter_time;
    case BOSS_WORM: return t->worm_enter_time;
    default:        return t->breaker_enter_time;
    }
}

float boss_points(BossType type, const Tuning *t)
{
    switch (type) {
    case BOSS_HIVE: return t->hive_points;
    case BOSS_WORM: return t->worm_points;
    default:        return t->breaker_points;
    }
}

float boss_xp(BossType type, const Tuning *t)
{
    switch (type) {
    case BOSS_HIVE: return t->hive_xp;
    case BOSS_WORM: return t->worm_xp;
    default:        return t->breaker_xp;
    }
}

void boss_phase_marks(BossType type, const Tuning *t, float out[2])
{
    switch (type) {
    case BOSS_HIVE:
        out[0] = t->hive_phase_2;
        out[1] = t->hive_phase_3;
        break;
    case BOSS_WORM:
        out[0] = t->worm_phase_2;
        out[1] = t->worm_phase_3;
        break;
    default:
        out[0] = t->breaker_phase_2;
        out[1] = t->breaker_phase_3;
        break;
    }
}

void boss_reset(Boss *b)
{
    memset(b, 0, sizeof *b);
    b->state = BOSS_OFF;
    b->rot = QuaternionIdentity();
    b->rot_prev = b->rot;
    b->hive.inflating = -1;
    b->hive.moon_scale = 1.0f;
}

bool boss_active(const Boss *b)
{
    return b->state != BOSS_OFF;
}

bool boss_vulnerable(const Boss *b)
{
    return b->state == BOSS_FIGHT;
}

Vector3 boss_point(const Boss *b, const Tuning *t, Vector3 local)
{
    (void)t;
    return Vector3Add(b->pos, Vector3RotateByQuaternion(Vector3Scale(local, b->scale), b->rot));
}

int boss_parts(const Boss *b, const Tuning *t, BossPart *out)
{
    if (!boss_active(b)) return 0;
    switch ((BossType)b->type) {
    case BOSS_HIVE: return hive_parts(b, t, out);
    case BOSS_WORM: return worm_parts(b, t, out);
    default:        return breaker_parts(b, t, out);
    }
}

float boss_core_factor(const Boss *b, const Tuning *t)
{
    if (!boss_vulnerable(b)) return 0.0f;
    switch ((BossType)b->type) {
    case BOSS_HIVE: return hive_core_factor(b, t);
    case BOSS_WORM: return worm_core_factor(b, t);
    default:        return breaker_core_factor(b, t);
    }
}

// ---------------------------------------------------------------------------

Vector3 boss_random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 p = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 1.0f, 0.0f, 0.0f });
    return Vector3Normalize(p);
}

void boss_face(Boss *b, Vector3 forward, Vector3 up, float rate, float dt)
{
    if (Vector3LengthSqr(forward) < 1e-6f) return;
    Quaternion target = quat_look(forward, up);
    b->rot = QuaternionNormalize(QuaternionSlerp(b->rot, target, 1.0f - expf(-rate * dt)));
}

void boss_spawn(Boss *b, BossType type, const BossContext *ctx, Vector3 dir)
{
    boss_reset(b);
    b->type = (uint8_t)type;
    b->state = BOSS_ENTER;
    b->phase = 1;
    switch (type) {
    case BOSS_HIVE: hive_spawn(b, ctx, Vector3Normalize(dir)); break;
    case BOSS_WORM: worm_spawn(b, ctx, Vector3Normalize(dir)); break;
    default:        breaker_spawn(b, ctx, Vector3Normalize(dir)); break;
    }
}

// Morrendo: explosões pelo casco, até a grande.
static void dying_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    b->timer -= dt;
    b->blast_wait -= dt;
    if (b->type == BOSS_HIVE && ctx->moon != NULL) {
        b->pos = ctx->moon->pos;   // a Colmeia morre agarrada à Lua
    } else if (b->type != BOSS_WORM) {   // o Verme morre onde está (o corpo não anda)
        b->vel = Vector3Scale(b->vel, 1.0f - dt);
        b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
    }
    if (b->blast_wait <= 0.0f) {
        BossPart parts[BOSS_PARTS_MAX];
        int n = boss_parts(b, t, parts);
        BossPart hull[BOSS_PARTS_MAX];
        int hulls = 0;
        for (int i = 0; i < n; i++) {
            if (parts[i].role == PART_HULL) hull[hulls++] = parts[i];
        }
        if (hulls > 0) {
            int k = (int)rng_below(ctx->rng, (uint32_t)hulls);
            Vector3 at = Vector3Add(hull[k].center, Vector3Scale(rng_unit_vec3(ctx->rng), hull[k].radius * 0.6f));
            events_push(ctx->events, ctx->tick, EV_BOSS_BLAST, b->type, at, b->scale);
            b->blast_now = true;
            b->blast_at = at;
        }
        b->blast_wait = DYING_BLAST_GAP;
    }
    if (b->timer <= 0.0f) {
        b->state = BOSS_OFF;
        b->defeated = true;
        events_push(ctx->events, ctx->tick, EV_BOSS_DOWN, b->type, b->pos, boss_points((BossType)b->type, t));
    }
}

void boss_tick(Boss *b, const BossContext *ctx)
{
    b->pos_prev = b->pos;
    b->rot_prev = b->rot;
    b->carve_count = 0;
    b->launch_count = 0;
    b->blast_now = false;
    // (hive.spore_count não: os esporos nascem de tiros, entre um passo e outro; o mundo esvazia.)
    WormState *m = &b->worm;
    memcpy(m->seg_prev, m->seg, sizeof m->seg);
    m->restore_count = 0;
    m->crater_count = 0;
    m->ship_damage = 0.0f;
    m->dust_now = false;
    if (b->state == BOSS_OFF) return;
    float dt = ctx->dt;
    b->age += dt;
    b->flash = fmaxf(b->flash - dt, 0.0f);
    b->core_flash = fmaxf(b->core_flash - dt, 0.0f);
    for (int i = 0; i < BOSS_CAPACITORS; i++) b->breaker.cap_flash[i] = fmaxf(b->breaker.cap_flash[i] - dt, 0.0f);
    for (int i = 0; i < HIVE_SACS; i++) b->hive.sac_flash[i] = fmaxf(b->hive.sac_flash[i] - dt, 0.0f);
    for (int i = 0; i < HIVE_THRUSTERS; i++) b->hive.thr_flash[i] = fmaxf(b->hive.thr_flash[i] - dt, 0.0f);
    for (int i = 0; i < WORM_NODULES; i++) m->nodule_flash[i] = fmaxf(m->nodule_flash[i] - dt, 0.0f);
    m->tail_flash = fmaxf(m->tail_flash - dt, 0.0f);

    switch (b->state) {
    case BOSS_ENTER:
        switch ((BossType)b->type) {
        case BOSS_HIVE: hive_enter(b, ctx); break;
        case BOSS_WORM: worm_enter(b, ctx); break;
        default:        breaker_enter(b, ctx); break;
        }
        break;
    case BOSS_FIGHT:
        switch ((BossType)b->type) {
        case BOSS_HIVE: hive_fight(b, ctx); break;
        case BOSS_WORM: worm_fight(b, ctx); break;
        default:        breaker_fight(b, ctx); break;
        }
        break;
    default:
        dying_tick(b, ctx);
        break;
    }
}

// ---------------------------------------------------------------------------

static void update_phase(Boss *b, const Tuning *t, Events *events, uint32_t tick)
{
    float marks[2];
    boss_phase_marks((BossType)b->type, t, marks);
    float ratio = b->hp / fmaxf(b->hp_max, 1e-3f);
    int phase = (ratio < marks[1]) ? 3 : (ratio < marks[0]) ? 2 : 1;
    if (phase > b->phase) {
        b->phase = (uint8_t)phase;
        if (b->type == BOSS_WORM) worm_new_phase(b, t);
        events_push(events, tick, EV_BOSS_PHASE, b->type, b->pos, (float)phase);
    }
}

float boss_damage_core(Boss *b, const Tuning *t, float amount, float factor, Events *events, uint32_t tick)
{
    float applied = amount * factor;
    if (applied <= 0.0f || !boss_vulnerable(b)) return 0.0f;
    b->hp -= applied;
    b->core_flash = 0.12f;
    b->flash = 0.08f;
    if (b->hp <= 0.0f) {
        b->hp = 0.0f;
        b->state = BOSS_DYING;
        b->action = 0;
        b->timer = BOSS_DYING_TIME;
        b->blast_wait = 0.0f;
        return applied;
    }
    update_phase(b, t, events, tick);
    return applied;
}

float boss_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick)
{
    if (!boss_vulnerable(b)) return 0.0f;
    switch ((BossType)b->type) {
    case BOSS_HIVE: return hive_hit(b, t, id, amount, events, tick);
    case BOSS_WORM: return worm_hit(b, t, id, amount, events, tick);
    default:        return breaker_hit(b, t, id, amount, events, tick);
    }
}

float boss_hit_core_raw(Boss *b, const Tuning *t, float amount, Events *events, uint32_t tick)
{
    return boss_damage_core(b, t, amount, 1.0f, events, tick);
}

void boss_set_health(Boss *b, const Tuning *t, float fraction, Events *events, uint32_t tick)
{
    if (!boss_vulnerable(b)) return;
    if (fraction <= 0.0f) {   // zero: mata (começa a explodir)
        boss_damage_core(b, t, b->hp + 1.0f, 1.0f, events, tick);
        return;
    }
    b->hp = fmaxf(b->hp_max * Clamp(fraction, 0.0f, 1.0f), 0.01f);
    update_phase(b, t, events, tick);
}
