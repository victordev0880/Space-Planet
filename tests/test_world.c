#include "test.h"
#include "test_util.h"

#include "game/world.h"

static Tuning tun;
static Arena  arena;

// Cada teste começa com a arena vazia; ela comporta vários mundos de teste.
static void make_world(World *w, uint64_t seed)
{
    if (!world_init(w, &tun, seed, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
}

static InputState state(uint32_t held, uint32_t pressed, float dx, float dy)
{
    InputState s = { held, pressed, 0u, dx, dy };
    return s;
}

static void run_ticks(World *w, InputState in, int count)
{
    for (int i = 0; i < count; i++) {
        world_tick(w, &in, &tun);
        in.pressed = 0u;   // a borda de "apertou" vale só no primeiro passo
    }
}

static void world_starts_in_orbit_with_a_whole_planet(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    CHECK(w.tick == 0u);
    CHECK(w.cam_mode == CAM_ORBIT);
    CHECK_NEAR(w.view.pos.y, tun.orbit_height, 1e-4);
    CHECK_NEAR(Vector3Distance(w.view.pos, w.view_prev.pos), 0.0f, 1e-6);

    CHECK(w.planet.size == TEST_GRID);
    CHECK(w.planet.volume > 0);
    CHECK_NEAR(planet_hp(&w.planet), 1.0f, 1e-6);
}

static void world_init_fails_without_memory(void)
{
    Arena tiny;
    CHECK(arena_init(&tiny, 1000));
    World w;
    CHECK(!world_init(&w, &tun, 1u, &tiny));
    arena_free(&tiny);
}

static void world_orbit_circles_and_faces_the_center(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    float expected_dist = sqrtf(tun.orbit_radius * tun.orbit_radius + tun.orbit_height * tun.orbit_height);

    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 500);
    CHECK(w.tick == 500u);
    CHECK_NEAR(Vector3Length(w.view.pos), expected_dist, expected_dist * 1e-4f);

    // Olhar para o centro = a direção da câmera aponta contra a posição.
    Vector3 to_center = Vector3Normalize(Vector3Negate(w.view.pos));
    CHECK_NEAR(Vector3DotProduct(cam_forward(w.view), to_center), 1.0f, 1e-4);

    if (tun.orbit_speed != 0.0f) CHECK(Vector3Distance(w.view.pos, w.view_prev.pos) > 0.0f);
}

static void world_same_inputs_same_hash(void)
{
    arena_reset(&arena);
    World a, b, c;
    make_world(&a, 77u);
    make_world(&b, 77u);
    make_world(&c, 77u);

    InputState enter = state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f);
    InputState fly = state(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST), 0u, 2.5f, -0.5f);
    InputState other = state(ACT_BIT(ACT_FORWARD), 0u, 2.5f, -0.5f);

    run_ticks(&a, enter, 1);
    run_ticks(&b, enter, 1);
    run_ticks(&c, enter, 1);
    run_ticks(&a, fly, 300);
    run_ticks(&b, fly, 300);
    run_ticks(&c, other, 300);

    CHECK(world_hash(&a) == world_hash(&b));
    CHECK(world_hash(&a) != world_hash(&c));

    World d;
    make_world(&d, 78u);   // seed diferente
    run_ticks(&d, enter, 1);
    run_ticks(&d, fly, 300);
    CHECK(world_hash(&a) != world_hash(&d));
}

static void world_hash_sees_the_planet(void)
{
    arena_reset(&arena);
    World a, b;
    make_world(&a, 5u);
    make_world(&b, 5u);
    CHECK(world_hash(&a) == world_hash(&b));
    b.planet.volume -= 1;   // um único ponto de densidade a menos
    CHECK(world_hash(&a) != world_hash(&b));
}

static void world_freecam_flies_forward(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    run_ticks(&w, state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f), 1);
    CHECK(w.cam_mode == CAM_FREE);

    Vector3 start = w.view.pos;
    Vector3 forward = cam_forward(w.view);
    int ticks = tun.sim_hz * 3;
    run_ticks(&w, state(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), ticks);

    Vector3 moved = Vector3Subtract(w.view.pos, start);
    float dist = Vector3Length(moved);
    CHECK(dist > 0.0f);
    // Andou na direção em que a câmera olha...
    CHECK_NEAR(Vector3DotProduct(Vector3Normalize(moved), forward), 1.0f, 1e-3);
    // ...e a velocidade chegou perto da de cruzeiro.
    CHECK_NEAR(Vector3Length(w.free_vel), tun.freecam_speed, tun.freecam_speed * 0.01f);
    CHECK(dist <= tun.freecam_speed * 3.0f * 1.001f);

    run_ticks(&w, state(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST), 0u, 0.0f, 0.0f), ticks);
    CHECK_NEAR(Vector3Length(w.free_vel), tun.freecam_speed * tun.freecam_boost,
               tun.freecam_speed * tun.freecam_boost * 0.01f);

    // Soltando tudo, a câmera desacelera até quase parar.
    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), ticks);
    CHECK(Vector3Length(w.free_vel) < tun.freecam_speed * 0.01f);
}

static void world_freecam_mouse_look(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    run_ticks(&w, state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f), 1);

    float yaw0 = w.view.yaw;
    float pitch0 = w.view.pitch;
    run_ticks(&w, state(0u, 0u, 10.0f, 0.0f), 1);
    CHECK_NEAR(w.view.yaw - yaw0, 10.0f * tun.freecam_mouse_sens * DEG2RAD, 1e-5);
    CHECK_NEAR(w.view.pitch, pitch0, 1e-6);

    // Mouse para cima (dy negativo) olha para cima, e o pitch para antes de virar.
    run_ticks(&w, state(0u, 0u, 0.0f, -100000.0f), 1);
    CHECK(w.view.pitch > pitch0);
    CHECK(w.view.pitch < 90.0f * DEG2RAD);
    run_ticks(&w, state(0u, 0u, 0.0f, 100000.0f), 1);
    CHECK(w.view.pitch > -90.0f * DEG2RAD && w.view.pitch < 0.0f);
}

static void world_orbit_ignores_mouse(void)
{
    arena_reset(&arena);
    World a, b;
    make_world(&a, 1u);
    make_world(&b, 1u);
    run_ticks(&a, state(0u, 0u, 0.0f, 0.0f), 60);
    run_ticks(&b, state(ACT_BIT(ACT_FORWARD), 0u, 25.0f, 9.0f), 60);
    CHECK(world_hash(&a) == world_hash(&b));
}

static void world_view_interpolates_between_ticks(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    run_ticks(&w, state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f), 1);
    run_ticks(&w, state(ACT_BIT(ACT_FORWARD), 0u, 40.0f, 0.0f), 30);

    CamPose p0 = world_view(&w, 0.0f);
    CamPose p1 = world_view(&w, 1.0f);
    CamPose mid = world_view(&w, 0.5f);
    CHECK_NEAR(Vector3Distance(p0.pos, w.view_prev.pos), 0.0f, 1e-6);
    CHECK_NEAR(Vector3Distance(p1.pos, w.view.pos), 0.0f, 1e-6);
    CHECK_NEAR(mid.yaw, 0.5f * (w.view_prev.yaw + w.view.yaw), 1e-6);
    CHECK_NEAR(Vector3Distance(mid.pos, Vector3Lerp(w.view_prev.pos, w.view.pos, 0.5f)), 0.0f, 1e-5);
    CHECK(Vector3Distance(p0.pos, p1.pos) > 0.0f);
}

static void world_returning_to_orbit_does_not_interpolate_the_jump(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    run_ticks(&w, state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f), 1);
    run_ticks(&w, state(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST), 0u, 0.0f, 0.0f), 120);
    run_ticks(&w, state(0u, ACT_BIT(ACT_FREECAM), 0.0f, 0.0f), 1);

    CHECK(w.cam_mode == CAM_ORBIT);
    CHECK_NEAR(Vector3Distance(w.view.pos, w.view_prev.pos), 0.0f, 1e-6);
    CHECK_NEAR(w.view.yaw, w.view_prev.yaw, 1e-6);
}

static void world_place_camera_looks_at_the_target(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);

    Vector3 positions[4] = { { 0, 40, 190 }, { -120, -30, 15 }, { 60, 5, -80 }, { 10, 150, 20 } };
    Vector3 targets[4] = { { 0, 0, 0 }, { 0, 0, 0 }, { 20, 30, 0 }, { 0, 0, 0 } };
    for (int i = 0; i < 4; i++) {
        world_place_camera(&w, positions[i], targets[i]);
        CHECK(w.cam_mode == CAM_FREE);
        CHECK_NEAR(Vector3Distance(w.view.pos, positions[i]), 0.0f, 1e-6);
        CHECK_NEAR(Vector3Distance(w.view.pos, w.view_prev.pos), 0.0f, 1e-6);   // sem interpolar o salto
        Vector3 wanted = Vector3Normalize(Vector3Subtract(targets[i], positions[i]));
        CHECK_NEAR(Vector3DotProduct(cam_forward(w.view), wanted), 1.0f, 1e-4);
    }

    // Parada, a câmera fica onde foi posta.
    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 30);
    CHECK_NEAR(Vector3Distance(w.view.pos, positions[3]), 0.0f, 1e-4);

    // Posição igual ao alvo não gera direção inválida.
    world_place_camera(&w, positions[0], positions[0]);
    CHECK(w.view.yaw == w.view.yaw && w.view.pitch == w.view.pitch);   // não é NaN
}

static void world_apply_tuning_regenerates_only_when_the_recipe_changes(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    int64_t volume = w.planet.volume;

    Tuning changed = tun;
    changed.orbit_speed += 1.0f;          // nada a ver com o planeta
    CHECK(!world_apply_tuning(&w, &changed));
    CHECK(w.planet.volume == volume);

    changed.planet_seed += 1;             // outro desenho de continentes
    CHECK(world_apply_tuning(&w, &changed));
    CHECK(w.planet.volume != volume);
    CHECK(w.planet.initial_volume == w.planet.volume);

    changed.planet_grid = 128;            // o tamanho da grade só vale ao iniciar
    CHECK(!world_apply_tuning(&w, &changed));
    CHECK(w.planet.size == TEST_GRID);
}

static void world_impact_digs_and_is_recorded(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    int64_t volume = w.planet.volume;
    Vector3 above = { 0.0f, 100.0f, 0.0f };
    Vector3 down = { 0.0f, -1.0f, 0.0f };

    CHECK(world_impact(&w, &tun, above, down, 4.0f, 3.0f));
    CHECK(w.planet.volume < volume);
    CHECK(w.impact_count == 1u);
    CHECK(w.particles.count > 0);   // destroços no ar

    const Impact *impact = world_find_impact(&w, 1u);
    CHECK(impact != NULL);
    if (impact != NULL) {
        CHECK(impact->id == 1u);
        CHECK_NEAR(impact->radius, 4.0f, 1e-6);
        CHECK_NEAR(impact->pos.x, 0.0f, 0.05f);
        CHECK(impact->pos.y > w.planet.params.radius - 1.0f);   // foi na superfície
    }

    // Um raio que não acerta o planeta não faz nada.
    volume = w.planet.volume;
    CHECK(!world_impact(&w, &tun, above, (Vector3){ 0.0f, 1.0f, 0.0f }, 4.0f, 3.0f));
    CHECK(w.planet.volume == volume && w.impact_count == 1u);

    CHECK(world_find_impact(&w, 0u) == NULL);
    CHECK(world_find_impact(&w, 2u) == NULL);   // ainda não aconteceu
}

static void world_remembers_only_recent_impacts(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 3u);
    Rng dirs;
    rng_seed(&dirs, 21u, 1u);

    uint32_t total = WORLD_IMPACT_HISTORY + 7;
    for (int tries = 0; tries < 1000 && w.impact_count < total; tries++) {
        Vector3 dir = rng_unit_vec3(&dirs);
        world_impact(&w, &tun, Vector3Scale(dir, 100.0f), Vector3Negate(dir), 1.5f, 1.0f);
    }
    CHECK(w.impact_count == total);

    CHECK(world_find_impact(&w, 1u) == NULL);       // o mais antigo já saiu da fila
    CHECK(world_find_impact(&w, total + 1u) == NULL);
    for (uint32_t id = total - WORLD_IMPACT_HISTORY + 1u; id <= total; id++) {
        const Impact *impact = world_find_impact(&w, id);
        CHECK(impact != NULL && impact->id == id);
    }
}

static void world_particles_fly_and_die(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    world_impact(&w, &tun, (Vector3){ 0.0f, 100.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, 5.0f, 4.0f);
    int born = w.particles.count;
    CHECK(born > 10);
    CHECK(w.particles.peak == born);

    // Acha uma pedra e confere que ela sai do lugar.
    int rock = -1;
    for (int i = 0; i < w.particles.count; i++) {
        if (w.particles.items[i].kind == PARTICLE_ROCK) { rock = i; break; }
    }
    CHECK(rock >= 0);
    if (rock < 0) return;
    Vector3 before = w.particles.items[rock].pos;
    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 1);
    CHECK(Vector3Distance(w.particles.items[rock].pos, before) > 0.0f);

    // Nenhuma posição vira um número inválido, e nenhuma partícula escapa
    // para longe: a gravidade traz as pedras de volta.
    bool sane = true;
    for (int tick = 0; tick < tun.sim_hz * 12; tick++) {
        run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 1);
        for (int i = 0; i < w.particles.count; i++) {
            float distance = Vector3Length(w.particles.items[i].pos);
            if (!(distance < 500.0f)) sane = false;   // também pega NaN
        }
    }
    CHECK(sane);
    CHECK(w.particles.count == 0);   // todas morreram (de velhice ou ao cair no chão)
    CHECK(w.particles.peak == born);
}

static void world_particles_never_overflow(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    Rng dirs;
    rng_seed(&dirs, 5u, 1u);

    // Muito mais destroços do que cabem no array.
    for (int i = 0; i < 400; i++) {
        Vector3 dir = rng_unit_vec3(&dirs);
        world_impact(&w, &tun, Vector3Scale(dir, 100.0f), Vector3Negate(dir), 6.0f, 2.0f);
    }
    CHECK(w.particles.count <= w.particles.capacity);
    CHECK(w.particles.capacity == PARTICLES_CAPACITY);
    CHECK(particles_spawn(&w.particles) == NULL || w.particles.count <= w.particles.capacity);

    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 30);   // continua simulando sem problema
    CHECK(w.particles.count <= w.particles.capacity);
}

static void world_collapses_when_the_planet_loses_too_much_mass(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    CHECK(!w.collapsed);
    Rng dirs;
    rng_seed(&dirs, 8u, 1u);

    for (int i = 0; i < 2000 && !w.collapsed; i++) {
        Vector3 dir = rng_unit_vec3(&dirs);
        world_impact(&w, &tun, Vector3Scale(dir, 100.0f), Vector3Negate(dir), 8.0f, 7.0f);
    }
    CHECK(w.collapsed);
    CHECK(planet_hp(&w.planet) < tun.defeat_planet_hp || planet_core_hp(&w.planet) < tun.defeat_core_hp);

    // O colapso não se desfaz sozinho...
    run_ticks(&w, state(0u, 0u, 0.0f, 0.0f), 10);
    CHECK(w.collapsed);

    // ...só com a ferramenta de debug que restaura o planeta.
    world_reset_planet(&w, &tun);
    CHECK(!w.collapsed);
    CHECK_NEAR(planet_hp(&w.planet), 1.0f, 1e-6);
    CHECK_NEAR(planet_core_hp(&w.planet), 1.0f, 1e-6);
    CHECK(w.particles.count == 0);
}

static void world_collapses_when_the_core_is_destroyed(void)
{
    arena_reset(&arena);
    World w;
    make_world(&w, 1u);
    Vector3 above = { 0.0f, 100.0f, 0.0f };
    Vector3 down = { 0.0f, -1.0f, 0.0f };

    // Um túnel reto, sempre no mesmo ponto, até atravessar o núcleo.
    for (int i = 0; i < 40 && !w.collapsed; i++) world_impact(&w, &tun, above, down, 4.0f, 4.0f);

    CHECK(w.collapsed);
    CHECK(planet_core_hp(&w.planet) < tun.defeat_core_hp);
    CHECK(planet_hp(&w.planet) > tun.defeat_planet_hp);   // ainda sobrava massa de sobra
}

void suite_world(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(world_starts_in_orbit_with_a_whole_planet);
    RUN(world_init_fails_without_memory);
    RUN(world_orbit_circles_and_faces_the_center);
    RUN(world_same_inputs_same_hash);
    RUN(world_hash_sees_the_planet);
    RUN(world_freecam_flies_forward);
    RUN(world_freecam_mouse_look);
    RUN(world_orbit_ignores_mouse);
    RUN(world_view_interpolates_between_ticks);
    RUN(world_returning_to_orbit_does_not_interpolate_the_jump);
    RUN(world_place_camera_looks_at_the_target);
    RUN(world_apply_tuning_regenerates_only_when_the_recipe_changes);
    RUN(world_impact_digs_and_is_recorded);
    RUN(world_remembers_only_recent_impacts);
    RUN(world_particles_fly_and_die);
    RUN(world_particles_never_overflow);
    RUN(world_collapses_when_the_planet_loses_too_much_mass);
    RUN(world_collapses_when_the_core_is_destroyed);
    arena_free(&arena);
}
