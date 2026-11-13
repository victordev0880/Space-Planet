#include "tools/autopilot.h"

#include <math.h>

#include "voxel/damage.h"

#define TURN_GAIN       0.35f   // fração do erro de mira corrigida a cada passo
#define TURN_MAX_DEG    6.0f    // giro máximo pedido por passo
#define FIRE_ANGLE_DEG  3.0f    // atira com o alvo até este ângulo do nariz
#define CHASE_FAR       50.0f   // longe assim, acelera (sem acelerar, a nave paira e para)
#define CHASE_BOOST     200.0f  // ... e usa o boost
#define CHASE_NEAR      28.0f   // perto assim, freia
#define SAFE_ALTITUDE   9.0f    // abaixo disso, sobe antes de qualquer coisa
#define ARMING_PRIORITY 0.35f   // um Bombardeiro armando parece estar a esta fração da distância real
#define SHIELDER_PRIORITY 0.5f  // Escudeiro: derrubá-lo abre os outros
#define HARM_PRIORITY   0.5f    // ferindo o planeta agora (broca, feixe)
#define HUNTER_PRIORITY 0.6f    // vindo atrás da nave (Kamikaze travado, Atirador mirando)
#define SHIELDED_PRIORITY 4.0f  // protegido por escudo: quase nunca vale a pena
#define TUNNEL_HOVER    22.0f   // altura sobre a boca do túnel de onde atira para baixo
#define TUNNEL_ARRIVE   8.0f    // chegou ao ponto sobre a boca...
#define TUNNEL_ALIGNED  2.0f    // ... ou já está alinhado com o túnel
#define TUNNEL_BRAKE    25.0f   // a esta distância do ponto sobre a boca, para de acelerar e freia
#define TUNNEL_CREEP    3.0f    // velocidade de aproximação final (e de frenagem sobre a boca)

typedef struct {
    bool    found;
    Vector3 pos;
    Vector3 vel;
    float   score;   // menor = mais urgente
    int     enemy;   // índice do inimigo (-1 = uma bomba)
} Target;

static void offer(Target *best, Vector3 from, Vector3 pos, Vector3 vel, float urgency, int enemy)
{
    float score = Vector3Distance(from, pos) * urgency;
    if (!best->found || score < best->score) {
        best->found = true;
        best->pos = pos;
        best->vel = vel;
        best->score = score;
        best->enemy = enemy;
    }
}

// Quanto mais urgente um inimigo, menor o número (multiplica a distância).
static float urgency(const World *w, const Tuning *t, int index)
{
    const Enemy *en = &w->enemies.items[index];
    // Protegido por um Escudeiro: atirar nele é perder tempo; o Escudeiro primeiro.
    if (enemies_shielded_by(&w->enemies, t, index) >= 0) return SHIELDED_PRIORITY;
    switch ((EnemyType)en->type) {
    case ENEMY_BOMBER:   return (en->arming > 0.0f) ? ARMING_PRIORITY : 1.0f;
    case ENEMY_SHIELDER: return SHIELDER_PRIORITY;
    case ENEMY_KAMIKAZE: return (en->state == KAMIKAZE_HUNT) ? HUNTER_PRIORITY : 1.0f;
    case ENEMY_SIPHON:   return (en->state == SIPHON_DRAIN) ? HARM_PRIORITY : 1.0f;
    case ENEMY_DIGGER:   return (en->state == DIGGER_DRILL) ? HARM_PRIORITY : 1.0f;
    case ENEMY_SNIPER:   return (en->arming > 0.0f) ? HUNTER_PRIORITY : 1.0f;
    default:             return 1.0f;
    }
}

static Target pick_target(const World *w, const Tuning *t)
{
    const Ship *s = &w->ship;
    Target best = { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, -1 };

    // Bombas caindo vêm antes de tudo.
    const Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        offer(&best, s->pos, p->items[i].pos, p->items[i].vel, 0.1f, -1);
    }
    if (best.found) return best;

    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        offer(&best, s->pos, e->items[i].pos, e->items[i].vel, urgency(w, t, i), i);
    }
    return best;
}

// Escavador dentro do túnel: o tiro só o alcança descendo pelo túnel. Em vez
// de mirar nele (a rocha está no caminho), a nave vai até um ponto acima da
// boca e, de lá, aponta reto para baixo.
typedef enum { TUNNEL_NONE, TUNNEL_GOING, TUNNEL_FIRING } TunnelMode;

static TunnelMode tunnel_approach(const World *w, const Target *target, Vector3 *aim, float *distance)
{
    if (target->enemy < 0) return TUNNEL_NONE;
    const Enemy *en = &w->enemies.items[target->enemy];
    if (en->type != ENEMY_DIGGER || en->state != DIGGER_DRILL) return TUNNEL_NONE;

    const Ship *s = &w->ship;
    Vector3 above = Vector3Scale(en->target, w->planet.params.radius + TUNNEL_HOVER);
    Vector3 on_axis = Vector3Scale(en->target, Vector3DotProduct(s->pos, en->target));
    float off_axis = Vector3Distance(s->pos, on_axis);
    float to_above = Vector3Distance(s->pos, above);
    if (to_above > TUNNEL_ARRIVE && off_axis > TUNNEL_ALIGNED) {
        *aim = above;
        *distance = to_above;
        return TUNNEL_GOING;
    }
    *aim = en->pos;
    *distance = Vector3Distance(s->pos, en->pos);
    return TUNNEL_FIRING;
}

// Velocidade da nave ao longo do nariz (negativa = andando de ré).
static float forward_speed(const Ship *s)
{
    return Vector3DotProduct(Vector3Add(s->vel, s->fall), ship_forward(s));
}

void autopilot_drive(const World *w, const Tuning *t, InputSample *sample)
{
    const uint32_t flight = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BACK) | ACT_BIT(ACT_BOOST) | ACT_BIT(ACT_FIRE);
    sample->held &= ~flight;
    sample->look_dx = 0.0f;
    sample->look_dy = 0.0f;

    // Cartas de evolução: fica com a primeira. A tecla é apertada só quando as
    // cartas já estão prontas (depois de um passo sem nenhuma tecla de carta).
    const uint32_t cards = ACT_BIT(ACT_CARD1) | ACT_BIT(ACT_CARD2) | ACT_BIT(ACT_CARD3);
    sample->held &= ~cards;
    if (w->evo.choosing) {
        if (w->evo.armed) sample->held |= ACT_BIT(ACT_CARD1);
        return;
    }

    const Ship *s = &w->ship;
    if (!s->alive || !w->ship_active) return;

    Vector3 forward = ship_forward(s), right = ship_right(s), up = ship_up(s);
    Vector3 radial = Vector3Normalize(s->pos);
    Target target = pick_target(w, t);

    Vector3 aim;
    float distance = 0.0f;
    bool blocked = false;
    TunnelMode tunnel = target.found ? tunnel_approach(w, &target, &aim, &distance) : TUNNEL_NONE;
    bool in_tunnel = (tunnel != TUNNEL_NONE);
    if (in_tunnel) {
        // (a mira e a distância já vêm prontas)
    } else if (target.found) {
        distance = Vector3Distance(s->pos, target.pos);
        Vector3 relative = Vector3Subtract(target.vel, Vector3Add(s->vel, s->fall));
        aim = Vector3Add(target.pos, Vector3Scale(relative, distance / t->weapon_speed));

        // Alvo do outro lado do planeta: contorna por cima em vez de bater.
        Vector3 to = Vector3Subtract(aim, s->pos);
        Vector3 hit;
        if (planet_raycast(&w->planet, s->pos, to, Vector3Length(to), &hit)) {
            Vector3 around = Vector3Subtract(to, Vector3Scale(radial, Vector3DotProduct(to, radial)));
            aim = Vector3Add(s->pos, Vector3Add(Vector3Normalize(around), Vector3Scale(radial, 0.4f)));
            blocked = true;
        }
    } else {
        // Sem alvo: voa nivelado, na direção em que já ia.
        Vector3 level = Vector3Subtract(forward, Vector3Scale(radial, Vector3DotProduct(forward, radial)));
        aim = Vector3Add(s->pos, Vector3Scale(Vector3Normalize(level), 50.0f));
    }

    Vector3 dir = Vector3Normalize(Vector3Subtract(aim, s->pos));
    if (!in_tunnel && s->altitude < SAFE_ALTITUDE && Vector3DotProduct(dir, radial) < 0.3f) {
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
    if (in_tunnel) {
        // Indo: acelera de longe e freia ao chegar. Atirando: nenhuma tecla de
        // movimento, para os estabilizadores segurarem a nave sobre a boca
        // (segurar S também conta como mexer, e aí a gravidade a puxaria túnel
        // abaixo); só freia se ainda vier rápido demais para eles.
        float along = forward_speed(s);
        if (tunnel == TUNNEL_GOING) {
            // Perto do ponto, anda devagar (sem teclas a nave pararia antes de chegar).
            if (distance > TUNNEL_BRAKE || along < TUNNEL_CREEP) sample->held |= ACT_BIT(ACT_FORWARD);
            else if (along > TUNNEL_CREEP * 2.5f) sample->held |= ACT_BIT(ACT_BACK);
        } else if (along > t->ship_hover_speed * 0.8f) {
            sample->held |= ACT_BIT(ACT_BACK);
        }
        if (tunnel == TUNNEL_FIRING && ahead > cosf(FIRE_ANGLE_DEG * DEG2RAD)) sample->held |= ACT_BIT(ACT_FIRE);
        return;
    }
    // Com o terreno no caminho ou rente ao chão, acelera para contornar e subir
    // (parada, a nave pairaria no fundo de uma cratera atirando na parede).
    if (blocked || s->altitude < SAFE_ALTITUDE) {
        sample->held |= ACT_BIT(ACT_FORWARD);
        return;
    }
    if (distance > CHASE_FAR) sample->held |= ACT_BIT(ACT_FORWARD);
    if (distance > CHASE_BOOST && s->energy > t->ship_energy_max * 0.5f) sample->held |= ACT_BIT(ACT_BOOST);
    if (distance < CHASE_NEAR) sample->held |= ACT_BIT(ACT_BACK);
    if (ahead > cosf(FIRE_ANGLE_DEG * DEG2RAD) && distance < t->weapon_range * 0.9f) {
        sample->held |= ACT_BIT(ACT_FIRE);
    }
}
