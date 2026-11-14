// Verme do Núcleo: nada dentro do planeta e salta para fora.
//
//   BURROW  debaixo da terra, rumo ao ponto de saída (na fase 2 passa antes
//           pelo núcleo e o morde). A cabeça cava um túnel por onde a nave
//           pode segui-lo; o chão treme em cima dele.
//   RUMBLE  sob a saída: o chão racha em vermelho (o aviso);
//   BREACH  salta num arco pelo espaço, de boca aberta (o núcleo dele leva o
//           dano inteiro) e com os nódulos do dorso à mostra; mergulha de
//           volta mais adiante;
//   SWIM    fase 3: nada rente à superfície, com o dorso para fora, abrindo um
//           sulco, e salta de tempos em tempos.
// O túnel desmorona depois de um tempo (se a nave não estiver nele). Acertar
// o ferrão da cauda, dentro do túnel, o faz sair na hora, atordoado: um salto
// lento e baixo. Cada nódulo destruído tira uma fatia da vida.
#include "game/boss.h"

#include <math.h>
#include <string.h>

#include "game/boss_internal.h"

#define TRAIL_STEP     1.0f    // um ponto do rastro a cada tanto de caminho
#define ENTER_DIVE     8.0f    // a entrada termina com a cabeça este tanto abaixo do chão
#define UNDER_EXIT     9.0f    // espera sob a saída a esta profundidade
#define ARRIVE         5.0f    // chegou a um ponto (distância)
#define CLOSE_TURN     20.0f   // perto do ponto, vira mais rápido (para não ficar rodando em volta)
#define RUMBLE_SPEED   3.0f
#define BURROW_TIMEOUT 14.0f   // tempo máximo de um mergulho: depois disso sai onde estiver
#define ARC_SINK       8.0f    // o arco do salto começa e termina este tanto abaixo do chão
#define PANIC_RUMBLE   0.7f
#define PANIC_HEIGHT   0.6f    // o salto atordoado é mais baixo
#define CORE_MARGIN    2.5f    // morde o núcleo a este tanto da borda dele
#define DUST_STEP      5.0f    // um tremor no chão a cada tanto de caminho debaixo da terra
#define SWIM_RUMBLE    0.9f
#define NODULE_SIZE    0.42f
#define TAIL_SIZE      0.75f
#define EXPOSED_DEPTH  1.5f    // a cabeça está "debaixo da terra" a partir desta profundidade

const int WORM_NODULE_SEGMENT[WORM_NODULES] = { 3, 6, 9, 12 };

#define ID_TAIL  WORM_NODULES
#define ID_MOUTH (WORM_NODULES + 1)
#define ID_HULL  (WORM_NODULES + 2)

// --- geometria ---------------------------------------------------------------------

float worm_segment_radius(const Tuning *t, int k)
{
    if (k == 0) return t->worm_scale;
    float u = (float)k / (float)(WORM_SEGMENTS - 1);
    return t->worm_scale * Lerp(0.9f, 0.45f, u);
}

Vector3 worm_segment_up(const Boss *b, int k)
{
    return Vector3Normalize(b->worm.seg[k]);
}

Vector3 worm_segment_forward(const Boss *b, int k)
{
    if (k == 0) return b->worm.heading;
    Vector3 d = Vector3Subtract(b->worm.seg[k - 1], b->worm.seg[k]);
    float l = Vector3Length(d);
    return (l > 1e-4f) ? Vector3Scale(d, 1.0f / l) : b->worm.heading;
}

Vector3 worm_nodule_point(const Boss *b, const Tuning *t, int n)
{
    int k = WORM_NODULE_SEGMENT[n];
    return Vector3Add(b->worm.seg[k], Vector3Scale(worm_segment_up(b, k), worm_segment_radius(t, k) * 0.8f));
}

int worm_nodules_left(const Boss *b)
{
    int n = 0;
    for (int i = 0; i < WORM_NODULES; i++) n += (b->worm.nodule_hp[i] > 0.0f) ? 1 : 0;
    return n;
}

// Distância do centro à superfície ORIGINAL do planeta na direção `dir`.
static float original_ground(const Planet *p, Vector3 dir)
{
    Terrain t = planet_terrain(&p->params, dir);
    return p->params.radius + (t.land ? t.elevation : 0.0f);
}

static Vector3 below(const Planet *p, Vector3 dir, float depth)
{
    dir = Vector3Normalize(dir);
    return Vector3Scale(dir, original_ground(p, dir) - depth);
}

bool worm_underground(const Boss *b, const Planet *planet)
{
    float r = Vector3Length(b->pos);
    return r < original_ground(planet, b->pos) - EXPOSED_DEPTH;
}

float worm_core_factor(const Boss *b, const Tuning *t)
{
    return (b->action == WORM_BREACH) ? 1.0f : t->worm_mouth_armor;
}

// --- rastro e segmentos -------------------------------------------------------------

// Rastro reto atrás da cabeça (no começo).
static void trail_reset(Boss *b, Vector3 back)
{
    WormState *m = &b->worm;
    m->trail_count = WORM_TRAIL;
    m->trail_head = WORM_TRAIL - 1;
    for (int i = 0; i < WORM_TRAIL; i++) {
        int idx = (m->trail_head - i + WORM_TRAIL) % WORM_TRAIL;
        m->trail[idx] = Vector3Add(b->pos, Vector3Scale(back, TRAIL_STEP * (float)i));
    }
}

static void trail_push(Boss *b)
{
    WormState *m = &b->worm;
    Vector3 last = m->trail[m->trail_head];
    for (int guard = 0; guard < 64 && Vector3Distance(b->pos, last) >= TRAIL_STEP; guard++) {
        last = Vector3Add(last, Vector3Scale(Vector3Normalize(Vector3Subtract(b->pos, last)), TRAIL_STEP));
        m->trail_head = (m->trail_head + 1) % WORM_TRAIL;
        m->trail[m->trail_head] = last;
        if (m->trail_count < WORM_TRAIL) m->trail_count++;
    }
}

// Cada segmento fica a `spacing` do anterior, medido ao longo do rastro.
static void place_segments(Boss *b, const Tuning *t)
{
    WormState *m = &b->worm;
    m->seg[0] = b->pos;
    Vector3 from = b->pos;
    float carried = 0.0f;
    int k = 1, idx = m->trail_head;
    for (int n = 0; n < m->trail_count && k < WORM_SEGMENTS; n++) {
        Vector3 to = m->trail[idx];
        float len = Vector3Distance(from, to);
        while (k < WORM_SEGMENTS && carried + len >= t->worm_spacing) {
            float f = (t->worm_spacing - carried) / fmaxf(len, 1e-6f);
            from = Vector3Lerp(from, to, f);
            len = Vector3Distance(from, to);
            carried = 0.0f;
            m->seg[k++] = from;
        }
        carried += len;
        from = to;
        idx = (idx - 1 + WORM_TRAIL) % WORM_TRAIL;
    }
    for (; k < WORM_SEGMENTS; k++) m->seg[k] = Vector3Subtract(m->seg[k - 1], Vector3Scale(m->heading, t->worm_spacing));
}

// --- movimento ----------------------------------------------------------------------

// Vira a cabeça para o ponto, no máximo `turn` radianos por segundo (perto
// dele, mais rápido) e anda.
static float steer_and_move(Boss *b, Vector3 goal, float speed, float turn, float dt)
{
    WormState *m = &b->worm;
    Vector3 want = Vector3Subtract(goal, b->pos);
    float d = Vector3Length(want);
    if (d > 1e-4f) {
        want = Vector3Scale(want, 1.0f / d);
        float boost = 1.0f + 3.0f * Clamp(1.0f - d / CLOSE_TURN, 0.0f, 1.0f);
        float angle = acosf(Clamp(Vector3DotProduct(m->heading, want), -1.0f, 1.0f));
        float step = turn * boost * dt;
        if (angle <= step) {
            m->heading = want;
        } else {
            Vector3 axis = Vector3CrossProduct(m->heading, want);
            if (Vector3LengthSqr(axis) < 1e-8f) axis = Vector3Perpendicular(m->heading);
            m->heading = Vector3Normalize(Vector3RotateByAxisAngle(m->heading, Vector3Normalize(axis), step));
        }
    }
    float travel = speed * dt;
    b->vel = Vector3Scale(m->heading, speed);
    b->pos = Vector3Add(b->pos, Vector3Scale(m->heading, travel));
    return travel;
}

// Uma direção (do centro) à frente da cabeça, entre `min_deg` e `max_deg`
// graus de planeta, um pouco para um lado ou para o outro.
static Vector3 ahead_dir(const Boss *b, Rng *rng, float min_deg, float max_deg)
{
    Vector3 up = Vector3Normalize(b->pos);
    Vector3 axis = Vector3CrossProduct(up, b->worm.heading);
    if (Vector3LengthSqr(axis) < 1e-6f) axis = boss_random_perpendicular(rng, up);
    axis = Vector3Normalize(Vector3RotateByAxisAngle(Vector3Normalize(axis), up, rng_range(rng, -0.6f, 0.6f)));
    return Vector3Normalize(Vector3RotateByAxisAngle(up, axis, rng_range(rng, min_deg, max_deg) * DEG2RAD));
}

// Cava o túnel enquanto anda debaixo da terra (a mordida e a fila para
// desmoronar depois) e levanta poeira no chão em cima.
static void dig(Boss *b, const BossContext *ctx, float travelled, float radius)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    m->dust_left -= travelled;
    if (m->dust_left <= 0.0f) {
        m->dust_left += DUST_STEP;
        m->dust_now = true;
    }
    m->carve_left -= travelled;
    if (m->carve_left > 0.0f) return;
    m->carve_left += t->worm_tunnel_spacing;
    if (b->carve_count < BOSS_CARVES) b->carves[b->carve_count++] = (CarveRequest){ b->pos, radius, ENEMY_TYPE_COUNT };
    // O que ele cava perto do núcleo não volta: a mordida é para valer.
    float core = ctx->planet->params.radius * ctx->planet->params.core_radius;
    if (Vector3Length(b->pos) < core + radius + 2.0f) return;
    if (m->tunnel_count >= WORM_TUNNEL) {   // fila cheia: o trecho mais velho desmorona já
        m->tunnel_first = (m->tunnel_first + 1) % WORM_TUNNEL;
        m->tunnel_count--;
    }
    int at = (m->tunnel_first + m->tunnel_count) % WORM_TUNNEL;
    m->tunnel[at] = b->pos;
    m->tunnel_age[at] = 0.0f;
    m->tunnel_count++;
}

// Trechos velhos do túnel desmoronam, a não ser perto da nave.
static void collapse(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    for (int i = 0; i < m->tunnel_count; i++) m->tunnel_age[(m->tunnel_first + i) % WORM_TUNNEL] += ctx->dt;
    while (m->tunnel_count > 0 && m->restore_count < WORM_RESTORES) {
        int i = m->tunnel_first;
        if (m->tunnel_age[i] < t->worm_tunnel_life) break;
        if (ctx->ship != NULL && Vector3Distance(ctx->ship->pos, m->tunnel[i]) < t->worm_tunnel_guard) break;
        m->restores[m->restore_count++] = m->tunnel[i];
        m->tunnel_first = (m->tunnel_first + 1) % WORM_TUNNEL;
        m->tunnel_count--;
    }
}

// --- as ações ---------------------------------------------------------------------

static void start_burrow(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    b->action = WORM_BURROW;
    b->timer = BURROW_TIMEOUT;
    m->stage = 0;
    m->to_core = b->phase >= 2;
    m->bit_core = false;
    m->breach_dir = ahead_dir(b, ctx->rng, 50.0f, 95.0f);
    Vector3 mid = Vector3Normalize(Vector3Add(Vector3Normalize(b->pos), m->breach_dir));
    if (m->to_core) {
        float core = ctx->planet->params.radius * ctx->planet->params.core_radius;
        m->goal = Vector3Scale(mid, core + CORE_MARGIN);
    } else {
        m->goal = below(ctx->planet, mid, t->worm_depth);
    }
}

static void start_swim(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    b->action = WORM_SWIM;
    b->timer = t->worm_swim_time;
    m->goal = below(ctx->planet, ahead_dir(b, ctx->rng, 25.0f, 40.0f), t->worm_swim_depth);
}

static void start_rumble(Boss *b, const BossContext *ctx, float time)
{
    WormState *m = &b->worm;
    b->action = WORM_RUMBLE;
    b->timer = time;
    m->goal = below(ctx->planet, m->breach_dir, UNDER_EXIT * 0.5f);
    Vector3 ground = below(ctx->planet, m->breach_dir, 0.0f);
    events_push(ctx->events, ctx->tick, EV_WORM_RUMBLE, b->type, ground, time);
}

static void start_breach(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    Vector3 out = below(ctx->planet, m->breach_dir, 0.0f);
    b->action = WORM_BREACH;
    m->arc_time = 0.0f;
    m->arc_total = m->panic ? t->worm_panic_time : t->worm_breach_time;
    float reach = m->panic ? t->worm_breach_reach * 0.6f : t->worm_breach_reach;
    m->land_dir = ahead_dir(b, ctx->rng, reach * 0.9f, reach * 1.1f);
    if (m->crater_count < 2) m->crater_at[m->crater_count++] = out;
    if (ctx->ship != NULL && Vector3Distance(ctx->ship->pos, out) < t->worm_breach_blast) m->ship_damage += t->worm_breach_damage;
    // Da fase 2 em diante, cospe larvas (Kamikazes) ao sair.
    if (b->phase >= 2 && t->worm_larvae > 0) {
        b->launch_count = t->worm_larvae;
        b->launch_type = ENEMY_KAMIKAZE;
        b->launch_from = Vector3Add(out, Vector3Scale(Vector3Normalize(out), 8.0f));
    }
    events_push(ctx->events, ctx->tick, EV_WORM_BREACH, b->type, out, m->arc_total);
}

static void breach_tick(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    m->arc_time += ctx->dt;
    float u = Clamp(m->arc_time / fmaxf(m->arc_total, 1e-3f), 0.0f, 1.0f);
    Vector3 dir = Vector3Normalize(Vector3Lerp(m->breach_dir, m->land_dir, u));
    float ground = Lerp(original_ground(ctx->planet, m->breach_dir), original_ground(ctx->planet, m->land_dir), u);
    float height = m->panic ? t->worm_breach_height * PANIC_HEIGHT : t->worm_breach_height;
    float h = -ARC_SINK + (height + ARC_SINK) * 4.0f * u * (1.0f - u);
    Vector3 next = Vector3Scale(dir, ground + h);
    Vector3 step = Vector3Subtract(next, b->pos);
    float len = Vector3Length(step);
    if (len > 1e-4f) m->heading = Vector3Scale(step, 1.0f / len);
    b->vel = Vector3Scale(step, 1.0f / ctx->dt);
    b->pos = next;
    if (u < 1.0f) return;

    // Mergulhou de volta.
    Vector3 land = below(ctx->planet, m->land_dir, 0.0f);
    if (m->crater_count < 2) m->crater_at[m->crater_count++] = land;
    events_push(ctx->events, ctx->tick, EV_WORM_DIVE, b->type, land, 0.0f);
    m->panic = false;
    if (b->phase >= 3) start_swim(b, ctx);
    else               start_burrow(b, ctx);
}

// Os nódulos e o ferrão só levam dano fora da rocha (no espaço, ou no túnel).
static void update_exposure(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    for (int n = 0; n < WORM_NODULES; n++) {
        m->nodule_open[n] = m->nodule_hp[n] > 0.0f && planet_sample(ctx->planet, worm_nodule_point(b, t, n)) < VOXEL_ISO;
    }
    int last = WORM_SEGMENTS - 1;
    bool hiding = b->action == WORM_BURROW || b->action == WORM_SWIM || b->action == WORM_RUMBLE;
    m->tail_open = hiding && planet_sample(ctx->planet, m->seg[last]) < VOXEL_ISO;
}

void worm_spawn(Boss *b, const BossContext *ctx, Vector3 dir)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    b->action = WORM_BURROW;
    b->scale = t->worm_scale;
    b->hp = b->hp_max = t->worm_hp;
    b->enter_from = Vector3Scale(dir, ctx->planet->params.radius * t->worm_spawn_distance);
    b->enter_to = below(ctx->planet, dir, ENTER_DIVE);
    b->pos = b->pos_prev = b->enter_from;
    m->heading = Vector3Negate(dir);
    b->rot = quat_look(m->heading, boss_random_perpendicular(ctx->rng, dir));
    b->rot_prev = b->rot;
    b->timer = t->worm_enter_time;
    trail_reset(b, dir);
    place_segments(b, t);
    memcpy(m->seg_prev, m->seg, sizeof m->seg);
    for (int n = 0; n < WORM_NODULES; n++) m->nodule_hp[n] = t->worm_nodule_hp;
    m->tail_hp = t->worm_tail_hp;
    m->carve_left = 0.0f;
    m->dust_left = DUST_STEP;
    events_push(ctx->events, ctx->tick, EV_BOSS_ENTER, (uint8_t)b->type, below(ctx->planet, dir, 0.0f), t->worm_enter_time);
}

void worm_enter(Boss *b, const BossContext *ctx)
{
    // Vem do espaço acelerando e mergulha no planeta, abrindo a cratera de entrada.
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    b->timer -= ctx->dt;
    float u = 1.0f - Clamp(b->timer / t->worm_enter_time, 0.0f, 1.0f);
    Vector3 before = b->pos;
    b->pos = Vector3Lerp(b->enter_from, b->enter_to, u * u);
    b->vel = Vector3Scale(Vector3Subtract(b->pos, before), 1.0f / ctx->dt);
    m->heading = Vector3Normalize(Vector3Subtract(b->enter_to, b->enter_from));
    bool inside = worm_underground(b, ctx->planet);
    if (inside && m->stage == 0) {
        m->stage = 1;   // furou a superfície agora
        Vector3 hole = below(ctx->planet, b->enter_to, 0.0f);
        if (m->crater_count < 2) m->crater_at[m->crater_count++] = hole;
        events_push(ctx->events, ctx->tick, EV_WORM_DIVE, b->type, hole, 0.0f);
    }
    if (inside) dig(b, ctx, Vector3Distance(before, b->pos), t->worm_tunnel_radius);
    trail_push(b);
    place_segments(b, t);
    if (b->timer <= 0.0f) {
        b->state = BOSS_FIGHT;
        start_burrow(b, ctx);
    }
}

void worm_fight(Boss *b, const BossContext *ctx)
{
    const Tuning *t = ctx->t;
    WormState *m = &b->worm;
    float dt = ctx->dt;

    // O ferrão foi acertado no túnel: sai já, atordoado.
    if (m->panic_now) {
        m->panic_now = false;
        if (b->action == WORM_BURROW || b->action == WORM_SWIM) {
            m->panic = true;
            m->breach_dir = Vector3Normalize(b->pos);
            start_rumble(b, ctx, PANIC_RUMBLE);
            events_push(ctx->events, ctx->tick, EV_BOSS_STAGGER, b->type, below(ctx->planet, m->breach_dir, 0.0f),
                        t->worm_panic_time);
        }
    }

    float travelled;
    switch (b->action) {
    case WORM_BURROW:
        travelled = steer_and_move(b, m->goal, t->worm_speed, t->worm_turn, dt);
        dig(b, ctx, travelled, t->worm_tunnel_radius);
        b->timer -= dt;
        if (Vector3Distance(b->pos, m->goal) < ARRIVE) {
            if (m->stage == 0) {
                if (m->to_core && !m->bit_core) {
                    // A mordida no núcleo.
                    m->bit_core = true;
                    if (b->carve_count < BOSS_CARVES) {
                        b->carves[b->carve_count++] = (CarveRequest){ b->pos, t->worm_core_bite, ENEMY_TYPE_COUNT };
                    }
                    events_push(ctx->events, ctx->tick, EV_WORM_CORE, b->type, b->pos, t->worm_core_bite);
                }
                m->stage = 1;
                m->goal = below(ctx->planet, m->breach_dir, UNDER_EXIT);
            } else {
                start_rumble(b, ctx, t->worm_rumble_time);
            }
        } else if (b->timer <= 0.0f) {
            m->breach_dir = Vector3Normalize(b->pos);   // demorou demais: sai onde está
            start_rumble(b, ctx, t->worm_rumble_time);
        }
        break;
    case WORM_SWIM:
        travelled = steer_and_move(b, m->goal, t->worm_swim_speed, t->worm_turn, dt);
        dig(b, ctx, travelled, t->worm_furrow_radius);
        b->timer -= dt;
        if (Vector3Distance(b->pos, m->goal) < ARRIVE * 1.5f) {
            m->goal = below(ctx->planet, ahead_dir(b, ctx->rng, 25.0f, 40.0f), t->worm_swim_depth);
        }
        if (b->timer <= 0.0f) {
            m->breach_dir = ahead_dir(b, ctx->rng, 8.0f, 14.0f);
            start_rumble(b, ctx, SWIM_RUMBLE);
        }
        break;
    case WORM_RUMBLE:
        travelled = steer_and_move(b, m->goal, RUMBLE_SPEED, t->worm_turn * 2.0f, dt);
        dig(b, ctx, travelled, t->worm_tunnel_radius);
        b->timer -= dt;
        if (b->timer <= 0.0f) start_breach(b, ctx);
        break;
    default:   // WORM_BREACH
        breach_tick(b, ctx);
        break;
    }

    trail_push(b);
    place_segments(b, t);
    collapse(b, ctx);
    update_exposure(b, ctx);
}

// --- peças e dano ---------------------------------------------------------------------

int worm_parts(const Boss *b, const Tuning *t, BossPart *out)
{
    const WormState *m = &b->worm;
    bool vulnerable = boss_vulnerable(b);
    int n = 0;
    for (int i = 0; i < WORM_NODULES; i++) {
        if (m->nodule_hp[i] <= 0.0f) continue;
        int k = WORM_NODULE_SEGMENT[i];
        bool open = vulnerable && m->nodule_open[i];
        out[n++] = (BossPart){ worm_nodule_point(b, t, i), NODULE_SIZE * worm_segment_radius(t, k), PART_TARGET, (uint8_t)i,
                               open, false, open ? 0.25f : 0.0f, worm_segment_up(b, k) };
    }
    int last = WORM_SEGMENTS - 1;
    Vector3 back = Vector3Negate(worm_segment_forward(b, last));
    bool tail = vulnerable && m->tail_open;
    // (O piloto automático não sabe entrar em túneis: o ferrão não é alvo dele.)
    out[n++] = (BossPart){ Vector3Add(m->seg[last], Vector3Scale(back, worm_segment_radius(t, last) * 0.9f)),
                           TAIL_SIZE * worm_segment_radius(t, last), PART_TARGET, ID_TAIL, tail, false, 0.0f, back };
    float factor = boss_core_factor(b, t);
    bool mouth = factor >= 0.99f;
    out[n++] = (BossPart){ Vector3Add(b->pos, Vector3Scale(m->heading, t->worm_scale * 0.85f)), t->worm_scale * 0.65f,
                           PART_CORE, ID_MOUTH, mouth, false, mouth ? 0.12f : 0.0f, m->heading };
    for (int k = 0; k < WORM_SEGMENTS; k++) {
        out[n++] = (BossPart){ m->seg[k], worm_segment_radius(t, k), PART_HULL, (uint8_t)(ID_HULL + k), false, true, 0.0f,
                               { 0.0f, 0.0f, 0.0f } };
    }
    return n;
}

float worm_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick)
{
    WormState *m = &b->worm;
    if (id < WORM_NODULES) {
        if (m->nodule_hp[id] <= 0.0f || !m->nodule_open[id]) return 0.0f;
        float applied = fminf(amount, m->nodule_hp[id]);
        m->nodule_hp[id] -= amount;
        m->nodule_flash[id] = 0.12f;
        if (m->nodule_hp[id] <= 0.0f) {
            m->nodule_hp[id] = 0.0f;
            m->nodule_open[id] = false;
            events_push(events, tick, EV_WORM_NODULE, b->type, worm_nodule_point(b, t, id), (float)worm_nodules_left(b));
            boss_damage_core(b, t, t->worm_nodule_damage * b->hp_max, 1.0f, events, tick);   // arranca uma fatia da vida
        }
        return applied;
    }
    if (id == ID_TAIL) {
        if (!m->tail_open) return 0.0f;
        float applied = fminf(amount, m->tail_hp);
        m->tail_hp -= amount;
        m->tail_flash = 0.12f;
        if (m->tail_hp <= 0.0f) {
            m->tail_hp = t->worm_tail_hp;
            m->panic_now = true;
        }
        return applied;
    }
    if (id == ID_MOUTH) return boss_damage_core(b, t, amount, worm_core_factor(b, t), events, tick);
    return 0.0f;   // carapaça
}

// Ao mudar de fase, os nódulos brotam de novo.
void worm_new_phase(Boss *b, const Tuning *t)
{
    for (int n = 0; n < WORM_NODULES; n++) b->worm.nodule_hp[n] = t->worm_nodule_hp;
}
