// Lê teclado e mouse reais (via raylib) e traduz para ações do jogo.
// É o único lugar que conhece teclas físicas; trocar um controle é mudar a
// tabela de platform_input.c.
#ifndef PLATFORM_INPUT_H
#define PLATFORM_INPUT_H

#include <stdbool.h>

#include "core/input.h"

// use_mouse_look: false zera o movimento do mouse (cursor solto, ou quadro
// logo após capturar o cursor, quando o raylib reporta um salto).
InputSample platform_poll_input(bool use_mouse_look);

#endif
