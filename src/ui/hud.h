// HUD: o que o jogador vê por cima da cena durante o jogo.
// Por enquanto, a vida do planeta: uma barra com a massa restante, a marca do
// ponto de colapso, a barra do núcleo quando ele é atingido e o aviso de
// colapso planetário.
#ifndef UI_HUD_H
#define UI_HUD_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

// center_x: em que coluna da tela centralizar as barras (o chamador desloca
// para o lado quando o overlay de debug está aberto).
// show_crosshair: desenha uma mira no centro da tela (câmera livre).
void hud_draw(const World *w, const Tuning *t, int center_x, bool show_crosshair);

#endif
