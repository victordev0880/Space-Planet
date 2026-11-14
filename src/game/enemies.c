#include "game/enemies.h"

#include <math.h>
#include <string.h>

#include "game/enemy_ai.h"
#include "voxel/damage.h"

// Constantes de comportamento comuns: dão a FORMA do movimento. O
// balanceamento (vida, velocidade, cadência, dano) fica em data/enemies.txt.
#define BANK_MAX         0.55f    // inclinação máxima nas curvas, em radianos (~32 graus)
#define BANK_GAIN        0.7f
#define BANK_SMOOTH      3.0f     // rapidez com que a inclinação acompanha a curva
#define TURN_SMOOTH      5.0f     // rapidez com que o modelo acompanha a direção do voo
#define STILL_SPEED      1.5f     // abaixo disto, sem `look`, o modelo não gira
#define SHOVE_DECAY      6.0f     // rapidez com que o empurrão dos tiros some
#define LOOK_SPEED_MIN   3.0f     // abaixo disto, olha só para `look`...
#define LOOK_SPEED_MAX   10.0f    // ... e acima disto, só para onde voa
#define GROUND_LOOKAHEAD 0.25f    // segundos à frente em que o terreno é conferido
#define GROUND_MARGIN    3.0f     // distância do terreno que os inimigos evitam
#define GROUND_PUSH      2.0f
#define MOON_MARGIN      10.0f
#define FLASH_TIME       0.12f
#define SPAWN_DEPTH      2.0f     // espalhamento ao longo da direção de chegada
#define SIZE_SPREAD      0.08f    // variação de tamanho entre inimigos do mesmo tipo (só desenho)
#define HEAVY_FIRST      0.5f     // fração do intervalo até a primeira bomba (ou lançamento)
#define SIPHON_SPIN      0.7f     // giro do Sifão em volta do próprio eixo, em radianos por segundo
#define SNIPER_AIM_TURN  14.0f    // rapidez com que o Atirador vira o cano para a mira
#define DODGE_KICK       0.5f     // fração da esquiva aplicada de uma vez, no começo dela

// O ponto fraco do Bombardeiro: o motor, atrás (em eixos do modelo, vezes a escala).
static const Vector3 BOMBER_ENGINE = { 0.0f, 0.08f, 1.05f };

bool enemies_init(Enemies *e, Arena *arena)
{
    memset(e, 0, sizeof *e);
    e->forced_variant = -1;
    e->items = ARENA_ARRAY(arena, Enemy, ENEMIES_CAPACITY);
    return e->items != NULL && pool_init(&e->pool, ENEMIES_CAPACITY, arena);
}

void enemies_clear(Enemies *e)
{
    pool_clear(&e->pool);
    memset(e->flocks, 0, sizeof e->flocks);
    memset(e->alive_by_type, 0, sizeof e->alive_by_type);
    e->carve_count = 0;
}

int enemies_alive(const Enemies *e)
{
    return e->pool.live;
}

// ---------------------------------------------------------------------------
//  Ajudantes da IA
// ---------------------------------------------------------------------------

Steer ai_steer(Vector3 desired, float max_speed)
{
    Steer s = { desired, max_speed, true, true, false };
    return s;
}

Vector3 ai_perpendicular(Vector3 v)
{
    Vector3 hint = (fabsf(v.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
    return Vector3Normalize(Vector3CrossProduct(v, hint));
}

Vector3 ai_random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 side = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(side) < 1e-6f) return ai_perpendicular(v);
    return Vector3Normalize(side);
}

Vector3 ai_level_heading(const Enemy *en, Vector3 radial)
{
    Vector3 level = Vector3Subtract(en->vel, Vector3Scale(radial, Vector3DotProduct(en->vel, radial)));
    if (Vector3LengthSqr(level) < 1e-4f) return ai_perpendicular(radial);
    return Vector3Normalize(level);
}

Vector3 ai_lead(Vector3 from, Vector3 target, Vector3 target_vel, float shot_speed)
{
    float time = Vector3Distance(from, target) / shot_speed;
    return Vector3Add(target, Vector3Scale(target_vel, time));
}

Vector3 ai_model_point(const Enemy *en, Vector3 local, float scale)
{
    return Vector3Add(en->pos, Vector3RotateByQuaternion(Vector3Scale(local, scale), en->rot));
}

bool ai_ship_targetable(const EnemyContext *ctx)
{
    return ctx->ship != NULL && ctx->ship->alive;
}

Vector3 ai_ship_velocity(const Ship *s)
{
    return Vector3Add(s->vel, s->fall);
}

void ai_fire(const EnemyContext *ctx, const Enemy *en, Vector3 from, Vector3 aim, float speed, float damage,
             float crater_radius, float crater_depth)
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

Vector3 ai_orbit(const Enemy *en, const EnemyContext *ctx, float altitude, float speed, float *error)
{
    float distance = Vector3Length(en->pos);
    Vector3 radial = Vector3Scale(en->pos, 1.0f / distance);
    *error = ctx->planet->params.radius + altitude - distance;   // positivo = abaixo da órbita
    Vector3 tangent = Vector3CrossProduct(en->axis, radial);
    if (Vector3LengthSqr(tangent) < 1e-6f) tangent = ai_perpendicular(radial);
    return Vector3Scale(Vector3Normalize(tangent), speed);
}

void ai_carve(Enemies *e, Vector3 center, float radius, EnemyType type)
{
    if (e->carve_count >= CARVES_CAPACITY) return;
    e->carves[e->carve_count++] = (CarveRequest){ center, radius, (uint8_t)type };
}

Vector3 enemy_weak_spot(const Enemy *en, const Tuning *t)
{
    return ai_model_point(en, BOMBER_ENGINE, t->enemy[en->type].scale);
}

void enemy_crater(const Tuning *t, EnemyType type, float *radius, float *depth)
{
    switch (type) {
    case ENEMY_BOMBER:
    case ENEMY_CARRIER:
        *radius = t->bomber_crater_radius;
        *depth = t->bomber_crater_depth;
        break;
    case ENEMY_KAMIKAZE:
    case ENEMY_DIGGER:
    case ENEMY_SIPHON:
        *radius = t->kamikaze_crater_radius;
        *depth = t->kamikaze_crater_depth;
        break;
    default:
        *radius = t->swarm_crater_radius;
        *depth = t->swarm_crater_depth;
        break;
    }
}

float digger_progress(const Enemy *en, const Planet *planet)
{
    float radius = planet->params.radius;
    float core = radius * planet->params.core_radius;
    return Clamp((radius - Vector3Length(en->pos)) / (radius - core), 0.0f, 1.0f);
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

// Estado inicial de cada tipo.
static void prepare(Enemies *e, Enemy *en, const EnemyContext *ctx, int slot, int flock, Vector3 radial)
{
    const Tuning *t = ctx->t;
    switch ((EnemyType)en->type) {
    case ENEMY_SWARM:
        en->state = SWARM_APPROACH;
        en->flock = (uint8_t)flock;
        en->slot = (uint8_t)slot;
        en->target = swarm_drone_target(&e->flocks[flock], slot);
        en->turret_wait = rng_range(ctx->rng, 1.0f, 3.0f);
        e->flocks[flock].members++;
        break;
    case ENEMY_BOMBER:
    case ENEMY_CARRIER:
        en->state = HEAVY_APPROACH;
        en->axis = ai_random_perpendicular(ctx->rng, radial);   // plano da órbita: contém a direção de chegada
        en->timer = HEAVY_FIRST * ((en->type == ENEMY_BOMBER) ? t->bomber_bomb_interval : t->carrier_launch_interval);
        en->turret_wait = rng_range(ctx->rng, 1.0f, t->bomber_turret_interval);
        break;
    case ENEMY_KAMIKAZE:
        en->state = KAMIKAZE_DIVE;
        en->target = Vector3Scale(radial, ctx->planet->params.radius);   // mergulha no lado de onde veio
        break;
    case ENEMY_DIGGER:
        en->state = DIGGER_DESCEND;
        en->target = radial;
        break;
    case ENEMY_SIPHON:
        en->state = SIPHON_APPROACH;
        en->target = radial;
        en->axis = ai_random_perpendicular(ctx->rng, radial);   // por onde o feixe vai andar
        break;
    case ENEMY_SNIPER:
        en->state = SNIPER_HUNT;
        en->timer = rng_range(ctx->rng, 0.5f, 1.0f) * t->sniper_fire_interval;
        en->axis = ai_random_perpendicular(ctx->rng, radial);   // sentido em que circula a nave
        break;
    case ENEMY_SHIELDER:
        en->state = SHIELDER_ESCORT;
        break;
    default:
        break;
    }
}

int ai_spawn(Enemies *e, const EnemyContext *ctx, EnemyType type, int count, Vector3 center, float spread)
{
    const Tuning *t = ctx->t;
    const EnemyStats *stats = &t->enemy[type];
    Vector3 radial = Vector3Normalize(center);
    Vector3 a = ai_perpendicular(radial);
    Vector3 b = Vector3CrossProduct(radial, a);

    int flock = -1;
    if (type == ENEMY_SWARM) {
        flock = free_flock(e);
        if (flock < 0) return 0;
        Flock *f = &e->flocks[flock];
        f->target = radial;   // ataca o lado do planeta de onde veio
        f->drift_axis = ai_random_perpendicular(ctx->rng, radial);
        f->pass = 0;
    }
    // O grupo inteiro tem o mesmo desenho e a mesma cor. (O sorteio acontece
    // mesmo quando um roteiro escolhe a variação: a sequência aleatória não muda.)
    uint8_t variant = (uint8_t)rng_below(ctx->rng, ENEMY_VARIANTS);
    if (e->forced_variant >= 0) variant = (uint8_t)(e->forced_variant % ENEMY_VARIANTS);

    int spawned = 0;
    for (int i = 0; i < count; i++) {
        int index = pool_acquire(&e->pool);
        if (index < 0) break;
        Enemy *en = &e->items[index];
        memset(en, 0, sizeof *en);

        // Espalhados em um disco de frente para o planeta; o primeiro fica
        // bem no centro.
        float angle = (float)i * GOLDEN_ANGLE + rng_range(ctx->rng, 0.0f, 0.5f);
        float r = spread * sqrtf((float)i / (float)count);
        en->pos = Vector3Add(center, Vector3Add(Vector3Scale(a, cosf(angle) * r), Vector3Scale(b, sinf(angle) * r)));
        if (i > 0) en->pos = Vector3Add(en->pos, Vector3Scale(radial, rng_range(ctx->rng, -SPAWN_DEPTH, SPAWN_DEPTH)));
        en->vel = Vector3Scale(radial, -stats->speed * 0.6f);
        en->rot = quat_look(Vector3Negate(radial), a);
        en->pos_prev = en->pos;
        en->rot_prev = en->rot;
        en->hp = en->hp_max = stats->hp * ((ctx->hp_scale > 0.0f) ? ctx->hp_scale : 1.0f);
        en->type = (uint8_t)type;
        en->variant = variant;
        en->size = 1.0f + rng_range(ctx->rng, -SIZE_SPREAD, SIZE_SPREAD);
        prepare(e, en, ctx, i, flock, radial);
        e->alive_by_type[type]++;
        spawned++;
    }
    return spawned;
}

int enemies_spawn_group(Enemies *e, const EnemyContext *ctx, EnemyType type, int count, Vector3 center)
{
    int spawned = ai_spawn(e, ctx, type, count, center, ctx->t->enemies_spawn_spread);
    if (spawned > 0) events_push(ctx->events, ctx->tick, EV_SPAWN, (uint8_t)type, center, (float)spawned);
    return spawned;
}

// ---------------------------------------------------------------------------
//  Escudo
// ---------------------------------------------------------------------------

int enemies_shielded_by(const Enemies *e, const Tuning *t, int index)
{
    if (e->alive_by_type[ENEMY_SHIELDER] == 0) return -1;
    const Enemy *en = &e->items[index];
    if (en->type == ENEMY_SHIELDER) return -1;   // Escudeiros não protegem uns aos outros
    float reach2 = t->shielder_radius * t->shielder_radius;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *s = &e->items[i];
        if (s->type != ENEMY_SHIELDER || s->hp <= 0.0f) continue;
        if (Vector3DistanceSqr(s->pos, en->pos) <= reach2) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
//  Passo
// ---------------------------------------------------------------------------

// Desvia do terreno e da Lua.
static Vector3 avoid_obstacles(const Enemy *en, const EnemyContext *ctx, Vector3 desired, float speed, bool ground)
{
    Vector3 radial = Vector3Normalize(en->pos);
    if (ground) {
        float blocked = VOXEL_ISO - GROUND_MARGIN * VOXEL_DENSITY_PER_UNIT;
        Vector3 ahead = Vector3Add(en->pos, Vector3Scale(en->vel, GROUND_LOOKAHEAD));
        if (planet_sample(ctx->planet, ahead) >= blocked || planet_sample(ctx->planet, en->pos) >= blocked) {
            desired = Vector3Add(desired, Vector3Scale(radial, speed * GROUND_PUSH));
        }
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
// inclina nas curvas como um avião. Devagar, a direção do voo deixa de valer
// (oscila à toa e faria o modelo tremer): ele olha para `look`, se a IA deu
// um, ou fica como está. A inclinação é filtrada à parte, para não trocar de
// lado de repente quando a curva muda.
static void orient(Enemy *en, Vector3 applied_accel, float accel_limit, float dt)
{
    float speed = Vector3Length(en->vel);
    bool has_look = Vector3LengthSqr(en->look) > 1e-4f;
    float look_weight = has_look ? Clamp(1.0f - (speed - LOOK_SPEED_MIN) / (LOOK_SPEED_MAX - LOOK_SPEED_MIN), 0.0f, 1.0f)
                                 : 0.0f;
    if (speed < STILL_SPEED && look_weight <= 0.0f) return;

    Vector3 forward = (speed > 1e-3f) ? Vector3Scale(en->vel, 1.0f / speed) : quat_forward(en->rot);
    if (look_weight > 0.0f) forward = Vector3Normalize(Vector3Lerp(forward, Vector3Normalize(en->look), look_weight));
    Vector3 radial = Vector3Normalize(en->pos);
    Vector3 up = Vector3Subtract(radial, Vector3Scale(forward, Vector3DotProduct(radial, forward)));
    if (Vector3LengthSqr(up) < 0.01f) up = quat_up(en->rot);   // voando na vertical: mantém o topo
    up = Vector3Normalize(up);
    Vector3 right = Vector3CrossProduct(forward, up);

    float target_bank = Clamp(Vector3DotProduct(applied_accel, right) / fmaxf(accel_limit, 1e-3f) * BANK_GAIN,
                              -BANK_MAX, BANK_MAX) * (1.0f - look_weight);
    en->bank += (target_bank - en->bank) * (1.0f - expf(-BANK_SMOOTH * dt));
    Vector3 banked = Vector3Add(Vector3Scale(up, cosf(en->bank)), Vector3Scale(right, sinf(en->bank)));
    Quaternion target = quat_look(forward, banked);
    en->rot = QuaternionNormalize(QuaternionSlerp(en->rot, target, 1.0f - expf(-TURN_SMOOTH * dt)));
}

// Alguns tipos não apontam para onde voam:
//   - o Sifão fica sempre de pé (cúpula para longe do planeta, tentáculos e
//     feixe para baixo), girando devagar em volta do próprio eixo;
//   - o Atirador, enquanto carrega, vira o cano para onde vai atirar.
// Os demais seguem orient().
static void orient_enemy(Enemy *en, const Tuning *t, Vector3 applied_accel, float dt)
{
    Vector3 radial = Vector3Normalize(en->pos);
    if (en->type == ENEMY_SIPHON) {
        Vector3 forward = quat_forward(en->rot);
        forward = Vector3Subtract(forward, Vector3Scale(radial, Vector3DotProduct(forward, radial)));
        if (Vector3LengthSqr(forward) < 1e-4f) forward = ai_perpendicular(radial);
        forward = Vector3RotateByAxisAngle(Vector3Normalize(forward), radial, SIPHON_SPIN * dt);
        Quaternion upright = quat_look(forward, radial);
        en->rot = QuaternionNormalize(QuaternionSlerp(en->rot, upright, 1.0f - expf(-TURN_SMOOTH * dt)));
        return;
    }
    if (en->type == ENEMY_SNIPER) {
        // Sempre de frente para a nave (`look`); carregando, o cano segue a mira, mais depressa.
        bool charging = en->arming > 0.0f;
        Vector3 aim = charging ? Vector3Subtract(en->aux, en->pos) : en->look;
        if (Vector3LengthSqr(aim) > 1e-4f) {
            Quaternion target = quat_look(aim, radial);
            float rate = charging ? SNIPER_AIM_TURN : TURN_SMOOTH;
            en->rot = QuaternionNormalize(QuaternionSlerp(en->rot, target, 1.0f - expf(-rate * dt)));
            return;
        }
    }
    orient(en, applied_accel, t->enemy[en->type].accel, dt);
}

// Quem sabe esquivar, e quando: os leves, fora dos momentos em que estão
// presos a uma tarefa (passada, carga do tiro).
static bool can_dodge(const Enemy *en)
{
    switch ((EnemyType)en->type) {
    case ENEMY_SWARM:    return en->state != SWARM_DIVE;
    case ENEMY_KAMIKAZE: return en->state == KAMIKAZE_HUNT;
    case ENEMY_SNIPER:   return en->arming <= 0.0f;
    case ENEMY_SHIELDER: return true;
    default:             return false;
    }
}

// Esquiva: com a nave atirando e o nariz apontado para ele, o inimigo às
// vezes dá um desvio lateral rápido (para o lado da linha de tiro). Devolve a
// velocidade a somar ao voo neste passo.
static Vector3 dodge(Enemy *en, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    Vector3 none = { 0.0f, 0.0f, 0.0f };
    en->dodge_wait -= ctx->dt;
    if (en->dodge_left > 0.0f) {
        en->dodge_left -= ctx->dt;
        return en->dodge;
    }
    const Ship *s = ctx->ship;
    if (s == NULL || s->muzzle <= 0.0f || en->dodge_wait > 0.0f || !can_dodge(en)) return none;
    Vector3 to = Vector3Subtract(en->pos, s->pos);
    float d = Vector3Length(to);
    if (d < 1e-3f || d > t->dodge_range) return none;
    if (Vector3DotProduct(ship_forward(s), Vector3Scale(to, 1.0f / d)) < cosf(t->dodge_cone * DEG2RAD)) return none;
    en->dodge_wait = t->dodge_cooldown;
    if (rng_float(ctx->rng) >= t->dodge_chance) return none;   // desta vez não percebeu

    // Para o lado, perpendicular à linha de tiro e ao "para cima" do planeta.
    Vector3 side = Vector3CrossProduct(to, Vector3Normalize(en->pos));
    if (Vector3LengthSqr(side) < 1e-6f) side = ai_perpendicular(to);
    side = Vector3Normalize(side);
    if (rng_float(ctx->rng) < 0.5f) side = Vector3Negate(side);
    en->dodge = Vector3Scale(side, t->dodge_speed);
    en->dodge_left = t->dodge_time;
    // Um empurrão na hora: sem ele, o limite de aceleração deixaria o
    // desvio lento demais para servir de alguma coisa.
    en->vel = Vector3Add(en->vel, Vector3Scale(en->dodge, DODGE_KICK));
    return en->dodge;
}

static Steer think(Enemies *e, int index, const EnemyContext *ctx)
{
    switch ((EnemyType)e->items[index].type) {
    case ENEMY_SWARM:    return swarm_think(e, index, ctx);
    case ENEMY_BOMBER:   return bomber_think(e, index, ctx);
    case ENEMY_KAMIKAZE: return kamikaze_think(e, index, ctx);
    case ENEMY_DIGGER:   return digger_think(e, index, ctx);
    case ENEMY_SIPHON:   return siphon_think(e, index, ctx);
    case ENEMY_SNIPER:   return sniper_think(e, index, ctx);
    case ENEMY_SHIELDER: return shielder_think(e, index, ctx);
    case ENEMY_CARRIER:  return carrier_think(e, index, ctx);
    default:             return ai_steer(e->items[index].vel, 0.0f);
    }
}

// Online: cada inimigo enxerga a nave viva mais perto dele (no jogo solo, a única).
static void pick_ship(EnemyContext *ctx, Vector3 pos)
{
    if (ctx->ship_count <= 0) return;
    const Ship *best = NULL;
    float best_d = 0.0f;
    for (int i = 0; i < ctx->ship_count; i++) {
        const Ship *s = ctx->ships[i];
        if (s == NULL || !s->alive) continue;
        float d = Vector3DistanceSqr(s->pos, pos);
        if (best == NULL || d < best_d) {
            best = s;
            best_d = d;
        }
    }
    ctx->ship = best;
}

void enemies_tick(Enemies *e, const EnemyContext *shared)
{
    EnemyContext local = *shared;
    EnemyContext *ctx = &local;
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    e->carve_count = 0;

    // Quantos drones de cada bando estão encarando a nave (os que morreram saem da conta).
    for (int k = 0; k < FLOCKS_CAPACITY; k++) e->flocks[k].engaged = 0;
    for (int i = 0; i < e->pool.capacity; i++) {
        const Enemy *en = &e->items[i];
        if (pool_alive(&e->pool, i) && en->hp > 0.0f && en->type == ENEMY_SWARM && en->state == SWARM_ENGAGE) {
            e->flocks[en->flock].engaged++;
        }
    }

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        Enemy *en = &e->items[i];
        en->pos_prev = en->pos;
        en->rot_prev = en->rot;
        if (en->hp <= 0.0f) continue;   // morto, esperando ser removido
        pick_ship(ctx, en->pos);

        const EnemyStats *stats = &t->enemy[en->type];
        en->age += dt;
        en->flash = fmaxf(en->flash - dt, 0.0f);
        en->fire_wait -= dt;
        en->turret_wait -= dt;

        Steer steer = think(e, i, ctx);
        if (steer.scripted) {
            orient_enemy(en, t, (Vector3){ 0.0f, 0.0f, 0.0f }, dt);
            continue;
        }
        Vector3 sidestep = dodge(en, ctx);
        if (Vector3LengthSqr(sidestep) > 0.0f) {
            steer.desired = Vector3Add(steer.desired, sidestep);
            steer.max_speed += t->dodge_speed;
        }
        Vector3 desired = avoid_obstacles(en, ctx, steer.desired, stats->speed, steer.avoid_ground);

        // Acelera em direção à velocidade desejada, com limite.
        Vector3 change = Vector3Subtract(desired, en->vel);
        float limit = stats->accel * dt;
        float amount = Vector3Length(change);
        if (amount > limit) change = Vector3Scale(change, limit / amount);
        en->vel = Vector3Add(en->vel, change);
        float speed = Vector3Length(en->vel);
        if (speed > steer.max_speed) en->vel = Vector3Scale(en->vel, steer.max_speed / speed);

        en->pos = Vector3Add(en->pos, Vector3Scale(Vector3Add(en->vel, en->shove), dt));
        en->shove = Vector3Scale(en->shove, expf(-SHOVE_DECAY * dt));
        orient_enemy(en, t, Vector3Scale(change, 1.0f / dt), dt);

        // Bateu no planeta ou na Lua.
        bool crashed = steer.solid && planet_sample(ctx->planet, en->pos) >= VOXEL_ISO;
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
    en->killer = e->damage_owner;
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
