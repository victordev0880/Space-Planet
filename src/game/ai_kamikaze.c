// Kamikaze: uma esfera de espinhos que se joga contra as coisas.
//
//   DIVE  mergulha no planeta, no lado de onde veio, e abre uma cratera média;
//   HUNT  se a nave passa perto (`aggro_range`), trava nela (com um bipe) e a
//         persegue; junto dela, explode. Se a nave se afasta demais, desiste
//         e volta a mergulhar no planeta.
// É frágil: o certo é derrubá-lo antes que chegue.
#include "game/enemy_ai.h"

#include <math.h>

#define KAMIKAZE_LOSE     1.6f   // desiste da nave além de aggro_range vezes isto
#define KAMIKAZE_DETONATE 0.6f   // explode quando chega a esta fração do raio da explosão
#define KAMIKAZE_LEAD     1.1f   // mira um pouco à frente da nave
#define KAMIKAZE_WEAVE    0.45f  // zigue-zague: desvio lateral, em frações da velocidade
#define KAMIKAZE_WEAVE_RATE 4.5f // ... e o ritmo dele, em radianos por segundo

Steer kamikaze_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    float speed = t->enemy[ENEMY_KAMIKAZE].speed;
    bool can_hunt = ai_ship_targetable(ctx);
    float distance = can_hunt ? Vector3Distance(ctx->ship->pos, en->pos) : INFINITY;

    if (en->state == KAMIKAZE_DIVE && can_hunt && distance < t->kamikaze_aggro_range) {
        en->state = KAMIKAZE_HUNT;
        events_push(ctx->events, ctx->tick, EV_LOCK, ENEMY_KAMIKAZE, en->pos, 0.0f);
    } else if (en->state == KAMIKAZE_HUNT && (!can_hunt || distance > t->kamikaze_aggro_range * KAMIKAZE_LOSE)) {
        en->state = KAMIKAZE_DIVE;
        en->target = Vector3Scale(Vector3Normalize(en->pos), ctx->planet->params.radius);   // o planeta embaixo
    }

    if (en->state == KAMIKAZE_HUNT) {
        if (distance < t->kamikaze_blast_radius * KAMIKAZE_DETONATE) {
            enemies_damage(e, index, en->hp, DEATH_DETONATE);
            return ai_steer(en->vel, speed);
        }
        // Vem em zigue-zague: mais difícil de acertar do que em linha reta.
        Vector3 aim = ai_lead(en->pos, ctx->ship->pos, ai_ship_velocity(ctx->ship), speed * KAMIKAZE_LEAD);
        Vector3 dir = Vector3Normalize(Vector3Subtract(aim, en->pos));
        Vector3 across = Vector3CrossProduct(dir, Vector3Normalize(en->pos));
        if (Vector3LengthSqr(across) > 1e-6f) {
            float weave = sinf(en->age * KAMIKAZE_WEAVE_RATE + (float)index) * KAMIKAZE_WEAVE;
            dir = Vector3Normalize(Vector3Add(dir, Vector3Scale(Vector3Normalize(across), weave)));
        }
        return ai_steer(Vector3Scale(dir, speed), speed);
    }

    // Mergulho: reto no alvo, sem desviar do chão (é para bater).
    Steer s = ai_steer(Vector3Scale(Vector3Normalize(Vector3Subtract(en->target, en->pos)), speed), speed);
    s.avoid_ground = false;
    return s;
}
