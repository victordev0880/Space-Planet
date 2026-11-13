// Desenho dos inimigos, dos projéteis e das marcas de alvo das bombas.
//
// Linguagem visual: o jogador é azul e branco; os inimigos, violeta e laranja.
//   Enxame       carapaça violeta escura, lâminas em forma de pinça e um
//                olho verde que brilha;
//   Bombardeiro  uma "arraia" grená e bronze, com aberturas laranja e o
//                ponto fraco (o motor, atrás) pulsando em magenta;
//   tiros        da nave: riscos azuis; dos inimigos: bolas violeta;
//   bombas       esferas vermelhas que piscam cada vez mais rápido. O ponto
//                onde cada bomba vai cair aparece no solo como um anel
//                vermelho, que se fecha enquanto o Bombardeiro arma.
#ifndef RENDER_ENEMIES_RENDER_H
#define RENDER_ENEMIES_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Chamar dentro de BeginMode3D, depois do planeta.
void enemies_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

// Tiros, bombas e marcas no solo (somam luz: chamar depois do que é opaco).
void projectiles_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
