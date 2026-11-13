// Enxame: drones em bando que atacam o planeta em passadas rasantes.
//
//   APPROACH  voa até o ponto de entrada da passada, na altura de cruzeiro;
//   DIVE      desce apontando para o alvo e atira enquanto ele está à frente
//             do nariz (cada tiro abre uma cratera pequena); passa por cima;
//   CLIMB     sobe de volta, e o alvo do bando anda alguns graus.
// Os pontos de entrada ficam escalonados: o bando ataca em fila. Dentro do
// bando cada drone mantém distância dos vizinhos ("boids"). Fora da passada,
// atira na nave se ela fica na frente dele.
#include "game/enemy_ai.h"

#include <math.h>

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
#define SWARM_SEPARATION     1.6f     // forças de bando ("boids")
#define SWARM_ALIGNMENT      0.25f
#define SWARM_COHESION       0.12f
#define SWARM_NEIGHBORHOOD   4.0f     // vizinhos até este tanto de `spacing` contam para alinhar e agrupar

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

// Forças de bando: separação de todos os drones; alinhamento e coesão só com
// os do mesmo bando, e fora da passada (na passada cada um vai por si).
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

    // O alvo, e a parte "na horizontal" do caminho até ele.
    Vector3 aim = Vector3Scale(en->target, radius);
    Vector3 to_aim = Vector3Subtract(aim, en->pos);
    Vector3 across = Vector3Subtract(to_aim, Vector3Scale(radial, Vector3DotProduct(to_aim, radial)));
    float across_length = Vector3Length(across);

    Vector3 desired;
    float max_speed = speed;

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

        // Passou por cima do alvo: a passada acabou.
        if (across_length < SWARM_PASSED || Vector3DotProduct(ai_level_heading(en, radial), across) < 0.0f) {
            en->state = SWARM_CLIMB;
        }
        break;
    }
    default:   // SWARM_CLIMB
        desired = Vector3Scale(Vector3Normalize(Vector3Add(Vector3Scale(radial, 1.3f), ai_level_heading(en, radial))),
                               speed);
        if (altitude > t->swarm_cruise_altitude - 2.0f) {
            // O primeiro a voltar leva o bando para o próximo alvo.
            if (en->pass == f->pass) {
                f->target = Vector3Normalize(Vector3RotateByAxisAngle(f->target, f->drift_axis,
                                                                      t->swarm_drift * DEG2RAD));
                f->pass++;
            }
            en->pass = f->pass;
            en->target = swarm_drone_target(f, en->slot);
            en->state = SWARM_APPROACH;
        }
        break;
    }

    // Com a nave na frente e perto, atira nela (só fora da passada).
    if (en->state != SWARM_DIVE && ai_ship_targetable(ctx) && t->swarm_aggro_rate > 0.0f && en->turret_wait <= 0.0f) {
        Vector3 to_ship = Vector3Subtract(ctx->ship->pos, en->pos);
        float d = Vector3Length(to_ship);
        Vector3 heading = Vector3Normalize(en->vel);
        if (d < t->swarm_aggro_range && d > 1e-3f
            && Vector3DotProduct(heading, Vector3Scale(to_ship, 1.0f / d)) > SWARM_AGGRO_CONE) {
            Vector3 nose = Vector3Add(en->pos, Vector3Scale(heading, t->enemy[ENEMY_SWARM].scale));
            Vector3 at_ship = ai_lead(nose, ctx->ship->pos, ai_ship_velocity(ctx->ship), t->swarm_shot_speed);
            ai_fire(ctx, en, nose, at_ship, t->swarm_shot_speed, t->swarm_shot_damage, t->swarm_crater_radius,
                    t->swarm_crater_depth);
            en->turret_wait = rng_range(ctx->rng, 0.8f, 1.25f) / t->swarm_aggro_rate;
        }
    }

    return ai_steer(Vector3Add(desired, flocking(e, index, t)), max_speed);
}
