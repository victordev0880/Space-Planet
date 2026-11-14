// A Lua: uma esfera sólida em órbita circular ao redor do planeta.
//
// Ela não gira sobre si mesma, só percorre a órbita. A nave colide com ela e,
// passando perto em velocidade, ganha o "estilingue" (ver game/ship.h).
#ifndef GAME_MOON_H
#define GAME_MOON_H

#include "core/vmath.h"
#include "game/tuning.h"

typedef struct {
    float   angle;      // posição na órbita, em radianos
    float   radius;     // raio da Lua, em unidades do mundo
    float   scale;      // distância ao planeta, em frações da órbita normal (a Colmeia Lunar empurra)
    Vector3 pos;
    Vector3 pos_prev;   // um passo antes: para interpolar o desenho e saber a velocidade
} Moon;

// Põe a Lua no ponto de partida da órbita.
void moon_init(Moon *m, const Tuning *t, float planet_radius);

// Avança a Lua um passo na órbita.
void moon_tick(Moon *m, const Tuning *t, float planet_radius, float dt);

// Ponto da órbita no ângulo `angle` (radianos). A órbita é um círculo em um
// plano inclinado em relação ao equador do planeta.
Vector3 moon_orbit_point(const Tuning *t, float planet_radius, float angle);

#endif
