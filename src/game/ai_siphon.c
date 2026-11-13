// Sifão: paira baixo e drena o solo com um feixe.
//
//   APPROACH  voa até a altura de drenagem, sobre o lado de onde veio;
//   DRAIN     paira e, de tempos em tempos, arranca uma pequena esfera do
//             solo onde o feixe toca. O ponto anda devagar pela superfície
//             (`wander`), deixando um sulco: a perda de massa é gradual e
//             contínua enquanto ele estiver vivo.
#include "game/enemy_ai.h"

#include <math.h>

#include "voxel/damage.h"

#define SIPHON_ARRIVE   6.0f    // tão perto do ponto de pairar, começa a drenar
#define SIPHON_SLOWDOWN 12.0f   // desacelera ao chegar a esta distância
#define SIPHON_SPRING   1.5f    // força que o mantém sobre o ponto
#define SIPHON_SINK     0.4f    // cada mordida fica esta fração do raio abaixo do solo

Steer siphon_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    float speed = t->enemy[ENEMY_SIPHON].speed;
    float radius = ctx->planet->params.radius;
    float hover = radius + t->siphon_altitude;

    if (en->state == SIPHON_APPROACH) {
        Vector3 to = Vector3Subtract(Vector3Scale(en->target, hover), en->pos);
        float d = Vector3Length(to);
        if (d < SIPHON_ARRIVE) {
            en->state = SIPHON_DRAIN;
            en->timer = 0.0f;
            en->aux = Vector3Scale(en->target, radius);
            events_push(ctx->events, ctx->tick, EV_DRAIN_START, ENEMY_SIPHON, en->aux, 0.0f);
        }
        float pace = speed * Clamp(d / SIPHON_SLOWDOWN, 0.2f, 1.0f);
        return ai_steer(Vector3Scale(to, pace / fmaxf(d, 1e-3f)), speed);
    }

    // O ponto de drenagem anda devagar pela superfície.
    en->target = Vector3Normalize(Vector3RotateByAxisAngle(en->target, en->axis, t->siphon_wander / radius * ctx->dt));
    Vector3 spot = Vector3Scale(en->target, hover);
    Vector3 desired = Vector3Scale(Vector3Subtract(spot, en->pos), SIPHON_SPRING);
    if (Vector3Length(desired) > speed) desired = Vector3Scale(Vector3Normalize(desired), speed);

    // O feixe desce reto até o solo.
    Vector3 down = Vector3Normalize(Vector3Negate(en->pos));
    Vector3 ground;
    if (planet_raycast(ctx->planet, en->pos, down, Vector3Length(en->pos), &ground)) en->aux = ground;

    en->timer -= ctx->dt;
    if (en->timer <= 0.0f) {
        Vector3 bite = Vector3Add(en->aux, Vector3Scale(down, t->siphon_drain_radius * SIPHON_SINK));
        ai_carve(e, bite, t->siphon_drain_radius, ENEMY_SIPHON);
        en->timer = t->siphon_drain_interval;
    }
    return ai_steer(desired, speed);
}
