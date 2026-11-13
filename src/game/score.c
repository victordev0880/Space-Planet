#include "game/score.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void score_reset(Score *s)
{
    memset(s, 0, sizeof *s);
    s->multiplier = 1.0f;
}

int score_kill(Score *s, const Tuning *t, float base)
{
    int gained = (int)lroundf(base * s->multiplier);
    s->points += gained;
    s->kills++;

    s->chain++;
    if (s->chain > s->best_chain) s->best_chain = s->chain;
    s->chain_left = t->score_combo_window;
    s->multiplier = fminf(1.0f + t->score_combo_step * (float)s->chain, t->score_combo_max);
    return gained;
}

void score_bonus(Score *s, int64_t points)
{
    s->points += points;
}

void score_tick(Score *s, float dt)
{
    if (s->chain == 0) return;
    s->chain_left -= dt;
    if (s->chain_left <= 0.0f) {
        s->chain = 0;
        s->chain_left = 0.0f;
        s->multiplier = 1.0f;
    }
}

int64_t score_load_best(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    long long best = 0;
    if (fscanf(f, "%lld", &best) != 1 || best < 0) best = 0;
    fclose(f);
    return (int64_t)best;
}

bool score_save_best(const char *path, int64_t best)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = fprintf(f, "%lld\n", (long long)best) > 0;
    return (fclose(f) == 0) && ok;
}
