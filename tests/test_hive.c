// Testes do marco 11: a Colmeia Lunar, os esporos e a Ninhada.
#include "test.h"
#include "test_util.h"

#include "core/input.h"
#include "game/combat.h"
#include "game/world.h"

#define CORE_ID (HIVE_SACS + HIVE_THRUSTERS)

static Tuning tun;
static Arena  arena;

static InputState keys(uint32_t held)
{
    InputState in = { held, 0u, 0u, 0.0f, 0.0f };
    return in;
}

static InputState press(uint32_t bit)
{
    InputState in = { bit, bit, 0u, 0.0f, 0.0f };
    return in;
}

static void run(World *w, const Tuning *t, InputState in, int ticks)
{
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

// Mundo sem hordas, a nave longe (do outro lado do planeta) e a Colmeia chegando.
static void make_world(World *w, const Tuning *t)
{
    arena_reset(&arena);
    if (!world_init(w, t, 21u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    Vector3 far = Vector3Scale(Vector3Normalize(w->moon.pos), -w->planet.params.radius * 5.0f);
    world_place_ship(w, t, far, Vector3Add(far, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    world_spawn_boss(w, t, BOSS_HIVE, (Vector3){ 0.0f, 1.0f, 0.0f });
}

// Passa a chegada e o brotar das bolsas: a Colmeia começa a fazer ninhadas.
static void until_nesting(World *w, const Tuning *t)
{
    for (int i = 0; i < seconds(t->hive_enter_time + t->hive_sac_regrow + 2.0f) && w->boss.action != HIVE_NEST; i++) {
        run(w, t, keys(0u), 1);
    }
}

static void hive_grabs_the_moon(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    CHECK(w.boss.state == BOSS_ENTER && saw_event(&w, EV_BOSS_ENTER));
    until_nesting(&w, &t);
    CHECK(w.boss.state == BOSS_FIGHT && w.boss.action == HIVE_NEST);
    CHECK(Vector3Distance(w.boss.pos, w.moon.pos) < 0.01f);
    for (int k = 0; k < HIVE_SACS; k++) CHECK(w.boss.hive.sac_grow[k] >= 1.0f);
    CHECK(hive_sacs_left(&w.boss) == HIVE_SACS);
    CHECK(boss_core_factor(&w.boss, &t) < 0.1f);   // coração fechado
}

// Estouradas todas as bolsas, o coração abre e leva o dano inteiro; depois fecha
// e as bolsas brotam de novo.
static void popping_every_sac_opens_the_heart(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    for (int k = 0; k < HIVE_SACS; k++) CHECK(boss_hit(&w.boss, &t, k, 1000.0f, &w.events, (uint32_t)w.tick) > 0.0f);
    CHECK(w.boss.action == HIVE_OPEN && saw_event(&w, EV_BOSS_STAGGER));
    CHECK_NEAR(boss_core_factor(&w.boss, &t), 1.0f, 1e-6);
    float hp = w.boss.hp;
    CHECK_NEAR(boss_hit(&w.boss, &t, CORE_ID, 10.0f, &w.events, (uint32_t)w.tick), 10.0f, 1e-4);
    CHECK_NEAR(w.boss.hp, hp - 10.0f, 1e-3);
    // Bolsas estouradas deixam esporos (uma a cada duas).
    run(&w, &t, keys(0u), 1);
    CHECK(rocks_count(&w.rocks, ROCK_SPORE) > 0);

    run(&w, &t, keys(0u), seconds(t.hive_open_time + 0.5f));
    CHECK(w.boss.action == HIVE_REGROW);
    CHECK(hive_sacs_left(&w.boss) == HIVE_SACS);
    CHECK(boss_core_factor(&w.boss, &t) < 0.1f);
}

// Uma bolsa incha e solta drones; estourada antes, a ninhada morre junto.
static void inflating_sac_births_drones_unless_popped(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    for (int i = 0; i < seconds(10.0f) && w.boss.hive.inflating < 0; i++) run(&w, &t, keys(0u), 1);
    CHECK(w.boss.hive.inflating >= 0 && saw_event(&w, EV_HIVE_INFLATE));
    int drones = w.enemies.alive_by_type[ENEMY_SWARM];
    run(&w, &t, keys(0u), seconds(t.hive_inflate_time + 0.2f));
    CHECK(w.enemies.alive_by_type[ENEMY_SWARM] == drones + t.hive_brood);
    CHECK(saw_event(&w, EV_HIVE_BIRTH));

    // A próxima: estourada antes de soltar.
    for (int i = 0; i < seconds(10.0f) && w.boss.hive.inflating < 0; i++) run(&w, &t, keys(0u), 1);
    int k = w.boss.hive.inflating;
    CHECK(k >= 0);
    drones = w.enemies.alive_by_type[ENEMY_SWARM];
    CHECK(boss_hit(&w.boss, &t, k, 1000.0f, &w.events, (uint32_t)w.tick) > 0.0f);
    CHECK(w.boss.hive.inflating < 0);
    run(&w, &t, keys(0u), seconds(t.hive_inflate_time + 0.2f));
    CHECK(w.enemies.alive_by_type[ENEMY_SWARM] <= drones);
}

// Fase 2: os propulsores empurram a Lua contra a Terra; perto, o tentáculo
// morde a crosta. Destruídos os propulsores, a Lua volta.
static void thrusters_push_the_moon(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    float distance = Vector3Length(w.moon.pos);
    boss_set_health(&w.boss, &t, 0.6f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.phase == 2);
    run(&w, &t, keys(0u), seconds(5.0f));
    CHECK(w.boss.hive.thr_born && w.boss.hive.pushing);
    CHECK(w.moon.scale < 1.0f && Vector3Length(w.moon.pos) < distance);

    // Com a Lua no ponto mais perto, o tentáculo arranca a crosta.
    w.boss.hive.moon_scale = t.hive_moon_min;
    int64_t volume = w.planet.volume;
    run(&w, &t, keys(0u), seconds(4.0f));
    CHECK(w.boss.hive.tide > 0.9f);
    CHECK(w.planet.volume < volume);

    for (int k = 0; k < HIVE_THRUSTERS; k++) {
        CHECK(boss_hit(&w.boss, &t, HIVE_SACS + k, 1000.0f, &w.events, (uint32_t)w.tick) > 0.0f);
    }
    CHECK(hive_thrusters_left(&w.boss) == 0 && saw_event(&w, EV_HIVE_THRUSTER));
    float scale = w.moon.scale;
    run(&w, &t, keys(0u), seconds(3.0f));
    CHECK(!w.boss.hive.pushing && w.moon.scale > scale);
}

// No fim da fase 3 o coração fica entreaberto: passa uma fração maior do dano.
static void heart_half_open_in_phase_three(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    boss_set_health(&w.boss, &t, 0.2f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.phase == 3);
    CHECK_NEAR(boss_hit(&w.boss, &t, CORE_ID, 10.0f, &w.events, (uint32_t)w.tick), 10.0f * t.hive_core_open, 1e-4);

    // Tiros de verdade, de frente para o coração, também o ferem.
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_parts(&w.boss, &t, parts);
    Vector3 core = { 0 }, facing = { 0 };
    for (int i = 0; i < count; i++) {
        if (parts[i].role == PART_CORE) {
            core = parts[i].center;
            facing = Vector3Normalize(parts[i].facing);
        }
    }
    float hp = w.boss.hp;
    Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(core, Vector3Scale(facing, 15.0f)),
                                Vector3Scale(facing, -t.weapon_speed), 1.0f);
    s->damage = 10.0f;
    for (int i = 0; i < 6; i++) {
        projectiles_move(&w.shots, 1.0f / (float)t.sim_hz);
        combat_resolve(&w, &t);
    }
    CHECK_NEAR(w.boss.hp, hp - 10.0f * t.hive_core_open, 1e-3);
}

// Um esporo pego pelo raio trator e arremessado fere o coração fechado.
static void thrown_spore_hurts_the_heart(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    w.boss.hive.inflate_wait = 100.0f;   // sem ninhadas atrapalhando
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_parts(&w.boss, &t, parts);
    for (int i = 0; i < count; i++) {
        if (parts[i].role != PART_CORE) continue;
        Vector3 facing = Vector3Normalize(parts[i].facing);
        world_place_ship(&w, &t, Vector3Add(parts[i].center, Vector3Scale(facing, 22.0f)), parts[i].center, 0.0f);
    }
    Vector3 ahead = Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 6.0f));
    CHECK(world_spawn_rock(&w, &t, ROCK_SPORE, ahead, (Vector3){ 0.0f, 0.0f, 0.0f }) != NULL);
    run(&w, &t, press(ACT_BIT(ACT_TRACTOR)), 1);
    CHECK(rocks_towed(&w.rocks) != NULL);
    run(&w, &t, keys(0u), seconds(0.4f));
    float hp = w.boss.hp;
    run(&w, &t, press(ACT_BIT(ACT_TRACTOR)), 1);
    for (int i = 0; i < seconds(2.0f) && !saw_event(&w, EV_SPORE_BLAST); i++) run(&w, &t, keys(0u), 1);
    CHECK(saw_event(&w, EV_SPORE_BLAST));
    CHECK(w.boss.hp <= hp - t.hive_spore_damage + 0.01f);
}

// Morta, a Colmeia solta a Lua e dá a Ninhada; a tecla G solta os ferrões.
static void hive_death_gives_the_brood(void)
{
    static World w;
    Tuning t = tun;
    make_world(&w, &t);
    until_nesting(&w, &t);
    for (int k = 0; k < HIVE_SACS; k++) boss_hit(&w.boss, &t, k, 1000.0f, &w.events, (uint32_t)w.tick);
    boss_hit(&w.boss, &t, CORE_ID, 1e6f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.state == BOSS_DYING);
    run(&w, &t, keys(0u), seconds(4.5f));
    CHECK(!boss_active(&w.boss) && w.brood.owned && saw_event(&w, EV_REWARD));
    // O XP do boss abre as cartas de evolução (a partida congela até escolher).
    for (int i = 0; i < 10 && (w.evo.choosing || w.evo.pending > 0); i++) {
        run(&w, &t, keys(0u), seconds(0.6f));
        run(&w, &t, press(ACT_BIT(ACT_CARD1)), 1);
    }

    world_spawn(&w, &t, ENEMY_SWARM, 4, Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 40.0f)));
    run(&w, &t, press(ACT_BIT(ACT_ABILITY2)), 1);
    CHECK(saw_event(&w, EV_BROOD) && w.brood.cooldown > 0.0f);
    CHECK(projectiles_count(&w.shots, SHOT_BOLT) >= t.brood_count);
}

void suite_hive(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(hive_grabs_the_moon);
    RUN(popping_every_sac_opens_the_heart);
    RUN(inflating_sac_births_drones_unless_popped);
    RUN(thrusters_push_the_moon);
    RUN(heart_half_open_in_phase_three);
    RUN(thrown_spore_hurts_the_heart);
    RUN(hive_death_gives_the_brood);
    arena_free(&arena);
}
