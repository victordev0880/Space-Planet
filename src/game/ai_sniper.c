// Atirador: fica longe da nave e carrega tiros fortes contra ela.
//
// Mantém a distância `range` e circula em volta da nave. A cada
// `fire_interval`, carrega por `charge_time` segundos: durante a carga a
// mira (en->aux) segue a nave, e o desenho liga o cano ao alvo com uma linha
// vermelha; nos últimos `lock_time` segundos a mira trava, e quem se mexe
// (ou dá um dash) escapa. Sem a nave em jogo, espera em órbita.
#include "game/enemy_ai.h"

#include <math.h>

#define SNIPER_CIRCLE   0.45f   // fração da velocidade usada para circular em volta da nave
#define SNIPER_SPRING   0.8f    // força que o mantém na distância certa
#define SNIPER_IDLE     40.0f   // altura em que espera, sem a nave em jogo
static const Vector3 SNIPER_MUZZLE = { 0.0f, 0.0f, -1.6f };   // em eixos do modelo, vezes a escala

Steer sniper_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    float speed = t->enemy[ENEMY_SNIPER].speed;
    Vector3 radial = Vector3Normalize(en->pos);

    if (!ai_ship_targetable(ctx)) {
        en->arming = 0.0f;
        en->look = (Vector3){ 0.0f, 0.0f, 0.0f };
        float error;
        Vector3 tangent = ai_orbit(en, ctx, SNIPER_IDLE, speed * 0.5f, &error);
        return ai_steer(Vector3Add(tangent, Vector3Scale(radial, error * SNIPER_SPRING)), speed);
    }

    // Posição: na distância certa da nave, circulando em volta dela, sempre de frente para ela.
    const Ship *ship = ctx->ship;
    en->look = Vector3Subtract(ship->pos, en->pos);
    Vector3 from_ship = Vector3Subtract(en->pos, ship->pos);
    float d = Vector3Length(from_ship);
    Vector3 away = (d > 1e-3f) ? Vector3Scale(from_ship, 1.0f / d) : radial;
    Vector3 hold = Vector3Add(ship->pos, Vector3Scale(away, t->sniper_range));
    Vector3 circle = Vector3CrossProduct(en->axis, away);
    if (Vector3LengthSqr(circle) < 1e-6f) circle = ai_perpendicular(away);
    circle = Vector3Scale(Vector3Normalize(circle), speed * SNIPER_CIRCLE);
    Vector3 desired = Vector3Add(Vector3Scale(Vector3Subtract(hold, en->pos), SNIPER_SPRING), circle);
    if (Vector3Length(desired) > speed) desired = Vector3Scale(Vector3Normalize(desired), speed);

    // Tiro carregado.
    en->timer -= ctx->dt;
    if (en->timer <= t->sniper_charge_time) {
        Vector3 muzzle = ai_model_point(en, SNIPER_MUZZLE, t->enemy[ENEMY_SNIPER].scale);
        if (en->arming <= 0.0f) {
            events_push(ctx->events, ctx->tick, EV_SNIPER_CHARGE, ENEMY_SNIPER, en->pos, t->sniper_charge_time);
        }
        en->arming = fmaxf(en->timer, ctx->dt);
        if (en->timer > t->sniper_lock_time) {
            en->aux = ai_lead(muzzle, ship->pos, ai_ship_velocity(ship), t->sniper_shot_speed);   // segue a nave
        }
        if (en->timer <= 0.0f) {
            ai_fire(ctx, en, muzzle, en->aux, t->sniper_shot_speed, t->sniper_shot_damage, 0.0f, 0.0f);
            en->timer = t->sniper_fire_interval;
            en->arming = 0.0f;
        }
    }
    return ai_steer(desired, speed);
}
