// Testes do marco 11: o Devorador, o chefe final.
#include "test.h"
#include "test_util.h"

#include "game/combat.h"
#include "game/world.h"

#define ID_EYE    0
#define ID_THROAT 2

static Tuning tun;
static Arena  arena;

static InputState idle(void)
{
    InputState in = { 0u, 0u, 0u, 0.0f, 0.0f };
    return in;
}

static void run(World *w, const Tuning *t, int ticks)
{
    InputState in = idle();
    for (int i = 0; i < ticks; i++) world_tick(w, &in, t);
}

static int seconds(float s)
{
    return (int)(s * (float)tun.sim_hz);
}

static bool saw_event(const World *w, EventType type)
{
    for (uint32_t id = 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e != NULL && e->type == type) return true;
    }
    return false;
}

// O planeta dos testes é pequeno (raio ~17): o Devorador encolhe junto.
static Tuning small(void)
{
    Tuning t = tun;
    t.dev_scale = 1.5f;
    t.dev_altitude = 8.0f;
    t.dev_aim_altitude = 5.0f;
    t.dev_spawn_distance = 3.0f;
    t.dev_speed = 4.0f;
    t.dev_bite_radius = 3.0f;
    t.dev_bite_depth = 2.0f;
    t.dev_vortex_range = 10.0f;
    t.dev_latch_radius = 1.5f;
    return t;
}

// Mundo sem hordas, a nave longe (bem abaixo do planeta) e o Devorador chegando por +y.
static void make_world(World *w, const Tuning *t)
{
    arena_reset(&arena);
    if (!world_init(w, t, 41u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    Vector3 far = { 0.0f, -w->planet.params.radius * 6.0f, 0.0f };
    world_place_ship(w, t, far, Vector3Add(far, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    world_spawn_boss(w, t, BOSS_DEVOURER, (Vector3){ 0.0f, 1.0f, 0.0f });
}

static void until_action(World *w, const Tuning *t, int action, float limit)
{
    for (int i = 0; i < seconds(limit) && !(w->boss.state == BOSS_FIGHT && w->boss.action == action); i++) run(w, t, 1);
}

static Vector3 bite_ground(const World *w)
{
    Terrain terrain = planet_terrain(&w->planet.params, w->boss.dev.bite_dir);
    return Vector3Scale(w->boss.dev.bite_dir, w->planet.params.radius + (terrain.land ? terrain.elevation : 0.0f));
}

// Chega, ronda e, antes de morder, marca o ponto (o aviso); a mordida abre
// uma cratera e ele mastiga com a garganta exposta.
static void aim_warns_then_bites(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    CHECK(w.boss.state == BOSS_ENTER);
    run(&w, &t, seconds(t.dev_enter_time + 0.5f));
    CHECK(w.boss.state == BOSS_FIGHT && w.boss.action == DEV_PROWL);
    CHECK(boss_core_factor(&w.boss, &t) < 0.1f);   // boca fechada na ronda

    until_action(&w, &t, DEV_AIM, 20.0f);
    CHECK(w.boss.action == DEV_AIM && saw_event(&w, EV_DEV_AIM));
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_parts(&w.boss, &t, parts);
    CHECK(count > 2 && parts[ID_EYE].open);   // os olhos acesos: o ponto fraco do aviso

    int64_t volume = w.planet.volume;
    until_action(&w, &t, DEV_CHEW, t.dev_aim_time + t.dev_dive_time + 1.0f);
    CHECK(w.boss.action == DEV_CHEW && saw_event(&w, EV_DEV_BITE));
    CHECK(w.planet.volume < volume);
    CHECK(planet_sample(&w.planet, bite_ground(&w)) < VOXEL_ISO);   // o chão sumiu onde ele mordeu
    CHECK_NEAR(boss_core_factor(&w.boss, &t), 1.0f, 1e-6);
    float hp = w.boss.hp;
    CHECK_NEAR(boss_hit(&w.boss, &t, ID_THROAT, 10.0f, &w.events, (uint32_t)w.tick), 10.0f, 1e-4);
    CHECK_NEAR(w.boss.hp, hp - 10.0f, 1e-3);

    // Mastigando, ele ergue a cabeça: a boca (e a garganta) não fica enfiada
    // no chão. (A nave está do outro lado do planeta: ele olha para o alto.)
    run(&w, &t, seconds(1.0f));
    CHECK(w.boss.action == DEV_CHEW);
    Vector3 mouth_dir = Vector3RotateByQuaternion((Vector3){ 0.0f, 0.0f, -1.0f }, w.boss.rot);
    CHECK(Vector3DotProduct(mouth_dir, Vector3Normalize(w.boss.pos)) > 0.5f);
    CHECK(planet_sample(&w.planet, boss_point(&w.boss, &t, DEV_THROAT)) < VOXEL_ISO);
}

// Mordidas repetidas no mesmo ponto não cavam mais fundo (medem do relevo
// original) e não soltam fragmentos (o pedaço vai para a boca).
static void bites_do_not_dig_deeper(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    Vector3 dir = { 0.0f, 1.0f, 0.0f };
    Terrain terrain = planet_terrain(&w.planet.params, dir);
    Vector3 ground = Vector3Scale(dir, w.planet.params.radius + (terrain.land ? terrain.elevation : 0.0f));
    int fragments = rocks_count(&w.rocks, ROCK_FRAGMENT);
    CHECK(world_impact_at(&w, &t, ground, t.dev_bite_radius, t.dev_bite_depth, false));
    int64_t after_one = w.planet.volume;
    world_impact_at(&w, &t, ground, t.dev_bite_radius, t.dev_bite_depth, false);
    CHECK(w.planet.volume == after_one);
    CHECK(rocks_count(&w.rocks, ROCK_FRAGMENT) == fragments);
}

// Dano bastante nos olhos durante o aviso: ele erra (atordoado) e o planeta fica inteiro.
static void eyes_make_it_miss(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    until_action(&w, &t, DEV_AIM, 25.0f);
    CHECK(w.boss.action == DEV_AIM);
    int64_t volume = w.planet.volume;
    CHECK(boss_hit(&w.boss, &t, ID_EYE, t.dev_flinch, &w.events, (uint32_t)w.tick) > 0.0f);
    run(&w, &t, 1);
    CHECK(w.boss.action == DEV_STUN && saw_event(&w, EV_DEV_MISS));
    CHECK_NEAR(boss_core_factor(&w.boss, &t), 1.0f, 1e-6);   // atordoado, a garganta fica exposta
    run(&w, &t, seconds(t.dev_flinch_stun + 0.5f));
    CHECK(w.planet.volume == volume);
    CHECK(!saw_event(&w, EV_DEV_BITE));
}

// Um escudo local no ponto da mordida quebra os dentes dele: atordoado por mais tempo, sem cratera.
static void shield_breaks_the_teeth(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    until_action(&w, &t, DEV_AIM, 25.0f);
    CHECK(w.boss.action == DEV_AIM);
    Vector3 ground = bite_ground(&w);
    Vector3 above = Vector3Add(ground, Vector3Scale(w.boss.dev.bite_dir, 3.0f));
    CHECK(defenses_build(&w.defenses, &t, DEF_SHIELD, above, Vector3Perpendicular(w.boss.dev.bite_dir), w.planet.params.radius,
                         &w.rng) != NULL);
    int64_t volume = w.planet.volume;
    for (int i = 0; i < seconds(t.dev_aim_time + t.dev_dive_time + 1.0f) && !saw_event(&w, EV_DEV_BROKEN); i++) run(&w, &t, 1);
    CHECK(saw_event(&w, EV_DEV_BROKEN) && !saw_event(&w, EV_DEV_BITE));
    CHECK(w.boss.action == DEV_STUN);
    CHECK_NEAR(w.boss.timer, t.dev_broken_stun, 0.05);
    CHECK(w.planet.volume == volume);
}

// O redemoinho puxa uma rocha para a boca; engolida, ela cura o Devorador.
static void vortex_swallows_rocks_and_heals(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    run(&w, &t, seconds(t.dev_enter_time + 0.5f));
    boss_set_health(&w.boss, &t, 0.5f, &w.events, (uint32_t)w.tick);
    float hp = w.boss.hp;
    Vector3 mouth = devourer_mouth(&w.boss, &t);
    Vector3 near = Vector3Add(mouth, Vector3Scale(Vector3Normalize(mouth), t.dev_vortex_range * 0.5f));
    CHECK(world_spawn_rock(&w, &t, ROCK_METEOR, near, (Vector3){ 0.0f, 0.0f, 0.0f }) != NULL);
    for (int i = 0; i < seconds(8.0f) && w.boss.dev.eaten == 0; i++) run(&w, &t, 1);
    CHECK(w.boss.dev.eaten == 1 && saw_event(&w, EV_DEV_SWALLOW));
    CHECK(w.boss.hp > hp);
    CHECK_NEAR(w.boss.hp, hp + t.dev_heal_meteor * w.boss.hp_max, 1e-2);
}

// O banquete: com o planeta abaixo do limite, ele se agarra e devora sem
// parar; dano bastante na garganta o arranca de lá.
static void feast_latches_until_torn(void)
{
    static World w;
    Tuning t = small();
    t.dev_frenzy_hp = 1.02f;   // o planeta inteiro já conta como "ferido"
    t.dev_feast_hp = 1.01f;
    make_world(&w, &t);
    until_action(&w, &t, DEV_LATCH, t.dev_enter_time + 2.0f);
    CHECK(w.boss.action == DEV_LATCH && saw_event(&w, EV_DEV_LATCH));
    CHECK(w.boss.phase == DEV_FEAST);
    CHECK_NEAR(boss_core_factor(&w.boss, &t), 1.0f, 1e-6);
    int64_t volume = w.planet.volume;
    run(&w, &t, seconds(2.0f));
    CHECK(w.planet.volume < volume);   // devorando

    boss_hit(&w.boss, &t, ID_THROAT, t.dev_latch_break * w.boss.hp_max + 1.0f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.action == DEV_STUN && saw_event(&w, EV_DEV_TORN));
    CHECK(w.boss.dev.latch_cooldown > 0.0f);
    // Passado o atordoamento, ele não volta a se agarrar antes da recarga.
    run(&w, &t, seconds(t.dev_torn_stun + t.dev_rise_time + 0.5f));
    CHECK(w.boss.action != DEV_LATCH);
}

// Morto, ele devolve rochas, que caem sobre as crateras e consertam o planeta.
static void death_rains_rocks_on_the_scars(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    run(&w, &t, seconds(t.dev_enter_time + 0.5f));
    Vector3 side = { w.planet.params.radius * 3.0f, 0.0f, 0.0f };
    world_impact(&w, &t, side, (Vector3){ -1.0f, 0.0f, 0.0f }, 4.0f, 3.0f);
    int64_t hurt = w.planet.volume;
    boss_set_health(&w.boss, &t, 0.0f, &w.events, (uint32_t)w.tick);
    for (int i = 0; i < seconds(6.0f) && boss_active(&w.boss); i++) run(&w, &t, 1);
    CHECK(!boss_active(&w.boss));
    int falling = 0;
    for (int i = 0; i < ROCKS_MAX; i++) falling += (w.rocks.items[i].alive && w.rocks.items[i].state == ROCK_FALLING) ? 1 : 0;
    CHECK(falling > 0);
    int delivered = w.rocks.delivered;
    run(&w, &t, seconds(20.0f));
    CHECK(w.rocks.delivered > delivered);
    CHECK(w.planet.volume > hurt);
}

void suite_devourer(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(aim_warns_then_bites);
    RUN(bites_do_not_dig_deeper);
    RUN(eyes_make_it_miss);
    RUN(shield_breaks_the_teeth);
    RUN(vortex_swallows_rocks_and_heals);
    RUN(feast_latches_until_torn);
    RUN(death_rains_rocks_on_the_scars);
    arena_free(&arena);
}
