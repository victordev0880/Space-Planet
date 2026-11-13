// Desenho da Lua.
//
// A Lua é uma esfera com o relevo embutido: crateras sorteadas (com a borda
// levantada), "mares" mais escuros e um grão fino de ruído. Como ela não gira
// sobre si mesma e o Sol não se mexe, a luz que cada ponto recebe nunca muda;
// por isso a iluminação é calculada uma única vez, ao criar a malha, e fica
// guardada na cor dos vértices. Não precisa de shader: fica igual com e sem
// --no-shaders.
#ifndef RENDER_MOON_RENDER_H
#define RENDER_MOON_RENDER_H

#include "raylib.h"

#include "game/moon.h"
#include "game/tuning.h"

typedef struct {
    Mesh     mesh;
    Material material;
    bool     has_mesh;
} MoonRender;

// Cria a malha (ou a recria, depois de um F5). Leva alguns milissegundos.
void moon_render_build(MoonRender *m, const Tuning *t);

// Chamar dentro de BeginMode3D, junto com o que é opaco.
// alpha: fração entre o passo de lógica anterior e o atual (0 a 1).
void moon_render_draw(const MoonRender *m, const Moon *moon, const Tuning *t, float planet_radius, float alpha);

void moon_render_unload(MoonRender *m);

#endif
