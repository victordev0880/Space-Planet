// Testes do marco 8: o Rompe-Mundos e o núcleo que ele deixa.
#include "test.h"
#include "test_util.h"

#include "core/input.h"
#include "game/combat.h"
#include "game/world.h"

static Tuning tun;
static Arena  arena;

static InputState keys(uint32_t held)
{
    InputState in = { held, 0u, 0u, 0.0f, 0.0f };
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

// O planeta dos testes é pequeno (raio ~17): o corte do boss fica menor aqui.
static Tuning small(void)
{
    Tuning t = tun;
    t.breaker_cut_radius = 2.5f;
    t.breaker_cut_sink = 1.0f;
    t.breaker_cut_spacing = 1.5f;
    t.breaker_altitude = 30.0f;
    t.breaker_scale = 4.0f;
    return t;
}

// Mundo sem hordas, com a nave longe (do outro lado) e o boss chegando por +y.
static void make_world(World *w, const Tuning *t)
{
    arena_reset(&arena);
    if (!world_init(w, t, 9u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    Vector3 far = { 0.0f, -w->planet.params.radius * 6.0f, 0.0f };
    world_place_ship(w, t, far, Vector3Add(far, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    world_spawn_boss(w, t, BOSS_BREAKER, (Vector3){ 0.0f, 1.0f, 0.0f });
}

// Passa a entrada: o boss chega à órbita e começa a lutar.
static void skip_entrance(World *w, const Tuning *t)
{
    for (int i = 0; i < seconds(t->breaker_enter_time + 1.0f) && w->boss.state == BOSS_ENTER; i++) {
        run(w, t, keys(0u), 1);
    }
}

static void boss_enters_then_fights(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    CHECK(w.boss.state == BOSS_ENTER && saw_event(&w, EV_BOSS_ENTER));
    CHECK(!boss_vulnerable(&w.boss));
    float hp = w.boss.hp;
    CHECK(boss_damage_core(&w.boss, &t, 100.0f, &w.events, 0u) == 0.0f);   // invulnerável na entrada
    CHECK(w.boss.hp == hp);

    skip_entrance(&w, &t);
    CHECK(w.boss.state == BOSS_FIGHT);
    run(&w, &t, keys(0u), seconds(1.0f));
    float altitude = Vector3Length(w.boss.pos) - w.planet.params.radius;
    CHECK(fabsf(altitude - t.breaker_altitude) < 3.0f);
}

// Destruídos os quatro capacitores durante a carga, o tiro falha: atordoado,
// núcleo aberto, e o planeta intacto.
static void boss_capacitors_cancel_the_cut(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    skip_entrance(&w, &t);
    for (int i = 0; i < seconds(20.0f) && w.boss.action != BREAKER_CHARGE; i++) run(&w, &t, keys(0u), 1);
    CHECK(w.boss.action == BREAKER_CHARGE && boss_capacitors_armed(&w.boss));
    CHECK(saw_event(&w, EV_BOSS_CHARGE));
    int64_t volume = w.planet.volume;

    float armor = boss_core_factor(&w.boss, &t);
    CHECK(armor < 0.5f);
    for (int k = 0; k < BOSS_CAPACITORS; k++) {
        CHECK(boss_damage_capacitor(&w.boss, &t, k, 1000.0f, &w.events, (uint32_t)w.tick) > 0.0f);
    }
    CHECK(w.boss.action == BREAKER_STAGGER && saw_event(&w, EV_BOSS_STAGGER));
    CHECK(boss_core_factor(&w.boss, &t) == 1.0f);
    float hp = w.boss.hp;
    CHECK_NEAR(boss_damage_core(&w.boss, &t, 10.0f, &w.events, (uint32_t)w.tick), 10.0f, 1e-4);
    CHECK_NEAR(w.boss.hp, hp - 10.0f, 1e-4);

    run(&w, &t, keys(0u), seconds(t.breaker_charge[0] + 1.0f));
    CHECK(w.planet.volume == volume);              // o corte não saiu
    CHECK(!saw_event(&w, EV_BOSS_FIRE));
}

// Sem resposta, o laser varre o traço e abre uma vala no planeta.
static void boss_cut_carves_the_planet(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    skip_entrance(&w, &t);
    int64_t volume = w.planet.volume;
    for (int i = 0; i < seconds(40.0f) && !saw_event(&w, EV_BOSS_FIRE); i++) run(&w, &t, keys(0u), 1);
    CHECK(saw_event(&w, EV_BOSS_FIRE));
    Vector3 from = w.boss.cut_from, mid = boss_cut_direction(&w.boss, 0.5f), to = boss_cut_direction(&w.boss, 1.0f);
    uint32_t impacts = w.impact_count;
    // Cada mordida fica na mesma profundidade em relação ao solo de antes do
    // corte (se medisse o chão já cavado, o corte desceria em escada até o núcleo).
    float deepest = 1e9f;
    for (int i = 0; i < seconds(t.breaker_fire_time + 0.5f); i++) {
        run(&w, &t, keys(0u), 1);
        for (int k = 0; k < w.boss.carve_count; k++) deepest = fminf(deepest, Vector3Length(w.boss.carves[k].center));
    }
    CHECK(deepest > w.planet.params.radius - t.breaker_cut_sink - 3.0f);
    CHECK(w.planet.volume < volume);
    CHECK(w.impact_count > impacts + 3u);
    // O solo sumiu ao longo do traço: no começo, no meio e no fim.
    float r = w.planet.params.radius;
    CHECK(planet_sample(&w.planet, Vector3Scale(from, r - 0.5f)) < VOXEL_ISO
          || planet_sample(&w.planet, Vector3Scale(mid, r - 0.5f)) < VOXEL_ISO);
    CHECK(planet_sample(&w.planet, Vector3Scale(mid, r - 0.5f)) < VOXEL_ISO);
    CHECK(planet_sample(&w.planet, Vector3Scale(to, r - 0.5f)) < VOXEL_ISO
          || planet_sample(&w.planet, Vector3Scale(boss_cut_direction(&w.boss, 0.9f), r - 0.5f)) < VOXEL_ISO);
    CHECK(w.boss.action == BREAKER_REST);
}

// Muitos cortes seguidos (nenhum capacitor destruído): cada um tira uma fatia
// de espessura fixa abaixo da superfície original; cruzando valas antigas, não
// cava mais fundo. O interior abaixo da fatia fica intacto.
static void repeated_cuts_never_dig_deeper(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    skip_entrance(&w, &t);
    int fires = 0;
    uint32_t seen = w.events.count;
    for (int i = 0; i < seconds(200.0f) && fires < 6; i++) {
        run(&w, &t, keys(0u), 1);
        for (uint32_t id = seen + 1; id <= w.events.count; id++) {
            const GameEvent *e = events_find(&w.events, id);
            if (e != NULL && e->type == EV_BOSS_FIRE) fires++;
        }
        seen = w.events.count;
    }
    run(&w, &t, keys(0u), seconds(t.breaker_fire_time + 0.5f));
    CHECK(fires >= 6);
    CHECK(planet_core_hp(&w.planet) == 1.0f);
    // Abaixo da fatia mais funda possível (o fundo do mar menos o corte, com a
    // faixa de densidade da borda), tudo continua sólido.
    float safe = w.planet.params.radius - w.planet.params.ocean_depth - t.breaker_cut_sink - t.breaker_cut_radius - 5.0f;
    int solid = 0, total = 0;
    for (int k = 0; k < 400; k++) {
        float y = 1.0f - 2.0f * ((float)k + 0.5f) / 400.0f;   // pontos espalhados pela esfera
        float ring = sqrtf(fmaxf(1.0f - y * y, 0.0f)), angle = 2.39996f * (float)k;
        Vector3 dir = { ring * cosf(angle), y, ring * sinf(angle) };
        total++;
        if (planet_sample(&w.planet, Vector3Scale(dir, safe)) >= VOXEL_ISO) solid++;
    }
    CHECK(solid == total);
}

// Fases pela vida; a partir da 2, drones.
static void boss_phases_and_drones(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    skip_entrance(&w, &t);
    w.boss.action = BREAKER_STAGGER;
    w.boss.timer = 100.0f;
    boss_damage_core(&w.boss, &t, w.boss.hp_max * (1.0f - t.breaker_phase_2) + 1.0f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.phase == 2 && saw_event(&w, EV_BOSS_PHASE));
    for (int i = 0; i < seconds(t.breaker_launch_interval + 1.0f) && enemies_alive(&w.enemies) == 0; i++) {
        run(&w, &t, keys(0u), 1);
    }
    CHECK(w.enemies.alive_by_type[ENEMY_SWARM] == t.breaker_launch_count);
    boss_damage_core(&w.boss, &t, w.boss.hp - w.boss.hp_max * t.breaker_phase_3 + 1.0f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.phase == 3);
    w.boss.action = BREAKER_REST;
    CHECK_NEAR(boss_core_factor(&w.boss, &t), t.breaker_core_open, 1e-6);   // fase 3: meio aberto
}

// Derrotado: explode por alguns segundos, some, dá pontos e o núcleo; a horda acaba.
static void boss_death_ends_the_wave_and_rewards(void)
{
    static World w;
    static WavePlan plan;
    Tuning t = small();
    arena_reset(&arena);
    CHECK(world_init(&w, &t, 4u, &arena));
    static Config c;   // grande demais para a pilha
    CHECK(config_parse(&c,"wave.1.pause = 0.1\nwave.1.group.1 = breaker 1 0 0 60\n"));
    char err[160];
    CHECK(waves_parse(&plan, &c, err, sizeof err));
    CHECK(wave_boss(&plan.waves[0]) == BOSS_BREAKER);
    world_set_plan(&w, &plan);
    run(&w, &t, keys(0u), seconds(1.0f));
    CHECK(boss_active(&w.boss));
    skip_entrance(&w, &t);
    run(&w, &t, keys(0u), seconds(3.0f));
    CHECK(w.state == GAME_PLAYING);   // com o boss vivo, a horda não acaba

    w.boss.action = BREAKER_STAGGER;
    w.boss.timer = 100.0f;
    boss_damage_core(&w.boss, &t, 1e6f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.state == BOSS_DYING);
    int64_t points = w.score.points;
    for (int i = 0; i < seconds(8.0f) && boss_active(&w.boss); i++) run(&w, &t, keys(0u), 1);
    CHECK(!boss_active(&w.boss));
    CHECK(saw_event(&w, EV_BOSS_BLAST) && saw_event(&w, EV_BOSS_DOWN) && saw_event(&w, EV_REWARD));
    CHECK(w.score.bosses == 1 && w.score.points > points);
    CHECK(w.ability.owned);
    run(&w, &t, keys(0u), 5);
    CHECK(w.state == GAME_WON);
}

// Tiros: o núcleo fechado só deixa passar uma fração; capacitor carregado leva tudo.
static void bolts_hit_boss_parts(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    skip_entrance(&w, &t);
    w.boss.action = BREAKER_REST;
    w.boss.timer = 100.0f;
    float hp = w.boss.hp;
    Vector3 core = boss_point(&w.boss, &t, BREAKER_CORE);
    Vector3 out = Vector3Normalize(core);   // de fora do planeta para o núcleo
    Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(core, Vector3Scale(out, 12.0f)),
                                Vector3Scale(out, -t.weapon_speed), 1.0f);
    s->damage = 10.0f;
    for (int i = 0; i < 4; i++) {
        projectiles_move(&w.shots, 1.0f / (float)t.sim_hz);
        combat_resolve(&w, &t);
    }
    CHECK_NEAR(w.boss.hp, hp - 10.0f * t.breaker_core_armor, 1e-3);
    CHECK(saw_event(&w, EV_ARMOR_HIT));
}

// O raio do núcleo: segurando o botão direito, atravessa os inimigos em linha.
static void ability_beam_hits_a_line(void)
{
    static World w;
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    arena_reset(&arena);
    CHECK(world_init(&w, &t, 6u, &arena));
    Vector3 at = { 0.0f, 0.0f, w.planet.params.radius * 3.0f };
    world_place_ship(&w, &t, at, Vector3Add(at, (Vector3){ 1.0f, 0.0f, 0.0f }), 0.0f);
    world_spawn(&w, &t, ENEMY_SIPHON, 1, Vector3Add(at, (Vector3){ 30.0f, 0.0f, 0.0f }));
    world_spawn(&w, &t, ENEMY_DIGGER, 1, Vector3Add(at, (Vector3){ 60.0f, 0.0f, 0.0f }));
    w.enemies.items[0].pos = Vector3Add(at, (Vector3){ 30.0f, 0.0f, 0.0f });
    w.enemies.items[1].pos = Vector3Add(at, (Vector3){ 60.0f, 0.0f, 0.0f });
    float hp0 = w.enemies.items[0].hp, hp1 = w.enemies.items[1].hp;

    // Sem o núcleo, nada.
    run(&w, &t, keys(ACT_BIT(ACT_ALT_FIRE)), seconds(t.ability_charge + 0.2f));
    CHECK(!saw_event(&w, EV_ABILITY));

    w.ability.owned = true;
    w.enemies.items[0].pos = Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 30.0f));
    w.enemies.items[1].pos = Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 60.0f));
    for (int i = 0; i < seconds(t.ability_charge + 0.2f) && !saw_event(&w, EV_ABILITY); i++) {
        w.enemies.items[0].pos = Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 30.0f));
        w.enemies.items[1].pos = Vector3Add(w.ship.pos, Vector3Scale(ship_forward(&w.ship), 60.0f));
        run(&w, &t, keys(ACT_BIT(ACT_ALT_FIRE)), 1);
    }
    CHECK(saw_event(&w, EV_ABILITY));
    CHECK(w.enemies.items[0].hp < hp0 && w.enemies.items[1].hp < hp1);
    CHECK(w.ability.cooldown > 0.0f);
}

// As hordas do jogo: o Rompe-Mundos é a horda 5 (um boss a cada 5 hordas).
static void boss_is_wave_five(void)
{
    static WavePlan plan;
    char err[160];
    bool ok = waves_load(&plan, "data/waves.txt", err, sizeof err);
    if (!ok) test_fail(__FILE__, __LINE__, err);
    CHECK(ok);
    CHECK(plan.count >= 5);
    for (int i = 0; i < plan.count; i++) {
        CHECK(wave_boss(&plan.waves[i]) == ((i == 4) ? BOSS_BREAKER : -1));
    }
}

void suite_boss(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(boss_enters_then_fights);
    RUN(boss_capacitors_cancel_the_cut);
    RUN(boss_cut_carves_the_planet);
    RUN(repeated_cuts_never_dig_deeper);
    RUN(boss_phases_and_drones);
    RUN(boss_death_ends_the_wave_and_rewards);
    RUN(bolts_hit_boss_parts);
    RUN(ability_beam_hits_a_line);
    RUN(boss_is_wave_five);
    arena_free(&arena);
}
