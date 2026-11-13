// Desenho da nave do jogador.
//
// A nave não vem de um arquivo de modelo: é montada em código, peça por peça
// (fuselagem, cabine, asas, motores, derivas), na primeira vez que é
// desenhada. São cerca de cem triângulos, então a cada quadro eles são girados
// para a orientação da nave, iluminados na CPU (cada face recebe uma cor
// única, o que dá o aspecto facetado) e enviados direto para desenho.
// Não usa shader próprio: fica igual com e sem --no-shaders.
#ifndef RENDER_SHIP_RENDER_H
#define RENDER_SHIP_RENDER_H

#include "raylib.h"

#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

// Chamar dentro de BeginMode3D, depois do planeta.
// alpha: fração entre o passo de lógica anterior e o atual (0 a 1).
void ship_draw(const Ship *s, const Planet *planet, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
