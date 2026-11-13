// Lê teclado, mouse e controle reais (via raylib) e traduz para ações do jogo
// e para a navegação dos menus. É o único lugar que conhece teclas e botões
// físicos; trocar um controle é mudar as tabelas de platform_input.c.
//
// Controle (layout Xbox; o raylib usa os mesmos nomes para outros):
//   analógico esquerdo  mover (W/A/S/D)        analógico direito  mirar
//   RT  atirar          LT  raio do núcleo     LB / RB  rolar
//   L3  boost (aperta e fica até soltar o "frente")
//   A   dash            B   drift              X  raio trator     Y  chamar horda
//   direcional esq. / cima / dir.  compra torre / escudo / satélite, ou escolhe a carta
//   Start  pausa
#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

#include <stdbool.h>

#include "core/input.h"
#include "core/settings.h"
#include "game/tuning.h"
#include "ui/menus.h"

// Chamar uma vez por quadro, antes das outras: percebe se o jogador está no
// controle ou no teclado (as dicas na tela mudam conforme).
void platform_input_update(void);
bool platform_using_gamepad(void);

// use_mouse_look: false zera o movimento do mouse (cursor solto, ou quadro
// logo após capturar o cursor, quando o raylib reporta um salto). As
// preferências dão a sensibilidade e a inversão do eixo vertical; o tuning,
// a zona morta e a curva do analógico.
InputSample platform_poll_input(bool use_mouse_look, const Settings *settings, const Tuning *t);

// Navegação dos menus (setas, Enter, Esc, mouse e controle).
MenuInput platform_menu_input(void);

// Esc ou Start: abre a pausa durante a partida.
bool platform_pause_pressed(void);

// Na tela de fim, pelo controle: 1 = A (jogar de novo), 2 = B (menu), 0 = nada.
int platform_end_choice(void);

#endif
