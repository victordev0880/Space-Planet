#include "game/allies.h"

#include <math.h>
#include <string.h>

#define ESCORT_SPIN     1.3f    // voltas da órbita, em radianos por segundo
#define ESCORT_FOLLOW   9.0f    // rapidez com que o drone acompanha o seu lugar
#define FIGHTER_ACCEL   110.0f  // aceleração máxima do caça
#define FIGHTER_PASS    14.0f   // perto assim do alvo, passa direto (e faz a volta depois)
#define FIGHTER_FIRE    75.0f   // atira até esta distância...
#define FIGHTER_CONE    0.92f   // ... com o alvo até este cosseno do nariz
#define FIGHTER_HOME    10.0f   // sem alvo, fica este tanto atrás da nave
#define GROUND_MARGIN   4.0f    // não desce abaixo desta altura
#define FIRE_JITTER     0.15f   // variação da cadência, para os tiros não saírem juntos

void allies_reset(Allies *a)
{
    memset(a, 0, sizeof *a);
}

int allies_alive(const Allies *a)
{
    int n = 0;
    for (int i = 0; i < ALLIES_MAX; i++) n += a->items[i].alive ? 1 : 0;
    return n;
}

void allies_set_count(Allies *a, const Ship *ship, int escorts, int fighters)
{
    if (escorts < 0) escorts = 0;
    if (fighters < 0) fighters = 0;
    if (escorts > ALLIES_MAX) escorts = ALLIES_MAX;
    if (escorts + fighters > ALLIES_MAX) fighters = ALLIES_MAX - escorts;
    a->escorts = escorts;
    a->fighters = fighters;
    for (int i = 0; i < ALLIES_MAX; i++) {
        Ally *al = &a->items[i];
        bool wanted = i < escorts + fighters;
        uint8_t kind = (uint8_t)((i < escorts) ? ALLY_ESCORT : ALLY_FIGHTER);
        if (wanted && (!al->alive || al->kind != kind)) {
            memset(al, 0, sizeof *al);
            al->alive = true;
            al->kind = kind;
            al->pos = ship->pos;
            al->pos_prev = ship->pos;
            al->rot = ship->rot;
            al->rot_prev = ship->rot;
            al->target = -1;
            al->fire_wait = 0.3f * (float)i;
        } else if (!wanted) {
            al->alive = false;
        }
    }
}

// O inimigo vivo mais perto de `from`, até `range` dele (-1 se nenhum).
static int nearest_enemy(const Enemies *e, Vector3 from, float range)
{
    int best = -1;
    float best_d = range * range;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        float d = Vector3DistanceSqr(e->items[i].pos, from);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static void fire(Ally *al, const AllyContext *ctx, const Enemy *target, float damage, float rate)
{
    const Tuning *t = ctx->t;
    if (al->fire_wait > 0.0f || rate <= 0.0f) return;
    float distance = Vector3Distance(al->pos, target->pos);
    Vector3 aim = Vector3Add(target->pos, Vector3Scale(target->vel, distance / t->weapon_speed));
    Vector3 dir = Vector3Normalize(Vector3Subtract(aim, al->pos));
    Shot *s = projectiles_spawn(ctx->shots, SHOT_BOLT, al->pos, Vector3Scale(dir, t->weapon_speed),
                                t->weapon_range / t->weapon_speed);
    if (s != NULL) {
        s->damage = t->weapon_damage * damage;
        s->source = BOLT_ALLY;
    }
    events_push(ctx->events, ctx->tick, EV_ALLY_SHOT, al->kind, al->pos, 0.0f);
    al->fire_wait = (1.0f + rng_range(ctx->rng, -FIRE_JITTER, FIRE_JITTER)) / rate;
}

// Olha para `forward`, com o topo para longe do planeta.
static void face(Ally *al, Vector3 forward, float dt)
{
    if (Vector3LengthSqr(forward) < 1e-6f) return;
    Quaternion target = quat_look(forward, al->pos);
    al->rot = QuaternionNormalize(QuaternionSlerp(al->rot, target, 1.0f - expf(-10.0f * dt)));
}

static void keep_off_ground(Ally *al, const Planet *planet)
{
    float distance = Vector3Length(al->pos);
    float floor = planet->params.radius + GROUND_MARGIN;
    if (distance < floor && distance > 1e-3f) al->pos = Vector3Scale(al->pos, floor / distance);
}

static void tick_escort(Ally *al, int slot, int count, const AllyContext *ctx)
{
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    float time = (float)ctx->tick * ctx->dt;
    float phase = time * ESCORT_SPIN + 2.0f * PI * (float)slot / (float)(count > 0 ? count : 1);
    Vector3 right = ship_right(s), up = ship_up(s), forward = ship_forward(s);
    Vector3 offset = Vector3Add(Vector3Scale(right, cosf(phase) * t->escort_orbit),
                                Vector3Add(Vector3Scale(up, (0.5f + 0.4f * sinf(phase)) * t->escort_orbit * 0.6f),
                                           Vector3Scale(forward, -0.3f * t->escort_orbit)));
    Vector3 spot = Vector3Add(s->pos, offset);
    Vector3 before = al->pos;
    al->pos = Vector3Lerp(al->pos, spot, 1.0f - expf(-ESCORT_FOLLOW * ctx->dt));
    al->vel = Vector3Scale(Vector3Subtract(al->pos, before), 1.0f / ctx->dt);

    al->target = nearest_enemy(ctx->enemies, s->pos, t->escort_range);
    if (al->target >= 0) {
        const Enemy *en = &ctx->enemies->items[al->target];
        face(al, Vector3Subtract(en->pos, al->pos), ctx->dt);
        fire(al, ctx, en, t->escort_damage, t->escort_rate * ctx->rate_mult);
    } else {
        face(al, forward, ctx->dt);
    }
}

static void tick_fighter(Ally *al, int slot, const AllyContext *ctx)
{
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    float speed = t->fighters_speed;
    al->target = nearest_enemy(ctx->enemies, s->pos, t->fighters_range);

    Vector3 desired;
    if (al->target >= 0) {
        const Enemy *en = &ctx->enemies->items[al->target];
        Vector3 to = Vector3Subtract(en->pos, al->pos);
        float d = Vector3Length(to);
        if (d < FIGHTER_PASS && Vector3LengthSqr(al->vel) > 1.0f) {
            desired = Vector3Scale(Vector3Normalize(al->vel), speed);   // passa direto e volta depois
        } else {
            desired = Vector3Scale(to, speed / fmaxf(d, 1e-3f));
        }
        Vector3 nose = (Vector3LengthSqr(al->vel) > 1e-4f) ? Vector3Normalize(al->vel) : ship_forward(s);
        if (d < FIGHTER_FIRE && Vector3DotProduct(nose, Vector3Scale(to, 1.0f / fmaxf(d, 1e-3f))) > FIGHTER_CONE) {
            fire(al, ctx, en, t->fighters_damage, t->fighters_rate * ctx->rate_mult);
        }
    } else {
        // Sem alvo: volta para perto da nave, um de cada lado.
        float side = (slot % 2) ? 1.0f : -1.0f;
        Vector3 home = Vector3Add(s->pos, Vector3Add(Vector3Scale(ship_forward(s), -FIGHTER_HOME),
                                                     Vector3Scale(ship_right(s), side * FIGHTER_HOME * 0.6f)));
        desired = Vector3Add(Vector3Scale(Vector3Subtract(home, al->pos), 2.0f), Vector3Add(s->vel, s->fall));
        if (Vector3Length(desired) > speed * 1.5f) desired = Vector3Scale(Vector3Normalize(desired), speed * 1.5f);
    }

    Vector3 change = Vector3Subtract(desired, al->vel);
    float limit = FIGHTER_ACCEL * ctx->dt;
    float amount = Vector3Length(change);
    if (amount > limit) change = Vector3Scale(change, limit / amount);
    al->vel = Vector3Add(al->vel, change);
    al->pos = Vector3Add(al->pos, Vector3Scale(al->vel, ctx->dt));
    face(al, al->vel, ctx->dt);
}

void allies_tick(Allies *a, const AllyContext *ctx)
{
    const Ship *s = ctx->ship;
    int escort_slot = 0, fighter_slot = 0;
    for (int i = 0; i < ALLIES_MAX; i++) {
        Ally *al = &a->items[i];
        if (!al->alive) continue;
        al->pos_prev = al->pos;
        al->rot_prev = al->rot;
        al->fire_wait -= ctx->dt;
        if (s == NULL || !s->alive) {
            // Nave destruída: os aliados esperam junto dela, sem atirar.
            al->target = -1;
            continue;
        }
        if (al->kind == ALLY_ESCORT) tick_escort(al, escort_slot++, a->escorts, ctx);
        else                         tick_fighter(al, fighter_slot++, ctx);
        keep_off_ground(al, ctx->planet);
    }
}
