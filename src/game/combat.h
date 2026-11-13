// Combate: a arma da nave, quem acerta quem, dano, mortes e pontos.
//
// Os módulos de inimigos e de projéteis só se movem; aqui fica o que acontece
// quando as coisas se encontram. Ordem dentro de um passo (ver world_tick):
//   combat_fire     a nave atira, se o botão está apertado;
//   ... inimigos pensam e os projéteis andam ...
//   combat_resolve  confere o trecho que cada projétil percorreu e as
//                   trombadas de inimigos com a nave;
//   combat_sweep    trata os inimigos que morreram neste passo (explosão,
//                   pontos) e os tira do jogo.
//
// Decisão de projeto: os tiros da nave NÃO cavam o planeta. Só os inimigos o
// destroem; se fosse diferente, quem mais estragaria o planeta seria o
// próprio jogador.
#ifndef GAME_COMBAT_H
#define GAME_COMBAT_H

#include <stdbool.h>

#include "core/input.h"
#include "game/tuning.h"
#include "game/world.h"

// Os dois canhões miram de modo a se cruzarem a esta distância à frente do
// nariz, onde o HUD desenha a mira.
#define COMBAT_AIM_DISTANCE 150.0f

// Alvo que a ajuda de mira escolheu. `enemy` e `bomb` são índices (no máximo
// um deles diferente de -1); `point` é onde mirar para acertar quem se move.
typedef struct {
    int     enemy;
    int     bomb;
    Vector3 point;
} AimAssist;

void combat_fire(World *w, const InputState *in, const Tuning *t, float dt);
void combat_resolve(World *w, const Tuning *t);
void combat_sweep(World *w, const Tuning *t);

// Alvo da ajuda de mira, se houver algum dentro do cone à frente do nariz.
bool combat_aim_assist(const World *w, const Tuning *t, AimAssist *out);

// Dano na nave do jogador, com toda a reação (aviso, tremor, congelamento,
// destruição). `point` é onde o golpe acertou.
void combat_damage_ship(World *w, const Tuning *t, float amount, Vector3 point);

// Efeitos da nave destruída (chamado também quando ela morre batendo no chão).
void combat_ship_wreck(World *w);

// Fração [0, 1] do trecho a->b em que ele entra na esfera, ou -1 se não toca.
float segment_sphere(Vector3 a, Vector3 b, Vector3 center, float radius);

#endif
