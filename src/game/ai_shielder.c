// Escudeiro: acompanha os aliados e os protege dos tiros da nave.
//
// Procura o aliado mais perto (que não seja outro Escudeiro) e fica um pouco
// acima e atrás do grupo em volta dele. Quem estiver a até `shield_radius`
// de um Escudeiro vivo não leva dano dos tiros (ver enemies_shielded_by e
// game/combat.c): é preciso derrubar o Escudeiro primeiro.
#include "game/enemy_ai.h"

#include <math.h>

#define SHIELDER_SPRING 1.5f    // força que o mantém no lugar junto do grupo
#define SHIELDER_ABOVE  0.6f    // fica acima do grupo, em frações de `follow`
#define SHIELDER_BEHIND 0.8f    // ... e atrás dele
#define SHIELDER_IDLE   30.0f   // sem ninguém para proteger, espera em órbita nesta altura
#define SHIELDER_GUARD  90.0f   // com a nave a esta distância do grupo, fica entre os dois...
#define SHIELDER_FRONT  0.9f    // ... a esta fração de `follow` à frente do grupo

Steer shielder_think(Enemies *e, int index, const EnemyContext *ctx)
{
    Enemy *en = &e->items[index];
    const Tuning *t = ctx->t;
    float speed = t->enemy[ENEMY_SHIELDER].speed;

    // O aliado mais perto.
    int nearest = -1;
    float best = INFINITY;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (i == index || !pool_alive(&e->pool, i)) continue;
        const Enemy *ally = &e->items[i];
        if (ally->type == ENEMY_SHIELDER || ally->hp <= 0.0f) continue;
        float d = Vector3DistanceSqr(ally->pos, en->pos);
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    if (nearest < 0) {
        float error;
        Vector3 tangent = ai_orbit(en, ctx, SHIELDER_IDLE, speed * 0.5f, &error);
        return ai_steer(Vector3Add(tangent, Vector3Scale(Vector3Normalize(en->pos), error)), speed);
    }

    // O grupo em volta dele: posição e velocidade médias.
    const Enemy *anchor = &e->items[nearest];
    Vector3 center = { 0.0f, 0.0f, 0.0f }, drift = { 0.0f, 0.0f, 0.0f };
    int members = 0;
    float reach2 = t->shielder_radius * t->shielder_radius;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *ally = &e->items[i];
        if (ally->type == ENEMY_SHIELDER || ally->hp <= 0.0f) continue;
        if (Vector3DistanceSqr(ally->pos, anchor->pos) > reach2) continue;
        center = Vector3Add(center, ally->pos);
        drift = Vector3Add(drift, ally->vel);
        members++;
    }
    center = Vector3Scale(center, 1.0f / (float)members);
    drift = Vector3Scale(drift, 1.0f / (float)members);

    // Um pouco acima do grupo (longe do planeta) e atrás dele. Com a nave por
    // perto, se põe entre ela e o grupo: escudo na frente, alvo exposto.
    Vector3 up = Vector3Normalize(center);
    Vector3 back = (Vector3LengthSqr(drift) > 1e-4f) ? Vector3Normalize(Vector3Negate(drift)) : ai_perpendicular(up);
    Vector3 spot = Vector3Add(center, Vector3Add(Vector3Scale(up, t->shielder_follow * SHIELDER_ABOVE),
                                                 Vector3Scale(back, t->shielder_follow * SHIELDER_BEHIND)));
    if (ai_ship_targetable(ctx)) {
        Vector3 to_ship = Vector3Subtract(ctx->ship->pos, center);
        float d = Vector3Length(to_ship);
        if (d < SHIELDER_GUARD && d > 1e-3f) {
            Vector3 front = Vector3Add(center, Vector3Scale(to_ship, t->shielder_follow * SHIELDER_FRONT / d));
            spot = Vector3Add(front, Vector3Scale(up, t->shielder_follow * 0.3f));
        }
    }
    Vector3 desired = Vector3Add(drift, Vector3Scale(Vector3Subtract(spot, en->pos), SHIELDER_SPRING));
    if (Vector3Length(desired) > speed) desired = Vector3Scale(Vector3Normalize(desired), speed);
    return ai_steer(desired, speed);
}
