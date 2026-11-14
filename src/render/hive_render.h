// Desenho da Colmeia Lunar: costelas vivas abraçando a Lua, o coração (um
// olho com pálpebras que abrem quando as bolsas acabam), as bolsas (incham e
// piscam antes de soltar drones), os propulsores atrás da Lua e o tentáculo
// que arranca a crosta quando a Lua chega perto.
#ifndef RENDER_HIVE_RENDER_H
#define RENDER_HIVE_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Dentro de BeginMode3D, depois do planeta e da Lua.
void hive_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

// Brilhos: bolsas, coração, chamas dos propulsores e o tentáculo.
void hive_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
