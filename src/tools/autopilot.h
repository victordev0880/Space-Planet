// Piloto automático do autoplay.
//
// Um roteiro de teclas não consegue acertar inimigos que se movem. Para o
// autoplay exercitar o combate de verdade (e as capturas mostrarem lutas), o
// comando "autopilot on" passa o controle a este piloto: ele escolhe o alvo
// mais urgente (bombas caindo, Bombardeiros armando, depois o inimigo mais
// perto), vira a nave com movimentos de mouse, acelera, freia e atira, sempre
// passando pela mesma camada de input que o jogador usa.
//
// É uma ferramenta de teste, não um jogador bom: só decide a partir do estado
// do mundo, então o resultado é determinístico.
#ifndef TOOLS_AUTOPILOT_H
#define TOOLS_AUTOPILOT_H

#include "core/input.h"
#include "game/tuning.h"
#include "game/world.h"

// Substitui, em `sample`, o mouse e as teclas de voo e de tiro.
void autopilot_drive(const World *w, const Tuning *t, InputSample *sample);

#endif
