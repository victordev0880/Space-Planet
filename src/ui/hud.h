// HUD: o que o jogador vê por cima da cena durante o jogo.
//   - o planeta: barra de massa restante, marca do ponto de colapso e barra do
//     núcleo quando ele é atingido;
//   - a nave: casco, escudo e energia, velocidade e altitude, avisos (rasante,
//     estilingue), a mira, a trava da ajuda de mira e o marcador de acerto;
//   - o combate: pontos e combo, a horda atual, avisos de horda, marcadores
//     de inimigos (setas na borda da tela para os que estão fora dela),
//     pontos ganhos subindo de cada abate e o aviso de dano nas bordas;
//   - o fim da partida: vitória ou colapso, com o resumo e o recorde.
//
// O HUD não guarda estado: tudo sai do mundo (inclusive a idade dos eventos,
// que diz quanto tempo um aviso ainda fica na tela).
#ifndef UI_HUD_H
#define UI_HUD_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

typedef struct {
    int      center_x;     // coluna em que centralizar as barras do planeta (o
                           // chamador desloca quando o overlay de debug está aberto)
    Camera3D camera;       // a câmera do quadro, para projetar coisas do mundo na tela
    int64_t  best_score;   // recorde salvo
    bool     new_best;     // a partida que acabou bateu o recorde
} HudContext;

void hud_draw(const World *w, const Tuning *t, const HudContext *ctx);

#endif
