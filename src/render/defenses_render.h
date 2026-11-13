// Desenho das defesas planetárias: a torre orbital e o satélite (modelos
// facetados montados em código), a cúpula translúcida do escudo local e o raio
// do satélite derrubando um tiro.
#ifndef RENDER_DEFENSES_RENDER_H
#define RENDER_DEFENSES_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Torres e satélites: junto dos outros modelos sólidos.
void defenses_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

// Cúpulas dos escudos e brilhos: junto dos outros efeitos aditivos.
void defenses_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
