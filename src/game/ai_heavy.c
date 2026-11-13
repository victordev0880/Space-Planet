// As naves pesadas, que orbitam o planeta: Bombardeiro e Porta-drones.
//
// Bombardeiro. Desce até a altura da órbita e dá voltas no planeta. Antes de
// cada bomba, marca no solo onde ela vai cair (`bomb_warning` segundos); a
// bomba cai exatamente ali e pode ser derrubada no ar. Uma torreta atira na
// nave quando ela chega perto. A frente é blindada; o motor, atrás, é o
// ponto fraco (ver game/combat.c).
//
// Porta-drones. Orbita mais alto e, de tempos em tempos, acende o hangar e
// solta um bando de Enxame, que ataca o planeta embaixo dele.
#include "game/enemy_ai.h"

#include <math.h>

#include "voxel/damage.h"

#define HEAVY_APPROACH_SPEED 1.8f    // desce até a órbita este tanto mais rápido
#define HEAVY_ALTITUDE_PULL  0.8f    // força que o mantém na altura da órbita
#define HEAVY_ARRIVE         3.0f    // tão perto da altura da órbita, passa a orbitar
#define BOMB_LIFE            12.0f
#define TURRET_GAP           0.18f   // segundos entre os tiros de uma rajada
#define LAUNCH_SPREAD        4.0f    // os drones saem juntinhos do hangar

static const Vector3 BOMB_BAY    = { 0.0f, -0.32f, 0.15f };   // em eixos do modelo, vezes a escala
static const Vector3 TURRET      = { 0.0f, 0.42f, -0.10f };
static const Vector3 CARRIER_BAY = { 0.0f, -0.30f, 0.20f };

// Movimento comum: descer até a órbita, depois dar voltas.
static Steer orbit_steer(Enemy *en, const EnemyContext *ctx, float altitude)
{
    float speed = ctx->t->enemy[en->type].speed;
    float error;
    Vector3 tangent = ai_orbit(en, ctx, altitude, speed, &error);
    Vector3 radial = Vector3Normalize(en->pos);

    if (en->state == HEAVY_APPROACH) {
        float climb = Clamp(error * HEAVY_ALTITUDE_PULL, -speed * 1.5f, speed * 1.5f);
        if (fabsf(error) < HEAVY_ARRIVE) en->state = HEAVY_ORBIT;
        return ai_steer(Vector3Add(tangent, Vector3Scale(radial, climb)), speed * HEAVY_APPROACH_SPEED);
    }
    return ai_steer(Vector3Add(tangent, Vector3Scale(radial, error * HEAVY_ALTITUDE_PULL)), speed * 1.1f);
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
    Vector3 bay = ai_model_point(en, BOMB_BAY, t->enemy[ENEMY_BOMBER].scale);
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
    if (!ai_ship_targetable(ctx) || t->bomber_turret_burst <= 0) return;

    Vector3 muzzle = ai_model_point(en, TURRET, t->enemy[ENEMY_BOMBER].scale);
    if (Vector3Distance(muzzle, ctx->ship->pos) > t->bomber_turret_range) {
        en->burst = 0;
        return;
    }
    if (en->burst > 0) {
        if (en->fire_wait <= 0.0f) {
            Vector3 aim = ai_lead(muzzle, ctx->ship->pos, ai_ship_velocity(ctx->ship), t->swarm_shot_speed);
            // Os tiros da torreta não cavam o planeta: o estrago do
            // Bombardeiro é o das bombas, que são avisadas.
            ai_fire(ctx, en, muzzle, aim, t->swarm_shot_speed, t->bomber_turret_damage, 0.0f, 0.0f);
            en->burst--;
            en->fire_wait = TURRET_GAP;
        }
    } else if (en->turret_wait <= 0.0f) {
        en->burst = t->bomber_turret_burst;
        en->turret_wait = t->bomber_turret_interval;
    }
}

Steer bomber_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    Steer steer = orbit_steer(en, ctx, t->bomber_altitude);

    if (en->state == HEAVY_ORBIT) {
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
    return steer;
}

// ---------------------------------------------------------------------------
//  Porta-drones
// ---------------------------------------------------------------------------

Steer carrier_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    Steer steer = orbit_steer(en, ctx, t->carrier_altitude);
    if (en->state != HEAVY_ORBIT) return steer;

    if (en->arming > 0.0f) {
        // Hangar aceso: o bando está para sair.
        en->arming -= ctx->dt;
        if (en->arming <= 0.0f) {
            en->arming = 0.0f;
            en->timer = t->carrier_launch_interval;
            if (e->alive_by_type[ENEMY_SWARM] < t->carrier_max_swarm) {
                Vector3 bay = ai_model_point(en, CARRIER_BAY, t->enemy[ENEMY_CARRIER].scale);
                int launched = ai_spawn(e, ctx, ENEMY_SWARM, t->carrier_launch_count, bay, LAUNCH_SPREAD);
                if (launched > 0) events_push(ctx->events, ctx->tick, EV_LAUNCH, ENEMY_CARRIER, bay, (float)launched);
            }
        }
    } else {
        en->timer -= ctx->dt;
        if (en->timer <= 0.0f) en->arming = fmaxf(t->carrier_launch_warning, ctx->dt);
    }
    return steer;
}
