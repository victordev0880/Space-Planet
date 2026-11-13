// Lê teclado, mouse e controle reais (via raylib) e traduz para ações do jogo
// e para a navegação dos menus. É o único lugar que conhece teclas e botões
// físicos; trocar um controle é mudar as tabelas de platform_input.c.
#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

#include <stdbool.h>

#include "core/input.h"
#include "core/settings.h"
#include "ui/menus.h"

// use_mouse_look: false zera o movimento do mouse (cursor solto, ou quadro
// logo após capturar o cursor, quando o raylib reporta um salto). As
// preferências dão a sensibilidade e a inversão do eixo vertical.
InputSample platform_poll_input(bool use_mouse_look, const Settings *settings);

// Navegação dos menus (setas, Enter, Esc, mouse e controle).
MenuInput platform_menu_input(void);

// Esc ou Start: abre a pausa durante a partida.
bool platform_pause_pressed(void);

#endif
