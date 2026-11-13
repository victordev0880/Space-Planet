#include "tools/autopilot.h"

#include <math.h>

#include "voxel/damage.h"

#define TURN_GAIN       0.35f   // fração do erro de mira corrigida a cada passo
#define TURN_MAX_DEG    6.0f    // giro máximo pedido por passo
#define FIRE_ANGLE_DEG  3.0f    // atira com o alvo até este ângulo do nariz
#define CHASE_FAR       90.0f   // longe assim, acelera
#define CHASE_BOOST     200.0f  // ... e usa o boost
#define CHASE_NEAR      28.0f   // perto assim, freia
#define SAFE_ALTITUDE   9.0f    // abaixo disso, sobe antes de qualquer coisa
#define ARMING_PRIORITY 0.35f   // um Bombardeiro armando parece estar a esta fração da distância real

typedef struct {
    bool    found;
    Vector3 pos;
    Vector3 vel;
    float   score;   // menor = mais urgente
} Target;

static void offer(Target *best, Vector3 from, Vector3 pos, Vector3 vel, float urgency)
{
    float score = Vector3Distance(from, pos) * urgency;
    if (!best->found || score < best->score) {
        best->found = true;
        best->pos = pos;
        best->vel = vel;
        best->score = score;
    }
}

static Target pick_target(const World *w)
{
    const Ship *s = &w->ship;
    Target best = { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f };

    // Bombas caindo vêm antes de tudo.
    const Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        offer(&best, s->pos, p->items[i].pos, p->items[i].vel, 0.1f);
    }
    if (best.found) return best;

    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        float urgency = (en->type == ENEMY_BOMBER && en->arming > 0.0f) ? ARMING_PRIORITY : 1.0f;
        offer(&best, s->pos, en->pos, en->vel, urgency);
    }
    return best;
}

void autopilot_drive(const World *w, const Tuning *t, InputSample *sample)
{
    const uint32_t flight = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BACK) | ACT_BIT(ACT_BOOST) | ACT_BIT(ACT_FIRE);
    sample->held &= ~flight;
    sample->look_dx = 0.0f;
    sample->look_dy = 0.0f;

    const Ship *s = &w->ship;
    if (!s->alive || !w->ship_active) return;

    Vector3 forward = ship_forward(s), right = ship_right(s), up = ship_up(s);
    Vector3 radial = Vector3Normalize(s->pos);
    Target target = pick_target(w);

    Vector3 aim;
    float distance = 0.0f;
    if (target.found) {
        distance = Vector3Distance(s->pos, target.pos);
        Vector3 relative = Vector3Subtract(target.vel, Vector3Add(s->vel, s->fall));
        aim = Vector3Add(target.pos, Vector3Scale(relative, distance / t->weapon_speed));

        // Alvo do outro lado do planeta: contorna por cima em vez de bater.
        Vector3 to = Vector3Subtract(aim, s->pos);
        Vector3 hit;
        if (planet_raycast(&w->planet, s->pos, to, Vector3Length(to), &hit)) {
            Vector3 around = Vector3Subtract(to, Vector3Scale(radial, Vector3DotProduct(to, radial)));
            aim = Vector3Add(s->pos, Vector3Add(Vector3Normalize(around), Vector3Scale(radial, 0.4f)));
        }
    } else {
        // Sem alvo: voa nivelado, na direção em que já ia.
        Vector3 level = Vector3Subtract(forward, Vector3Scale(radial, Vector3DotProduct(forward, radial)));
        aim = Vector3Add(s->pos, Vector3Scale(Vector3Normalize(level), 50.0f));
    }

    Vector3 dir = Vector3Normalize(Vector3Subtract(aim, s->pos));
    if (s->altitude < SAFE_ALTITUDE && Vector3DotProduct(dir, radial) < 0.3f) {
        dir = Vector3Normalize(Vector3Add(dir, Vector3Scale(radial, 0.8f)));
    }

    // Erro de mira nos eixos da nave, convertido em "pixels de mouse".
    float ahead = Vector3DotProduct(dir, forward);
    float yaw = atan2f(Vector3DotProduct(dir, right), ahead) * RAD2DEG;
    float pitch = atan2f(Vector3DotProduct(dir, up), ahead) * RAD2DEG;
    float turn_yaw = Clamp(yaw * TURN_GAIN, -TURN_MAX_DEG, TURN_MAX_DEG);
    float turn_pitch = Clamp(pitch * TURN_GAIN, -TURN_MAX_DEG, TURN_MAX_DEG);
    sample->look_dx = turn_yaw / t->ship_mouse_sens;
    sample->look_dy = -turn_pitch / t->ship_mouse_sens;   // mouse para cima levanta o nariz

    if (!target.found) return;
    if (distance > CHASE_FAR) sample->held |= ACT_BIT(ACT_FORWARD);
    if (distance > CHASE_BOOST && s->energy > t->ship_energy_max * 0.5f) sample->held |= ACT_BIT(ACT_BOOST);
    if (distance < CHASE_NEAR) sample->held |= ACT_BIT(ACT_BACK);
    if (ahead > cosf(FIRE_ANGLE_DEG * DEG2RAD) && distance < t->weapon_range * 0.9f) {
        sample->held |= ACT_BIT(ACT_FIRE);
    }
}
