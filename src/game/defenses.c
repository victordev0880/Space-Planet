#include "game/defenses.h"

#include <math.h>
#include <string.h>

#define FLASH_TIME     0.12f
#define ZAP_TIME       0.15f
#define BLOCK_FLASH    0.35f
#define SHIELD_LIFT    0.0f    // o escudo nasce no ponto sob a nave (a cúpula é desenhada a partir do solo)
#define TURRET_AHEAD   22.0f   // a torre nasce este tanto à frente da nave, pela órbita

void defenses_reset(Defenses *d)
{
    memset(d, 0, sizeof *d);
}

int defenses_count(const Defenses *d, DefenseType type)
{
    int n = 0;
    for (int i = 0; i < DEFENSES_MAX; i++) n += (d->items[i].alive && d->items[i].type == type) ? 1 : 0;
    return n;
}

int defense_cost(const Tuning *t, DefenseType type)
{
    switch (type) {
    case DEF_TURRET: return t->turret_cost;
    case DEF_SHIELD: return t->shield_cost;
    default:         return t->satellite_cost;
    }
}

int defense_max(const Tuning *t, DefenseType type)
{
    switch (type) {
    case DEF_TURRET: return t->turret_max;
    case DEF_SHIELD: return t->shield_max;
    default:         return t->satellite_max;
    }
}

const char *defense_name(DefenseType type)
{
    switch (type) {
    case DEF_TURRET: return "Torre orbital";
    case DEF_SHIELD: return "Escudo local";
    default:         return "Satélite";
    }
}

// Uma direção perpendicular a `v` (comprimento 1), sorteada.
static Vector3 random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 p = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 1.0f, 0.0f, 0.0f });
    return Vector3Normalize(p);
}

Defense *defenses_build(Defenses *d, const Tuning *t, DefenseType type, Vector3 ship_pos, Vector3 ship_forward,
                        float planet_radius, Rng *rng)
{
    if (defenses_count(d, type) >= defense_max(t, type)) return NULL;
    float length = Vector3Length(ship_pos);
    Vector3 up = (length > 1e-3f) ? Vector3Scale(ship_pos, 1.0f / length) : (Vector3){ 0.0f, 1.0f, 0.0f };
    if (type == DEF_TURRET) {
        // À frente da nave, pela superfície (o nariz sem a parte vertical).
        Vector3 ahead = Vector3Subtract(ship_forward, Vector3Scale(up, Vector3DotProduct(ship_forward, up)));
        if (Vector3LengthSqr(ahead) > 1e-4f) {
            float angle = TURRET_AHEAD / fmaxf(planet_radius + t->turret_altitude, 1.0f);
            up = Vector3Normalize(Vector3RotateByAxisAngle(up, Vector3Normalize(Vector3CrossProduct(up, ahead)), angle));
        }
    }
    for (int i = 0; i < DEFENSES_MAX; i++) {
        Defense *f = &d->items[i];
        if (f->alive) continue;
        memset(f, 0, sizeof *f);
        f->alive = true;
        f->type = (uint8_t)type;
        f->dir = up;
        switch (type) {
        case DEF_TURRET:
            f->pos = Vector3Scale(up, planet_radius + t->turret_altitude);
            break;
        case DEF_SHIELD:
            f->pos = Vector3Scale(up, planet_radius + SHIELD_LIFT);
            f->hp = t->shield_hp;
            f->life = t->shield_life;
            break;
        default: {   // satélite: órbita em volta do planeta, passando sobre a nave
            float orbit = planet_radius + t->satellite_altitude;
            f->pos = Vector3Scale(up, orbit);
            f->axis = random_perpendicular(rng, up);
            f->rate = t->satellite_speed / orbit;
            break;
        }
        }
        f->pos_prev = f->pos;
        return f;
    }
    return NULL;
}

// Torre: o inimigo mais perto ao alcance (os protegidos por um Escudeiro não:
// o tiro pararia no escudo).
static void tick_turret(Defense *f, const DefenseContext *ctx)
{
    const Tuning *t = ctx->t;
    if (f->wait > 0.0f) return;
    Enemies *e = ctx->enemies;
    int best = -1;
    float best_distance = t->turret_range;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        if (enemies_shielded_by(e, t, i) >= 0) continue;
        float distance = Vector3Distance(f->pos, e->items[i].pos);
        if (distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    if (best < 0) return;
    const Enemy *en = &e->items[best];
    Vector3 aim = Vector3Add(en->pos, Vector3Scale(en->vel, best_distance / t->weapon_speed));
    Vector3 dir = Vector3Normalize(Vector3Subtract(aim, f->pos));
    Shot *s = projectiles_spawn(ctx->shots, SHOT_BOLT, Vector3Add(f->pos, Vector3Scale(dir, 1.5f)),
                                Vector3Scale(dir, t->weapon_speed), t->turret_range / t->weapon_speed + 0.1f);
    if (s != NULL) {
        s->damage = t->turret_damage;
        s->source = BOLT_ALLY;
    }
    f->dir = dir;
    f->flash = FLASH_TIME;
    f->wait = 1.0f / t->turret_rate;
    events_push(ctx->events, ctx->tick, EV_TURRET_SHOT, 0, f->pos, 0.0f);
}

// Satélite: derruba a bomba (antes de tudo) ou o tiro inimigo mais perto.
static void tick_satellite(Defense *f, const DefenseContext *ctx)
{
    const Tuning *t = ctx->t;
    f->pos = Vector3RotateByAxisAngle(f->pos, f->axis, f->rate * ctx->dt);
    if (f->wait > 0.0f) return;
    Projectiles *p = ctx->shots;
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        if (s->kind != SHOT_PLASMA && s->kind != SHOT_BOMB) continue;
        float distance = Vector3Distance(f->pos, s->pos);
        if (distance > t->satellite_range) continue;
        float score = distance * ((s->kind == SHOT_BOMB) ? 0.3f : 1.0f);
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    if (best < 0) return;
    Vector3 at = p->items[best].pos;
    uint8_t kind = p->items[best].kind;
    projectiles_remove(p, best);
    f->zap = at;
    f->zap_time = ZAP_TIME;
    f->flash = FLASH_TIME;
    f->wait = 1.0f / t->satellite_rate;
    events_push(ctx->events, ctx->tick, EV_INTERCEPT, kind, at, 0.0f);
}

void defenses_tick(Defenses *d, const DefenseContext *ctx)
{
    float dt = ctx->dt;
    for (int i = 0; i < DEFENSES_MAX; i++) {
        Defense *f = &d->items[i];
        if (!f->alive) continue;
        f->pos_prev = f->pos;
        f->age += dt;
        f->wait = fmaxf(f->wait - dt, 0.0f);
        f->flash = fmaxf(f->flash - dt, 0.0f);
        f->zap_time = fmaxf(f->zap_time - dt, 0.0f);
        switch ((DefenseType)f->type) {
        case DEF_TURRET:
            tick_turret(f, ctx);
            break;
        case DEF_SHIELD:
            f->life -= dt;
            if (f->life <= 0.0f) {
                f->alive = false;
                events_push(ctx->events, ctx->tick, EV_SHIELD_DOWN, 0, f->pos, 0.0f);
            }
            break;
        default:
            tick_satellite(f, ctx);
            break;
        }
    }
}

bool defenses_absorb(Defenses *d, const Tuning *t, float planet_radius, Vector3 ground, float radius, Events *events,
                     uint32_t tick)
{
    float length = Vector3Length(ground);
    if (length < 1e-3f) return false;
    Vector3 dir = Vector3Scale(ground, 1.0f / length);
    for (int i = 0; i < DEFENSES_MAX; i++) {
        Defense *f = &d->items[i];
        if (!f->alive || f->type != DEF_SHIELD) continue;
        float along = planet_radius * acosf(Clamp(Vector3DotProduct(dir, f->dir), -1.0f, 1.0f));   // pela superfície
        if (along > t->shield_radius) continue;
        f->hp -= radius;
        f->flash = BLOCK_FLASH;
        events_push(events, tick, EV_SHIELD_BLOCK, 0, ground, radius);
        if (f->hp <= 0.0f) {
            f->alive = false;
            events_push(events, tick, EV_SHIELD_DOWN, 0, f->pos, 1.0f);
        }
        return true;
    }
    return false;
}
