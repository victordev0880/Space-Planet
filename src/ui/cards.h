// Cartas de evolução: a tela de escolha que abre a cada nível.
//
// Três cartas lado a lado, cada uma com a cor da linha (o tipo de inimigo de
// onde a evolução vem; ouro nas sinergias), um ícone, o nome e uma linha só
// do que ela dá. Escolhe-se com 1, 2, 3 ou com um clique.
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

// Uma linha curta do que a evolução dá ao subir para o nível `rank`.
const char *cards_effect(const Tuning *t, EvolutionId id, int rank);

#endif
