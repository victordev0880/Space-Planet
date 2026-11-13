// Radar: o planeta inteiro aberto num disco, em volta da nave.
//
// O centro do disco é o chão embaixo da nave; a borda é o ponto do outro lado
// do planeta. A distância ao centro é o quanto se teria de voar em volta do
// planeta para chegar lá, e a direção é relativa à nave (a frente dela aponta
// para cima). Assim, uma horda atacando do outro lado aparece perto da borda,
// e o jogador decide se vai defender ou continua caçando.
//
// Mostra inimigos (os que ferem o planeta piscam), o boss, bombas caindo,
// rochas, cicatrizes abertas, defesas e um anel a cada impacto no planeta.
#ifndef UI_RADAR_H
#define UI_RADAR_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

void radar_draw(const World *w, const Tuning *t, Vector2 center, float radius);

#endif
