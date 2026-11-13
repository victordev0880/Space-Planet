// Pontuação: pontos por abate, multiplicador de combo e recorde.
//
// Combo: cada abate feito a menos de `score.combo_window` segundos do
// anterior aumenta o multiplicador em `score.combo_step`, até
// `score.combo_max`. Passado esse tempo sem abater nada, ele volta a 1.
#ifndef GAME_SCORE_H
#define GAME_SCORE_H

#include <stdbool.h>
#include <stdint.h>

#include "game/enemy_types.h"
#include "game/tuning.h"

typedef struct {
    int64_t points;
    int     chain;          // abates seguidos (0 = nenhum em andamento)
    int     best_chain;
    float   chain_left;     // segundos até o combo acabar
    float   multiplier;     // multiplicador que o PRÓXIMO abate vai receber
    int     kills;
    int     kills_by_type[ENEMY_TYPE_COUNT];
    int     bombs_down;     // bombas derrubadas no ar
    int64_t planet_bonus;   // soma dos bônus de planeta intacto
} Score;

void score_reset(Score *s);

// Registra um abate que vale `base` pontos e devolve quanto foi somado (já com
// o multiplicador). O multiplicador sobe para o abate seguinte.
int  score_kill(Score *s, const Tuning *t, float base);

// Soma pontos sem mexer no combo (bônus).
void score_bonus(Score *s, int64_t points);

void score_tick(Score *s, float dt);

// Recorde guardado em um arquivo de texto com um número. Ler um arquivo que
// não existe (primeira partida) dá 0.
int64_t score_load_best(const char *path);
bool    score_save_best(const char *path, int64_t best);

#endif
