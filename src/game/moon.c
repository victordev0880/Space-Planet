#include "game/moon.h"

#include <math.h>

Vector3 moon_orbit_point(const Tuning *t, float planet_radius, float angle)
{
    float distance = t->moon_orbit_radius * planet_radius;
    float tilt = t->moon_orbit_tilt * DEG2RAD;
    // Um círculo no plano XZ, tombado em torno do eixo X.
    float across = distance * sinf(angle);
    float along = distance * cosf(angle);
    return (Vector3){ across, -along * sinf(tilt), along * cosf(tilt) };
}

void moon_init(Moon *m, const Tuning *t, float planet_radius)
{
    m->angle = t->moon_start_angle * DEG2RAD;
    m->radius = t->moon_radius * planet_radius;
    m->pos = moon_orbit_point(t, planet_radius, m->angle);
    m->pos_prev = m->pos;
}

void moon_tick(Moon *m, const Tuning *t, float planet_radius, float dt)
{
    m->pos_prev = m->pos;
    m->angle += 2.0f * PI * dt / t->moon_orbit_period;
    if (m->angle > 2.0f * PI) m->angle -= 2.0f * PI;   // mantém o ângulo pequeno, para não perder precisão

    // Raio e órbita são relidos a cada passo: assim o F5 vale na hora.
    m->radius = t->moon_radius * planet_radius;
    m->pos = moon_orbit_point(t, planet_radius, m->angle);
}
