#include "game/enemies.h"

#include <math.h>
#include <string.h>

#include "voxel/damage.h"

// Constantes de comportamento: dão a FORMA do movimento. O balanceamento
// (vida, velocidade, cadência, dano) fica em data/enemies.txt.

// --- Enxame ---
// A passada é rasante: o drone desce planando em direção ao alvo, atira
// enquanto ele está à frente do nariz, passa por cima e sobe.
#define SWARM_DIVE_SPEED     1.25f    // na passada voa este tanto mais rápido
#define SWARM_RUN_IN         50.0f    // a passada começa esta distância antes do alvo
#define SWARM_ENTRY          10.0f    // perto assim do ponto de entrada, começa a descer
#define SWARM_GLIDE          1.5f     // quão depressa a descida busca a altura de ataque
#define SWARM_FIRE_RANGE     55.0f    // atira no alvo até esta distância
#define SWARM_FIRE_CONE      0.866f   // cosseno do ângulo máximo entre o nariz e o alvo para atirar (30 graus)
#define SWARM_PASSED         3.0f     // tão perto (na horizontal) do alvo, a passada acabou
#define SWARM_AGGRO_CONE     0.80f    // cosseno do ângulo para atirar na nave (~37 graus)
#define SWARM_SHOT_JITTER    1.4f     // espalhamento dos tiros no solo
#define SWARM_TARGET_SPREAD  0.035f   // espalhamento dos alvos dentro do bando, em radianos
#define SWARM_HOLD_ALTITUDE  0.5f     // força que segura a altura de cruzeiro
#define SWARM_SEPARATION     1.6f     // forças de bando ("boids")
#define SWARM_ALIGNMENT      0.25f
#define SWARM_COHESION       0.12f
#define SWARM_NEIGHBORHOOD   4.0f     // vizinhos até este tanto de `spacing` contam para alinhar e agrupar
#define GOLDEN_ANGLE         2.39996f // espalha pontos em espiral sem que se alinhem

// --- Bombardeiro ---
#define BOMBER_APPROACH_SPEED 1.8f    // desce até a órbita este tanto mais rápido
#define BOMBER_ALTITUDE_PULL  0.8f    // força que o mantém na altura da órbita
#define BOMBER_ARRIVE         3.0f    // tão perto da altura da órbita, passa a orbitar
#define BOMBER_FIRST_BOMB     0.5f    // fração do intervalo até a primeira bomba
#define BOMB_LIFE             12.0f
#define TURRET_GAP            0.18f   // segundos entre os tiros de uma rajada
static const Vector3 BOMB_BAY = { 0.0f, -0.32f, 0.15f };   // em eixos do modelo, vezes a escala
static const Vector3 TURRET   = { 0.0f, 0.42f, -0.10f };
static const Vector3 ENGINE   = { 0.0f, 0.08f, 1.05f };    // o ponto fraco

// --- todos ---
#define BANK_MAX         1.0f     // inclinação máxima nas curvas, em radianos
#define BANK_GAIN        0.9f
#define TURN_SMOOTH      8.0f     // rapidez com que o modelo acompanha a direção do voo
#define GROUND_LOOKAHEAD 0.25f    // segundos à frente em que o terreno é conferido
#define GROUND_MARGIN    3.0f     // distância do terreno que os inimigos evitam
#define GROUND_PUSH      2.0f
#define MOON_MARGIN      10.0f
#define FLASH_TIME       0.12f
#define SPAWN_DEPTH      2.0f     // espalhamento ao longo da direção de chegada

bool enemies_init(Enemies *e, Arena *arena)
{
    memset(e, 0, sizeof *e);
    e->items = ARENA_ARRAY(arena, Enemy, ENEMIES_CAPACITY);
    return e->items != NULL && pool_init(&e->pool, ENEMIES_CAPACITY, arena);
}

void enemies_clear(Enemies *e)
{
    pool_clear(&e->pool);
    memset(e->flocks, 0, sizeof e->flocks);
    memset(e->alive_by_type, 0, sizeof e->alive_by_type);
}

int enemies_alive(const Enemies *e)
{
    return e->pool.live;
}

// Uma direção qualquer perpendicular a `v` (comprimento 1).
static Vector3 perpendicular(Vector3 v)
{
    Vector3 hint = (fabsf(v.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
    return Vector3Normalize(Vector3CrossProduct(v, hint));
}

// Direção perpendicular a `v`, sorteada.
static Vector3 random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 side = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(side) < 1e-6f) return perpendicular(v);
    return Vector3Normalize(side);
}

// Alvo de um drone: perto do alvo do bando, em espiral conforme a posição dele
// no bando, para as crateras de uma passada saírem agrupadas, mas separadas.
static Vector3 drone_target(const Flock *f, int slot)
{
    Vector3 a = perpendicular(f->target);
    Vector3 b = Vector3CrossProduct(f->target, a);
    float angle = (float)slot * GOLDEN_ANGLE;
    float spread = SWARM_TARGET_SPREAD * sqrtf((float)slot + 0.5f);
    Vector3 offset = Vector3Add(Vector3Scale(a, cosf(angle) * spread), Vector3Scale(b, sinf(angle) * spread));
    return Vector3Normalize(Vector3Add(f->target, offset));
}

// Ponto de entrada da passada: na altura de cruzeiro, antes do alvo no
// sentido em que o bando avança. Cada drone entra um pouco mais atrás que o
// anterior, então o bando ataca em fila.
static Vector3 run_entry(const Enemy *en, const Flock *f, float radius, const Tuning *t)
{
    float orbit = radius + t->swarm_cruise_altitude;
    float behind = SWARM_RUN_IN + (float)en->slot * t->swarm_dive_stagger * t->enemy[ENEMY_SWARM].speed;
    Vector3 dir = Vector3RotateByAxisAngle(en->target, f->drift_axis, -behind / orbit);
    return Vector3Scale(Vector3Normalize(dir), orbit);
}

// Onde mirar para acertar quem se move: onde ele vai estar quando o tiro chegar.
static Vector3 lead(Vector3 from, Vector3 target, Vector3 target_vel, float shot_speed)
{
    float time = Vector3Distance(from, target) / shot_speed;
    return Vector3Add(target, Vector3Scale(target_vel, time));
}

static Vector3 model_point(const Enemy *en, Vector3 local, float scale)
{
    return Vector3Add(en->pos, Vector3RotateByQuaternion(Vector3Scale(local, scale), en->rot));
}

Vector3 enemy_weak_spot(const Enemy *en, const Tuning *t)
{
    return model_point(en, ENGINE, t->enemy[en->type].scale);
}

static void fire_plasma(const EnemyContext *ctx, const Enemy *en, Vector3 from, Vector3 aim, float speed,
                        float damage, float crater_radius, float crater_depth)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(aim, from));
    Shot *s = projectiles_spawn(ctx->shots, SHOT_PLASMA, from, Vector3Scale(dir, speed), ctx->t->enemies_shot_life);
    if (s == NULL) return;
    s->damage = damage;
    s->crater_radius = crater_radius;
    s->crater_depth = crater_depth;
    s->source = en->type;
    events_push(ctx->events, ctx->tick, EV_ENEMY_SHOT, en->type, from, 0.0f);
}

static bool ship_targetable(const EnemyContext *ctx)
{
    return ctx->ship != NULL && ctx->ship->alive;
}

static Vector3 ship_velocity(const Ship *s)
{
    return Vector3Add(s->vel, s->fall);
}

// ---------------------------------------------------------------------------
//  Entrada em cena
// ---------------------------------------------------------------------------

static int free_flock(const Enemies *e)
{
    for (int i = 0; i < FLOCKS_CAPACITY; i++) {
        if (e->flocks[i].members == 0) return i;
    }
    return -1;
}

int enemies_spawn_group(Enemies *e, const EnemyContext *ctx, EnemyType type, int count, Vector3 center)
{
    const Tuning *t = ctx->t;
    const EnemyStats *stats = &t->enemy[type];
    Vector3 radial = Vector3Normalize(center);
    Vector3 a = perpendicular(radial);
    Vector3 b = Vector3CrossProduct(radial, a);

    int flock = -1;
    if (type == ENEMY_SWARM) {
        flock = free_flock(e);
        if (flock < 0) return 0;
        Flock *f = &e->flocks[flock];
        f->target = radial;   // ataca o lado do planeta de onde veio
        f->drift_axis = random_perpendicular(ctx->rng, radial);
        f->pass = 0;
    }

    int spawned = 0;
    for (int i = 0; i < count; i++) {
        int index = pool_acquire(&e->pool);
        if (index < 0) break;
        Enemy *en = &e->items[index];
        memset(en, 0, sizeof *en);

        // Espalhados em um disco de frente para o planeta; o primeiro fica
        // bem no centro.
        float angle = (float)i * GOLDEN_ANGLE + rng_range(ctx->rng, 0.0f, 0.5f);
        float r = t->enemies_spawn_spread * sqrtf((float)i / (float)count);
        en->pos = Vector3Add(center, Vector3Add(Vector3Scale(a, cosf(angle) * r), Vector3Scale(b, sinf(angle) * r)));
        if (i > 0) en->pos = Vector3Add(en->pos, Vector3Scale(radial, rng_range(ctx->rng, -SPAWN_DEPTH, SPAWN_DEPTH)));
        en->vel = Vector3Scale(radial, -stats->speed * 0.6f);
        en->rot = quat_look(Vector3Negate(radial), a);
        en->pos_prev = en->pos;
        en->rot_prev = en->rot;
        en->hp = stats->hp;
        en->type = (uint8_t)type;

        if (type == ENEMY_SWARM) {
            en->state = SWARM_APPROACH;
            en->flock = (uint8_t)flock;
            en->slot = (uint8_t)i;
            en->target = drone_target(&e->flocks[flock], i);
            en->turret_wait = rng_range(ctx->rng, 1.0f, 3.0f);
            e->flocks[flock].members++;
        } else {
            en->state = BOMBER_APPROACH;
            en->axis = random_perpendicular(ctx->rng, radial);   // plano da órbita: contém a direção de chegada
            en->timer = t->bomber_bomb_interval * BOMBER_FIRST_BOMB;
            en->turret_wait = rng_range(ctx->rng, 1.0f, t->bomber_turret_interval);
        }
        e->alive_by_type[type]++;
        spawned++;
    }

    if (spawned > 0) events_push(ctx->events, ctx->tick, EV_SPAWN, (uint8_t)type, center, (float)spawned);
    return spawned;
}

// ---------------------------------------------------------------------------
//  Enxame
// ---------------------------------------------------------------------------

// Forças de bando: separação de todos os drones; alinhamento e coesão só com
// os do mesmo bando, e fora do mergulho (no mergulho cada um vai por si).
static Vector3 flocking(const Enemies *e, int self, const Tuning *t)
{
    const Enemy *en = &e->items[self];
    float spacing = t->swarm_spacing;
    Vector3 push = { 0.0f, 0.0f, 0.0f };
    Vector3 velocity_sum = { 0.0f, 0.0f, 0.0f };
    Vector3 position_sum = { 0.0f, 0.0f, 0.0f };
    int mates = 0;

    for (int j = 0; j < e->pool.capacity; j++) {
        if (j == self || !pool_alive(&e->pool, j)) continue;
        const Enemy *other = &e->items[j];
        if (other->type != ENEMY_SWARM || other->hp <= 0.0f) continue;

        Vector3 offset = Vector3Subtract(en->pos, other->pos);
        float distance = Vector3Length(offset);
        if (distance < spacing && distance > 1e-3f) {
            push = Vector3Add(push, Vector3Scale(offset, (spacing - distance) / (spacing * distance)));
        }
        if (other->flock == en->flock && distance < spacing * SWARM_NEIGHBORHOOD) {
            velocity_sum = Vector3Add(velocity_sum, other->vel);
            position_sum = Vector3Add(position_sum, other->pos);
            mates++;
        }
    }

    float speed = t->enemy[ENEMY_SWARM].speed;
    Vector3 force = Vector3Scale(push, speed * SWARM_SEPARATION);
    if (mates > 0 && en->state != SWARM_DIVE) {
        Vector3 align = Vector3Subtract(Vector3Scale(velocity_sum, 1.0f / (float)mates), en->vel);
        Vector3 gather = Vector3Subtract(Vector3Scale(position_sum, 1.0f / (float)mates), en->pos);
        force = Vector3Add(force, Vector3Add(Vector3Scale(align, SWARM_ALIGNMENT), Vector3Scale(gather, SWARM_COHESION)));
    }
    return force;
}

// Rumo atual sem a parte vertical: para onde o drone vai "nivelado".
static Vector3 level_heading(const Enemy *en, Vector3 radial)
{
    Vector3 level = Vector3Subtract(en->vel, Vector3Scale(radial, Vector3DotProduct(en->vel, radial)));
    if (Vector3LengthSqr(level) < 1e-4f) return perpendicular(radial);
    return Vector3Normalize(level);
}

static Vector3 swarm_think(Enemies *e, int index, const EnemyContext *ctx, float *max_speed)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    Flock *f = &e->flocks[en->flock];
    float radius = ctx->planet->params.radius;
    float speed = t->enemy[ENEMY_SWARM].speed;
    float distance = Vector3Length(en->pos);
    Vector3 radial = Vector3Scale(en->pos, 1.0f / distance);
    float altitude = distance - radius;

    // O alvo, e a parte "na horizontal" do caminho até ele.
    Vector3 aim = Vector3Scale(en->target, radius);
    Vector3 to_aim = Vector3Subtract(aim, en->pos);
    Vector3 across = Vector3Subtract(to_aim, Vector3Scale(radial, Vector3DotProduct(to_aim, radial)));
    float across_length = Vector3Length(across);

    Vector3 desired;
    *max_speed = speed;

    switch (en->state) {
    case SWARM_APPROACH: {
        // Voa até o ponto de entrada da passada.
        Vector3 to_entry = Vector3Subtract(run_entry(en, f, radius, t), en->pos);
        float gap = Vector3Length(to_entry);
        desired = Vector3Scale(to_entry, speed / fmaxf(gap, 1e-3f));
        if (gap < SWARM_ENTRY) en->state = SWARM_DIVE;
        break;
    }
    case SWARM_DIVE: {
        // Aponta para o alvo, mas desce só até a altura de ataque: perto
        // dela, a descida vai sumindo e o drone passa nivelado por cima.
        *max_speed = speed * SWARM_DIVE_SPEED;
        desired = Vector3Scale(Vector3Normalize(to_aim), *max_speed);
        float sink_limit = Clamp((altitude - t->swarm_attack_altitude) * SWARM_GLIDE, 0.0f, speed * 0.7f);
        float sink = -Vector3DotProduct(desired, radial);
        if (sink > sink_limit) desired = Vector3Add(desired, Vector3Scale(radial, sink - sink_limit));

        // Atira enquanto o alvo está à frente do nariz.
        Vector3 heading = Vector3Normalize(en->vel);
        float range = Vector3Length(to_aim);
        if (en->fire_wait <= 0.0f && t->swarm_fire_rate > 0.0f && range < SWARM_FIRE_RANGE
            && Vector3DotProduct(heading, Vector3Scale(to_aim, 1.0f / range)) > SWARM_FIRE_CONE) {
            Vector3 jitter = Vector3Scale(random_perpendicular(ctx->rng, en->target),
                                          rng_range(ctx->rng, 0.0f, SWARM_SHOT_JITTER));
            Vector3 nose = Vector3Add(en->pos, Vector3Scale(heading, t->enemy[ENEMY_SWARM].scale));
            fire_plasma(ctx, en, nose, Vector3Add(aim, jitter), t->swarm_shot_speed, t->swarm_shot_damage,
                        t->swarm_crater_radius, t->swarm_crater_depth);
            en->fire_wait = 1.0f / t->swarm_fire_rate;
        }

        // Passou por cima do alvo: a passada acabou.
        if (across_length < SWARM_PASSED || Vector3DotProduct(level_heading(en, radial), across) < 0.0f) {
            en->state = SWARM_CLIMB;
        }
        break;
    }
    default:   // SWARM_CLIMB
        desired = Vector3Scale(Vector3Normalize(Vector3Add(Vector3Scale(radial, 1.3f), level_heading(en, radial))),
                               speed);
        if (altitude > t->swarm_cruise_altitude - 2.0f) {
            // O primeiro a voltar leva o bando para o próximo alvo.
            if (en->pass == f->pass) {
                f->target = Vector3Normalize(Vector3RotateByAxisAngle(f->target, f->drift_axis,
                                                                      t->swarm_drift * DEG2RAD));
                f->pass++;
            }
            en->pass = f->pass;
            en->target = drone_target(f, en->slot);
            en->state = SWARM_APPROACH;
        }
        break;
    }

    // Com a nave na frente e perto, atira nela (só fora da passada).
    bool diving = (en->state == SWARM_DIVE);
    if (!diving && ship_targetable(ctx) && t->swarm_aggro_rate > 0.0f && en->turret_wait <= 0.0f) {
        Vector3 to_ship = Vector3Subtract(ctx->ship->pos, en->pos);
        float d = Vector3Length(to_ship);
        Vector3 heading = Vector3Normalize(en->vel);
        if (d < t->swarm_aggro_range && d > 1e-3f && Vector3DotProduct(heading, Vector3Scale(to_ship, 1.0f / d)) > SWARM_AGGRO_CONE) {
            Vector3 nose = Vector3Add(en->pos, Vector3Scale(heading, t->enemy[ENEMY_SWARM].scale));
            Vector3 at_ship = lead(nose, ctx->ship->pos, ship_velocity(ctx->ship), t->swarm_shot_speed);
            fire_plasma(ctx, en, nose, at_ship, t->swarm_shot_speed, t->swarm_shot_damage, t->swarm_crater_radius,
                        t->swarm_crater_depth);
            en->turret_wait = rng_range(ctx->rng, 0.8f, 1.25f) / t->swarm_aggro_rate;
        }
    }

    return Vector3Add(desired, flocking(e, index, t));
}

// ---------------------------------------------------------------------------
//  Bombardeiro
// ---------------------------------------------------------------------------

static void start_arming(Enemy *en, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    // A bomba sai daqui a `bomb_warning` segundos, mais à frente na órbita;
    // ela cai reto para baixo a partir de lá.
    Vector3 release = Vector3Add(en->pos, Vector3Scale(en->vel, t->bomber_bomb_warning));
    Vector3 ground;
    if (!planet_raycast(ctx->planet, release, Vector3Negate(release), Vector3Length(release), &ground)) {
        en->timer = 1.0f;   // sem chão embaixo (não deveria acontecer): tenta de novo logo
        return;
    }
    en->target = ground;
    en->arming = fmaxf(t->bomber_bomb_warning, ctx->dt);
    events_push(ctx->events, ctx->tick, EV_BOMB_ARMING, ENEMY_BOMBER, ground, t->bomber_bomb_warning);
}

static void drop_bomb(Enemy *en, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    Vector3 bay = model_point(en, BOMB_BAY, t->enemy[ENEMY_BOMBER].scale);
    Vector3 dir = Vector3Normalize(Vector3Subtract(en->target, bay));
    Shot *b = projectiles_spawn(ctx->shots, SHOT_BOMB, bay, Vector3Scale(dir, t->bomber_bomb_speed), BOMB_LIFE);
    if (b == NULL) return;
    b->target = en->target;
    b->accel = t->bomber_bomb_accel;
    b->hp = t->bomber_bomb_hp;
    b->damage = t->enemy[ENEMY_BOMBER].ram_damage;
    b->crater_radius = t->bomber_crater_radius;
    b->crater_depth = t->bomber_crater_depth;
    b->source = ENEMY_BOMBER;
    events_push(ctx->events, ctx->tick, EV_BOMB_DROP, ENEMY_BOMBER, bay, 0.0f);
}

static void bomber_turret(Enemy *en, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    if (!ship_targetable(ctx) || t->bomber_turret_burst <= 0) return;

    Vector3 muzzle = model_point(en, TURRET, t->enemy[ENEMY_BOMBER].scale);
    if (Vector3Distance(muzzle, ctx->ship->pos) > t->bomber_turret_range) {
        en->burst = 0;
        return;
    }
    if (en->burst > 0) {
        if (en->fire_wait <= 0.0f) {
            Vector3 aim = lead(muzzle, ctx->ship->pos, ship_velocity(ctx->ship), t->swarm_shot_speed);
            // Os tiros da torreta não cavam o planeta: o estrago do
            // Bombardeiro é o das bombas, que são avisadas.
            fire_plasma(ctx, en, muzzle, aim, t->swarm_shot_speed, t->bomber_turret_damage, 0.0f, 0.0f);
            en->burst--;
            en->fire_wait = TURRET_GAP;
        }
    } else if (en->turret_wait <= 0.0f) {
        en->burst = t->bomber_turret_burst;
        en->turret_wait = t->bomber_turret_interval;
    }
}

static Vector3 bomber_think(Enemy *en, const EnemyContext *ctx, float *max_speed)
{
    const Tuning *t = ctx->t;
    float speed = t->enemy[ENEMY_BOMBER].speed;
    float distance = Vector3Length(en->pos);
    Vector3 radial = Vector3Scale(en->pos, 1.0f / distance);
    float error = ctx->planet->params.radius + t->bomber_altitude - distance;   // positivo = abaixo da órbita
    Vector3 tangent = Vector3CrossProduct(en->axis, radial);
    if (Vector3LengthSqr(tangent) < 1e-6f) tangent = perpendicular(radial);
    tangent = Vector3Normalize(tangent);

    Vector3 desired;
    if (en->state == BOMBER_APPROACH) {
        *max_speed = speed * BOMBER_APPROACH_SPEED;
        float climb = Clamp(error * BOMBER_ALTITUDE_PULL, -speed * 1.5f, speed * 1.5f);
        desired = Vector3Add(Vector3Scale(tangent, speed), Vector3Scale(radial, climb));
        if (fabsf(error) < BOMBER_ARRIVE) en->state = BOMBER_ORBIT;
    } else {
        *max_speed = speed * 1.1f;
        desired = Vector3Add(Vector3Scale(tangent, speed), Vector3Scale(radial, error * BOMBER_ALTITUDE_PULL));

        if (en->arming > 0.0f) {
            en->arming -= ctx->dt;
            if (en->arming <= 0.0f) {
                en->arming = 0.0f;
                drop_bomb(en, ctx);
                en->timer = t->bomber_bomb_interval;
            }
        } else {
            en->timer -= ctx->dt;
            if (en->timer <= 0.0f) start_arming(en, ctx);
        }
    }

    bomber_turret(en, ctx);
    return desired;
}

// ---------------------------------------------------------------------------
//  Passo
// ---------------------------------------------------------------------------

// Desvia do terreno e da Lua.
static Vector3 avoid_obstacles(const Enemy *en, const EnemyContext *ctx, Vector3 desired, float speed)
{
    Vector3 radial = Vector3Normalize(en->pos);
    float blocked = VOXEL_ISO - GROUND_MARGIN * VOXEL_DENSITY_PER_UNIT;
    Vector3 ahead = Vector3Add(en->pos, Vector3Scale(en->vel, GROUND_LOOKAHEAD));
    if (planet_sample(ctx->planet, ahead) >= blocked || planet_sample(ctx->planet, en->pos) >= blocked) {
        desired = Vector3Add(desired, Vector3Scale(radial, speed * GROUND_PUSH));
    }
    if (ctx->moon != NULL) {
        Vector3 away = Vector3Subtract(en->pos, ctx->moon->pos);
        float d = Vector3Length(away);
        if (d < ctx->moon->radius + MOON_MARGIN && d > 1e-3f) {
            desired = Vector3Add(desired, Vector3Scale(away, speed * 2.0f / d));
        }
    }
    return desired;
}

// O modelo aponta para onde voa, com o topo para longe do planeta, e se
// inclina nas curvas como um avião.
static void orient(Enemy *en, Vector3 applied_accel, float accel_limit, float dt)
{
    float speed = Vector3Length(en->vel);
    if (speed < 0.1f) return;
    Vector3 forward = Vector3Scale(en->vel, 1.0f / speed);
    Vector3 radial = Vector3Normalize(en->pos);
    Vector3 up = Vector3Subtract(radial, Vector3Scale(forward, Vector3DotProduct(radial, forward)));
    if (Vector3LengthSqr(up) < 0.01f) up = quat_up(en->rot);   // voando na vertical: mantém o topo
    up = Vector3Normalize(up);
    Vector3 right = Vector3CrossProduct(forward, up);

    float bank = Clamp(Vector3DotProduct(applied_accel, right) / fmaxf(accel_limit, 1e-3f) * BANK_GAIN, -BANK_MAX,
                       BANK_MAX);
    Vector3 banked = Vector3Add(Vector3Scale(up, cosf(bank)), Vector3Scale(right, sinf(bank)));
    Quaternion target = quat_look(forward, banked);
    en->rot = QuaternionNormalize(QuaternionSlerp(en->rot, target, 1.0f - expf(-TURN_SMOOTH * dt)));
}

void enemies_tick(Enemies *e, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    float dt = ctx->dt;

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        Enemy *en = &e->items[i];
        en->pos_prev = en->pos;
        en->rot_prev = en->rot;
        if (en->hp <= 0.0f) continue;   // morto, esperando ser removido

        const EnemyStats *stats = &t->enemy[en->type];
        en->age += dt;
        en->flash = fmaxf(en->flash - dt, 0.0f);
        en->fire_wait -= dt;
        en->turret_wait -= dt;

        float max_speed = stats->speed;
        Vector3 desired = (en->type == ENEMY_SWARM) ? swarm_think(e, i, ctx, &max_speed)
                                                    : bomber_think(en, ctx, &max_speed);
        desired = avoid_obstacles(en, ctx, desired, stats->speed);

        // Acelera em direção à velocidade desejada, com limite.
        Vector3 change = Vector3Subtract(desired, en->vel);
        float limit = stats->accel * dt;
        float amount = Vector3Length(change);
        if (amount > limit) change = Vector3Scale(change, limit / amount);
        en->vel = Vector3Add(en->vel, change);
        float speed = Vector3Length(en->vel);
        if (speed > max_speed) en->vel = Vector3Scale(en->vel, max_speed / speed);

        en->pos = Vector3Add(en->pos, Vector3Scale(en->vel, dt));
        orient(en, Vector3Scale(change, 1.0f / dt), stats->accel, dt);

        // Bateu no planeta ou na Lua.
        bool crashed = planet_sample(ctx->planet, en->pos) >= VOXEL_ISO;
        if (ctx->moon != NULL && Vector3Distance(en->pos, ctx->moon->pos) < ctx->moon->radius) crashed = true;
        if (crashed) enemies_damage(e, i, en->hp, DEATH_CRASH);
    }
}

bool enemies_damage(Enemies *e, int index, float amount, DeathCause cause)
{
    if (index < 0 || index >= e->pool.capacity || !pool_alive(&e->pool, index)) return false;
    Enemy *en = &e->items[index];
    if (en->hp <= 0.0f || amount <= 0.0f) return false;
    en->hp -= amount;
    en->flash = FLASH_TIME;
    if (en->hp > 0.0f) return false;
    en->hp = 0.0f;
    en->death = (uint8_t)cause;
    return true;
}

void enemies_remove(Enemies *e, int index)
{
    if (index < 0 || index >= e->pool.capacity || !pool_alive(&e->pool, index)) return;
    Enemy *en = &e->items[index];
    if (en->type == ENEMY_SWARM && e->flocks[en->flock].members > 0) e->flocks[en->flock].members--;
    e->alive_by_type[en->type]--;
    pool_release(&e->pool, index);
}
