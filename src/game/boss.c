#include "game/boss.h"

#include <math.h>
#include <string.h>

#include "voxel/damage.h"

#define ORBIT_PULL         0.8f     // força que o mantém na altura da órbita
#define CHARGE_SLOW        0.35f    // na carga voa este tanto da velocidade
#define STAGGER_SLOW       0.2f     // ... e atordoado, este tanto
#define TURN_SMOOTH        2.5f     // rapidez com que o casco acompanha a direção do voo
#define CUT_LEAD           0.10f    // o corte começa este tanto (radianos) à frente dele
#define CUT_PROBE          40.0f    // o laser procura o solo a partir desta altura
#define TURRET_GAP         0.16f    // segundos entre os tiros de uma rajada
#define DYING_TIME         3.6f     // segundos de explosões antes de sumir
#define DYING_BLAST_GAP    0.22f
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

const char *boss_name(BossType type)
{
    (void)type;
    return "ROMPE-MUNDOS";
}

void boss_reset(Boss *b)
{
    memset(b, 0, sizeof *b);
    b->state = BOSS_OFF;
    b->rot = QuaternionIdentity();
    b->rot_prev = b->rot;
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
    return Vector3Add(b->pos, Vector3RotateByQuaternion(Vector3Scale(local, t->breaker_scale), b->rot));
}

bool boss_capacitors_armed(const Boss *b)
{
    return b->state == BOSS_FIGHT && b->action == BREAKER_CHARGE;
}

int boss_capacitors_left(const Boss *b)
{
    int n = 0;
    for (int i = 0; i < BOSS_CAPACITORS; i++) n += (b->cap_hp[i] > 0.0f) ? 1 : 0;
    return n;
}

float boss_core_factor(const Boss *b, const Tuning *t)
{
    if (!boss_vulnerable(b)) return 0.0f;
    if (b->action == BREAKER_STAGGER) return 1.0f;
    return (b->phase >= 3) ? t->breaker_core_open : t->breaker_core_armor;
}

Vector3 boss_cut_direction(const Boss *b, float u)
{
    return Vector3Normalize(Vector3RotateByAxisAngle(b->cut_from, b->axis, b->cut_angle * u));
}

// Altura do solo (medida na carga) na fração `u` do corte; `solid` diz se ali
// ainda havia chão.
static float cut_ground_at(const Boss *b, float u, bool *solid)
{
    float x = Clamp(u, 0.0f, 1.0f) * (float)(BOSS_CUT_SAMPLES - 1);
    int i = (int)x;
    if (i > BOSS_CUT_SAMPLES - 2) i = BOSS_CUT_SAMPLES - 2;
    float r0 = b->cut_ground[i], r1 = b->cut_ground[i + 1];
    if (solid != NULL) *solid = (r0 > 0.0f && r1 > 0.0f);
    return Lerp(fabsf(r0), fabsf(r1), x - (float)i);
}

Vector3 boss_cut_point(const Boss *b, float u)
{
    return Vector3Scale(boss_cut_direction(b, u), cut_ground_at(b, u, NULL));
}

// ---------------------------------------------------------------------------

// Uma direção qualquer perpendicular a `v` (comprimento 1), sorteada.
static Vector3 random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 p = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 1.0f, 0.0f, 0.0f });
    return Vector3Normalize(p);
}

void boss_spawn(Boss *b, BossType type, const BossContext *ctx, Vector3 dir)
{
    const Tuning *t = ctx->t;
    float radius = ctx->planet->params.radius;
    boss_reset(b);
    dir = Vector3Normalize(dir);
    b->type = (uint8_t)type;
    b->state = BOSS_ENTER;
    b->action = BREAKER_REST;
    b->phase = 1;
    b->hp = b->hp_max = t->breaker_hp;
    b->axis = random_perpendicular(ctx->rng, dir);
    b->enter_from = Vector3Scale(dir, radius * t->breaker_spawn_distance);
    b->enter_to = Vector3Scale(dir, radius + t->breaker_altitude);
    b->pos = b->pos_prev = b->enter_from;
    b->rot = quat_look(Vector3Negate(dir), Vector3CrossProduct(b->axis, dir));
    b->rot_prev = b->rot;
    b->timer = t->breaker_enter_time;
    b->turret_wait = t->breaker_turret_interval[0];
    b->launch_wait = t->breaker_launch_interval;
    events_push(ctx->events, ctx->tick, EV_BOSS_ENTER, (uint8_t)type, b->enter_to, t->breaker_enter_time);
}

static void face(Boss *b, Vector3 forward, float dt)
{
    if (Vector3LengthSqr(forward) < 1e-6f) return;
    Quaternion target = quat_look(forward, b->pos);   // topo para longe do planeta
    b->rot = QuaternionNormalize(QuaternionSlerp(b->rot, target, 1.0f - expf(-TURN_SMOOTH * dt)));
}

// Distância do centro ao solo na direção `dir` (procurando de fora para dentro);
// negativa se o raio atravessa tudo (o planeta já está oco ali).
static float ground_radius(const Planet *planet, Vector3 dir)
{
    Vector3 from = Vector3Scale(dir, planet->params.radius + CUT_PROBE);
    Vector3 hit;
    if (planet_raycast(planet, from, Vector3Negate(dir), CUT_PROBE + planet->params.radius, &hit)) {
        return Vector3Length(hit);
    }
    return -planet->params.radius;
}

static void start_charge(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    int p = b->phase - 1;
    b->action = BREAKER_CHARGE;
    b->timer = t->breaker_charge[p];
    for (int i = 0; i < BOSS_CAPACITORS; i++) b->cap_hp[i] = t->breaker_capacitor_hp;
    // Antes de mirar, a órbita gira um pouco em volta da vertical dele: cada
    // corte cai num lugar diferente do planeta.
    Vector3 radial = Vector3Normalize(b->pos);
    float turn = t->breaker_cut_turn * DEG2RAD * (0.5f + 0.5f * rng_float(ctx->rng));
    if (rng_below(ctx->rng, 2) == 0) turn = -turn;
    b->axis = Vector3Normalize(Vector3RotateByAxisAngle(b->axis, radial, turn));
    // O corte: um arco no chão à frente dele, no sentido em que ele voa.
    b->cut_from = Vector3Normalize(Vector3RotateByAxisAngle(radial, b->axis, CUT_LEAD));
    b->cut_angle = t->breaker_cut_length[p] * DEG2RAD;
    b->cut_done = 0.0f;
    for (int i = 0; i < BOSS_CUT_SAMPLES; i++) {
        b->cut_ground[i] = ground_radius(ctx->planet, boss_cut_direction(b, (float)i / (float)(BOSS_CUT_SAMPLES - 1)));
    }
    events_push(ctx->events, ctx->tick, EV_BOSS_CHARGE, b->type, b->pos, b->timer);
}

static void fire_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    float radius = ctx->planet->params.radius;
    float progress = Clamp(1.0f - b->timer / t->breaker_fire_time, 0.0f, 1.0f);
    // Uma mordida a cada `cut_spacing` de arco, até onde o laser já passou,
    // sempre na mesma profundidade em relação ao solo de antes do corte.
    float step = (t->breaker_cut_spacing / radius) / fmaxf(b->cut_angle, 1e-4f);
    b->carve_count = 0;
    while (b->cut_done + step <= progress + 1e-6f && b->carve_count < BOSS_CARVES) {
        b->cut_done += step;
        bool solid;
        float ground = cut_ground_at(b, b->cut_done, &solid);
        if (!solid) continue;
        Vector3 dir = boss_cut_direction(b, b->cut_done);
        b->carves[b->carve_count++] = (CarveRequest){
            Vector3Scale(dir, ground - t->breaker_cut_sink), t->breaker_cut_radius, ENEMY_TYPE_COUNT };
    }
    b->beam_point = boss_cut_point(b, progress);
}

static void turrets_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    if (s == NULL || !s->alive || t->breaker_turret_burst <= 0) return;
    b->fire_wait -= ctx->dt;
    if (b->burst > 0) {
        if (b->fire_wait > 0.0f) return;
        Vector3 muzzle = boss_point(b, t, BREAKER_TURRET[b->turret]);
        float d = Vector3Distance(muzzle, s->pos);
        if (d > t->breaker_turret_range) {
            b->burst = 0;
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
        b->turret ^= 1;
        b->burst--;
        b->fire_wait = TURRET_GAP;
        return;
    }
    b->turret_wait -= ctx->dt;
    if (b->turret_wait <= 0.0f && Vector3Distance(b->pos, s->pos) < t->breaker_turret_range) {
        b->burst = t->breaker_turret_burst;
        b->turret_wait = t->breaker_turret_interval[b->phase - 1];
    }
}

static void fight_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
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
    face(b, tangent, dt);

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
            b->beam_point = boss_cut_point(b, 0.0f);
            events_push(ctx->events, ctx->tick, EV_BOSS_FIRE, b->type, b->beam_point, t->breaker_fire_time);
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
        b->launch_wait -= dt;
        if (b->launch_wait <= 0.0f) {
            b->launch_request = t->breaker_launch_count;
            b->launch_wait = t->breaker_launch_interval;
            events_push(ctx->events, ctx->tick, EV_LAUNCH, ENEMY_CARRIER, boss_point(b, t, BREAKER_BAY),
                        (float)t->breaker_launch_count);
        }
    }
}

void boss_tick(Boss *b, const BossContext *ctx)
{
    b->pos_prev = b->pos;
    b->rot_prev = b->rot;
    b->carve_count = 0;
    b->launch_request = 0;
    b->blast_now = false;
    if (b->state == BOSS_OFF) return;
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    b->age += dt;
    b->flash = fmaxf(b->flash - dt, 0.0f);
    b->core_flash = fmaxf(b->core_flash - dt, 0.0f);
    for (int i = 0; i < BOSS_CAPACITORS; i++) b->cap_flash[i] = fmaxf(b->cap_flash[i] - dt, 0.0f);

    switch (b->state) {
    case BOSS_ENTER: {
        // Chega rápido e freia até parar na órbita (desaceleração suave).
        b->timer -= dt;
        float u = 1.0f - Clamp(b->timer / t->breaker_enter_time, 0.0f, 1.0f);
        float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
        Vector3 before = b->pos;
        b->pos = Vector3Lerp(b->enter_from, b->enter_to, ease);
        b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / dt);
        Vector3 radial = Vector3Normalize(b->pos);
        face(b, Vector3Lerp(Vector3Negate(radial), Vector3CrossProduct(b->axis, radial), ease), dt);
        if (b->timer <= 0.0f) {
            b->state = BOSS_FIGHT;
            b->action = BREAKER_REST;
            b->timer = t->breaker_rest[0];
        }
        break;
    }
    case BOSS_FIGHT:
        fight_tick(b, ctx);
        break;
    default: {   // BOSS_DYING
        // Explosões pelo casco, até a grande.
        b->timer -= dt;
        b->blast_wait -= dt;
        b->vel = Vector3Scale(b->vel, 1.0f - dt);
        b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
        if (b->blast_wait <= 0.0f) {
            int k = (int)rng_below(ctx->rng, BOSS_HULL_SPHERES);
            Vector3 at = boss_point(b, t, BREAKER_HULL[k].center);
            at = Vector3Add(at, Vector3Scale(rng_unit_vec3(ctx->rng), BREAKER_HULL[k].radius * t->breaker_scale * 0.6f));
            events_push(ctx->events, ctx->tick, EV_BOSS_BLAST, b->type, at, t->breaker_scale);
            b->blast_now = true;
            b->blast_at = at;
            b->blast_wait = DYING_BLAST_GAP;
        }
        if (b->timer <= 0.0f) {
            b->state = BOSS_OFF;
            b->defeated = true;
            events_push(ctx->events, ctx->tick, EV_BOSS_DOWN, b->type, b->pos, t->breaker_points);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------

static void update_phase(Boss *b, const Tuning *t, Events *events, uint32_t tick)
{
    float ratio = b->hp / fmaxf(b->hp_max, 1e-3f);
    int phase = (ratio < t->breaker_phase_3) ? 3 : (ratio < t->breaker_phase_2) ? 2 : 1;
    if (phase > b->phase) {
        b->phase = (uint8_t)phase;
        events_push(events, tick, EV_BOSS_PHASE, b->type, b->pos, (float)phase);
    }
}

float boss_damage_core(Boss *b, const Tuning *t, float amount, Events *events, uint32_t tick)
{
    float applied = amount * boss_core_factor(b, t);
    if (applied <= 0.0f) return 0.0f;
    b->hp -= applied;
    b->core_flash = 0.12f;
    b->flash = 0.08f;
    if (b->hp <= 0.0f) {
        b->hp = 0.0f;
        b->state = BOSS_DYING;
        b->action = BREAKER_REST;
        b->timer = DYING_TIME;
        b->blast_wait = 0.0f;
        return applied;
    }
    update_phase(b, t, events, tick);
    return applied;
}

void boss_set_health(Boss *b, const Tuning *t, float fraction, Events *events, uint32_t tick)
{
    if (!boss_vulnerable(b)) return;
    b->hp = fmaxf(b->hp_max * Clamp(fraction, 0.0f, 1.0f), 0.01f);
    update_phase(b, t, events, tick);
}

float boss_damage_capacitor(Boss *b, const Tuning *t, int index, float amount, Events *events, uint32_t tick)
{
    if (!boss_capacitors_armed(b) || index < 0 || index >= BOSS_CAPACITORS || b->cap_hp[index] <= 0.0f) return 0.0f;
    float applied = fminf(amount, b->cap_hp[index]);
    b->cap_hp[index] -= amount;
    b->cap_flash[index] = 0.12f;
    if (b->cap_hp[index] <= 0.0f) {
        b->cap_hp[index] = 0.0f;
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
