#include "game/world.h"

#include <math.h>
#include <string.h>

#include "game/combat.h"
#include "voxel/damage.h"

#define PITCH_LIMIT (89.0f * DEG2RAD)

// Alcance máximo do raio de um impacto, em unidades do mundo.
#define IMPACT_RAY_LENGTH 5000.0f

// --- câmera em terceira pessoa ---
#define CHASE_MAX_STRETCH 2.0f   // o quanto a câmera pode ficar para trás, em distâncias normais
#define CHASE_PATH_STEPS  10     // pontos conferidos entre a nave e a câmera
#define CHASE_MIN_FRACTION 0.45f // mais perto que isto (em distâncias normais), a câmera sobe por cima
#define SLING_FOV         0.6f   // quanto o estilingue abre o campo de visão, em relação ao boost
#define RUSH_MAX          1.4f   // teto da abertura com boost e estilingue juntos

// --- efeitos da nave ---
static const Vector3 ENGINES[2] = {   // em eixos da nave
    { SHIP_ENGINE_X, SHIP_ENGINE_Y, SHIP_ENGINE_Z }, { -SHIP_ENGINE_X, SHIP_ENGINE_Y, SHIP_ENGINE_Z },
};
#define TRAIL_SPEED       7.0f     // velocidade com que o rastro sai para trás
#define TRAIL_LIFE        0.30f
#define TRAIL_SIZE        0.28f
#define TRAIL_BOOST_SIZE  0.40f
#define TRAIL_INHERIT     0.5f     // fração da velocidade da nave que o rastro leva junto
#define TRAIL_SPACING     0.55f    // distância entre dois pontos do rastro, em tamanhos
#define TRAIL_MAX_POINTS  5        // pontos soltos por motor em um passo
#define SPARKS_MIN_SPEED  2.5f     // batidas mais leves que isto não soltam faíscas
#define SPARKS_PER_SPEED  0.9f
#define SPARKS_MAX        50
#define DASH_STREAKS         36     // pontos de brilho soltos por um dash
#define DASH_STREAK_SPAN     1.3f   // espalhados por esta distância em volta do centro da nave
#define DASH_STREAK_SPEED    16.0f  // velocidade máxima com que ficam para trás
#define DASH_STREAK_INHERIT  0.55f  // fração da velocidade da nave que levam junto

static const Rgb8 TRAIL_COLOR       = { 110, 190, 255 };
static const Rgb8 TRAIL_BOOST_COLOR = { 255, 196, 120 };
static const Rgb8 TRAIL_SLING_COLOR = { 190, 150, 255 };
static const Rgb8 SPARK_COLOR       = { 255, 214, 150 };

// ---------------------------------------------------------------------------
//  Câmeras
// ---------------------------------------------------------------------------

static Vector3 angles_to_forward(float yaw, float pitch)
{
    float cp = cosf(pitch);
    return (Vector3){ sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp };
}

// Câmera de apresentação: anda em um círculo e olha sempre para o centro.
static CamPose orbit_pose(float angle, const Tuning *t)
{
    CamPose p;
    p.pos = (Vector3){ t->orbit_radius * sinf(angle), t->orbit_height, t->orbit_radius * cosf(angle) };
    p.rot = quat_look(Vector3Negate(p.pos), (Vector3){ 0.0f, 1.0f, 0.0f });
    p.fov = t->camera_fov;
    return p;
}

static CamPose free_pose(const World *w, const Tuning *t)
{
    CamPose p;
    p.pos = w->view.pos;
    p.rot = quat_look(angles_to_forward(w->free_yaw, w->free_pitch), (Vector3){ 0.0f, 1.0f, 0.0f });
    p.fov = t->camera_fov;
    return p;
}

// Anda da nave até `to` e devolve o último ponto livre antes do primeiro
// obstáculo (terreno ou Lua).
static Vector3 clear_camera_path(const World *w, Vector3 from, Vector3 to, float clearance)
{
    // Densidade que o campo tem a `clearance` de distância da superfície.
    float blocked = VOXEL_ISO - clearance * VOXEL_DENSITY_PER_UNIT;
    float moon_reach = w->moon.radius + clearance;
    Vector3 last_free = from;
    for (int i = 1; i <= CHASE_PATH_STEPS; i++) {
        Vector3 point = Vector3Lerp(from, to, (float)i / CHASE_PATH_STEPS);
        if (planet_sample(&w->planet, point) >= blocked) return last_free;
        if (Vector3Distance(point, w->moon.pos) < moon_reach) return last_free;
        last_free = point;
    }
    return to;
}

// Lugar da câmera: o desejado, recuado se houver terreno no caminho. Se o
// recuo a deixaria colada na nave (dentro do modelo, com a nave pousada no
// chão ou no fundo de um buraco), ela sobe por cima da nave.
static Vector3 place_chase_camera(const World *w, const Tuning *t, Vector3 ship, Vector3 desired)
{
    Vector3 pos = clear_camera_path(w, ship, desired, t->chase_clearance);
    if (Vector3Distance(pos, ship) >= t->chase_distance * CHASE_MIN_FRACTION) return pos;

    Vector3 up = Vector3Normalize(ship);
    Vector3 above = Vector3Add(ship, Vector3Add(Vector3Scale(up, t->chase_distance),
                                                Vector3Scale(Vector3Subtract(desired, ship), 0.4f)));
    Vector3 lifted = clear_camera_path(w, ship, above, t->chase_clearance);
    return (Vector3Distance(lifted, ship) > Vector3Distance(pos, ship)) ? lifted : pos;
}

// Câmera de jogo: fica atrás e acima da nave, com atraso. O atraso é o que
// dá a sensação de aceleração; no boost o campo de visão também abre.
static void tick_chase_camera(World *w, const Tuning *t, float dt, bool snap)
{
    const Ship *s = &w->ship;
    if (!s->alive && !snap) return;   // destruída: a câmera fica olhando o lugar

    Vector3 forward = ship_forward(s), up = ship_up(s);
    Vector3 desired = Vector3Add(s->pos, Vector3Add(Vector3Scale(forward, -t->chase_distance),
                                                    Vector3Scale(up, t->chase_height)));
    Vector3 pos = desired;
    if (!snap) {
        pos = Vector3Lerp(w->view.pos, desired, 1.0f - expf(-t->chase_pos_smooth * dt));

        // Em alta velocidade o atraso cresce; aqui ele tem um teto.
        Vector3 offset = Vector3Subtract(pos, s->pos);
        float length = Vector3Length(offset);
        float longest = t->chase_distance * CHASE_MAX_STRETCH;
        if (length > longest) pos = Vector3Add(s->pos, Vector3Scale(offset, longest / length));
    }
    pos = place_chase_camera(w, t, s->pos, pos);

    // Olha para um ponto à frente da nave, e não para ela: assim sobra tela
    // para ver aonde se está indo.
    Vector3 look = Vector3Add(s->pos, Vector3Scale(forward, t->chase_look_ahead));

    // No voo rasante esse ponto desce em direção ao planeta. Sem isso, como o
    // planeta é pequeno, o chão some logo abaixo da tela e não se vê o
    // relevo passando.
    float distance = Vector3Length(s->pos);
    if (s->graze > 0.0f && distance > 1e-3f) {
        float tilt = t->chase_ground_tilt * DEG2RAD * s->graze;
        float drop = tanf(tilt) * (t->chase_look_ahead + t->chase_distance);
        look = Vector3Subtract(look, Vector3Scale(s->pos, drop / distance));
    }
    Quaternion target = quat_look(Vector3Subtract(look, pos), up);

    w->view.pos = pos;
    w->view.rot = snap ? target : QuaternionSlerp(w->view.rot, target, 1.0f - expf(-t->chase_rot_smooth * dt));
    // O campo de visão abre com a velocidade extra: no boost e no estilingue.
    float rush = Clamp(s->boost_blend + s->sling_blend * SLING_FOV, 0.0f, RUSH_MAX);
    w->view.fov = t->camera_fov + rush * t->chase_fov_boost;
}

static void tick_free_camera(World *w, const InputState *in, const Tuning *t, float dt)
{
    float sens = t->freecam_mouse_sens * DEG2RAD;
    w->free_yaw += in->look_dx * sens;
    w->free_pitch -= in->look_dy * sens;   // mouse para baixo (dy positivo) olha para baixo
    w->free_pitch = Clamp(w->free_pitch, -PITCH_LIMIT, PITCH_LIMIT);

    Vector3 forward = angles_to_forward(w->free_yaw, w->free_pitch);
    Vector3 right = { cosf(w->free_yaw), 0.0f, sinf(w->free_yaw) };
    Vector3 wish = { 0.0f, 0.0f, 0.0f };
    if (in->held & ACT_BIT(ACT_FORWARD)) wish = Vector3Add(wish, forward);
    if (in->held & ACT_BIT(ACT_BACK))    wish = Vector3Subtract(wish, forward);
    if (in->held & ACT_BIT(ACT_RIGHT))   wish = Vector3Add(wish, right);
    if (in->held & ACT_BIT(ACT_LEFT))    wish = Vector3Subtract(wish, right);
    if (in->held & ACT_BIT(ACT_DASH))    wish.y += 1.0f;
    if (in->held & ACT_BIT(ACT_DRIFT))   wish.y -= 1.0f;

    float speed = t->freecam_speed;
    if (in->held & ACT_BIT(ACT_BOOST)) speed *= t->freecam_boost;
    Vector3 target = Vector3Scale(Vector3Normalize(wish), speed);

    // Suavização exponencial: a velocidade persegue o alvo em vez de saltar
    // para ele. A fórmula com exp() dá o mesmo resultado em qualquer dt.
    float blend = 1.0f - expf(-t->freecam_smooth * dt);
    w->free_vel = Vector3Lerp(w->free_vel, target, blend);
    w->view.pos = Vector3Add(w->view.pos, Vector3Scale(w->free_vel, dt));
    w->view = free_pose(w, t);
}

// Entra na câmera livre a partir de onde a câmera atual está olhando.
static void enter_free_camera(World *w)
{
    Vector3 forward = cam_forward(w->view);
    w->free_pitch = Clamp(asinf(Clamp(forward.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    w->free_yaw = atan2f(forward.x, -forward.z);
    w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    if (w->cam_mode != CAM_FREE) w->cam_return = w->cam_mode;
    w->cam_mode = CAM_FREE;
}

void world_set_view(World *w, const Tuning *t, CamMode mode)
{
    if (mode == CAM_FREE) {
        enter_free_camera(w);
        w->view = free_pose(w, t);
    } else if (mode == CAM_ORBIT) {
        w->cam_mode = CAM_ORBIT;
        w->cam_return = CAM_ORBIT;
        w->ship_active = false;
        w->view = orbit_pose(w->orbit_angle, t);
    } else {
        w->cam_mode = CAM_SHIP;
        w->cam_return = CAM_SHIP;
        w->ship_active = true;
        tick_chase_camera(w, t, 0.0f, true);
    }
    w->view_prev = w->view;   // corte seco, sem interpolar o salto
}

void world_place_camera(World *w, Vector3 pos, Vector3 target)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, pos));
    if (Vector3LengthSqr(dir) < 0.5f) dir = (Vector3){ 0.0f, 0.0f, -1.0f };   // pos == target

    if (w->cam_mode != CAM_FREE) w->cam_return = w->cam_mode;
    w->cam_mode = CAM_FREE;
    w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    // Inverso de angles_to_forward: de uma direção para yaw e pitch.
    w->free_pitch = Clamp(asinf(Clamp(dir.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    w->free_yaw = atan2f(dir.x, -dir.z);

    float fov = w->view.fov;
    w->view.pos = pos;
    w->view.rot = quat_look(angles_to_forward(w->free_yaw, w->free_pitch), (Vector3){ 0.0f, 1.0f, 0.0f });
    w->view.fov = fov;
    w->view_prev = w->view;   // corte seco, sem interpolar o salto
}

void world_place_ship(World *w, const Tuning *t, Vector3 pos, Vector3 target, float speed)
{
    ship_place(&w->ship, t, &w->planet, pos, Vector3Subtract(target, pos), speed);
    world_set_view(w, t, CAM_SHIP);
}

// ---------------------------------------------------------------------------
//  Planeta
// ---------------------------------------------------------------------------

// O planeta morre se perder massa demais ou se o núcleo for destruído.
static void check_collapse(World *w, const Tuning *t)
{
    if (planet_hp(&w->planet) < t->defeat_planet_hp || planet_core_hp(&w->planet) < t->defeat_core_hp) {
        w->collapsed = true;
    }
}

bool world_impact(World *w, const Tuning *t, Vector3 origin, Vector3 dir, float rim_radius, float depth)
{
    Vector3 ground;
    if (!planet_raycast(&w->planet, origin, dir, IMPACT_RAY_LENGTH, &ground)) return false;

    Vector3 normal = planet_normal(&w->planet, ground);
    float sphere_radius = 0.0f;
    Vector3 center = crater_sphere(ground, normal, rim_radius, depth, &sphere_radius);
    CarveResult carve = planet_carve(&w->planet, center, sphere_radius);
    if (carve.removed == 0) return true;   // acertou, mas não havia mais o que tirar

    particles_impact(&w->particles, &w->rng, t, ground, normal, rim_radius, &carve);

    Impact *impact = &w->impacts[w->impact_count % WORLD_IMPACT_HISTORY];
    w->impact_count++;
    impact->pos = ground;
    impact->radius = rim_radius;
    impact->id = w->impact_count;

    check_collapse(w, t);
    return true;
}

const Impact *world_find_impact(const World *w, uint32_t id)
{
    if (id == 0 || id > w->impact_count) return NULL;
    if (w->impact_count - id >= WORLD_IMPACT_HISTORY) return NULL;   // já foi sobrescrito
    return &w->impacts[(id - 1) % WORLD_IMPACT_HISTORY];
}

// Receita do planeta para a grade que já existe. O tamanho da grade só vale
// ao iniciar; montar a receita com outro tamanho calcularia o raio (que é uma
// fração da grade) para uma grade que não é a nossa.
static PlanetParams current_planet_params(const World *w, const Tuning *t)
{
    Tuning fixed = *t;
    fixed.planet_grid = w->planet.size;
    fixed.planet_chunk = w->planet.chunk_size;
    return tuning_planet_params(&fixed);
}

void world_reset_planet(World *w, const Tuning *t)
{
    PlanetParams params = current_planet_params(w, t);
    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    w->collapsed = false;
}

bool world_apply_tuning(World *w, const Tuning *t)
{
    PlanetParams params = current_planet_params(w, t);
    if (planet_params_equal(&params, &w->planet.params)) return false;

    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    w->collapsed = false;
    return true;
}

// ---------------------------------------------------------------------------
//  Efeitos da nave
// ---------------------------------------------------------------------------

static Particle *spawn_glow(World *w, ParticleKind kind, Vector3 pos, Vector3 vel, float size, float life, Rgb8 color)
{
    Particle *p = particles_spawn(&w->particles);
    if (p == NULL) return NULL;
    p->kind = (uint8_t)kind;
    p->pos = pos;
    p->vel = vel;
    p->size = size;
    p->life = life;
    p->r = color.r;
    p->g = color.g;
    p->b = color.b;
    return p;
}

void world_jolt(World *w, float strength)
{
    w->jolt_count++;
    w->jolt_strength = strength;
}

static void ship_effects(World *w, const ShipEvents *ev)
{
    const Ship *s = &w->ship;

    // Rastro dos motores: mais forte e de outra cor no boost. O rastro fica
    // quase parado no espaço enquanto a nave segue, então em alta velocidade
    // ele vira um risco comprido. Para o risco não ter falhas, cada passo
    // solta vários pontos ao longo do trecho que o motor percorreu.
    if (s->alive && s->thrust_blend > 0.15f) {
        Vector3 back = Vector3Negate(ship_forward(s));
        Rgb8 color = s->boosting ? TRAIL_BOOST_COLOR : (s->sling > 0.0f) ? TRAIL_SLING_COLOR : TRAIL_COLOR;
        float size = Lerp(TRAIL_SIZE, TRAIL_BOOST_SIZE, fmaxf(s->boost_blend, s->sling_blend)) * s->thrust_blend;
        Vector3 carried = Vector3Scale(Vector3Add(s->vel, s->fall), TRAIL_INHERIT);

        float left_behind = Vector3Distance(s->pos, s->pos_prev) * (1.0f - TRAIL_INHERIT);
        int points = 1 + (int)(left_behind / (size * TRAIL_SPACING));
        if (points > TRAIL_MAX_POINTS) points = TRAIL_MAX_POINTS;

        for (int i = 0; i < 2; i++) {
            Vector3 now = Vector3Add(s->pos, Vector3RotateByQuaternion(ENGINES[i], s->rot));
            Vector3 before = Vector3Add(s->pos_prev, Vector3RotateByQuaternion(ENGINES[i], s->rot_prev));
            for (int k = 0; k < points; k++) {
                Vector3 at = Vector3Lerp(before, now, ((float)k + rng_float(&w->rng)) / (float)points);
                Vector3 vel = Vector3Add(carried, Vector3Scale(back, TRAIL_SPEED * rng_range(&w->rng, 0.9f, 1.1f)));
                spawn_glow(w, PARTICLE_TRAIL, at, vel, size, TRAIL_LIFE * rng_range(&w->rng, 0.7f, 1.2f), color);
            }
        }
    }

    // Dash: riscos de brilho que ficam para trás, saindo de toda a nave, na
    // direção contrária à do impulso.
    if (ev->dashed) {
        Vector3 carried = Vector3Scale(s->vel, DASH_STREAK_INHERIT);
        for (int i = 0; i < DASH_STREAKS; i++) {
            Vector3 local = {
                rng_range(&w->rng, -DASH_STREAK_SPAN, DASH_STREAK_SPAN),
                rng_range(&w->rng, -0.2f, 0.3f),
                rng_range(&w->rng, -DASH_STREAK_SPAN, DASH_STREAK_SPAN),
            };
            Vector3 at = Vector3Add(s->pos, Vector3RotateByQuaternion(local, s->rot));
            Vector3 vel = Vector3Subtract(carried, Vector3Scale(ev->dash_dir, rng_range(&w->rng, 2.0f, DASH_STREAK_SPEED)));
            spawn_glow(w, PARTICLE_TRAIL, at, vel, rng_range(&w->rng, 0.25f, 0.5f), rng_range(&w->rng, 0.2f, 0.45f),
                       TRAIL_COLOR);
        }
        events_push(&w->events, (uint32_t)w->tick, EV_DASH, 0, s->pos, 0.0f);
    }

    // Batida no terreno: faíscas saltando da superfície.
    if (ev->hit && ev->hit_speed > SPARKS_MIN_SPEED) {
        int sparks = (int)(ev->hit_speed * SPARKS_PER_SPEED);
        if (sparks > SPARKS_MAX) sparks = SPARKS_MAX;
        for (int i = 0; i < sparks; i++) {
            Vector3 dir = Vector3Normalize(Vector3Add(ev->hit_normal, Vector3Scale(rng_unit_vec3(&w->rng), 0.9f)));
            spawn_glow(w, PARTICLE_EMBER, ev->hit_point,
                       Vector3Scale(dir, ev->hit_speed * rng_range(&w->rng, 0.2f, 0.7f)),
                       rng_range(&w->rng, 0.18f, 0.4f), rng_range(&w->rng, 0.3f, 0.9f), SPARK_COLOR);
        }
        world_jolt(w, ev->hit_speed);
        events_push(&w->events, (uint32_t)w->tick, EV_BUMP, 0, ev->hit_point, ev->hit_speed);
    }

    // Destruída: clarão e destroços em todas as direções.
    if (ev->died) combat_ship_wreck(w);
}

// ---------------------------------------------------------------------------
//  Mundo
// ---------------------------------------------------------------------------

bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena)
{
    memset(w, 0, sizeof *w);
    rng_seed(&w->rng, seed, 1u);

    PlanetParams params = tuning_planet_params(t);
    if (!planet_init(&w->planet, &params, arena) || !particles_init(&w->particles, arena)) return false;
    if (!enemies_init(&w->enemies, arena) || !projectiles_init(&w->shots, arena)) return false;

    moon_init(&w->moon, t, w->planet.params.radius);
    ship_spawn(&w->ship, t, &w->planet);
    score_reset(&w->score);
    director_stop(&w->director);
    w->state = GAME_PLAYING;
    w->view.rot = QuaternionIdentity();
    w->view.fov = t->camera_fov;
    world_set_view(w, t, CAM_SHIP);
    return true;
}

// ---------------------------------------------------------------------------
//  Partida
// ---------------------------------------------------------------------------

bool world_in_play(const World *w)
{
    return w->cam_mode == CAM_SHIP && w->ship_active;
}

EnemyContext world_enemy_context(World *w, const Tuning *t)
{
    EnemyContext c;
    c.planet = &w->planet;
    c.moon = &w->moon;
    c.ship = (w->ship_active && w->ship.alive) ? &w->ship : NULL;
    c.shots = &w->shots;
    c.events = &w->events;
    c.rng = &w->rng;
    c.t = t;
    c.tick = (uint32_t)w->tick;
    c.dt = 1.0f / (float)t->sim_hz;
    return c;
}

int world_spawn(World *w, const Tuning *t, EnemyType type, int count, Vector3 center)
{
    EnemyContext ctx = world_enemy_context(w, t);
    return enemies_spawn_group(&w->enemies, &ctx, type, count, center);
}

void world_set_plan(World *w, const WavePlan *plan)
{
    w->plan = *plan;
    w->waves_enabled = true;
    director_start(&w->director, &w->plan);
}

void world_set_waves(World *w, bool on)
{
    w->waves_enabled = on;
    if (on) director_start(&w->director, &w->plan);
    else    director_stop(&w->director);
}

void world_restart(World *w, const Tuning *t)
{
    world_reset_planet(w, t);
    enemies_clear(&w->enemies);
    projectiles_clear(&w->shots);
    score_reset(&w->score);
    ship_spawn(&w->ship, t, &w->planet);
    w->state = GAME_PLAYING;
    w->state_tick = 0;
    w->hitstop = 0;
    memset(&w->frozen_input, 0, sizeof w->frozen_input);
    w->hit_marker = 0.0f;
    w->kill_marker = 0.0f;
    if (w->waves_enabled) director_start(&w->director, &w->plan);
    else                  director_stop(&w->director);
    world_set_view(w, t, CAM_SHIP);
}

// Faz a sequência de hordas andar: solta os grupos na hora e paga o bônus
// de planeta intacto ao fim de cada horda.
static void run_director(World *w, const Tuning *t, float dt)
{
    if (w->state != GAME_PLAYING || w->director.phase == DIRECTOR_OFF) return;
    uint32_t tick = (uint32_t)w->tick;
    DirectorStep step = director_tick(&w->director, &w->plan, enemies_alive(&w->enemies), dt);

    if (step.wave_started) {
        events_push(&w->events, tick, EV_WAVE_START, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, (float)(w->director.wave + 1));
    }
    float distance = t->enemies_spawn_distance * w->planet.params.radius;
    for (int i = 0; i < step.spawn_count; i++) {
        const WaveGroup *g = step.spawn[i];
        world_spawn(w, t, (EnemyType)g->type, g->count, Vector3Scale(wave_group_direction(g), distance));
    }
    if (step.wave_cleared) {
        float bonus = (planet_hp(&w->planet) >= t->score_planet_bonus_hp) ? t->score_planet_bonus : 0.0f;
        score_bonus(&w->score, (int64_t)bonus);
        w->score.planet_bonus += (int64_t)bonus;
        events_push(&w->events, tick, EV_WAVE_CLEAR, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, bonus);
    }
}

static void update_game_state(World *w)
{
    if (w->state != GAME_PLAYING) return;
    uint32_t tick = (uint32_t)w->tick;
    Vector3 origin = { 0.0f, 0.0f, 0.0f };
    if (w->collapsed) {
        w->state = GAME_LOST;
        w->state_tick = w->tick;
        events_push(&w->events, tick, EV_GAME_LOST, 0, origin, 0.0f);
    } else if (w->director.phase == DIRECTOR_DONE && enemies_alive(&w->enemies) == 0) {
        w->state = GAME_WON;
        w->state_tick = w->tick;
        events_push(&w->events, tick, EV_GAME_WON, 0, origin, 0.0f);
    }
}

// Deixa parado tudo que é desenhado com interpolação: com a posição anterior
// igual à atual, nada desliza na tela enquanto a simulação está congelada.
static void hold_still(World *w)
{
    w->ship.pos_prev = w->ship.pos;
    w->ship.rot_prev = w->ship.rot;
    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        e->items[i].pos_prev = e->items[i].pos;
        e->items[i].rot_prev = e->items[i].rot;
    }
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (pool_alive(&p->pool, i)) p->items[i].pos_prev = p->items[i].pos;
    }
}

// Guarda o que o jogador fez durante o congelamento, para valer depois dele:
// sem isso, um movimento de mouse ou um toque de tecla nesse instante se perderia.
static void hold_input(InputState *frozen, const InputState *in)
{
    frozen->held = in->held;
    frozen->pressed |= in->pressed;
    frozen->released |= in->released;
    frozen->look_dx += in->look_dx;
    frozen->look_dy += in->look_dy;
}

void world_tick(World *w, const InputState *in, const Tuning *t)
{
    float dt = 1.0f / (float)t->sim_hz;

    // Hit-stop: nos golpes mais fortes o jogo congela por alguns passos. O
    // tempo de jogo continua (as animações e o tremor de câmera seguem).
    if (w->hitstop > 0) {
        w->hitstop--;
        hold_input(&w->frozen_input, in);
        w->view_prev = w->view;
        hold_still(w);
        w->moon.pos_prev = w->moon.pos;
        w->tick++;
        return;
    }
    InputState input = *in;
    input.pressed |= w->frozen_input.pressed;
    input.released |= w->frozen_input.released;
    input.look_dx += w->frozen_input.look_dx;
    input.look_dy += w->frozen_input.look_dy;
    memset(&w->frozen_input, 0, sizeof w->frozen_input);

    bool snap = false;
    w->view_prev = w->view;

    // F2 alterna entre a câmera livre e a câmera em que se estava.
    if (input.pressed & ACT_BIT(ACT_FREECAM)) {
        if (w->cam_mode != CAM_FREE) {
            enter_free_camera(w);   // parte de onde a outra câmera estava
        } else {
            w->cam_mode = w->cam_return;
            snap = true;            // corte seco: não interpolar através do salto
        }
    }

    moon_tick(&w->moon, t, w->planet.params.radius, dt);

    // A partida só anda na câmera de jogo. Nas outras ela fica congelada.
    if (world_in_play(w)) {
        ShipEvents ev = ship_tick(&w->ship, &input, &w->planet, &w->moon, t, dt);
        ship_effects(w, &ev);
        combat_fire(w, &input, t, dt);
        run_director(w, t, dt);

        EnemyContext ctx = world_enemy_context(w, t);
        enemies_tick(&w->enemies, &ctx);
        projectiles_move(&w->shots, dt);
        combat_resolve(w, t);
        combat_sweep(w, t);

        score_tick(&w->score, dt);
        w->hit_marker = fmaxf(w->hit_marker - dt, 0.0f);
        w->kill_marker = fmaxf(w->kill_marker - dt, 0.0f);

        tick_chase_camera(w, t, dt, snap || ev.respawned);
        if (ev.respawned) snap = true;
    } else {
        hold_still(w);
        if (w->cam_mode == CAM_ORBIT) {
            w->orbit_angle += t->orbit_speed * DEG2RAD * dt;
            w->view = orbit_pose(w->orbit_angle, t);
        } else if (w->cam_mode == CAM_FREE) {
            tick_free_camera(w, &input, t, dt);
        } else {
            tick_chase_camera(w, t, dt, snap);   // câmera de jogo com a nave fora de cena
        }
    }

    if (snap) w->view_prev = w->view;

    particles_tick(&w->particles, &w->planet, t, dt);
    check_collapse(w, t);
    update_game_state(w);
    w->tick++;
}

CamPose world_view(const World *w, float alpha)
{
    CamPose p;
    p.pos = Vector3Lerp(w->view_prev.pos, w->view.pos, alpha);
    p.rot = QuaternionSlerp(w->view_prev.rot, w->view.rot, alpha);
    p.fov = Lerp(w->view_prev.fov, w->view.fov, alpha);
    return p;
}

// FNV-1a de 64 bits. Cada campo entra separado (e não a struct inteira de uma
// vez) para os bytes de preenchimento entre campos não contaminarem o resultado.
static uint64_t hash_bytes(uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

#define HASH(h, field) ((h) = hash_bytes((h), &(field), sizeof(field)))

static uint64_t hash_vec3(uint64_t h, Vector3 v)
{
    HASH(h, v.x);
    HASH(h, v.y);
    HASH(h, v.z);
    return h;
}

static uint64_t hash_quat(uint64_t h, Quaternion q)
{
    HASH(h, q.x);
    HASH(h, q.y);
    HASH(h, q.z);
    HASH(h, q.w);
    return h;
}

uint64_t world_hash(const World *w)
{
    uint64_t h = 14695981039346656037ULL;
    int mode = (int)w->cam_mode;
    HASH(h, w->tick);
    HASH(h, w->rng.state);
    HASH(h, w->rng.inc);
    HASH(h, mode);
    HASH(h, w->orbit_angle);
    h = hash_vec3(h, w->free_vel);
    h = hash_vec3(h, w->view.pos);
    h = hash_quat(h, w->view.rot);
    HASH(h, w->view.fov);

    h = hash_vec3(h, w->ship.pos);
    h = hash_vec3(h, w->ship.vel);
    h = hash_vec3(h, w->ship.fall);
    h = hash_quat(h, w->ship.rot);
    HASH(h, w->ship.hull);
    HASH(h, w->ship.shield);
    HASH(h, w->ship.energy);
    HASH(h, w->ship.sling);
    HASH(h, w->jolt_count);
    HASH(h, w->moon.angle);

    HASH(h, w->planet.volume);
    HASH(h, w->planet.core_volume);
    HASH(h, w->impact_count);

    int state = (int)w->state;
    HASH(h, state);
    HASH(h, w->hitstop);
    HASH(h, w->events.count);
    HASH(h, w->score.points);
    HASH(h, w->score.kills);
    HASH(h, w->director.phase);
    HASH(h, w->director.wave);
    HASH(h, w->director.timer);
    HASH(h, w->enemies.pool.live);
    for (int i = 0; i < w->enemies.pool.capacity; i++) {
        if (!pool_alive(&w->enemies.pool, i)) continue;
        h = hash_vec3(h, w->enemies.items[i].pos);
        HASH(h, w->enemies.items[i].hp);
    }
    HASH(h, w->shots.pool.live);
    for (int i = 0; i < w->shots.pool.capacity; i++) {
        if (pool_alive(&w->shots.pool, i)) h = hash_vec3(h, w->shots.items[i].pos);
    }

    HASH(h, w->particles.count);
    // As partículas entram no resumo pela soma das posições: barato, e
    // suficiente para denunciar qualquer diferença na simulação delas.
    Vector3 sum = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < w->particles.count; i++) sum = Vector3Add(sum, w->particles.items[i].pos);
    h = hash_vec3(h, sum);
    return h;
}
