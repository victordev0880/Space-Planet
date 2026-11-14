// Desenho do Devorador: o corpo escuro com veias acesas, duas mandíbulas com
// dentes (abrem para morder e mastigar), os olhos (acesos na mira: o ponto
// fraco), a garganta em brasa, o redemoinho girando em volta da boca e o
// círculo vermelho no chão onde ele vai morder.
#ifndef RENDER_DEVOURER_RENDER_H
#define RENDER_DEVOURER_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

void devourer_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);
void devourer_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
