// Testes dos tipos de inimigo do marco 6: Kamikaze, Escavador, Sifão,
// Atirador, Escudeiro e Porta-drones (Enxame e Bombardeiro estão em
// test_combat.c).
#include "test.h"
#include "test_util.h"

#include <stdio.h>

#include "core/input.h"
#include "game/combat.h"
#include "game/world.h"
#include "tools/autopilot.h"
#include "voxel/damage.h"

static Tuning tun;
static Arena  arena;
static float  dt;

static InputState idle(void)
{
    InputState in = { 0u, 0u, 0u, 0.0f, 0.0f };
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

// Mundo sem hordas, com a nave parada (os estabilizadores a seguram) em `ship_at`.
static void make_world(World *w, Vector3 ship_at)
{
    arena_reset(&arena);
    if (!world_init(w, &tun, 11u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    world_place_ship(w, &tun, ship_at, Vector3Add(ship_at, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
}

// Mundo com a nave parada longe de tudo, na direção `dir` (a 5 raios do planeta).
static void make_world_far(World *w, Vector3 dir)
{
    make_world(w, (Vector3){ 0.0f, 0.0f, 1.0f });
    Vector3 spot = Vector3Scale(Vector3Normalize(dir), w->planet.params.radius * 5.0f);
    world_place_ship(w, &tun, spot, Vector3Add(spot, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
}

static int find_type(const World *w, EnemyType type)
{
    for (int i = 0; i < w->enemies.pool.capacity; i++) {
        if (pool_alive(&w->enemies.pool, i) && w->enemies.items[i].type == type) return i;
    }
    return -1;
}

static int count_events(const World *w, EventType type)
{
    int n = 0;
    for (uint32_t id = 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e != NULL && e->type == type) n++;
    }
    return n;
}

static Tuning exact(void)
{
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;   // o primeiro de um grupo nasce no centro; aqui, todos
    return t;
}

// ---------------------------------------------------------------------------

static void kamikaze_hunts_the_ship_and_detonates(void)
{
    static World w;
    Vector3 ship_at = { 0.0f, 0.0f, 60.0f };
    make_world(&w, ship_at);
    Tuning t = exact();
    world_spawn(&w, &t, ENEMY_KAMIKAZE, 1, Vector3Add(ship_at, (Vector3){ 50.0f, 0.0f, 0.0f }));
    float shield = w.ship.shield;

    for (int i = 0; i < seconds(6.0f) && enemies_alive(&w.enemies) > 0; i++) run(&w, &t, idle(), 1);
    CHECK(count_events(&w, EV_LOCK) == 1);
    CHECK(count_events(&w, EV_DETONATE) == 1);
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(w.ship.shield < shield);   // a explosão pegou a nave
    CHECK(w.score.points == 0);      // e não vale pontos
}

static void kamikaze_dives_into_the_planet(void)
{
    static World w;
    Vector3 dir = { 0.3f, 1.0f, 0.1f };
    make_world_far(&w, Vector3Negate(dir));
    int64_t volume = w.planet.volume;
    world_spawn(&w, &tun, ENEMY_KAMIKAZE, 1, Vector3Scale(Vector3Normalize(dir), w.planet.params.radius * 2.5f));

    for (int i = 0; i < seconds(8.0f) && enemies_alive(&w.enemies) > 0; i++) run(&w, &tun, idle(), 1);
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(count_events(&w, EV_CRASH) == 1 && count_events(&w, EV_LOCK) == 0);
    CHECK(w.planet.volume < volume);
    const Impact *impact = world_find_impact(&w, w.impact_count);
    CHECK(impact != NULL);
    if (impact != NULL) {
        CHECK_NEAR(impact->radius, tun.kamikaze_crater_radius, 1e-4);
        CHECK(Vector3DotProduct(Vector3Normalize(impact->pos), Vector3Normalize(dir)) > 0.95f);
    }
}

static void digger_tunnels_toward_the_core(void)
{
    static World w;
    Vector3 dir = { 0.0f, 1.0f, 0.0f };
    make_world_far(&w, (Vector3){ 0.0f, -1.0f, 0.0f });
    float radius = w.planet.params.radius;
    int64_t volume = w.planet.volume;
    world_spawn(&w, &tun, ENEMY_DIGGER, 1, Vector3Scale(dir, radius * 2.0f));
    int index = find_type(&w, ENEMY_DIGGER);
    Enemy *en = &w.enemies.items[index];

    run(&w, &tun, idle(), seconds(5.0f));
    CHECK(count_events(&w, EV_DIG_START) == 1);
    CHECK(en->state == DIGGER_DRILL);
    float early = digger_progress(en, &w.planet);

    run(&w, &tun, idle(), seconds(4.0f));
    float later = digger_progress(en, &w.planet);
    CHECK(later > early + 0.1f);                        // desce
    CHECK(en->hp > 0.0f);                               // vivo lá dentro
    CHECK(w.planet.volume < volume);
    CHECK(Vector3Length(en->pos) < radius - 2.0f);      // abaixo da superfície
    // O túnel atrás dele fica aberto: dá para a nave entrar.
    Vector3 mouth = Vector3Scale(dir, radius - 1.0f);
    Vector3 behind = Vector3Add(en->pos, Vector3Scale(en->target, 2.0f));
    CHECK(planet_sample(&w.planet, mouth) < VOXEL_ISO);
    CHECK(planet_sample(&w.planet, behind) < VOXEL_ISO);
    CHECK(count_events(&w, EV_CRASH) == 0);

    // Dentro do túnel, um tiro o acerta.
    float hp = en->hp;
    Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(en->pos, Vector3Scale(en->target, 4.0f)),
                                Vector3Scale(en->target, -tun.weapon_speed), 2.0f);
    s->damage = tun.weapon_damage;
    run(&w, &tun, idle(), 3);
    CHECK(en->hp < hp);
}

// O piloto automático (ferramenta de teste) derruba um Escavador que já está
// no túnel: para sobre a boca e atira reto para baixo, como um jogador faria.
static void autopilot_shoots_down_the_tunnel(void)
{
    static World w;
    // O planeta dos testes é pequeno: o Escavador chega ao núcleo em poucos
    // segundos. Aqui a partida não termina por isso, para medir só a caçada.
    Tuning t = tun;
    t.defeat_core_hp = 0.0f;
    make_world(&w, (Vector3){ 30.0f, 40.0f, 0.0f });
    world_spawn(&w, &t, ENEMY_DIGGER, 1, (Vector3){ 0.0f, w.planet.params.radius * 2.0f, 0.0f });
    Enemy *en = &w.enemies.items[find_type(&w, ENEMY_DIGGER)];
    for (int i = 0; i < seconds(8.0f) && en->state != DIGGER_DRILL; i++) run(&w, &t, idle(), 1);
    CHECK(en->state == DIGGER_DRILL);

    InputTracker tracker;
    input_init(&tracker);
    for (int i = 0; i < seconds(30.0f) && enemies_alive(&w.enemies) > 0; i++) {
        InputSample sample = { 0u, 0.0f, 0.0f };
        autopilot_drive(&w, &t, &sample);
        input_feed(&tracker, &sample);
        InputState st = input_tick(&tracker);
        world_tick(&w, &st, &t);
    }
    CHECK(w.state == GAME_PLAYING);
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(w.score.kills_by_type[ENEMY_DIGGER] == 1);
    CHECK(w.ship.alive);
    CHECK(Vector3Length(w.ship.pos) > w.planet.params.radius);   // atirou de fora, sem entrar no túnel
}

static void siphon_drains_the_ground(void)
{
    static World w;
    Vector3 dir = { 1.0f, 0.2f, 0.0f };
    make_world_far(&w, Vector3Negate(dir));
    float radius = w.planet.params.radius;
    int64_t volume = w.planet.volume;
    world_spawn(&w, &tun, ENEMY_SIPHON, 1, Vector3Scale(Vector3Normalize(dir), radius * 3.0f));
    int index = find_type(&w, ENEMY_SIPHON);
    const Enemy *en = &w.enemies.items[index];

    run(&w, &tun, idle(), seconds(12.0f));
    CHECK(count_events(&w, EV_DRAIN_START) == 1);
    CHECK(en->state == SIPHON_DRAIN);
    CHECK_NEAR(Vector3Length(en->pos) - radius, tun.siphon_altitude, 2.5f);   // pairando
    CHECK(w.planet.volume < volume);

    // Cada mordida do feixe é uma escavação silenciosa, sem cratera de impacto.
    int quiet = 0, loud = 0;
    for (uint32_t id = 1; id <= w.impact_count; id++) {
        const Impact *impact = world_find_impact(&w, id);
        if (impact == NULL) continue;
        if (impact->quiet) quiet++;
        else loud++;
    }
    CHECK(quiet >= 5 && loud == 0);

    // A perda continua enquanto ele estiver vivo.
    int64_t before = w.planet.volume;
    run(&w, &tun, idle(), seconds(3.0f));
    CHECK(w.planet.volume < before);
}

static void sniper_keeps_away_and_charges_before_firing(void)
{
    static World w;
    Vector3 ship_at = { 0.0f, 60.0f, 0.0f };
    make_world(&w, ship_at);
    Tuning t = exact();
    world_spawn(&w, &t, ENEMY_SNIPER, 1, Vector3Add(ship_at, (Vector3){ 60.0f, 20.0f, 0.0f }));
    int index = find_type(&w, ENEMY_SNIPER);
    const Enemy *en = &w.enemies.items[index];
    float shield = w.ship.shield;

    // Espera a primeira carga começar: antes dela, nenhum tiro.
    int guard = 0;
    while (en->arming <= 0.0f && guard++ < seconds(10.0f)) run(&w, &t, idle(), 1);
    CHECK(count_events(&w, EV_SNIPER_CHARGE) == 1);
    CHECK(count_events(&w, EV_ENEMY_SHOT) == 0);
    CHECK(Vector3Distance(en->aux, w.ship.pos) < 2.0f);   // a mira está na nave

    // Parada, a nave leva o tiro.
    run(&w, &t, idle(), seconds(t.sniper_charge_time + 1.5f));
    CHECK(count_events(&w, EV_ENEMY_SHOT) == 1);
    CHECK(w.ship.shield < shield);

    // Ele fica longe: perto da distância que procura manter.
    run(&w, &t, idle(), seconds(4.0f));
    float d = Vector3Distance(en->pos, w.ship.pos);
    CHECK(d > t.sniper_range * 0.6f && d < t.sniper_range * 1.4f);
}

static void sniper_shot_can_be_dodged_after_the_lock(void)
{
    static World w;
    Vector3 ship_at = { 0.0f, 60.0f, 0.0f };
    make_world(&w, ship_at);
    Tuning t = exact();
    world_spawn(&w, &t, ENEMY_SNIPER, 1, Vector3Add(ship_at, (Vector3){ 60.0f, 20.0f, 0.0f }));
    const Enemy *en = &w.enemies.items[find_type(&w, ENEMY_SNIPER)];

    // Espera a mira travar e então tira a nave do lugar.
    int guard = 0;
    while (!(en->arming > 0.0f && en->timer < t.sniper_lock_time * 0.8f) && guard++ < seconds(10.0f)) {
        run(&w, &t, idle(), 1);
    }
    CHECK(en->arming > 0.0f);
    Vector3 locked = en->aux;
    w.ship.pos = Vector3Add(w.ship.pos, (Vector3){ 0.0f, 0.0f, 12.0f });
    w.ship.pos_prev = w.ship.pos;
    float shield = w.ship.shield;
    run(&w, &t, idle(), seconds(2.0f));
    CHECK(count_events(&w, EV_ENEMY_SHOT) == 1);
    CHECK(w.ship.shield == shield);                          // passou longe
    CHECK(Vector3Distance(locked, w.ship.pos) > 10.0f);
}

static void shielder_protects_allies_until_it_falls(void)
{
    static World w;
    Vector3 spot = { 0.0f, 70.0f, 0.0f };
    make_world_far(&w, (Vector3){ 0.0f, -1.0f, 0.0f });
    Tuning t = exact();
    world_spawn(&w, &t, ENEMY_SWARM, 1, spot);
    world_spawn(&w, &t, ENEMY_SHIELDER, 1, Vector3Add(spot, (Vector3){ 8.0f, 0.0f, 0.0f }));
    int drone = find_type(&w, ENEMY_SWARM), shielder = find_type(&w, ENEMY_SHIELDER);
    CHECK(enemies_shielded_by(&w.enemies, &t, drone) == shielder);
    CHECK(enemies_shielded_by(&w.enemies, &t, shielder) == -1);   // ele mesmo não é protegido

    // Tiro no drone: para no escudo.
    Enemy *en = &w.enemies.items[drone];
    float hp = en->hp;
    Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(en->pos, (Vector3){ 0.0f, 0.0f, -20.0f }),
                                (Vector3){ 0.0f, 0.0f, t.weapon_speed }, 2.0f);
    s->damage = t.weapon_damage;
    for (int i = 0; i < 10; i++) {
        projectiles_move(&w.shots, dt);
        combat_resolve(&w, &t);
    }
    CHECK(en->hp == hp);
    CHECK(count_events(&w, EV_SHIELDED) == 1);

    // Sem o Escudeiro, o drone fica exposto.
    enemies_damage(&w.enemies, shielder, 1000.0f, DEATH_PLAYER);
    combat_sweep(&w, &t);
    CHECK(enemies_shielded_by(&w.enemies, &t, drone) == -1);
    s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(en->pos, (Vector3){ 0.0f, 0.0f, -20.0f }),
                          (Vector3){ 0.0f, 0.0f, t.weapon_speed }, 2.0f);
    s->damage = t.weapon_damage;
    for (int i = 0; i < 10; i++) {
        projectiles_move(&w.shots, dt);
        combat_resolve(&w, &t);
    }
    CHECK(en->hp < hp);
}

static void shielder_follows_its_group(void)
{
    static World w;
    Vector3 dir = { 0.2f, 1.0f, 0.3f };
    make_world_far(&w, Vector3Negate(dir));
    Vector3 spawn = Vector3Scale(Vector3Normalize(dir), w.planet.params.radius * 3.0f);
    world_spawn(&w, &tun, ENEMY_SWARM, 5, spawn);
    world_spawn(&w, &tun, ENEMY_SHIELDER, 1, Vector3Add(spawn, (Vector3){ 30.0f, 0.0f, 0.0f }));
    int shielder = find_type(&w, ENEMY_SHIELDER);

    // Depois de um tempo, ele cobre o bando.
    run(&w, &tun, idle(), seconds(8.0f));
    int covered = 0;
    for (int i = 0; i < w.enemies.pool.capacity; i++) {
        if (!pool_alive(&w.enemies.pool, i) || w.enemies.items[i].type != ENEMY_SWARM) continue;
        if (enemies_shielded_by(&w.enemies, &tun, i) == shielder) covered++;
    }
    CHECK(covered >= 2);
}

static void carrier_warns_then_launches_drones(void)
{
    static World w;
    make_world_far(&w, (Vector3){ -1.0f, 0.0f, 0.0f });
    Tuning t = exact();
    float radius = w.planet.params.radius;
    world_spawn(&w, &t, ENEMY_CARRIER, 1, (Vector3){ radius + t.carrier_altitude, 0.0f, 0.0f });
    Enemy *en = &w.enemies.items[find_type(&w, ENEMY_CARRIER)];
    en->state = HEAVY_ORBIT;
    en->timer = 0.2f;

    run(&w, &t, idle(), seconds(0.5f));
    CHECK(en->arming > 0.0f);                       // hangar aceso: aviso
    CHECK(w.enemies.alive_by_type[ENEMY_SWARM] == 0);
    run(&w, &t, idle(), seconds(t.carrier_launch_warning + 0.2f));
    CHECK(count_events(&w, EV_LAUNCH) == 1);
    CHECK(w.enemies.alive_by_type[ENEMY_SWARM] == t.carrier_launch_count);

    // Os drones lançados atacam o planeta embaixo dele.
    int64_t volume = w.planet.volume;
    run(&w, &t, idle(), seconds(10.0f));
    CHECK(w.planet.volume < volume);

    // Com drones demais em cena, não lança mais.
    t.carrier_max_swarm = 0;
    int launches = count_events(&w, EV_LAUNCH);
    run(&w, &t, idle(), seconds(t.carrier_launch_interval * 2.0f));
    CHECK(count_events(&w, EV_LAUNCH) == launches);
}

// Uma horda com todos os tipos, por meio minuto: ninguém sai do mapa, nada
// vira NaN, e o resultado se repete.
static uint64_t mixed_horde(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 0.0f, 120.0f });
    float radius = w.planet.params.radius;
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
        float angle = (float)type * 0.8f;
        Vector3 dir = { cosf(angle), 0.3f, sinf(angle) };
        world_spawn(&w, &tun, (EnemyType)type, (type == ENEMY_SWARM) ? 6 : 2,
                    Vector3Scale(Vector3Normalize(dir), radius * tun.enemies_spawn_distance));
    }
    bool sane = true;
    for (int i = 0; i < seconds(30.0f); i++) {
        run(&w, &tun, idle(), 1);
        for (int k = 0; k < w.enemies.pool.capacity; k++) {
            if (!pool_alive(&w.enemies.pool, k)) continue;
            Vector3 p = w.enemies.items[k].pos;
            if (!(Vector3Length(p) < radius * 20.0f)) sane = false;   // também pega NaN
        }
    }
    CHECK(sane);
    return world_hash(&w);
}

static void mixed_horde_is_stable_and_deterministic(void)
{
    uint64_t first = mixed_horde();
    uint64_t second = mixed_horde();
    CHECK(first == second);
}

static void enter_calls_the_next_wave(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 0.0f, 120.0f });
    static WavePlan plan;
    plan.count = 1;
    plan.waves[0].pause = 30.0f;
    plan.waves[0].group_count = 1;
    plan.waves[0].groups[0] = (WaveGroup){ ENEMY_SWARM, 3, 0.0f, 0.0f, 0.0f };
    world_set_plan(&w, &plan);
    run(&w, &tun, idle(), 10);
    CHECK(w.director.phase == DIRECTOR_PAUSE);

    InputState enter = idle();
    enter.pressed = ACT_BIT(ACT_READY);
    run(&w, &tun, enter, 1);
    run(&w, &tun, idle(), 1);
    CHECK(count_events(&w, EV_CALM_SKIP) == 1);
    CHECK(w.director.phase == DIRECTOR_ACTIVE);
    CHECK(enemies_alive(&w.enemies) == 3);
}

// As hordas do jogo: todos os tipos aparecem, e a pressão cresce.
static void waves_grow_and_use_every_type(void)
{
    static WavePlan plan;
    char err[160];
    bool ok = waves_load(&plan, "data/waves.txt", err, sizeof err);
    if (!ok) test_fail(__FILE__, __LINE__, err);
    CHECK(ok);
    CHECK(plan.count >= 8);

    bool used[ENEMY_TYPE_COUNT] = { false };
    int first = 0, last = 0;
    for (int i = 0; i < plan.count; i++) {
        int total = 0;
        for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
            int n = wave_enemy_count(&plan.waves[i], (EnemyType)type);
            if (n > 0) used[type] = true;
            total += n;
        }
        if (i == 0) first = total;
        last = total;
    }
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) CHECK(used[type]);
    CHECK(first >= 10);          // pedido do Victor: começa com uns 10
    CHECK(last >= first * 2);    // ... e cresce
}

void suite_enemies(void)
{
    tun = test_tuning();
    dt = 1.0f / (float)tun.sim_hz;
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(kamikaze_hunts_the_ship_and_detonates);
    RUN(kamikaze_dives_into_the_planet);
    RUN(digger_tunnels_toward_the_core);
    RUN(autopilot_shoots_down_the_tunnel);
    RUN(siphon_drains_the_ground);
    RUN(sniper_keeps_away_and_charges_before_firing);
    RUN(sniper_shot_can_be_dodged_after_the_lock);
    RUN(shielder_protects_allies_until_it_falls);
    RUN(shielder_follows_its_group);
    RUN(carrier_warns_then_launches_drones);
    RUN(mixed_horde_is_stable_and_deterministic);
    RUN(enter_calls_the_next_wave);
    RUN(waves_grow_and_use_every_type);
    arena_free(&arena);
}
