// Escavador: pousa e cava um túnel reto rumo ao núcleo.
//
//   DESCEND  voa até o ponto de pouso, no lado do planeta de onde veio;
//   DRILL    ao encostar no solo, passa a descer devagar (`drill_speed`),
//            mordendo uma esfera à frente de tempos em tempos. O túnel é
//            largo o bastante para a nave entrar atrás dele: lá dentro, é o
//            único jeito de alcançá-lo.
// Chegando ao núcleo, continua cavando, e o núcleo começa a perder vida.
#include "game/enemy_ai.h"

#include <math.h>

#define DIGGER_SLOWDOWN 20.0f   // desacelera ao chegar a esta distância do ponto de pouso
#define DIGGER_FEELER   1.2f    // confere o solo este tanto (em raios) à frente do corpo
#define DIGGER_BITE     0.45f   // a broca morde esta fração do raio do túnel à frente do corpo
#define DIGGER_BOTTOM   2.0f    // não passa desta distância do centro do planeta

Steer digger_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    const EnemyStats *stats = &t->enemy[ENEMY_DIGGER];
    float radius = ctx->planet->params.radius;

    if (en->state == DIGGER_DESCEND) {
        Vector3 landing = Vector3Scale(en->target, radius);
        Vector3 to = Vector3Subtract(landing, en->pos);
        float d = Vector3Length(to);
        float pace = stats->speed * Clamp(d / DIGGER_SLOWDOWN, 0.3f, 1.0f);

        // Encostou no solo: começa a cavar, reto para o centro.
        Vector3 down = Vector3Normalize(Vector3Negate(en->pos));
        Vector3 feeler = Vector3Add(en->pos, Vector3Scale(down, stats->radius * DIGGER_FEELER));
        if (planet_sample(ctx->planet, feeler) >= VOXEL_ISO) {
            en->state = DIGGER_DRILL;
            en->target = Vector3Negate(down);   // direção da boca do túnel
            en->vel = Vector3Scale(down, t->digger_drill_speed);
            en->timer = 0.0f;
            events_push(ctx->events, ctx->tick, EV_DIG_START, ENEMY_DIGGER, en->pos, 0.0f);
        }

        Steer s = ai_steer(Vector3Scale(to, pace / fmaxf(d, 1e-3f)), stats->speed);
        s.avoid_ground = false;   // quer chegar no chão
        s.solid = false;          // ... e não se destrói ao encostar
        return s;
    }

    // Cavando: o movimento é dele, sem física de voo.
    float distance = Vector3Length(en->pos);
    float next = fmaxf(distance - t->digger_drill_speed * ctx->dt, DIGGER_BOTTOM);
    en->pos = Vector3Scale(en->target, next);
    en->vel = Vector3Scale(en->target, -t->digger_drill_speed);

    en->timer -= ctx->dt;
    if (en->timer <= 0.0f) {
        Vector3 bite = Vector3Subtract(en->pos, Vector3Scale(en->target, t->digger_drill_radius * DIGGER_BITE));
        ai_carve(e, bite, t->digger_drill_radius, ENEMY_DIGGER);
        en->timer = t->digger_drill_interval;
    }

    Steer s = ai_steer(en->vel, stats->speed);
    s.scripted = true;
    return s;
}
