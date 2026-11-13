// Desenho do boss: o casco, os capacitores (acesos na carga, apagados quando
// destruídos), o núcleo com a blindagem em pétalas (que abre no
// atordoamento), o traço do corte no chão durante a carga e o laser.
// Também o raio do núcleo Rompe-Mundos, quando a nave o dispara.
#ifndef RENDER_BOSS_RENDER_H
#define RENDER_BOSS_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// Dentro de BeginMode3D, depois do planeta.
void boss_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

// O traço do corte, o laser e o raio da nave: brilhos, por cima de tudo.
void boss_effects_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera);

#endif
