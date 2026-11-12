// Desenho das partículas de impacto.
//
// Cada partícula é um quadradinho com a textura de brilho redondo, sempre de
// frente para a câmera. Pedras e poeira são desenhadas com transparência
// comum (e escurecem no lado noturno do planeta); brasas e clarões somam seu
// brilho ao que já está na tela.
#ifndef RENDER_PARTICLES_RENDER_H
#define RENDER_PARTICLES_RENDER_H

#include "raylib.h"

#include "game/particles.h"
#include "game/tuning.h"

// Chamar dentro de BeginMode3D, depois do planeta.
// alpha: fração entre o passo de lógica anterior e o atual (0 a 1).
void particles_draw(const Particles *ps, Camera3D camera, Texture2D glow, const Tuning *t, float alpha);

#endif
