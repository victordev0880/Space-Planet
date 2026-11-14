#include "game/rocks.h"

#include <math.h>
#include <string.h>

#define SPIN_MIN        0.2f    // giro das rochas, em rad/s
#define SPIN_MAX        1.1f
#define METEOR_SPIN     1.6f
#define DESPAWN_RADII   6.0f    // meteoro que passou e já está a este tanto (em raios) vai embora
#define TOW_DROP        0.5f    // a rocha a reboque desce mais este tanto do raio dela
#define CONTACT_DEPTH   0.6f    // toca o chão quando este tanto do raio dela entra no solo
#define EJECT_LIFT      0.35f   // o fragmento sobe em arco: este tanto da altura extra no meio do caminho
#define ORE_SPIN        3.0f    // giro dos cristais (rad/s, só desenho)
#define ORE_SCATTER     7.0f    // velocidade máxima com que os cristais se espalham da rocha quebrada
#define ORE_SIDE_DRAG   4.0f    // cristal puxado pela nave: o quanto perde da velocidade de lado
#define SPORE_DRAG      0.6f    // esporo boiando: o quanto da velocidade perde por segundo
#define SPORE_FLIGHT    4.0f    // esporo arremessado: some se não acertar nada neste tempo

void rocks_reset(Rocks *r)
{
    memset(r, 0, sizeof *r);
    r->towed = -1;
}

int rocks_alive(const Rocks *r)
{
    int n = 0;
    for (int i = 0; i < ROCKS_MAX; i++) n += r->items[i].alive ? 1 : 0;
    return n;
}

int rocks_count(const Rocks *r, RockKind kind)
{
    int n = 0;
    for (int i = 0; i < ROCKS_MAX; i++) n += (r->items[i].alive && r->items[i].kind == kind) ? 1 : 0;
    return n;
}

static Rock *new_rock(Rocks *r, Rng *rng, RockKind kind, float radius)
{
    for (int i = 0; i < ROCKS_MAX; i++) {
        Rock *k = &r->items[i];
        if (k->alive) continue;
        memset(k, 0, sizeof *k);
        k->alive = true;
        k->kind = (uint8_t)kind;
        k->shape = (uint8_t)rng_below(rng, ROCK_SHAPES);
        k->radius = radius;
        k->rot = QuaternionFromAxisAngle(rng_unit_vec3(rng), rng_range(rng, 0.0f, 2.0f * PI));
        k->rot_prev = k->rot;
        float spin = (kind == ROCK_METEOR) ? METEOR_SPIN : rng_range(rng, SPIN_MIN, SPIN_MAX);
        k->spin = Vector3Scale(rng_unit_vec3(rng), spin);
        return k;
    }
    return NULL;
}

// Uma direção perpendicular a `v` (comprimento 1), sorteada.
static Vector3 random_perpendicular(Rng *rng, Vector3 v)
{
    Vector3 p = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 1.0f, 0.0f, 0.0f });
    return Vector3Normalize(p);
}

Rock *rocks_spawn_fragment(Rocks *r, Rng *rng, const Tuning *t, float planet_radius, Vector3 ground, uint8_t material)
{
    float radius = rng_range(rng, t->fragment_radius_min, t->fragment_radius_max);
    Rock *k = new_rock(r, rng, ROCK_FRAGMENT, radius);
    if (k == NULL) return NULL;
    float mid = 0.5f * (t->fragment_radius_min + t->fragment_radius_max);
    k->fill = t->fragment_fill * radius / fmaxf(mid, 1e-3f);
    k->hp = t->fragment_hp;
    k->material = material;
    k->state = ROCK_EJECT;
    k->pos = k->pos_prev = k->eject_from = ground;

    // Órbita: um círculo em volta do planeta que passa por cima da cratera,
    // a uma altura sorteada, num sentido sorteado.
    Vector3 up = Vector3Normalize(ground);
    float orbit_radius = planet_radius + rng_range(rng, t->fragment_alt_min, t->fragment_alt_max);
    k->axis = random_perpendicular(rng, up);
    k->orbit = Vector3Scale(up, orbit_radius);
    k->orbit_rate = t->fragment_speed / orbit_radius;
    return k;
}

Rock *rocks_spawn_meteor(Rocks *r, Rng *rng, const Tuning *t, Vector3 pos, Vector3 vel)
{
    float radius = rng_range(rng, t->meteor_radius_min, t->meteor_radius_max);
    Rock *k = new_rock(r, rng, ROCK_METEOR, radius);
    if (k == NULL) return NULL;
    float mid = 0.5f * (t->meteor_radius_min + t->meteor_radius_max);
    k->fill = t->meteor_fill * radius / fmaxf(mid, 1e-3f);
    k->hp = t->meteor_hp;
    k->state = ROCK_FLYBY;
    k->pos = k->pos_prev = pos;
    k->vel = vel;
    return k;
}

Rock *rocks_spawn_spore(Rocks *r, Rng *rng, const Tuning *t, Vector3 pos, Vector3 vel)
{
    Rock *k = new_rock(r, rng, ROCK_SPORE, t->hive_spore_size);
    if (k == NULL) return NULL;
    k->hp = 0.5f;   // um tiro basta
    k->state = ROCK_DRIFT;
    k->pos = k->pos_prev = pos;
    k->vel = vel;
    return k;
}

void rocks_throw(Rocks *r, Vector3 vel)
{
    if (r->towed < 0) return;
    Rock *k = &r->items[r->towed];
    k->state = ROCK_THROWN;
    k->vel = vel;
    k->age = 0.0f;
    r->towed = -1;
}

// A rocha encostou no chão (com parte do corpo dentro do solo)?
static bool touches_ground(const Planet *p, const Rock *k)
{
    float distance = Vector3Length(k->pos);
    if (distance < 1e-3f) return true;
    Vector3 below = Vector3Scale(k->pos, (distance - k->radius * CONTACT_DEPTH) / distance);
    return planet_sample(p, below) >= VOXEL_ISO;
}

static void deposit(Rocks *r, int index)
{
    Rock *k = &r->items[index];
    if (r->deposit_count >= ROCK_DEPOSITS) return;   // fila cheia: tenta no próximo passo
    r->deposits[r->deposit_count++] = (RockDeposit){ k->pos, k->fill, k->kind };
    r->delivered++;
    if (r->towed == index) r->towed = -1;
    k->alive = false;
}

static void tick_towed(Rocks *r, int index, const RockContext *ctx)
{
    Rock *k = &r->items[index];
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    if (s == NULL || !s->alive) {
        k->state = ROCK_FALLING;
        r->towed = -1;
        events_push(ctx->events, ctx->tick, EV_TRACTOR_BREAK, k->kind, k->pos, 0.0f);
        return;
    }
    // Mola entre a rocha e um ponto à frente e abaixo da nave (em direção ao
    // planeta), como numa abdução: ali a câmera, que vem atrás, enxerga a
    // rocha sobre o chão, e dá para mirar a cratera antes de soltar. O
    // amortecimento puxa a velocidade da rocha para a da nave (sem ele ela
    // balançaria sem parar).
    Vector3 down = Vector3Normalize(Vector3Negate(s->pos));
    Vector3 anchor = Vector3Add(s->pos, Vector3Scale(ship_forward(s), t->tractor_distance + k->radius));
    anchor = Vector3Add(anchor, Vector3Scale(down, t->tractor_drop + k->radius * TOW_DROP));
    Vector3 ship_vel = Vector3Add(s->vel, s->fall);
    Vector3 pull = Vector3Scale(Vector3Subtract(anchor, k->pos), t->tractor_stiffness);
    Vector3 drag = Vector3Scale(Vector3Subtract(ship_vel, k->vel), t->tractor_damping);
    k->vel = Vector3Add(k->vel, Vector3Scale(Vector3Add(pull, drag), ctx->dt));
    k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, ctx->dt));

    if (Vector3Distance(k->pos, s->pos) > t->tractor_break + k->radius) {
        k->state = ROCK_FALLING;
        r->towed = -1;
        events_push(ctx->events, ctx->tick, EV_TRACTOR_BREAK, k->kind, k->pos, 1.0f);
    }
}

static void tick_falling(Rock *k, const RockContext *ctx)
{
    const Tuning *t = ctx->t;
    float distance = Vector3Length(k->pos);
    if (distance > 1e-3f) {
        Vector3 down = Vector3Scale(k->pos, -1.0f / distance);
        // Cai quase reto: a parte da velocidade de lado vai sumindo.
        Vector3 side = Vector3Subtract(k->vel, Vector3Scale(down, Vector3DotProduct(k->vel, down)));
        k->vel = Vector3Subtract(k->vel, Vector3Scale(side, Clamp(t->rock_fall_drag * ctx->dt, 0.0f, 1.0f)));
        k->vel = Vector3Add(k->vel, Vector3Scale(down, t->rock_fall_accel * ctx->dt));
    }
    k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, ctx->dt));
}

// Cristais: freiam aos poucos; com a nave perto, são puxados até ela e coletados.
static void tick_ore(Rocks *r, const RockContext *ctx)
{
    const Tuning *t = ctx->t;
    const Ship *s = ctx->ship;
    float dt = ctx->dt;
    r->collected = 0;
    for (int i = 0; i < ORE_MAX; i++) {
        Ore *o = &r->ore[i];
        if (!o->alive) continue;
        o->pos_prev = o->pos;
        o->age += dt;
        o->spin += dt * ORE_SPIN;
        if (o->age > t->ore_life) {
            o->alive = false;
            continue;
        }
        bool pulled = false;
        if (s != NULL && s->alive) {
            Vector3 to = Vector3Subtract(s->pos, o->pos);
            float distance = Vector3Length(to);
            if (distance < t->ship_radius + t->ore_collect) {
                o->alive = false;
                r->collected++;
                continue;
            }
            if (distance < t->ore_magnet) {
                // Puxado pela nave: acelera até ela, com a velocidade de lado
                // sumindo, para não ficar orbitando em volta.
                Vector3 dir = Vector3Scale(to, 1.0f / distance);
                Vector3 side = Vector3Subtract(o->vel, Vector3Scale(dir, Vector3DotProduct(o->vel, dir)));
                o->vel = Vector3Subtract(o->vel, Vector3Scale(side, Clamp(ORE_SIDE_DRAG * dt, 0.0f, 1.0f)));
                o->vel = Vector3Add(o->vel, Vector3Scale(dir, t->ore_pull * dt));
                float speed = Vector3Length(o->vel);
                if (speed > t->ore_speed) o->vel = Vector3Scale(o->vel, t->ore_speed / speed);
                pulled = true;
            }
        }
        if (!pulled) o->vel = Vector3Scale(o->vel, 1.0f - Clamp(t->ore_drag * dt, 0.0f, 1.0f));
        o->pos = Vector3Add(o->pos, Vector3Scale(o->vel, dt));
        // Não atravessa o chão.
        if (planet_sample(ctx->planet, o->pos) >= VOXEL_ISO) {
            o->pos = o->pos_prev;
            o->vel = Vector3Scale(Vector3Normalize(o->pos), 2.0f);
        }
    }
}

int rocks_ore_alive(const Rocks *r)
{
    int n = 0;
    for (int i = 0; i < ORE_MAX; i++) n += r->ore[i].alive ? 1 : 0;
    return n;
}

static void spawn_ore(Rocks *r, Rng *rng, Vector3 pos, Vector3 vel, int count, float spread)
{
    for (int i = 0; i < ORE_MAX && count > 0; i++) {
        Ore *o = &r->ore[i];
        if (o->alive) continue;
        memset(o, 0, sizeof *o);
        o->alive = true;
        o->pos = o->pos_prev = Vector3Add(pos, Vector3Scale(rng_unit_vec3(rng), spread * 0.5f));
        o->vel = Vector3Add(vel, Vector3Scale(rng_unit_vec3(rng), rng_range(rng, 2.0f, ORE_SCATTER)));
        o->spin = rng_range(rng, 0.0f, 2.0f * PI);
        count--;
    }
}

bool rocks_damage(Rocks *r, Rng *rng, const Tuning *t, int index, float damage)
{
    if (index < 0 || index >= ROCKS_MAX) return false;
    Rock *k = &r->items[index];
    if (!k->alive) return false;
    k->hp -= damage;
    k->flash = 0.1f;
    if (k->hp > 0.0f) return false;
    int count = (k->kind == ROCK_METEOR) ? t->ore_meteor : t->ore_fragment;
    spawn_ore(r, rng, k->pos, Vector3Scale(k->vel, 0.5f), count, k->radius);
    if (r->towed == index) r->towed = -1;
    k->alive = false;
    return true;
}

void rocks_tick(Rocks *r, const RockContext *ctx)
{
    const Tuning *t = ctx->t;
    float dt = ctx->dt;
    r->deposit_count = 0;
    float despawn = ctx->planet->params.radius * DESPAWN_RADII;
    tick_ore(r, ctx);

    for (int i = 0; i < ROCKS_MAX; i++) {
        Rock *k = &r->items[i];
        if (!k->alive) continue;
        k->pos_prev = k->pos;
        k->rot_prev = k->rot;
        k->age += dt;
        k->flash = fmaxf(k->flash - dt, 0.0f);
        float spin = Vector3Length(k->spin);
        if (spin > 1e-4f) {
            Quaternion step = QuaternionFromAxisAngle(Vector3Scale(k->spin, 1.0f / spin), spin * dt);
            k->rot = QuaternionNormalize(QuaternionMultiply(step, k->rot));
        }

        switch ((RockState)k->state) {
        case ROCK_EJECT:
        case ROCK_ORBIT: {
            k->orbit = Vector3RotateByAxisAngle(k->orbit, k->axis, k->orbit_rate * dt);
            if (k->state == ROCK_EJECT) {
                // Sobe da cratera até a órbita, em arco, freando no fim.
                float u = Clamp(k->age / t->fragment_eject_time, 0.0f, 1.0f);
                float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
                Vector3 path = Vector3Lerp(k->eject_from, k->orbit, ease);
                float lift = sinf(PI * u) * EJECT_LIFT * (Vector3Length(k->orbit) - Vector3Length(k->eject_from));
                k->pos = Vector3Add(path, Vector3Scale(Vector3Normalize(path), lift));
                if (u >= 1.0f) k->state = ROCK_ORBIT;
            } else {
                k->pos = k->orbit;
            }
            k->vel = Vector3Scale(Vector3Subtract(k->pos, k->pos_prev), 1.0f / dt);
            break;
        }
        case ROCK_FLYBY:
            k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, dt));
            if (Vector3Length(k->pos) > despawn && Vector3DotProduct(k->pos, k->vel) > 0.0f) {
                k->alive = false;   // passou e foi embora
                continue;
            }
            break;
        case ROCK_TOWED:
            tick_towed(r, i, ctx);
            break;
        case ROCK_DRIFT:
            // Esporo boiando: vai freando; passado o tempo, murcha.
            k->vel = Vector3Scale(k->vel, 1.0f - Clamp(SPORE_DRAG * dt, 0.0f, 1.0f));
            k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, dt));
            if (k->age > t->hive_spore_life) k->alive = false;
            continue;
        case ROCK_THROWN:
            // Arremessado: reto, até explodir em alguma coisa (o mundo confere) ou sumir.
            k->pos = Vector3Add(k->pos, Vector3Scale(k->vel, dt));
            if (k->age > SPORE_FLIGHT) k->alive = false;
            continue;
        default:   // ROCK_FALLING
            tick_falling(k, ctx);
            break;
        }

        // Só a rocha puxada ou solta toca o chão: as outras voam alto. (O
        // esporo não conserta nada: o mundo o explode antes.)
        if (k->kind != ROCK_SPORE && (k->state == ROCK_TOWED || k->state == ROCK_FALLING) && touches_ground(ctx->planet, k)) {
            deposit(r, i);
        }
    }
}

int rocks_find_grab(const Rocks *r, const Ship *s, const Tuning *t)
{
    if (s == NULL || !s->alive) return -1;
    Vector3 forward = ship_forward(s);
    float cone = cosf(t->tractor_cone * DEG2RAD);
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &r->items[i];
        if (!k->alive || k->state == ROCK_TOWED || k->state == ROCK_FALLING || k->state == ROCK_THROWN) continue;
        Vector3 to = Vector3Subtract(k->pos, s->pos);
        float distance = Vector3Length(to);
        if (distance - k->radius > t->tractor_range) continue;
        float facing = (distance > 1e-3f) ? Vector3DotProduct(to, forward) / distance : 1.0f;
        if (facing < cone) continue;
        // A mais perto da mira, desempatando pela distância.
        float score = (1.0f - facing) * 400.0f + distance;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

void rocks_grab(Rocks *r, int index)
{
    if (index < 0 || index >= ROCKS_MAX || !r->items[index].alive) return;
    if (r->towed >= 0) rocks_release(r);
    r->items[index].state = ROCK_TOWED;
    r->towed = index;
}

void rocks_release(Rocks *r)
{
    if (r->towed < 0) return;
    r->items[r->towed].state = ROCK_FALLING;
    r->towed = -1;
}

float rocks_tow_load(const Rocks *r, const Tuning *t)
{
    const Rock *k = rocks_towed(r);
    if (k == NULL) return 0.0f;
    float biggest = fmaxf(t->meteor_radius_max, t->fragment_radius_max);
    float u = k->radius / fmaxf(biggest, 1e-3f);
    return Clamp(u * u, 0.0f, 1.0f);   // o peso cresce mais rápido que o tamanho
}

const Rock *rocks_towed(const Rocks *r)
{
    if (r->towed < 0 || r->towed >= ROCKS_MAX || !r->items[r->towed].alive) return NULL;
    return &r->items[r->towed];
}
