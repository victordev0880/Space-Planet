// Uso interno dos bosses: o que boss.c (a parte comum) e cada tipo
// (boss_breaker.c, boss_hive.c...) compartilham. Ninguém fora deles inclui.
#ifndef GAME_BOSS_INTERNAL_H
#define GAME_BOSS_INTERNAL_H

#include "game/boss.h"

#define BOSS_DYING_TIME 3.6f    // segundos de explosões antes de sumir

// Uma direção qualquer perpendicular a `v` (comprimento 1), sorteada.
Vector3 boss_random_perpendicular(Rng *rng, Vector3 v);

// Gira o boss aos poucos para olhar para `forward`, com o topo para `up`.
void boss_face(Boss *b, Vector3 forward, Vector3 up, float rate, float dt);

// Dano no núcleo com a fração `factor` (1 = aberto). Muda de fase ou começa a
// morte, se for o caso.
float boss_damage_core(Boss *b, const Tuning *t, float amount, float factor, Events *events, uint32_t tick);

// --- cada tipo ------------------------------------------------------------------
void  breaker_spawn(Boss *b, const BossContext *ctx, Vector3 dir);
void  breaker_enter(Boss *b, const BossContext *ctx);
void  breaker_fight(Boss *b, const BossContext *ctx);
int   breaker_parts(const Boss *b, const Tuning *t, BossPart *out);
float breaker_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick);
float breaker_core_factor(const Boss *b, const Tuning *t);

void  hive_spawn(Boss *b, const BossContext *ctx, Vector3 dir);
void  hive_enter(Boss *b, const BossContext *ctx);
void  hive_fight(Boss *b, const BossContext *ctx);
int   hive_parts(const Boss *b, const Tuning *t, BossPart *out);
float hive_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick);
float hive_core_factor(const Boss *b, const Tuning *t);

void  worm_spawn(Boss *b, const BossContext *ctx, Vector3 dir);
void  worm_enter(Boss *b, const BossContext *ctx);
void  worm_fight(Boss *b, const BossContext *ctx);
int   worm_parts(const Boss *b, const Tuning *t, BossPart *out);
float worm_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick);
float worm_core_factor(const Boss *b, const Tuning *t);
void  worm_new_phase(Boss *b, const Tuning *t);

#endif
