// Fonte do jogo: Rajdhani (assets/fonts, licença OFL), carregada em alguns
// tamanhos para o texto sair nítido do pequeno ao grande.
//
// ui_draw_text e ui_measure_text têm a mesma assinatura de DrawText e
// MeasureText do raylib, e o mesmo sentido para `size`: as letras maiúsculas
// ocupam o mesmo espaço que ocupariam na fonte padrão, então o HUD não precisa
// ser redesenhado. Sem os arquivos da fonte, tudo cai na fonte padrão.
#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdbool.h>

#include "raylib.h"

// Chamar depois de InitWindow. Retorna false se a fonte não foi encontrada
// (o jogo segue com a fonte padrão).
bool ui_font_load(void);
void ui_font_unload(void);

void ui_draw_text(const char *text, int x, int y, int size, Color color);
int  ui_measure_text(const char *text, int size);

// Igual, com uma sombra deslocada `shadow` pixels (0 = sem sombra).
void ui_draw_text_shadow(const char *text, int x, int y, int size, Color color, int shadow);

#endif
