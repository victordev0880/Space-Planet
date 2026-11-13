// Cartas de evolução: a tela de escolha que abre a cada nível.
//
// Três cartas lado a lado, cada uma com a cor da linha (o tipo de inimigo de
// onde a evolução vem), o nome, o nível que ela vai ter e o que faz. Embaixo,
// as essências do jogador, que explicam por que essas cartas apareceram.
// Escolhe-se com 1, 2, 3 ou com um clique.
#ifndef UI_CARDS_H
#define UI_CARDS_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Desenha as cartas, se estiverem abertas. `time` é o relógio real (para a
// animação de entrada, que roda com a partida congelada).
void cards_draw(const World *w, const Tuning *t, double time);

// A carta sob o ponto da tela `p` (0 a 2), ou -1.
int cards_hit(const World *w, Vector2 p);

// Texto curto do que a evolução faz no nível `rank` (1 = o primeiro).
const char *cards_describe(const Tuning *t, EvolutionId id, int rank);

#endif
