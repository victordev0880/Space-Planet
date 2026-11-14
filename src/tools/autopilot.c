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
#define WORM_WATCH_PRIORITY 0.3f    // Verme: o ponto de onde ele vai sair (no aviso)
#define WORM_SHADOW_PRIORITY 3.0f   // ... e a cabeça dele debaixo da terra (quando não há nada melhor)
#define TUNNEL_HOVER    22.0f   // altura sobre a boca do túnel de onde atira para baixo
#define TUNNEL_ARRIVE   8.0f    // chegou ao ponto sobre a boca...
#define TUNNEL_ALIGNED  2.0f    // ... ou já está alinhado com o túnel
#define TUNNEL_BRAKE    25.0f   // a esta distância do ponto sobre a boca, para de acelerar e freia
#define TUNNEL_CREEP    3.0f    // velocidade de aproximação final (e de frenagem sobre a boca)
#define PART_VANTAGE    32.0f   // peça do boss: atira desta distância, de frente para ela
#define PART_CLEAR_COS  0.55f   // ... a peça está à vista dentro deste cone em volta da normal dela
#define PART_BRAKE      22.0f   // indo ao ponto de tiro: a esta distância dele, freia
#define PART_CREEP      8.0f    // ... e chega nesta velocidade
#define ROCK_METEOR_PRIORITY 0.6f   // um meteoro parece estar a esta fração da distância real
#define SCAR_SIZE_PRIORITY   3.0f   // cada unidade de raio da cicatriz conta como esta distância a menos
#define DROP_HEIGHT     16.0f   // leva a rocha com a nave a esta altura sobre a superfície original
#define DROP_MIN_OFFSET 4.0f    // solta com a rocha a até isto (ou meio raio da cicatriz) do centro dela

// Aproximações especiais: indo ao ponto de onde se atira, ou já atirando.
typedef enum { TUNNEL_NONE, TUNNEL_GOING, TUNNEL_FIRING } TunnelMode;

typedef struct {
    bool    found;
    Vector3 pos;
    Vector3 vel;
    float   score;   // menor = mais urgente
    int     enemy;   // índice do inimigo (-1 = uma bomba ou o boss)
    bool    part;    // uma peça do boss: só se acerta de frente para ela
    Vector3 normal;  // ... a direção para onde a peça está voltada
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
        best->part = false;
    }
}

static void offer_part(Target *best, Vector3 from, Vector3 pos, Vector3 vel, float urgency, Vector3 normal)
{
    bool was = best->found;
    float score = best->score;
    offer(best, from, pos, vel, urgency, -1);
    if (!was || best->score < score) {
        best->part = true;
        best->normal = normal;
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
    Target best = { false, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, -1, false, { 0.0f, 1.0f, 0.0f } };

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

    // O boss: cada peça diz a própria urgência (na carga do Rompe-Mundos, os
    // capacitores antes de tudo; aberto, o núcleo...) e de que lado ela se
    // mostra.
    const Boss *b = &w->boss;
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_parts(b, t, parts);
    for (int k = 0; k < count; k++) {
        if (parts[k].aim <= 0.0f) continue;
        offer_part(&best, s->pos, parts[k].center, b->vel, parts[k].aim, parts[k].facing);
    }

    // Verme debaixo da terra: segue o tremor como um jogador. No aviso da
    // saída, corre para cima do ponto; no resto do tempo, sem nada melhor a
    // fazer, acompanha a cabeça dele por cima. Assim está perto quando ele salta.
    if (b->type == BOSS_WORM && boss_vulnerable(b) && b->action != WORM_BREACH) {
        float radius = w->planet.params.radius;
        bool warning = (b->action == WORM_RUMBLE);
        Vector3 dir = warning ? b->worm.breach_dir : Vector3Normalize(b->pos);
        offer_part(&best, s->pos, Vector3Scale(dir, radius), (Vector3){ 0.0f, 0.0f, 0.0f },
                   warning ? WORM_WATCH_PRIORITY : WORM_SHADOW_PRIORITY, dir);
    }
    return best;
}

// Peça do boss: o casco tapa a peça de quase todos os lados. A nave vai até um
// ponto de tiro de frente para ela e só atira de lá. Do lado errado do boss,
// passa antes por um ponto ao lado, para não atravessar o casco.
static TunnelMode part_approach(const Ship *s, const Target *target, Vector3 *aim, float *distance)
{
    if (!target->part) return TUNNEL_NONE;
    Vector3 from_part = Vector3Subtract(s->pos, target->pos);
    float d = Vector3Length(from_part);
    float facing = Vector3DotProduct(from_part, target->normal) / fmaxf(d, 1e-3f);
    if (facing > PART_CLEAR_COS && d < PART_VANTAGE * 2.0f) {
        *aim = target->pos;
        *distance = d;
        return TUNNEL_FIRING;
    }
    Vector3 vantage = Vector3Add(target->pos, Vector3Scale(target->normal, PART_VANTAGE));
    if (facing < 0.0f) {
        Vector3 side = Vector3Subtract(from_part, Vector3Scale(target->normal, Vector3DotProduct(from_part, target->normal)));
        if (Vector3LengthSqr(side) < 1e-4f) side = Vector3Perpendicular(target->normal);
        vantage = Vector3Add(target->pos, Vector3Add(Vector3Scale(Vector3Normalize(side), PART_VANTAGE * 1.2f),
                                                     Vector3Scale(target->normal, PART_VANTAGE * 0.3f)));
    }
    *aim = vantage;
    *distance = Vector3Distance(s->pos, vantage);
    return TUNNEL_GOING;
}

// Escavador dentro do túnel: o tiro só o alcança descendo pelo túnel. Em vez
// de mirar nele (a rocha está no caminho), a nave vai até um ponto acima da
// boca e, de lá, aponta reto para baixo.
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

// Rocha a buscar (sem inimigo para caçar): a mais perto, com preferência
// pelos meteoros, que vão embora. -1 se não há nenhuma livre.
static int pick_rock(const World *w)
{
    const Ship *s = &w->ship;
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive || k->state == ROCK_TOWED || k->state == ROCK_FALLING || k->state == ROCK_EJECT) continue;
        if (k->kind == ROCK_SPORE) continue;   // esporo não conserta nem dá minério
        float score = Vector3Distance(s->pos, k->pos) * ((k->kind == ROCK_METEOR) ? ROCK_METEOR_PRIORITY : 1.0f);
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

// Com uma rocha a reboque: escolhe a cicatriz (grande e perto) e o ponto para
// onde voar, um pouco antes dela no sentido da chegada, para a rocha (que vai
// pendurada à frente) ficar em cima. Devolve true quando é hora de soltar.
static bool plan_delivery(const World *w, const Tuning *t, const Rock *rock, Vector3 *aim, float *distance)
{
    const Ship *s = &w->ship;
    int best = -1;
    float best_score = 0.0f;
    Vector3 best_point = s->pos;
    for (int i = 0; i < REPAIR_SCARS; i++) {
        const Scar *scar = &w->repair.scars[i];
        if (!scar->used) continue;
        Vector3 point = repair_scar_point(&w->planet, scar);
        float score = Vector3Distance(s->pos, point) - scar->radius * SCAR_SIZE_PRIORITY;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
            best_point = point;
        }
    }
    if (best < 0) {   // nada a consertar: solta onde está
        *aim = Vector3Add(s->pos, Vector3Scale(ship_forward(s), 50.0f));
        *distance = 50.0f;
        return true;
    }
    const Scar *scar = &w->repair.scars[best];
    Vector3 to = Vector3Subtract(best_point, s->pos);
    Vector3 flat = Vector3Subtract(to, Vector3Scale(scar->dir, Vector3DotProduct(to, scar->dir)));
    Vector3 approach = (Vector3Length(flat) > 1.0f) ? Vector3Normalize(flat) : ship_forward(s);
    *aim = Vector3Add(best_point, Vector3Subtract(Vector3Scale(scar->dir, DROP_HEIGHT),
                                                  Vector3Scale(approach, t->tractor_distance + rock->radius)));
    *distance = Vector3Distance(s->pos, *aim);

    // A rocha já está em cima da cicatriz?
    float over = Vector3Length(best_point) * acosf(Clamp(Vector3DotProduct(Vector3Normalize(rock->pos), scar->dir), -1.0f, 1.0f));
    return over < fmaxf(scar->radius * 0.5f, DROP_MIN_OFFSET);
}

void autopilot_drive(const World *w, const Tuning *t, InputSample *sample)
{
    const uint32_t flight = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BACK) | ACT_BIT(ACT_BOOST) | ACT_BIT(ACT_FIRE)
                          | ACT_BIT(ACT_ALT_FIRE) | ACT_BIT(ACT_TRACTOR) | ACT_BIT(ACT_BUY1) | ACT_BIT(ACT_BUY2)
                          | ACT_BIT(ACT_BUY3);
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

    // Rochas: com uma a reboque, leva até uma cicatriz; sem inimigo para
    // caçar, busca uma (havendo cratera aberta) ou a quebra para minerar.
    const Rock *towed = rocks_towed(&w->rocks);
    bool delivering = (towed != NULL), fetching = false, drop = false, mining = false;

    // Sem inimigo à vista, compra o que der (nos passos ímpares, para cada
    // compra ser um aperto novo): satélite, torre, escudo.
    if (!target.found && towed == NULL && (w->tick % 2u) == 1u) {
        static const DefenseType ORDER[DEF_TYPE_COUNT] = { DEF_SATELLITE, DEF_TURRET, DEF_SHIELD };
        for (int k = 0; k < DEF_TYPE_COUNT; k++) {
            DefenseType type = ORDER[k];
            if (defenses_count(&w->defenses, type) < defense_max(t, type) && w->ore >= defense_cost(t, type)) {
                sample->held |= ACT_BIT(ACT_BUY1 + (int)type);
                break;
            }
        }
    }
    Vector3 aim = s->pos;
    float distance = 0.0f;
    bool any_scar = false;
    for (int i = 0; i < REPAIR_SCARS; i++) any_scar = any_scar || w->repair.scars[i].used;
    if (delivering) {
        drop = plan_delivery(w, t, towed, &aim, &distance);
    } else if (!target.found && any_scar) {
        int rock = pick_rock(w);
        if (rock >= 0) {
            const Rock *k = &w->rocks.items[rock];
            target = (Target){ true, k->pos, k->vel, 0.0f, -1, false, { 0.0f, 1.0f, 0.0f } };
            fetching = true;
        }
    } else if (!target.found) {
        int rock = pick_rock(w);
        if (rock >= 0) {
            const Rock *k = &w->rocks.items[rock];
            target = (Target){ true, k->pos, k->vel, 0.0f, -1, false, { 0.0f, 1.0f, 0.0f } };
            mining = true;
        }
    }

    bool blocked = false;
    TunnelMode tunnel = (target.found && !delivering) ? tunnel_approach(w, &target, &aim, &distance) : TUNNEL_NONE;
    bool in_tunnel = (tunnel != TUNNEL_NONE);
    bool to_vantage = !delivering && !in_tunnel && target.found
                   && part_approach(s, &target, &aim, &distance) == TUNNEL_GOING;
    if (delivering || in_tunnel || to_vantage) {
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

    // O F só conta quando é apertado de novo: segurá-lo de um passo para o
    // outro (pegar num, soltar no seguinte) não faria nada. Por isso o piloto
    // só o aperta nos passos pares.
    bool tractor_tick = (w->tick % 2u) == 0u;
    if (delivering) {
        // Como indo a um ponto de tiro: acelera de longe, chega devagar; com a
        // rocha em cima da cicatriz, solta (F).
        float along = forward_speed(s);
        if (distance > PART_BRAKE || along < PART_CREEP) sample->held |= ACT_BIT(ACT_FORWARD);
        else if (along > PART_CREEP * 2.0f) sample->held |= ACT_BIT(ACT_BACK);
        if (drop && tractor_tick) sample->held |= ACT_BIT(ACT_TRACTOR);
        return;
    }
    if (!target.found) return;
    if (fetching) {
        // Buscando uma rocha: chega perto e, com ela ao alcance do raio, puxa.
        if (blocked || s->altitude < SAFE_ALTITUDE || distance > CHASE_FAR) sample->held |= ACT_BIT(ACT_FORWARD);
        if (distance > CHASE_BOOST && s->energy > t->ship_energy_max * 0.5f) sample->held |= ACT_BIT(ACT_BOOST);
        if (distance < CHASE_NEAR) sample->held |= ACT_BIT(ACT_BACK);
        if (tractor_tick && rocks_find_grab(&w->rocks, s, t) >= 0) sample->held |= ACT_BIT(ACT_TRACTOR);
        return;
    }
    if (to_vantage) {
        // Indo ao ponto de tiro da peça do boss: acelera de longe, chega devagar.
        float along = forward_speed(s);
        if (distance > PART_BRAKE || along < PART_CREEP) sample->held |= ACT_BIT(ACT_FORWARD);
        else if (along > PART_CREEP * 2.0f) sample->held |= ACT_BIT(ACT_BACK);
        return;
    }
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
        // Com o núcleo Rompe-Mundos pronto, segura o botão direito também (numa
        // rocha não: o raio não a quebra).
        if (!mining && w->ability.owned && w->ability.cooldown <= 0.0f && distance < t->ability_range * 0.8f) {
            sample->held |= ACT_BIT(ACT_ALT_FIRE);
        }
    }
}
