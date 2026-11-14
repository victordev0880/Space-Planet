// Devorador: o chefe final, uma bocarra colossal.
//
//   PROWL  ronda o planeta; um redemoinho (o mundo executa) suga rochas e
//          minério para a boca, e cada rocha engolida cura;
//   AIM    marca um ponto do planeta (o aviso) e paira sobre ele, olhos
//          acesos e boca abrindo. Dano bastante nos olhos: ele erra (STUN);
//   DIVE   mergulha no ponto; o mundo confere se um escudo local o cobre:
//          com escudo, quebra os dentes (STUN longo); sem, a mordida abre
//          uma cratera enorme;
//   CHEW   mastiga, com a garganta exposta (dano inteiro);
//   RISE   volta à altura de ronda.
// O "humor" segue o HP do planeta: faminto, frenético (mordidas mais
// rápidas e rajadas de dentes na nave) e o banquete (LATCH: agarrado ao
// planeta, devorando sem parar, garganta sempre aberta, até o planeta morrer
// ou um tanto de dano arrancá-lo). Consertar o planeta acalma o Devorador.
#include "game/boss.h"

#include <math.h>

#include "game/boss_internal.h"

#define FACE_RATE     3.0f
#define ORBIT_PULL    0.8f
#define MOVE_RATE     2.2f    // rapidez com que ele vai a um ponto de pairar
#define JAW_RATE      5.0f
#define HUNT_TILT     0.45f   // na ronda, a boca aponta este tanto (radianos) para baixo
#define TEETH_SPREAD  0.12f   // abertura do leque de dentes (radianos entre um e outro)
#define TEETH_RANGE   220.0f
#define SHOT_LIFE     4.0f
#define LATCH_DEPTH   0.65f   // agarrado, o centro fica este tanto (em escalas) acima do chão
#define DRAIN_RING    0.7f    // as mordidas contínuas giram num anel deste tanto do raio dele
#define DRIFT_UP      4.0f    // atordoado, sobe devagar
#define CHEW_LIFT     1.6f    // mastigando, ergue a cabeça este tanto (em escalas) acima do chão
#define BEHIND_PLANET -0.2f   // a nave mais "para baixo" que isto (cosseno): ele olha para o alto

const Vector3 DEV_EYE[2] = { { 0.38f, 0.62f, -0.6f }, { -0.38f, 0.62f, -0.6f } };
const float   DEV_EYE_RADIUS = 0.14f;
const Vector3 DEV_THROAT = { 0.0f, 0.0f, -0.78f };
const float   DEV_THROAT_RADIUS = 0.26f;
const Vector3 DEV_HUMP = { 0.0f, 0.55f, 0.55f };
const float   DEV_HUMP_RADIUS = 0.42f;

#define ID_THROAT 2
#define ID_HULL   3

const char *devourer_mood_name(int mood)
{
    switch (mood) {
    case DEV_FRENZY: return "FRENÉTICO";
    case DEV_FEAST:  return "BANQUETE";
    default:         return "FAMINTO";
    }
}

static float original_ground(const Planet *p, Vector3 dir)
{
    Terrain t = planet_terrain(&p->params, dir);
    return p->params.radius + (t.land ? t.elevation : 0.0f);
}

Vector3 devourer_mouth(const Boss *b, const Tuning *t)
{
    return boss_point(b, t, (Vector3){ 0.0f, 0.0f, -0.95f });
}

float devourer_core_factor(const Boss *b, const Tuning *t)
{
    bool open = b->action == DEV_CHEW || b->action == DEV_STUN || b->action == DEV_LATCH;
    return open ? 1.0f : t->dev_throat_armor;
}

// Centro de uma mandíbula (eixos do modelo), girada `open` em volta da dobradiça.
static Vector3 jaw_center(float sign, float open)
{
    Vector3 hinge = { 0.0f, 0.25f * sign, -0.35f };
    Vector3 arm = { 0.0f, 0.07f * sign, -0.5f };
    return Vector3Add(hinge, Vector3RotateByAxisAngle(arm, (Vector3){ 1.0f, 0.0f, 0.0f }, sign * open * 0.9f));
}

static int mood_for(const Planet *planet, const Tuning *t)
{
    float hp = planet_hp(planet);
    return (hp < t->dev_feast_hp) ? DEV_FEAST : (hp < t->dev_frenzy_hp) ? DEV_FRENZY : DEV_HUNGRY;
}

// --- movimento ----------------------------------------------------------------------

// Ronda: órbita na altura dele, a boca para a frente e um pouco para baixo.
static void prowl_move(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    float dt = ctx->dt;
    float distance = Vector3Length(b->pos);
    Vector3 radial = Vector3Scale(b->pos, 1.0f / fmaxf(distance, 1e-3f));
    Vector3 tangent = Vector3Normalize(Vector3CrossProduct(d->orbit_axis, radial));
    float error = ctx->planet->params.radius + t->dev_altitude - distance;
    b->vel = Vector3Add(Vector3Scale(tangent, t->dev_speed), Vector3Scale(radial, error * ORBIT_PULL));
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
    Vector3 forward = Vector3Normalize(Vector3Subtract(Vector3Scale(tangent, cosf(HUNT_TILT)), Vector3Scale(radial, sinf(HUNT_TILT))));
    boss_face(b, forward, radial, FACE_RATE, dt);
}

// Vira para `forward`, com o topo para longe do planeta (se der).
static void turn_to(Boss *b, Vector3 forward, float rate, float dt)
{
    Vector3 up = Vector3Perpendicular(forward);
    Vector3 radial = Vector3Normalize(b->pos);
    Vector3 flat = Vector3Subtract(radial, Vector3Scale(forward, Vector3DotProduct(radial, forward)));
    if (Vector3LengthSqr(flat) > 1e-4f) up = Vector3Normalize(flat);
    boss_face(b, forward, up, rate, dt);
}

// Vai até um ponto suavemente, olhando para `forward`.
static void glide(Boss *b, Vector3 to, Vector3 forward, float rate, float dt)
{
    Vector3 before = b->pos;
    b->pos = Vector3Lerp(b->pos, to, 1.0f - expf(-rate * dt));
    b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
    turn_to(b, forward, FACE_RATE * 1.5f, dt);
}

// Com a garganta aberta (mastigando, atordoado), ele encara a nave e ruge:
// a garganta fica à vista de quem vai atacá-la. Com a nave do outro lado do
// planeta, olha para o alto.
static Vector3 face_ship(const Boss *b, const BossContext *ctx)
{
    Vector3 radial = Vector3Normalize(b->pos);
    const Ship *s = ctx->ship;
    if (s == NULL || !s->alive) return radial;
    Vector3 to = Vector3Subtract(s->pos, b->pos);
    float d = Vector3Length(to);
    if (d < 1e-3f) return radial;
    to = Vector3Scale(to, 1.0f / d);
    return (Vector3DotProduct(to, radial) < BEHIND_PLANET) ? radial : to;
}

// --- as ações ---------------------------------------------------------------------

static void start_aim(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    Vector3 under = Vector3Normalize(b->pos);
    Vector3 side = boss_random_perpendicular(ctx->rng, under);
    float angle = t->dev_bite_spread * DEG2RAD * rng_float(ctx->rng);
    d->bite_dir = Vector3Normalize(Vector3RotateByAxisAngle(under, side, angle));
    d->hover = Vector3Scale(d->bite_dir, original_ground(ctx->planet, d->bite_dir) + t->dev_aim_altitude);
    d->flinch = 0.0f;
    b->action = DEV_AIM;
    b->timer = t->dev_aim_time;
    events_push(ctx->events, ctx->tick, EV_DEV_AIM, b->type,
                Vector3Scale(d->bite_dir, original_ground(ctx->planet, d->bite_dir)), t->dev_aim_time);
}

static void start_latch(Boss *b, const BossContext *ctx)
{
    DevourerState *d = &b->dev;
    d->bite_dir = Vector3Normalize(b->pos);
    b->action = DEV_LATCH;
    b->timer = 0.0f;
    d->latch_damage = 0.0f;
    d->drain_wait = 0.5f;
    events_push(ctx->events, ctx->tick, EV_DEV_LATCH, b->type, b->pos, 0.0f);
}

static void stun(Boss *b, float seconds)
{
    b->action = DEV_STUN;
    b->timer = seconds;
}

static void teeth_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    const Ship *s = ctx->ship;
    if (b->phase < DEV_FRENZY || s == NULL || !s->alive || t->dev_teeth_count <= 0) return;
    d->teeth_wait -= ctx->dt;
    if (d->teeth_wait > 0.0f) return;
    d->teeth_wait = t->dev_teeth_interval;
    Vector3 from = devourer_mouth(b, t);
    float range = Vector3Distance(from, s->pos);
    if (range > TEETH_RANGE) return;
    Vector3 aim = Vector3Normalize(Vector3Subtract(Vector3Add(s->pos, Vector3Scale(s->vel, range / t->dev_teeth_speed)), from));
    Vector3 side = Vector3Normalize(Vector3CrossProduct(aim, Vector3Normalize(from)));
    for (int i = 0; i < t->dev_teeth_count; i++) {
        float offset = ((float)i - 0.5f * (float)(t->dev_teeth_count - 1)) * TEETH_SPREAD;
        Vector3 dir = Vector3Normalize(Vector3RotateByAxisAngle(aim, Vector3CrossProduct(side, aim), offset));
        Shot *shot = projectiles_spawn(ctx->shots, SHOT_PLASMA, from, Vector3Scale(dir, t->dev_teeth_speed), SHOT_LIFE);
        if (shot == NULL) break;
        shot->damage = t->dev_teeth_damage;
        shot->source = ENEMY_KAMIKAZE;   // vermelho, como um dente em brasa
    }
    events_push(ctx->events, ctx->tick, EV_DEV_TEETH, b->type, from, (float)t->dev_teeth_count);
}

void devourer_spawn(Boss *b, const BossContext *ctx, Vector3 dir)
{
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    float radius = ctx->planet->params.radius;
    b->action = DEV_PROWL;
    b->scale = t->dev_scale;
    b->hp = b->hp_max = t->dev_hp;
    b->phase = (uint8_t)mood_for(ctx->planet, t);
    d->orbit_axis = boss_random_perpendicular(ctx->rng, dir);
    b->enter_from = Vector3Scale(dir, radius * t->dev_spawn_distance);
    b->enter_to = Vector3Scale(dir, radius + t->dev_altitude);
    b->pos = b->pos_prev = b->enter_from;
    b->rot = quat_look(Vector3Negate(dir), d->orbit_axis);
    b->rot_prev = b->rot;
    b->timer = t->dev_enter_time;
    d->bite_wait = t->dev_bite_interval[0] * 0.6f;
    d->teeth_wait = t->dev_teeth_interval;
    events_push(ctx->events, ctx->tick, EV_BOSS_ENTER, (uint8_t)b->type, b->enter_to, t->dev_enter_time);
}

void devourer_enter(Boss *b, const BossContext *ctx)
{
    // Chega devagar, de boca escancarada (o rugido da chegada), e fecha.
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    float dt = ctx->dt;
    b->timer -= dt;
    float u = 1.0f - Clamp(b->timer / t->dev_enter_time, 0.0f, 1.0f);
    float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    Vector3 before = b->pos;
    b->pos = Vector3Lerp(b->enter_from, b->enter_to, ease);
    b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
    boss_face(b, Vector3Negate(Vector3Normalize(b->pos)), d->orbit_axis, FACE_RATE, dt);
    d->jaw_target = (u > 0.5f && u < 0.85f) ? 1.0f : 0.1f;
    d->jaw += (d->jaw_target - d->jaw) * (1.0f - expf(-JAW_RATE * dt));
    if (b->timer <= 0.0f) {
        b->state = BOSS_FIGHT;
        b->action = DEV_PROWL;
    }
}

void devourer_fight(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    DevourerState *d = &b->dev;
    float dt = ctx->dt;
    d->bite_now = false;
    d->latch_cooldown = fmaxf(d->latch_cooldown - dt, 0.0f);
    for (int k = 0; k < 2; k++) d->eye_flash[k] = fmaxf(d->eye_flash[k] - dt, 0.0f);

    // O humor segue o planeta (consertá-lo acalma o Devorador).
    int mood = mood_for(ctx->planet, t);
    if (mood != b->phase) {
        b->phase = (uint8_t)mood;
        events_push(ctx->events, ctx->tick, EV_BOSS_PHASE, b->type, b->pos, (float)mood);
    }
    float radius = ctx->planet->params.radius;
    float now = b->age;

    switch (b->action) {
    case DEV_PROWL:
        prowl_move(b, ctx);
        d->jaw_target = 0.15f;
        teeth_tick(b, ctx);
        if (mood == DEV_FEAST && d->latch_cooldown <= 0.0f) {
            start_latch(b, ctx);
            break;
        }
        d->bite_wait -= dt;
        if (d->bite_wait <= 0.0f) start_aim(b, ctx);
        break;
    case DEV_AIM: {
        glide(b, d->hover, Vector3Negate(d->bite_dir), MOVE_RATE, dt);
        d->jaw_target = 0.3f + 0.6f * (1.0f - b->timer / fmaxf(t->dev_aim_time, 1e-3f));
        b->timer -= dt;
        if (d->flinch >= t->dev_flinch) {
            // Os olhos acertados: ele erra a mordida, atordoado.
            stun(b, t->dev_flinch_stun);
            d->bite_wait = t->dev_bite_interval[mood >= DEV_FRENZY ? 1 : 0];
            events_push(ctx->events, ctx->tick, EV_DEV_MISS, b->type, b->pos, t->dev_flinch_stun);
        } else if (b->timer <= 0.0f) {
            b->action = DEV_DIVE;
            b->timer = t->dev_dive_time;
        }
        break;
    }
    case DEV_DIVE: {
        float u = 1.0f - Clamp(b->timer / fmaxf(t->dev_dive_time, 1e-3f), 0.0f, 1.0f);
        Vector3 bite = Vector3Scale(d->bite_dir, original_ground(ctx->planet, d->bite_dir) + b->scale * LATCH_DEPTH);
        Vector3 before = b->pos;
        b->pos = Vector3Lerp(d->hover, bite, u * u);
        b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
        boss_face(b, Vector3Negate(d->bite_dir), Vector3Perpendicular(d->bite_dir), FACE_RATE * 2.0f, dt);
        d->jaw_target = 1.0f;
        b->timer -= dt;
        if (b->timer <= 0.0f) d->bite_now = true;   // o mundo confere o escudo e chama devourer_bite_result
        break;
    }
    case DEV_CHEW: {
        // Ergue a cabeça da cratera e mastiga encarando a nave.
        Vector3 ground = Vector3Scale(d->bite_dir, original_ground(ctx->planet, d->bite_dir));
        glide(b, Vector3Add(ground, Vector3Scale(d->bite_dir, b->scale * CHEW_LIFT)), face_ship(b, ctx), MOVE_RATE, dt);
        d->jaw_target = 0.55f + 0.45f * sinf(now * 7.0f);
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = DEV_RISE;
            b->timer = t->dev_rise_time;
        }
        break;
    }
    case DEV_STUN: {
        // Sobe devagar, a boca pendurada aberta, virando-se para a nave.
        Vector3 up = Vector3Normalize(b->pos);
        b->vel = Vector3Scale(up, DRIFT_UP);
        b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
        turn_to(b, face_ship(b, ctx), FACE_RATE, dt);
        d->jaw_target = 1.0f;
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = DEV_RISE;
            b->timer = t->dev_rise_time;
        }
        break;
    }
    case DEV_LATCH: {
        if (mood != DEV_FEAST) {   // o planeta melhorou: solta
            b->action = DEV_RISE;
            b->timer = t->dev_rise_time;
            break;
        }
        Vector3 ground = Vector3Scale(d->bite_dir, original_ground(ctx->planet, d->bite_dir));
        glide(b, Vector3Add(ground, Vector3Scale(d->bite_dir, b->scale * LATCH_DEPTH)), Vector3Negate(d->bite_dir), MOVE_RATE * 2.0f, dt);
        d->jaw_target = 0.6f + 0.4f * sinf(now * 9.0f);
        d->drain_wait -= dt;
        if (d->drain_wait <= 0.0f && b->carve_count < BOSS_CARVES) {
            // Mordidas contínuas num anel em volta do ponto: abre uma bacia.
            d->drain_wait = t->dev_latch_drain;
            Vector3 side = Vector3Perpendicular(d->bite_dir);
            float spin = now * 2.3f;
            Vector3 ring = Vector3RotateByAxisAngle(side, d->bite_dir, spin);
            Vector3 at = Vector3Add(ground, Vector3Scale(ring, b->scale * DRAIN_RING));
            at = Vector3Scale(Vector3Normalize(at), Vector3Length(ground) - t->dev_latch_radius * 0.4f);
            b->carves[b->carve_count++] = (CarveRequest){ at, t->dev_latch_radius, ENEMY_TYPE_COUNT };
        }
        break;
    }
    default: {   // DEV_RISE
        Vector3 up = Vector3Normalize(b->pos);
        glide(b, Vector3Scale(up, radius + t->dev_altitude), Vector3Normalize(Vector3CrossProduct(d->orbit_axis, up)), MOVE_RATE,
              dt);
        d->jaw_target = 0.2f;
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = DEV_PROWL;
            d->bite_wait = t->dev_bite_interval[mood >= DEV_FRENZY ? 1 : 0];
            d->orbit_axis = boss_random_perpendicular(ctx->rng, up);   // a ronda segue de onde ele está, num rumo novo
        }
        break;
    }
    }
    d->jaw += (d->jaw_target - d->jaw) * (1.0f - expf(-JAW_RATE * dt));
}

void devourer_bite_result(Boss *b, const Tuning *t, bool blocked, Events *events, uint32_t tick)
{
    DevourerState *d = &b->dev;
    d->bite_now = false;
    if (blocked) {
        stun(b, t->dev_broken_stun);
        events_push(events, tick, EV_DEV_BROKEN, b->type, b->pos, t->dev_broken_stun);
    } else {
        b->action = DEV_CHEW;
        b->timer = t->dev_chew_time;
        events_push(events, tick, EV_DEV_BITE, b->type, b->pos, 0.0f);
    }
}

void devourer_swallow(Boss *b, float fraction, bool rock)
{
    if (b->state != BOSS_FIGHT && b->state != BOSS_ENTER) return;
    b->hp = fminf(b->hp + fraction * b->hp_max, b->hp_max);
    if (rock) b->dev.eaten++;
}

// --- peças e dano ---------------------------------------------------------------------

int devourer_parts(const Boss *b, const Tuning *t, BossPart *out)
{
    const DevourerState *d = &b->dev;
    bool vulnerable = boss_vulnerable(b);
    bool aiming = vulnerable && (b->action == DEV_AIM || b->action == DEV_DIVE);
    Vector3 forward = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, -1.0f }, b->rot);
    int n = 0;
    for (int k = 0; k < 2; k++) {
        out[n++] = (BossPart){ boss_point(b, t, DEV_EYE[k]), DEV_EYE_RADIUS * b->scale, PART_TARGET, (uint8_t)k, aiming, false,
                               aiming ? 0.1f : 0.0f, forward };
    }
    float factor = boss_core_factor(b, t);
    bool open = factor >= 0.99f;
    // O ponto fraco: a garganta; no banquete (a boca enterrada no planeta),
    // o papo nas costas, por onde se vê o que ele engole.
    bool feasting = b->action == DEV_LATCH;
    Vector3 top = Vector3RotateByQuaternion((Vector3){ 0.0f, 1.0f, 0.0f }, b->rot);
    out[n++] = feasting ? (BossPart){ boss_point(b, t, DEV_HUMP), DEV_HUMP_RADIUS * b->scale, PART_CORE, ID_THROAT, open, false,
                                      open ? 0.12f : 0.0f, top }
                        : (BossPart){ boss_point(b, t, DEV_THROAT), DEV_THROAT_RADIUS * b->scale, PART_CORE, ID_THROAT, open,
                                      false, open ? 0.12f : 0.0f, forward };
    // Casco: o corpo, a corcova (menos no banquete, quando ela é o alvo) e as
    // duas mandíbulas (que se abrem e destapam a garganta).
    const BossSphere hull[2] = { { { 0.0f, 0.05f, 0.15f }, 0.88f }, { { 0.0f, 0.42f, 0.62f }, 0.5f } };
    for (int k = 0; k < 2; k++) {
        if (k == 1 && feasting) continue;
        out[n++] = (BossPart){ boss_point(b, t, hull[k].center), hull[k].radius * b->scale, PART_HULL, (uint8_t)(ID_HULL + k),
                               false, true, 0.0f, { 0.0f, 0.0f, 0.0f } };
    }
    for (int k = 0; k < 2; k++) {
        float sign = (k == 0) ? 1.0f : -1.0f;
        out[n++] = (BossPart){ boss_point(b, t, jaw_center(sign, d->jaw)), 0.42f * b->scale, PART_HULL, (uint8_t)(ID_HULL + 2 + k),
                               false, true, 0.0f, { 0.0f, 0.0f, 0.0f } };
    }
    return n;
}

float devourer_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick)
{
    DevourerState *d = &b->dev;
    if (id < 2) {
        if (b->action != DEV_AIM && b->action != DEV_DIVE) return 0.0f;
        d->flinch += amount;
        d->eye_flash[id] = 0.12f;
        boss_damage_core(b, t, amount, 0.25f, events, tick);
        return amount;
    }
    if (id == ID_THROAT) {
        float applied = boss_damage_core(b, t, amount, devourer_core_factor(b, t), events, tick);
        if (b->action == DEV_LATCH) {
            d->latch_damage += applied;
            if (d->latch_damage >= t->dev_latch_break * b->hp_max && b->state == BOSS_FIGHT) {
                // Arrancado do planeta.
                stun(b, t->dev_torn_stun);
                d->latch_cooldown = t->dev_latch_cooldown;
                events_push(events, tick, EV_DEV_TORN, b->type, b->pos, t->dev_torn_stun);
            }
        }
        return applied;
    }
    return 0.0f;
}
