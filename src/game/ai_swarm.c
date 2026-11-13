// Enxame: drones em bando que atacam o planeta e encaram a nave.
//
//   APPROACH  voa até o seu lugar na formação, no ponto de espera do bando:
//             na altura de cruzeiro, um pouco antes do alvo;
//   HOLD      fica ali, quase parado, balançando de leve e virado para a nave
//             (ou para o alvo); atira na nave se ela está perto;
//   DIVE      passada: desce em direção ao alvo atirando no solo e passa por
//             cima dele;
//   CLIMB     sobe, já rumo ao próximo ponto de espera, que fica adiante (o
//             alvo do bando anda alguns graus a cada passada). Como o ponto
//             novo fica à frente, o drone nunca precisa dar meia-volta;
//   ENGAGE    com a nave por perto, parte do bando sai para encará-la: fica a
//             uma distância fixa dela, circulando devagar e atirando, e
//             depois de um tempo volta ao bando.
// Os drones saem para a passada um de cada vez (o bando ataca em fila).
#include "game/enemy_ai.h"

#include <math.h>

#define SWARM_DIVE_SPEED     1.2f     // na passada voa este tanto mais rápido
#define SWARM_RUN_IN         34.0f    // o ponto de espera fica esta distância antes do alvo
#define SWARM_ARRIVE         4.0f     // perto assim do seu lugar, passa a esperar
#define SWARM_SLOWDOWN       16.0f    // desacelera ao chegar a esta distância do seu lugar
#define SWARM_HOLD_SPRING    1.2f     // força que o mantém no lugar enquanto espera
#define SWARM_BOB            0.8f     // balanço enquanto espera (altura, em unidades)
#define SWARM_BOB_RATE       1.7f     // ... e ritmo (radianos por segundo)
#define SWARM_GLIDE          1.5f     // quão depressa a descida busca a altura de ataque
#define SWARM_FIRE_RANGE     55.0f    // atira no alvo até esta distância
#define SWARM_FIRE_CONE      0.866f   // cosseno do ângulo máximo entre o nariz e o alvo para atirar (30 graus)
#define SWARM_PASSED         3.0f     // tão perto (na horizontal) do alvo, a passada acabou
#define SWARM_CLIMB_TO       6.0f     // sobe até a altura de ataque mais isto antes de seguir
#define SWARM_FACING         0.85f    // cosseno do ângulo para atirar na nave (~32 graus)
#define SWARM_SHOT_JITTER    1.4f     // espalhamento dos tiros no solo
#define SWARM_TARGET_SPREAD  0.035f   // espalhamento dos alvos dentro do bando, em radianos
#define SWARM_SEPARATION     1.0f     // forças de bando ("boids")
#define SWARM_ALIGNMENT      0.25f
#define SWARM_NEIGHBORHOOD   4.0f     // vizinhos até este tanto de `spacing` contam para alinhar
#define SWARM_ENGAGE_SPRING  0.9f     // força que o mantém na distância certa da nave
#define SWARM_ENGAGE_CIRCLE  0.3f     // fração da velocidade usada para circular em volta da nave
#define SWARM_ENGAGE_FOLLOW  0.5f     // acompanha esta fração da velocidade da nave
#define SWARM_ENGAGE_LEAVE   1.8f     // desiste se a nave passa de aggro_range vezes isto
#define SWARM_ENGAGE_FLOOR   6.0f     // não desce abaixo desta altura para encarar a nave
#define SWARM_REENGAGE       4.0f     // segundos no bando antes de poder sair de novo

// Alvo de um drone: perto do alvo do bando, em espiral conforme a posição dele
// no bando, para as crateras de uma passada saírem agrupadas, mas separadas.
Vector3 swarm_drone_target(const Flock *f, int slot)
{
    Vector3 a = ai_perpendicular(f->target);
    Vector3 b = Vector3CrossProduct(f->target, a);
    float angle = (float)slot * GOLDEN_ANGLE;
    float spread = SWARM_TARGET_SPREAD * sqrtf((float)slot + 0.5f);
    Vector3 offset = Vector3Add(Vector3Scale(a, cosf(angle) * spread), Vector3Scale(b, sinf(angle) * spread));
    return Vector3Normalize(Vector3Add(f->target, offset));
}

// Lugar do drone na formação: o ponto de espera fica na altura de cruzeiro,
// antes do alvo no sentido em que o bando avança; a formação é um "V" aberto
// para trás, com o drone 0 na ponta.
static Vector3 station(const Enemy *en, const Flock *f, float radius, const Tuning *t)
{
    float orbit = radius + t->swarm_cruise_altitude;
    Vector3 dir = Vector3Normalize(Vector3RotateByAxisAngle(en->target, f->drift_axis, -SWARM_RUN_IN / orbit));
    Vector3 center = Vector3Scale(dir, orbit);

    Vector3 ahead = Vector3Normalize(Vector3CrossProduct(f->drift_axis, dir));   // sentido em que o bando avança
    Vector3 side = Vector3CrossProduct(dir, ahead);
    int rank = (en->slot + 1) / 2;
    float lateral = (float)rank * t->swarm_spacing * 1.3f * ((en->slot % 2) ? 1.0f : -1.0f);
    float back = (float)rank * t->swarm_spacing * 0.8f;
    return Vector3Add(center, Vector3Add(Vector3Scale(side, lateral), Vector3Scale(ahead, -back)));
}

// Separação de todos os drones; alinhamento só com os do mesmo bando, e só
// enquanto viajam (parados ou na passada, cada um cuida de si).
static Vector3 flocking(const Enemies *e, int self, const Tuning *t)
{
    const Enemy *en = &e->items[self];
    float spacing = t->swarm_spacing;
    Vector3 push = { 0.0f, 0.0f, 0.0f };
    Vector3 velocity_sum = { 0.0f, 0.0f, 0.0f };
    int mates = 0;
    bool travelling = (en->state == SWARM_APPROACH || en->state == SWARM_CLIMB);

    for (int j = 0; j < e->pool.capacity; j++) {
        if (j == self || !pool_alive(&e->pool, j)) continue;
        const Enemy *other = &e->items[j];
        if (other->type != ENEMY_SWARM || other->hp <= 0.0f) continue;

        Vector3 offset = Vector3Subtract(en->pos, other->pos);
        float distance = Vector3Length(offset);
        if (distance < spacing && distance > 1e-3f) {
            // Empurrão que cresce suave com a proximidade (sem tranco na borda).
            float closeness = (spacing - distance) / spacing;
            push = Vector3Add(push, Vector3Scale(offset, closeness * closeness / distance));
        }
        if (travelling && other->flock == en->flock && distance < spacing * SWARM_NEIGHBORHOOD) {
            velocity_sum = Vector3Add(velocity_sum, other->vel);
            mates++;
        }
    }

    float speed = t->enemy[ENEMY_SWARM].speed;
    Vector3 force = Vector3Scale(push, speed * SWARM_SEPARATION);
    if (mates > 0) {
        Vector3 align = Vector3Subtract(Vector3Scale(velocity_sum, 1.0f / (float)mates), en->vel);
        force = Vector3Add(force, Vector3Scale(align, SWARM_ALIGNMENT));
    }
    return force;
}

// Chega a `spot` desacelerando, sem passar do ponto.
static Vector3 arrive(Vector3 pos, Vector3 spot, float speed, float *gap)
{
    Vector3 to = Vector3Subtract(spot, pos);
    *gap = Vector3Length(to);
    float pace = speed * Clamp(*gap / SWARM_SLOWDOWN, 0.15f, 1.0f);
    return Vector3Scale(to, pace / fmaxf(*gap, 1e-3f));
}

// Atira na nave, com antecipação, se ela está na frente do nariz.
static void shoot_ship(Enemy *en, const EnemyContext *ctx, float rate)
{
    const Tuning *t = ctx->t;
    if (rate <= 0.0f || en->turret_wait > 0.0f) return;
    Vector3 forward = enemy_forward(en);
    Vector3 to_ship = Vector3Subtract(ctx->ship->pos, en->pos);
    float d = Vector3Length(to_ship);
    if (d < 1e-3f || d > t->swarm_aggro_range * SWARM_ENGAGE_LEAVE) return;
    if (Vector3DotProduct(forward, Vector3Scale(to_ship, 1.0f / d)) < SWARM_FACING) return;

    Vector3 nose = Vector3Add(en->pos, Vector3Scale(forward, t->enemy[ENEMY_SWARM].scale));
    Vector3 aim = ai_lead(nose, ctx->ship->pos, ai_ship_velocity(ctx->ship), t->swarm_shot_speed);
    ai_fire(ctx, en, nose, aim, t->swarm_shot_speed, t->swarm_shot_damage, t->swarm_crater_radius, t->swarm_crater_depth);
    en->turret_wait = rng_range(ctx->rng, 0.8f, 1.25f) / rate;
}

// A nave está perto e ainda cabe mais um drone deste bando encarando-a?
static bool may_engage(const Enemy *en, const Flock *f, const EnemyContext *ctx)
{
    const Tuning *t = ctx->t;
    if (!ai_ship_targetable(ctx) || en->cooldown > 0.0f || t->swarm_engage_share <= 0.0f) return false;
    if (Vector3Distance(en->pos, ctx->ship->pos) > t->swarm_aggro_range) return false;
    int allowed = (int)ceilf((float)f->members * t->swarm_engage_share);
    return f->engaged < allowed;
}

static void start_engage(Enemy *en, Flock *f, const EnemyContext *ctx)
{
    en->state = SWARM_ENGAGE;
    en->timer = ctx->t->swarm_engage_time * rng_range(ctx->rng, 0.8f, 1.2f);
    Vector3 from = Vector3Normalize(Vector3Subtract(en->pos, ctx->ship->pos));
    en->axis = ai_random_perpendicular(ctx->rng, from);   // sentido em que circula a nave
    f->engaged++;
}

static void leave_engage(Enemy *en, Flock *f)
{
    en->state = SWARM_APPROACH;
    en->cooldown = SWARM_REENGAGE;
    if (f->engaged > 0) f->engaged--;
}

Steer swarm_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    Flock *f = &e->flocks[en->flock];
    float radius = ctx->planet->params.radius;
    float speed = t->enemy[ENEMY_SWARM].speed;
    float distance = Vector3Length(en->pos);
    Vector3 radial = Vector3Scale(en->pos, 1.0f / distance);
    float altitude = distance - radius;
    en->cooldown = fmaxf(en->cooldown - ctx->dt, 0.0f);

    // O alvo, e a parte "na horizontal" do caminho até ele.
    Vector3 aim = Vector3Scale(en->target, radius);
    Vector3 to_aim = Vector3Subtract(aim, en->pos);
    Vector3 across = Vector3Subtract(to_aim, Vector3Scale(radial, Vector3DotProduct(to_aim, radial)));
    float across_length = Vector3Length(across);

    Vector3 desired = { 0.0f, 0.0f, 0.0f };
    float max_speed = speed;
    en->look = (Vector3){ 0.0f, 0.0f, 0.0f };

    switch (en->state) {
    case SWARM_APPROACH: {
        float gap;
        desired = arrive(en->pos, station(en, f, radius, t), speed, &gap);
        if (gap < SWARM_ARRIVE) {
            // Chegou: espera a sua vez na fila.
            en->state = SWARM_HOLD;
            en->timer = t->swarm_hold_time + (float)en->slot * t->swarm_dive_stagger;
        } else if (may_engage(en, f, ctx)) {
            start_engage(en, f, ctx);
        }
        break;
    }
    case SWARM_HOLD: {
        // Parado no lugar, balançando de leve, de olho na nave (ou no alvo).
        float bob = sinf(en->age * SWARM_BOB_RATE + (float)en->slot) * SWARM_BOB;
        Vector3 spot = Vector3Add(station(en, f, radius, t), Vector3Scale(radial, bob));
        desired = Vector3Scale(Vector3Subtract(spot, en->pos), SWARM_HOLD_SPRING);
        max_speed = speed * 0.5f;
        bool ship_near = ai_ship_targetable(ctx)
                      && Vector3Distance(en->pos, ctx->ship->pos) < t->swarm_aggro_range * 1.3f;
        en->look = ship_near ? Vector3Subtract(ctx->ship->pos, en->pos) : to_aim;
        if (ship_near) shoot_ship(en, ctx, t->swarm_aggro_rate * 0.5f);

        en->timer -= ctx->dt;
        if (may_engage(en, f, ctx)) {
            start_engage(en, f, ctx);
        } else if (en->timer <= 0.0f) {
            en->state = SWARM_DIVE;
        }
        break;
    }
    case SWARM_DIVE: {
        // Aponta para o alvo, mas desce só até a altura de ataque: perto
        // dela, a descida vai sumindo e o drone passa nivelado por cima.
        max_speed = speed * SWARM_DIVE_SPEED;
        desired = Vector3Scale(Vector3Normalize(to_aim), max_speed);
        float sink_limit = Clamp((altitude - t->swarm_attack_altitude) * SWARM_GLIDE, 0.0f, speed * 0.7f);
        float sink = -Vector3DotProduct(desired, radial);
        if (sink > sink_limit) desired = Vector3Add(desired, Vector3Scale(radial, sink - sink_limit));

        // Atira enquanto o alvo está à frente do nariz.
        Vector3 heading = Vector3Normalize(en->vel);
        float range = Vector3Length(to_aim);
        if (en->fire_wait <= 0.0f && t->swarm_fire_rate > 0.0f && range < SWARM_FIRE_RANGE
            && Vector3DotProduct(heading, Vector3Scale(to_aim, 1.0f / range)) > SWARM_FIRE_CONE) {
            Vector3 jitter = Vector3Scale(ai_random_perpendicular(ctx->rng, en->target),
                                          rng_range(ctx->rng, 0.0f, SWARM_SHOT_JITTER));
            Vector3 nose = Vector3Add(en->pos, Vector3Scale(heading, t->enemy[ENEMY_SWARM].scale));
            ai_fire(ctx, en, nose, Vector3Add(aim, jitter), t->swarm_shot_speed, t->swarm_shot_damage,
                    t->swarm_crater_radius, t->swarm_crater_depth);
            en->fire_wait = 1.0f / t->swarm_fire_rate;
        }

        // Passou por cima do alvo: a passada acabou. O primeiro a terminar
        // leva o bando para o próximo alvo, mais adiante.
        if (across_length < SWARM_PASSED || Vector3DotProduct(ai_level_heading(en, radial), across) < 0.0f) {
            if (en->pass == f->pass) {
                f->target = Vector3Normalize(Vector3RotateByAxisAngle(f->target, f->drift_axis,
                                                                      t->swarm_drift * DEG2RAD));
                f->pass++;
            }
            en->pass = f->pass;
            en->target = swarm_drone_target(f, en->slot);
            en->state = SWARM_CLIMB;
        }
        break;
    }
    case SWARM_ENGAGE: {
        const Ship *ship = ctx->ship;
        en->timer -= ctx->dt;
        float d = ai_ship_targetable(ctx) ? Vector3Distance(en->pos, ship->pos) : INFINITY;
        if (en->timer <= 0.0f || d > t->swarm_aggro_range * SWARM_ENGAGE_LEAVE) {
            leave_engage(en, f);
            float gap;
            desired = arrive(en->pos, station(en, f, radius, t), speed, &gap);
            break;
        }
        // Na distância certa da nave, circulando devagar em volta dela.
        Vector3 from = Vector3Scale(Vector3Subtract(en->pos, ship->pos), 1.0f / fmaxf(d, 1e-3f));
        Vector3 spot = Vector3Add(ship->pos, Vector3Scale(from, t->swarm_engage_distance));
        float spot_altitude = Vector3Length(spot) - radius;
        if (spot_altitude < SWARM_ENGAGE_FLOOR) {
            spot = Vector3Add(spot, Vector3Scale(Vector3Normalize(spot), SWARM_ENGAGE_FLOOR - spot_altitude));
        }
        Vector3 circle = Vector3CrossProduct(en->axis, from);
        if (Vector3LengthSqr(circle) < 1e-6f) circle = ai_perpendicular(from);
        circle = Vector3Scale(Vector3Normalize(circle), speed * SWARM_ENGAGE_CIRCLE);
        desired = Vector3Add(Vector3Scale(Vector3Subtract(spot, en->pos), SWARM_ENGAGE_SPRING), circle);
        desired = Vector3Add(desired, Vector3Scale(ai_ship_velocity(ship), SWARM_ENGAGE_FOLLOW));
        en->look = Vector3Subtract(ship->pos, en->pos);
        shoot_ship(en, ctx, t->swarm_aggro_rate);
        break;
    }
    default: {   // SWARM_CLIMB
        // Sobe e segue em direção ao próximo ponto de espera.
        Vector3 toward = Vector3Subtract(station(en, f, radius, t), en->pos);
        Vector3 level = Vector3Subtract(toward, Vector3Scale(radial, Vector3DotProduct(toward, radial)));
        Vector3 heading = (Vector3LengthSqr(level) > 1e-4f) ? Vector3Normalize(level) : ai_level_heading(en, radial);
        desired = Vector3Scale(Vector3Normalize(Vector3Add(radial, heading)), speed);
        if (altitude > t->swarm_attack_altitude + SWARM_CLIMB_TO) en->state = SWARM_APPROACH;
        break;
    }
    }

    if (Vector3Length(desired) > max_speed) desired = Vector3Scale(Vector3Normalize(desired), max_speed);
    return ai_steer(Vector3Add(desired, flocking(e, index, t)), max_speed);
}
