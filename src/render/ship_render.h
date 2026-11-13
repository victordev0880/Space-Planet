// Desenho da nave do jogador e dos aliados dela.
//
// A nave não vem de um arquivo de modelo: é montada em código, peça por peça
// (fuselagem, cabine, asas, motores, derivas), na primeira vez que é
// desenhada. A cada quadro os triângulos são girados para a orientação da
// nave, iluminados na CPU (cada face recebe uma cor única, o que dá o aspecto
// facetado) e enviados direto para desenho. Não usa shader próprio: fica
// igual com e sem --no-shaders.
//
// A evolução muda a nave: cada evolução escolhida acrescenta uma peça (com um
// detalhe aceso na cor da linha de onde ela veio) e o brilho dos motores puxa
// para a cor da linha dominante.
#ifndef RENDER_SHIP_RENDER_H
#define RENDER_SHIP_RENDER_H

#include "raylib.h"

#include "game/allies.h"
#include "game/evolution.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

// Chamar dentro de BeginMode3D, depois do planeta.
// alpha: fração entre o passo de lógica anterior e o atual (0 a 1).
// time: tempo de jogo em segundos (para o pisca da luz da cauda).
// evo pode ser NULL (nave sem evolução).
void ship_draw(const Ship *s, const Evolution *evo, const Planet *planet, const Tuning *t, float alpha, float time,
               Texture2D glow, Camera3D camera);

// Os drones de escolta e os caças aliados.
void allies_draw(const Allies *a, const Planet *planet, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
