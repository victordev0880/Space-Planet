// Rompe-Mundos: um canhão colossal em órbita. Ciclo de ataque:
//   REST     orbita e atira na nave com duas torretas;
//   CHARGE   desacelera e carrega o canhão. O traço do corte aparece no chão
//            (um arco à frente dele) e quatro capacitores acendem no casco:
//            só nessa hora eles levam dano. Destruídos os quatro antes do fim
//            da carga, o tiro falha e ele fica atordoado;
//   FIRE     o laser varre o traço e abre uma vala no planeta;
//   STAGGER  atordoado, com o núcleo aberto: o núcleo leva o dano inteiro.
// Fora do atordoamento o núcleo é blindado (só passa uma fração do dano; na
// fase 3, uma fração maior). Três fases, pela vida: cargas mais curtas,
// cortes mais longos e, da fase 2 em diante, drones.
#include "game/boss.h"

#include <math.h>

#include "game/boss_internal.h"
#include "voxel/damage.h"

#define ORBIT_PULL         0.8f     // força que o mantém na altura da órbita
#define CHARGE_SLOW        0.35f    // na carga voa este tanto da velocidade
#define STAGGER_SLOW       0.2f     // ... e atordoado, este tanto
#define TURN_SMOOTH        2.5f     // rapidez com que o casco acompanha a direção do voo
#define CUT_LEAD           0.10f    // o corte começa este tanto (radianos) à frente dele
#define TURRET_GAP         0.16f    // segundos entre os tiros de uma rajada
#define SHOT_LIFE          4.0f

// --- geometria (eixos do modelo, vezes a escala) ---------------------------------
const Vector3 BREAKER_CAPACITOR[BOSS_CAPACITORS] = {
    { 0.62f, 0.06f, -0.45f }, { -0.62f, 0.06f, -0.45f }, { 0.62f, 0.06f, 0.45f }, { -0.62f, 0.06f, 0.45f },
};
const float      BREAKER_CAPACITOR_RADIUS = 0.2f;
const Vector3    BREAKER_CORE = { 0.0f, 0.52f, 0.15f };
const float      BREAKER_CORE_RADIUS = 0.24f;
const BossSphere BREAKER_HULL[BOSS_HULL_SPHERES] = {
    { { 0.0f, 0.15f, -1.25f }, 0.36f }, { { 0.0f, 0.15f, -0.62f }, 0.44f }, { { 0.0f, 0.15f, 0.0f }, 0.46f },
    { { 0.0f, 0.15f, 0.62f }, 0.44f },  { { 0.0f, 0.15f, 1.20f }, 0.38f },  { { 0.0f, -0.25f, -1.45f }, 0.24f },
    { { 0.0f, -0.25f, -1.95f }, 0.2f },
};
const Vector3 BREAKER_MUZZLE = { 0.0f, -0.25f, -2.25f };
const Vector3 BREAKER_TURRET[BOSS_TURRETS] = { { 0.32f, 0.52f, -0.85f }, { -0.32f, 0.52f, -0.85f } };
const Vector3 BREAKER_BAY = { 0.0f, -0.3f, 0.6f };

bool boss_capacitors_armed(const Boss *b)
{
    return b->type == BOSS_BREAKER && b->state == BOSS_FIGHT && b->action == BREAKER_CHARGE;
}

int boss_capacitors_left(const Boss *b)
{
    int n = 0;
    for (int i = 0; i < BOSS_CAPACITORS; i++) n += (b->breaker.cap_hp[i] > 0.0f) ? 1 : 0;
    return n;
}

float breaker_core_factor(const Boss *b, const Tuning *t)
{
    if (b->action == BREAKER_STAGGER) return 1.0f;
    return (b->phase >= 3) ? t->breaker_core_open : t->breaker_core_armor;
}

Vector3 boss_cut_direction(const Boss *b, float u)
{
    return Vector3Normalize(Vector3RotateByAxisAngle(b->breaker.cut_from, b->axis, b->breaker.cut_angle * u));
}

// Altura do solo (medida na carga) na fração `u` do corte; `solid` diz se ali
// ainda havia chão.
static float cut_ground_at(const Boss *b, float u, bool *solid)
{
    float x = Clamp(u, 0.0f, 1.0f) * (float)(BOSS_CUT_SAMPLES - 1);
    int i = (int)x;
    if (i > BOSS_CUT_SAMPLES - 2) i = BOSS_CUT_SAMPLES - 2;
    float r0 = b->breaker.cut_ground[i], r1 = b->breaker.cut_ground[i + 1];
    if (solid != NULL) *solid = (r0 > 0.0f && r1 > 0.0f);
    return Lerp(fabsf(r0), fabsf(r1), x - (float)i);
}

Vector3 boss_cut_point(const Boss *b, float u)
{
    return Vector3Scale(boss_cut_direction(b, u), cut_ground_at(b, u, NULL));
}

// ---------------------------------------------------------------------------

void breaker_spawn(Boss *b, const BossContext *ctx, Vector3 dir)
{
    const Tuning *t = ctx->t;
    float radius = ctx->planet->params.radius;
    b->action = BREAKER_REST;
    b->scale = t->breaker_scale;
    b->hp = b->hp_max = t->breaker_hp;
    b->axis = boss_random_perpendicular(ctx->rng, dir);
    b->enter_from = Vector3Scale(dir, radius * t->breaker_spawn_distance);
    b->enter_to = Vector3Scale(dir, radius + t->breaker_altitude);
    b->pos = b->pos_prev = b->enter_from;
    b->rot = quat_look(Vector3Negate(dir), Vector3CrossProduct(b->axis, dir));
    b->rot_prev = b->rot;
    b->timer = t->breaker_enter_time;
    b->breaker.turret_wait = t->breaker_turret_interval[0];
    b->breaker.launch_wait = t->breaker_launch_interval;
    events_push(ctx->events, ctx->tick, EV_BOSS_ENTER, (uint8_t)b->type, b->enter_to, t->breaker_enter_time);
}

void breaker_enter(Boss *b, const BossContext *ctx)
{
    // Chega rápido e freia até parar na órbita (desaceleração suave).
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    b->timer -= dt;
    float u = 1.0f - Clamp(b->timer / t->breaker_enter_time, 0.0f, 1.0f);
    float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    Vector3 before = b->pos;
    b->pos = Vector3Lerp(b->enter_from, b->enter_to, ease);
    b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
    Vector3 radial = Vector3Normalize(b->pos);
    boss_face(b, Vector3Lerp(Vector3Negate(radial), Vector3CrossProduct(b->axis, radial), ease), b->pos, TURN_SMOOTH, dt);
    if (b->timer <= 0.0f) {
        b->state = BOSS_FIGHT;
        b->action = BREAKER_REST;
        b->timer = t->breaker_rest[0];
    }
}

// Distância do centro à superfície ORIGINAL (a do começo da partida) na
// direção `dir`. O corte é fundo em relação a ela, e não ao chão de agora:
// assim ele tira sempre uma fatia da mesma espessura, e onde uma vala antiga
// passou ele não cava mais nada. Medindo o chão de agora, cada corte que
// cruzasse uma vala antiga desceria mais 20 e, alguns cruzamentos depois,
// chegaria ao núcleo.
static float ground_radius(const Planet *planet, Vector3 dir)
{
    Terrain t = planet_terrain(&planet->params, dir);
    return planet->params.radius + (t.land ? t.elevation : 0.0f);
}

static void start_charge(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    BreakerState *br = &b->breaker;
    int p = b->phase - 1;
    b->action = BREAKER_CHARGE;
    b->timer = t->breaker_charge[p];
    for (int i = 0; i < BOSS_CAPACITORS; i++) br->cap_hp[i] = t->breaker_capacitor_hp;
    // Antes de mirar, a órbita gira um pouco em volta da vertical dele: cada
    // corte cai num lugar diferente do planeta.
    Vector3 radial = Vector3Normalize(b->pos);
    float turn = t->breaker_cut_turn * DEG2RAD * (0.5f + 0.5f * rng_float(ctx->rng));
    if (rng_below(ctx->rng, 2) == 0) turn = -turn;
    b->axis = Vector3Normalize(Vector3RotateByAxisAngle(b->axis, radial, turn));
    // O corte: um arco no chão à frente dele, no sentido em que ele voa.
    br->cut_from = Vector3Normalize(Vector3RotateByAxisAngle(radial, b->axis, CUT_LEAD));
    br->cut_angle = t->breaker_cut_length[p] * DEG2RAD;
    br->cut_done = 0.0f;
    for (int i = 0; i < BOSS_CUT_SAMPLES; i++) {
        br->cut_ground[i] = ground_radius(ctx->planet, boss_cut_direction(b, (float)i / (float)(BOSS_CUT_SAMPLES - 1)));
    }
    events_push(ctx->events, ctx->tick, EV_BOSS_CHARGE, b->type, b->pos, b->timer);
}

static void fire_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    BreakerState *br = &b->breaker;
    float radius = ctx->planet->params.radius;
    float progress = Clamp(1.0f - b->timer / t->breaker_fire_time, 0.0f, 1.0f);
    // Uma mordida a cada `cut_spacing` de arco, até onde o laser já passou,
    // sempre na mesma profundidade em relação ao solo de antes do corte.
    float step = (t->breaker_cut_spacing / radius) / fmaxf(br->cut_angle, 1e-4f);
    b->carve_count = 0;
    while (br->cut_done + step <= progress + 1e-6f && b->carve_count < BOSS_CARVES) {
        br->cut_done += step;
        bool solid;
        float ground = cut_ground_at(b, br->cut_done, &solid);
        if (!solid) continue;
        Vector3 dir = boss_cut_direction(b, br->cut_done);
        b->carves[b->carve_count++] = (CarveRequest){
            Vector3Scale(dir, ground - t->breaker_cut_sink), t->breaker_cut_radius, ENEMY_TYPE_COUNT };
    }
    br->beam_point = boss_cut_point(b, progress);
}

static void turrets_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    BreakerState *br = &b->breaker;
    if (s == NULL || !s->alive || t->breaker_turret_burst <= 0) return;
    br->fire_wait -= ctx->dt;
    if (br->burst > 0) {
        if (br->fire_wait > 0.0f) return;
        Vector3 muzzle = boss_point(b, t, BREAKER_TURRET[br->turret]);
        float d = Vector3Distance(muzzle, s->pos);
        if (d > t->breaker_turret_range) {
            br->burst = 0;
            return;
        }
        Vector3 aim = Vector3Add(s->pos, Vector3Scale(Vector3Add(s->vel, s->fall), d / t->breaker_turret_speed));
        Vector3 dir = Vector3Normalize(Vector3Subtract(aim, muzzle));
        Shot *shot = projectiles_spawn(ctx->shots, SHOT_PLASMA, muzzle, Vector3Scale(dir, t->breaker_turret_speed), SHOT_LIFE);
        if (shot != NULL) {
            shot->damage = t->breaker_turret_damage;
            shot->source = ENEMY_BOMBER;   // desenhado e ouvido como o tiro de um pesado
        }
        events_push(ctx->events, ctx->tick, EV_ENEMY_SHOT, ENEMY_BOMBER, muzzle, 0.0f);
        br->turret ^= 1;
        br->burst--;
        br->fire_wait = TURRET_GAP;
        return;
    }
    br->turret_wait -= ctx->dt;
    if (br->turret_wait <= 0.0f && Vector3Distance(b->pos, s->pos) < t->breaker_turret_range) {
        br->burst = t->breaker_turret_burst;
        br->turret_wait = t->breaker_turret_interval[b->phase - 1];
    }
}

void breaker_fight(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    BreakerState *br = &b->breaker;
    float dt = ctx->dt;
    float radius = ctx->planet->params.radius;

    // Órbita: devagar na carga e mais ainda atordoado.
    float distance = Vector3Length(b->pos);
    Vector3 radial = Vector3Scale(b->pos, 1.0f / distance);
    Vector3 tangent = Vector3Normalize(Vector3CrossProduct(b->axis, radial));
    float pace = (b->action == BREAKER_CHARGE || b->action == BREAKER_FIRE) ? CHARGE_SLOW
               : (b->action == BREAKER_STAGGER)                          ? STAGGER_SLOW
                                                                         : 1.0f;
    float error = radius + t->breaker_altitude - distance;
    b->vel = Vector3Add(Vector3Scale(tangent, t->breaker_speed * pace), Vector3Scale(radial, error * ORBIT_PULL));
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
    boss_face(b, tangent, b->pos, TURN_SMOOTH, dt);

    switch (b->action) {
    case BREAKER_REST:
        b->timer -= dt;
        if (b->timer <= 0.0f) start_charge(b, ctx);
        break;
    case BREAKER_CHARGE:
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = BREAKER_FIRE;
            b->timer = t->breaker_fire_time;
            br->beam_point = boss_cut_point(b, 0.0f);
            events_push(ctx->events, ctx->tick, EV_BOSS_FIRE, b->type, br->beam_point, t->breaker_fire_time);
        }
        break;
    case BREAKER_FIRE:
        b->timer -= dt;
        fire_tick(b, ctx);
        if (b->timer <= 0.0f) {
            b->action = BREAKER_REST;
            b->timer = t->breaker_rest[b->phase - 1];
        }
        break;
    default:   // BREAKER_STAGGER
        b->timer -= dt;
        if (b->timer <= 0.0f) {
            b->action = BREAKER_REST;
            b->timer = t->breaker_rest[b->phase - 1] * 0.5f;
        }
        break;
    }

    if (b->action != BREAKER_STAGGER) turrets_tick(b, ctx);

    // Da fase 2 em diante, solta drones de tempos em tempos.
    if (b->phase >= 2 && t->breaker_launch_count > 0) {
        br->launch_wait -= dt;
        if (br->launch_wait <= 0.0f) {
            b->launch_count = t->breaker_launch_count;
            b->launch_type = ENEMY_SWARM;
            b->launch_from = boss_point(b, t, BREAKER_BAY);
            br->launch_wait = t->breaker_launch_interval;
            events_push(ctx->events, ctx->tick, EV_LAUNCH, ENEMY_CARRIER, b->launch_from, (float)t->breaker_launch_count);
        }
    }
}

// ---------------------------------------------------------------------------

// Peças: capacitores (0 a 3), núcleo (4) e o casco.
int breaker_parts(const Boss *b, const Tuning *t, BossPart *out)
{
    int n = 0;
    bool vulnerable = boss_vulnerable(b);
    bool armed = boss_capacitors_armed(b);
    for (int k = 0; k < BOSS_CAPACITORS; k++) {
        bool alive = b->breaker.cap_hp[k] > 0.0f;
        Vector3 local = { BREAKER_CAPACITOR[k].x, 0.0f, 0.0f };
        out[n++] = (BossPart){ boss_point(b, t, BREAKER_CAPACITOR[k]), BREAKER_CAPACITOR_RADIUS * b->scale, PART_TARGET,
                               (uint8_t)k, armed && alive, false, (vulnerable && armed && alive) ? 0.15f : 0.0f,
                               Vector3Normalize(Vector3RotateByQuaternion(local, b->rot)) };
    }
    float factor = boss_core_factor(b, t);
    float core_aim = !vulnerable ? 0.0f : (factor >= 0.99f) ? 0.2f : (factor >= 0.3f) ? 0.7f : 2.5f;
    out[n++] = (BossPart){ boss_point(b, t, BREAKER_CORE), BREAKER_CORE_RADIUS * b->scale, PART_CORE, BOSS_CAPACITORS,
                           factor >= 0.99f, false, core_aim,
                           Vector3RotateByQuaternion((Vector3){ 0.0f, 1.0f, 0.0f }, b->rot) };
    for (int k = 0; k < BOSS_HULL_SPHERES; k++) {
        out[n++] = (BossPart){ boss_point(b, t, BREAKER_HULL[k].center), BREAKER_HULL[k].radius * b->scale, PART_HULL,
                               (uint8_t)(BOSS_CAPACITORS + 1 + k), false, true, 0.0f, { 0.0f, 0.0f, 0.0f } };
    }
    return n;
}

static float damage_capacitor(Boss *b, const Tuning *t, int index, float amount, Events *events, uint32_t tick)
{
    BreakerState *br = &b->breaker;
    if (!boss_capacitors_armed(b) || index < 0 || index >= BOSS_CAPACITORS || br->cap_hp[index] <= 0.0f) return 0.0f;
    float applied = fminf(amount, br->cap_hp[index]);
    br->cap_hp[index] -= amount;
    br->cap_flash[index] = 0.12f;
    if (br->cap_hp[index] <= 0.0f) {
        br->cap_hp[index] = 0.0f;
        int left = boss_capacitors_left(b);
        events_push(events, tick, EV_BOSS_CAPACITOR, b->type, boss_point(b, t, BREAKER_CAPACITOR[index]), (float)left);
        if (left == 0) {
            // Sem capacitores, o tiro falha: atordoado, com o núcleo aberto.
            b->action = BREAKER_STAGGER;
            b->timer = t->breaker_stagger_time;
            events_push(events, tick, EV_BOSS_STAGGER, b->type, b->pos, t->breaker_stagger_time);
        }
    }
    return applied;
}

float breaker_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick)
{
    if (id < BOSS_CAPACITORS) return damage_capacitor(b, t, id, amount, events, tick);
    if (id == BOSS_CAPACITORS) return boss_damage_core(b, t, amount, breaker_core_factor(b, t), events, tick);
    return 0.0f;   // casco
}
