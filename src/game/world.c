#include "game/world.h"

#include <math.h>
#include <string.h>

#include "voxel/damage.h"

#define PITCH_LIMIT (89.0f * DEG2RAD)

// Alcance máximo do raio de um impacto, em unidades do mundo.
#define IMPACT_RAY_LENGTH 5000.0f

Vector3 cam_forward(CamPose p)
{
    float cp = cosf(p.pitch);
    return (Vector3){ sinf(p.yaw) * cp, sinf(p.pitch), -cosf(p.yaw) * cp };
}

Vector3 cam_right(CamPose p)
{
    return (Vector3){ cosf(p.yaw), 0.0f, sinf(p.yaw) };
}

// Câmera de apresentação: anda em um círculo e olha sempre para o centro.
static CamPose orbit_pose(float angle, const Tuning *t)
{
    CamPose p;
    p.pos = (Vector3){ t->orbit_radius * sinf(angle), t->orbit_height, t->orbit_radius * cosf(angle) };
    p.yaw = -angle;
    p.pitch = -atan2f(t->orbit_height, t->orbit_radius);
    return p;
}

bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena)
{
    memset(w, 0, sizeof *w);
    rng_seed(&w->rng, seed, 1u);
    w->cam_mode = CAM_ORBIT;
    w->view = orbit_pose(w->orbit_angle, t);
    w->view_prev = w->view;

    PlanetParams params = tuning_planet_params(t);
    return planet_init(&w->planet, &params, arena) && particles_init(&w->particles, arena);
}

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

void world_reset_planet(World *w, const Tuning *t)
{
    // A receita é montada com a grade que existe (ver world_apply_tuning).
    Tuning fixed = *t;
    fixed.planet_grid = w->planet.size;
    fixed.planet_chunk = w->planet.chunk_size;
    PlanetParams params = tuning_planet_params(&fixed);

    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    w->collapsed = false;
}

bool world_apply_tuning(World *w, const Tuning *t)
{
    // O tamanho da grade só vale ao iniciar: ela já foi reservada. A receita é
    // montada com a grade que existe, senão o raio (que é uma fração dela)
    // sairia calculado para uma grade de outro tamanho.
    Tuning fixed = *t;
    fixed.planet_grid = w->planet.size;
    fixed.planet_chunk = w->planet.chunk_size;
    PlanetParams params = tuning_planet_params(&fixed);

    if (planet_params_equal(&params, &w->planet.params)) return false;
    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    w->collapsed = false;
    return true;
}

void world_place_camera(World *w, Vector3 pos, Vector3 target)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, pos));
    if (Vector3LengthSqr(dir) < 0.5f) dir = (Vector3){ 0.0f, 0.0f, -1.0f };   // pos == target

    w->cam_mode = CAM_FREE;
    w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    w->view.pos = pos;
    // Inverso de cam_forward: de uma direção para yaw e pitch.
    w->view.pitch = Clamp(asinf(Clamp(dir.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    w->view.yaw = atan2f(dir.x, -dir.z);
    w->view_prev = w->view;   // corte seco, sem interpolar o salto
}

static void tick_free_camera(World *w, const InputState *in, const Tuning *t, float dt)
{
    CamPose *v = &w->view;

    float sens = t->freecam_mouse_sens * DEG2RAD;
    v->yaw += in->look_dx * sens;
    v->pitch -= in->look_dy * sens;   // mouse para baixo (dy positivo) olha para baixo
    v->pitch = Clamp(v->pitch, -PITCH_LIMIT, PITCH_LIMIT);

    Vector3 forward = cam_forward(*v);
    Vector3 right = cam_right(*v);
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
    v->pos = Vector3Add(v->pos, Vector3Scale(w->free_vel, dt));
}

void world_tick(World *w, const InputState *in, const Tuning *t)
{
    float dt = 1.0f / (float)t->sim_hz;
    bool snap = false;

    w->view_prev = w->view;

    if (in->pressed & ACT_BIT(ACT_FREECAM)) {
        if (w->cam_mode == CAM_ORBIT) {
            w->cam_mode = CAM_FREE;   // a câmera livre parte de onde a outra estava
            w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
        } else {
            w->cam_mode = CAM_ORBIT;
            snap = true;              // corte seco: não interpolar através do salto
        }
    }

    if (w->cam_mode == CAM_ORBIT) {
        w->orbit_angle += t->orbit_speed * DEG2RAD * dt;
        w->view = orbit_pose(w->orbit_angle, t);
    } else {
        tick_free_camera(w, in, t, dt);
    }

    if (snap) w->view_prev = w->view;

    particles_tick(&w->particles, &w->planet, t, dt);
    check_collapse(w, t);
    w->tick++;
}

CamPose world_view(const World *w, float alpha)
{
    CamPose p;
    p.pos = Vector3Lerp(w->view_prev.pos, w->view.pos, alpha);
    p.yaw = Lerp(w->view_prev.yaw, w->view.yaw, alpha);
    p.pitch = Lerp(w->view_prev.pitch, w->view.pitch, alpha);
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

uint64_t world_hash(const World *w)
{
    uint64_t h = 14695981039346656037ULL;
    int mode = (int)w->cam_mode;
    HASH(h, w->tick);
    HASH(h, w->rng.state);
    HASH(h, w->rng.inc);
    HASH(h, mode);
    HASH(h, w->orbit_angle);
    HASH(h, w->free_vel.x);
    HASH(h, w->free_vel.y);
    HASH(h, w->free_vel.z);
    HASH(h, w->view.pos.x);
    HASH(h, w->view.pos.y);
    HASH(h, w->view.pos.z);
    HASH(h, w->view.yaw);
    HASH(h, w->view.pitch);
    HASH(h, w->planet.volume);
    HASH(h, w->planet.core_volume);
    HASH(h, w->impact_count);
    HASH(h, w->particles.count);
    // As partículas entram no resumo pela soma das posições: barato, e
    // suficiente para denunciar qualquer diferença na simulação delas.
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    for (int i = 0; i < w->particles.count; i++) {
        px += w->particles.items[i].pos.x;
        py += w->particles.items[i].pos.y;
        pz += w->particles.items[i].pos.z;
    }
    HASH(h, px);
    HASH(h, py);
    HASH(h, pz);
    return h;
}
