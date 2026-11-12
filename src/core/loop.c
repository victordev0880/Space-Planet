#include "core/loop.h"

#include <math.h>

void fixed_loop_init(FixedLoop *l, int hz, int max_steps)
{
    if (hz < 1) hz = 1;
    if (max_steps < 1) max_steps = 1;
    l->dt = 1.0 / (double)hz;
    l->accumulator = 0.0;
    l->max_steps = max_steps;
}

int fixed_loop_advance(FixedLoop *l, double frame_seconds)
{
    if (frame_seconds > 0.0) l->accumulator += frame_seconds;

    int steps = 0;
    while (l->accumulator >= l->dt && steps < l->max_steps) {
        l->accumulator -= l->dt;
        steps++;
    }

    // Bateu no teto e ainda sobrou mais de um passo: joga fora os passos
    // inteiros atrasados e mantém só a fração, para o alpha continuar válido.
    if (l->accumulator >= l->dt) l->accumulator = fmod(l->accumulator, l->dt);

    return steps;
}

double fixed_loop_alpha(const FixedLoop *l)
{
    return l->accumulator / l->dt;
}
