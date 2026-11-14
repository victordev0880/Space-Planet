// Desenho do Verme do Núcleo: segmentos blindados com rachaduras de magma, a
// cabeça com quatro mandíbulas (abertas no salto), os nódulos acesos no
// dorso, o ferrão da cauda e, antes de cada saída, a rachadura vermelha no
// chão onde ele vai irromper.
#ifndef RENDER_WORM_RENDER_H
#define RENDER_WORM_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

void worm_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);
void worm_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
