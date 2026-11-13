#include "raylib.h"
#include "rlgl.h"

#include "render/render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/enemies_render.h"
#include "render/particles_render.h"
#include "render/ship_render.h"

static const Color SPACE_COLOR = { 2, 3, 8, 255 };   // quase preto, puxando para o azul

// As estrelas e o Sol ficam em uma esfera a esta fração do plano "far".
#define SKY_DISTANCE_FRACTION 0.5f

// Comprimento dos eixos de debug, em raios do planeta.
#define AXIS_LENGTH 1.6f

// O giro em torno do eixo de visão treme menos que os outros dois: girar o
// horizonte incomoda mais do que balançar a mira.
#define SHAKE_ROLL_FACTOR 0.5f

// Tremor das explosões de inimigos, em "raios de cratera" por unidade de
// tamanho do inimigo (o Bombardeiro, grande, sacode como uma cratera grande).
#define KILL_SHAKE      2.4f
#define KILL_SHAKE_BOMB 1.0f

bool render_init(Renderer *r, const World *w, const Tuning *t, bool use_shaders)
{
    *r = (Renderer){ 0 };
    stars_build(&r->stars, t);
    if (!planet_render_init(&r->planet, &w->planet, t, use_shaders)) return false;
    city_lights_build(&r->cities, &w->planet, t, stars_glow_texture(&r->stars));
    atmosphere_init(&r->atmosphere);
    moon_render_build(&r->moon, t);
    r->impacts_seen = w->impact_count;
    r->jolts_seen = w->jolt_count;
    r->events_seen = w->events.count;
    return true;
}

void render_apply_tuning(Renderer *r, const World *w, const Tuning *t)
{
    stars_build(&r->stars, t);
    planet_render_reload_shader(&r->planet);   // o F5 também relê os arquivos de shader
    planet_render_rebuild(&r->planet, &w->planet, t);
    city_lights_build(&r->cities, &w->planet, t, stars_glow_texture(&r->stars));
    moon_render_build(&r->moon, t);
    r->impacts_seen = w->impact_count;
    r->jolts_seen = w->jolt_count;
    r->events_seen = w->events.count;
    r->shake = 0.0f;
}

Camera3D render_camera(const World *w, const Tuning *t, float alpha)
{
    CamPose pose = world_view(w, alpha);

    (void)t;
    Camera3D camera = { 0 };
    camera.position = pose.pos;
    camera.target = Vector3Add(pose.pos, cam_forward(pose));
    camera.up = cam_up(pose);
    camera.fovy = pose.fov;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}

// Reage aos impactos que aconteceram desde o último quadro: soma tremor à
// câmera (mais forte quanto maior e mais perto a explosão) e apaga as luzes
// de cidades atingidas.
static void react_to_impacts(Renderer *r, const World *w, const Tuning *t, Vector3 camera_pos)
{
    if (w->impact_count < r->impacts_seen) r->impacts_seen = w->impact_count;   // o mundo foi reiniciado

    for (uint32_t id = r->impacts_seen + 1; id <= w->impact_count; id++) {
        const Impact *impact = world_find_impact(w, id);
        if (impact == NULL) continue;   // já saiu da fila do mundo: perdemos este

        float distance = Vector3Distance(camera_pos, impact->pos);
        r->shake += t->shake_strength * impact->radius / (1.0f + distance / t->shake_falloff);
        city_lights_impact(&r->cities, &w->planet, impact->pos, impact->radius);
    }
    r->impacts_seen = w->impact_count;

    // Pancadas na própria nave (batidas no terreno, dano, destruição).
    if (w->jolt_count != r->jolts_seen) {
        r->shake += t->shake_jolt * w->jolt_strength;
        r->jolts_seen = w->jolt_count;
    }

    // Explosões de inimigos: tremem conforme o tamanho e a distância, como
    // os impactos no planeta.
    if (w->events.count < r->events_seen) r->events_seen = w->events.count;
    for (uint32_t id = r->events_seen + 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL || (e->type != EV_KILL && e->type != EV_CRASH && e->type != EV_BOMB_DOWN)) continue;
        float size = (e->type == EV_BOMB_DOWN) ? KILL_SHAKE_BOMB : t->enemy[e->sub].scale;
        float distance = Vector3Distance(camera_pos, e->pos);
        r->shake += t->shake_strength * size * KILL_SHAKE / (1.0f + distance / t->shake_falloff);
    }
    r->events_seen = w->events.count;
    if (r->shake > 1.0f) r->shake = 1.0f;
}

// Balança a câmera de acordo com o tremor atual. O balanço cresce com o
// quadrado do tremor: explosões pequenas mal se notam, as grandes sacodem.
static void apply_shake(const Renderer *r, const Tuning *t, float time, Camera3D *camera)
{
    if (r->shake <= 0.0f) return;

    float amount = r->shake * r->shake * t->shake_max_angle * DEG2RAD;
    float phase = time * t->shake_frequency;
    // Três ondas fora de sincronia: parece aleatório, mas é repetível.
    float yaw = amount * sinf(phase) * sinf(phase * 0.53f + 1.7f);
    float pitch = amount * sinf(phase * 1.31f + 0.9f) * sinf(phase * 0.37f + 4.1f);
    float roll = amount * SHAKE_ROLL_FACTOR * sinf(phase * 0.83f + 2.3f);

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
    Vector3 up = Vector3CrossProduct(right, forward);

    forward = Vector3RotateByAxisAngle(forward, up, yaw);
    forward = Vector3RotateByAxisAngle(forward, right, pitch);
    camera->target = Vector3Add(camera->position, forward);
    camera->up = Vector3RotateByAxisAngle(camera->up, forward, roll);
}

void render_scene(Renderer *r, const World *w, const Tuning *t, float alpha, bool debug)
{
    Camera3D camera = render_camera(w, t, alpha);

    // Tempo de jogo, contado em passos de lógica (e não pelo relógio), para
    // as animações saírem iguais em toda execução do autoplay.
    float time = ((float)w->tick + alpha) / (float)t->sim_hz;
    float elapsed = time - r->last_time;
    r->last_time = time;
    if (elapsed < 0.0f || elapsed > 1.0f) elapsed = 0.0f;   // primeiro quadro, ou o tempo saltou

    // Antes de desenhar: põe em dia as malhas dos chunks que mudaram.
    planet_render_update(&r->planet, &w->planet, t, camera.position);

    react_to_impacts(r, w, t, camera.position);
    apply_shake(r, t, time, &camera);
    r->shake -= t->shake_decay * elapsed;
    if (r->shake < 0.0f) r->shake = 0.0f;

    ClearBackground(SPACE_COLOR);

    // Planos de corte: o que está mais perto que "near" ou mais longe que
    // "far" não é desenhado. Precisa ser definido antes de BeginMode3D.
    rlSetClipPlanes(t->camera_near, t->camera_far);
    BeginMode3D(camera);

    // A ordem importa. Primeiro o fundo; depois o que é opaco (o planeta);
    // por último o que é translúcido e se soma à imagem.
    float sky = t->camera_far * SKY_DISTANCE_FRACTION;
    stars_draw(&r->stars, camera.position, sky);
    stars_draw_sun(&r->stars, camera, t, sky);

    planet_render_draw(&r->planet, t, camera.position, time, r->wireframe);
    moon_render_draw(&r->moon, &w->moon, t, w->planet.params.radius, alpha);
    city_lights_draw(&r->cities);
    atmosphere_draw(&r->atmosphere, t, w->planet.params.radius, camera.position, planet_hp(&w->planet));

    // Durante o hit-stop nada se move: as partículas não podem ser
    // "adiantadas" pela interpolação.
    Texture2D glow = stars_glow_texture(&r->stars);
    float moving = (w->hitstop > 0) ? 1.0f : alpha;
    enemies_draw(w, t, alpha, glow, camera);
    if (w->ship_active) ship_draw(&w->ship, &w->planet, t, alpha, glow, camera);
    projectiles_draw(w, t, alpha, glow, camera);
    particles_draw(&w->particles, camera, glow, t, moving);

    if (debug) {
        float len = w->planet.params.radius * AXIS_LENGTH;
        Vector3 origin = { 0.0f, 0.0f, 0.0f };
        DrawLine3D(origin, (Vector3){ len, 0.0f, 0.0f }, RED);     // +X
        DrawLine3D(origin, (Vector3){ 0.0f, len, 0.0f }, GREEN);   // +Y
        DrawLine3D(origin, (Vector3){ 0.0f, 0.0f, len }, BLUE);    // +Z
    }

    EndMode3D();
}

void render_shutdown(Renderer *r)
{
    particles_render_unload();
    moon_render_unload(&r->moon);
    atmosphere_unload(&r->atmosphere);
    city_lights_unload(&r->cities);
    planet_render_unload(&r->planet);
    stars_unload(&r->stars);
}
