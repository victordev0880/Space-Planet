// Desenho das rochas (fragmentos e meteoros), do raio trator e dos consertos.
//
// As rochas são esferas facetadas e deformadas, montadas em código (quatro
// desenhos). O fragmento é um pedaço do planeta: rocha da crosta com o "topo"
// da cor do lugar de onde saiu (grama, areia, gelo). O meteoro é basalto
// escuro com rachaduras em brasa e uma cauda de fogo. O raio trator é um feixe
// verde-água da nave até a rocha, com anéis correndo por ele.
#ifndef RENDER_ROCKS_RENDER_H
#define RENDER_ROCKS_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Rochas sólidas: depois do planeta e antes dos efeitos translúcidos.
void rocks_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

// Brilhos (caudas, raio trator, consertos): junto dos outros efeitos aditivos.
void rocks_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
