// Peças compartilhadas pela IA dos inimigos. Uso interno de game/enemies.c e
// dos arquivos game/ai_*.c (um por tipo); o resto do jogo usa game/enemies.h.
#ifndef GAME_ENEMY_AI_H
#define GAME_ENEMY_AI_H

#include <stdbool.h>

#include "game/enemies.h"

#define GOLDEN_ANGLE 2.39996f   // espalha pontos em espiral sem que se alinhem

// O que a IA de um tipo decide em um passo.
typedef struct {
    Vector3 desired;        // velocidade desejada
    float   max_speed;
    bool    avoid_ground;   // desviar do terreno (falso para quem quer bater ou cavar)
    bool    solid;          // encostar na matéria destrói (falso para quem está cavando)
    bool    scripted;       // a IA já moveu o inimigo neste passo: só falta orientar o modelo
} Steer;

// Velocidade desejada com as regras normais (desvia do terreno, bate e morre).
Steer ai_steer(Vector3 desired, float max_speed);

// --- comportamento de cada tipo (um arquivo cada) ---------------------------
Steer swarm_think(Enemies *e, int index, const EnemyContext *ctx);
Steer bomber_think(Enemies *e, int index, const EnemyContext *ctx);
Steer carrier_think(Enemies *e, int index, const EnemyContext *ctx);
Steer kamikaze_think(Enemies *e, int index, const EnemyContext *ctx);
Steer digger_think(Enemies *e, int index, const EnemyContext *ctx);
Steer siphon_think(Enemies *e, int index, const EnemyContext *ctx);
Steer sniper_think(Enemies *e, int index, const EnemyContext *ctx);
Steer shielder_think(Enemies *e, int index, const EnemyContext *ctx);

// Alvo de um drone dentro do bando (game/ai_swarm.c).
Vector3 swarm_drone_target(const Flock *f, int slot);

// --- ajudantes --------------------------------------------------------------
Vector3 ai_perpendicular(Vector3 v);                     // uma direção qualquer perpendicular a v
Vector3 ai_random_perpendicular(Rng *rng, Vector3 v);    // ... sorteada
Vector3 ai_level_heading(const Enemy *en, Vector3 radial);   // rumo atual, sem a parte vertical

// Onde mirar para acertar quem se move: onde ele vai estar quando o tiro chegar.
Vector3 ai_lead(Vector3 from, Vector3 target, Vector3 target_vel, float shot_speed);

// Ponto do modelo (em eixos do modelo, vezes `scale`) no mundo.
Vector3 ai_model_point(const Enemy *en, Vector3 local, float scale);

bool    ai_ship_targetable(const EnemyContext *ctx);
Vector3 ai_ship_velocity(const Ship *s);

// Tiro de plasma de `from` para `aim`. crater_radius 0 = não cava o planeta.
void ai_fire(const EnemyContext *ctx, const Enemy *en, Vector3 from, Vector3 aim, float speed, float damage,
             float crater_radius, float crater_depth);

// Velocidade para dar voltas no planeta na altura `altitude`, em torno do
// eixo en->axis. `error` recebe o quanto falta para chegar na altura.
Vector3 ai_orbit(const Enemy *en, const EnemyContext *ctx, float altitude, float speed, float *error);

// Pede ao mundo para retirar uma esfera do planeta.
void ai_carve(Enemies *e, Vector3 center, float radius, EnemyType type);

// Põe inimigos em cena com o espalhamento pedido (o Porta-drones lança perto de si).
int ai_spawn(Enemies *e, const EnemyContext *ctx, EnemyType type, int count, Vector3 center, float spread);

#endif
