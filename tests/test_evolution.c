// Testes do marco 7: XP, níveis, cartas ponderadas pelas essências e os
// efeitos das evoluções.
#include "test.h"
#include <stddef.h>
#include "test_util.h"

#include "core/input.h"
#include "game/combat.h"
#include "game/evolution.h"
#include "game/world.h"
#include "tools/autopilot.h"

static Tuning tun;
static Arena  arena;

static InputState keys(uint32_t held)
{
    InputState in = { held, 0u, 0u, 0.0f, 0.0f };
    return in;
}

static void run(World *w, InputState in, int ticks)
{
    for (int i = 0; i < ticks; i++) world_tick(w, &in, &tun);
}

static int seconds(float s)
{
    return (int)(s * (float)tun.sim_hz);
}

static void make_world(World *w)
{
    arena_reset(&arena);
    if (!world_init(w, &tun, 5u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    Vector3 spot = { 0.0f, w->planet.params.radius * 4.0f, 0.0f };
    world_place_ship(w, &tun, spot, Vector3Add(spot, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
}

static bool saw_event(const World *w, EventType type)
{
    for (uint32_t id = 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e != NULL && e->type == type) return true;
    }
    return false;
}

// Os níveis pedem cada vez mais XP; vários níveis de uma vez também contam.
static void evolution_levels_grow(void)
{
    Evolution e;
    evolution_reset(&e);
    CHECK(e.level == 1);
    float first = evolution_xp_needed(&tun, 1), second = evolution_xp_needed(&tun, 2);
    CHECK(second > first);
    CHECK(evolution_gain(&e, &tun, ENEMY_SWARM, first * 0.5f) == 0);
    CHECK(evolution_gain(&e, &tun, ENEMY_SWARM, first * 0.5f + second + 0.01f) == 2);
    CHECK(e.level == 3 && e.pending == 2);
    CHECK_NEAR(e.essence[ENEMY_SWARM], first + second + 0.01f, 1e-3);
}

// Três cartas, todas diferentes; quem tem mais essência de uma linha vê mais
// cartas dela.
static void evolution_cards_follow_essence(void)
{
    Rng rng;
    rng_seed(&rng, 99u, 3u);
    Evolution e;
    evolution_reset(&e);
    e.essence[ENEMY_SWARM] = 200.0f;

    int swarm_first = 0, trials = 2000;
    for (int i = 0; i < trials; i++) {
        e.pending = 1;
        e.choosing = false;
        evolution_offer(&e, &tun, &rng);
        CHECK(e.choosing && e.choice_count == EVO_CHOICES);
        CHECK(e.choices[0] != e.choices[1] && e.choices[1] != e.choices[2] && e.choices[0] != e.choices[2]);
        if (evolution_info((EvolutionId)e.choices[0])->line == ENEMY_SWARM) swarm_first++;
    }
    // Pesos: duas do Enxame com 1 + 0.08 x 200 = 17, as outras 14 com 1.
    float swarm_weight = 2.0f * evolution_weight(&e, &tun, EVO_ESCORT);
    float expected = swarm_weight / (swarm_weight + 14.0f * tun.cards_base_weight);
    CHECK_NEAR((float)swarm_first / (float)trials, expected, 0.05);
    CHECK((float)swarm_first / (float)trials > 0.5f);
}

// Evolução no nível máximo não aparece mais; sem nada para oferecer, o nível passa.
static void evolution_maxed_cards_disappear(void)
{
    Rng rng;
    rng_seed(&rng, 7u, 1u);
    Evolution e;
    evolution_reset(&e);
    for (int i = 0; i < EVO_COUNT; i++) e.rank[i] = (uint8_t)tun.evo_max[i];
    e.rank[EVO_CRIT] = 0;
    e.rank[EVO_PIERCE] = 0;
    e.pending = 1;
    evolution_offer(&e, &tun, &rng);
    CHECK(e.choosing && e.choice_count == 2);
    for (int c = 0; c < e.choice_count; c++) CHECK(e.choices[c] == EVO_CRIT || e.choices[c] == EVO_PIERCE);

    for (int i = 0; i < EVO_COUNT; i++) e.rank[i] = (uint8_t)tun.evo_max[i];
    e.choosing = false;
    e.pending = 1;
    evolution_offer(&e, &tun, &rng);
    CHECK(!e.choosing && e.pending == 0);
}

// Subir de nível abre as cartas e congela a partida; uma tecla que já estava
// apertada não escolhe; soltar e apertar escolhe.
static void evolution_level_up_freezes_and_picks(void)
{
    static World w;
    make_world(&w);
    Vector3 spot = Vector3Add(w.ship.pos, (Vector3){ 0.0f, 0.0f, 60.0f });
    world_spawn(&w, &tun, ENEMY_SWARM, 3, spot);

    world_give_xp(&w, &tun, evolution_xp_needed(&tun, 1));
    CHECK(saw_event(&w, EV_LEVEL_UP));
    CHECK(w.evo.level == 2 && w.evo.pending == 1);
    for (int i = 0; i < seconds(1.0f) && !w.evo.choosing; i++) run(&w, keys(0u), 1);
    CHECK(w.evo.choosing && w.evo.choice_count == EVO_CHOICES);

    // Congelado: nada anda.
    Vector3 enemy = w.enemies.items[0].pos, ship = w.ship.pos;
    run(&w, keys(ACT_BIT(ACT_FORWARD)), 30);
    CHECK(Vector3Distance(w.enemies.items[0].pos, enemy) == 0.0f);
    CHECK(Vector3Distance(w.ship.pos, ship) == 0.0f);

    // A tecla 2 já apertada quando as cartas abrem (sem passo solto entre elas) não escolhe.
    static World again;
    again = w;
    again.evo.armed = false;
    run(&again, keys(ACT_BIT(ACT_CARD2)), 5);
    CHECK(again.evo.choosing);

    // Soltar e apertar: escolhe a segunda carta.
    int picked = w.evo.choices[1];
    run(&w, keys(0u), 1);
    run(&w, keys(ACT_BIT(ACT_CARD2)), 1);
    CHECK(!w.evo.choosing && w.evo.pending == 0);
    CHECK(w.evo.rank[picked] == 1);
    CHECK(w.evo.history_count == 1 && w.evo.history[0] == picked);
    CHECK(saw_event(&w, EV_EVOLVE));

    // E a partida volta a andar.
    run(&w, keys(0u), 30);
    CHECK(Vector3Distance(w.enemies.items[0].pos, enemy) > 0.0f);
}

// Os efeitos numéricos entram no tuning efetivo.
static void evolution_numbers_change_the_ship(void)
{
    static World w;
    make_world(&w);
    float hull = w.ship.hull;
    world_set_evolution(&w, &tun, EVO_HEAVY_HULL, 2);
    world_set_evolution(&w, &tun, EVO_BIG_SHIELD, 1);
    world_set_evolution(&w, &tun, EVO_LONG_RANGE, 1);
    run(&w, keys(0u), 1);
    const Tuning *eff = world_tuning(&w);
    CHECK_NEAR(eff->ship_hull_max, tun.ship_hull_max + 2.0f * tun.heavy_hull_hull, 1e-3);
    CHECK_NEAR(eff->ship_shield_max, tun.ship_shield_max + tun.big_shield_shield, 1e-3);
    CHECK(eff->weapon_range > tun.weapon_range && eff->weapon_speed > tun.weapon_speed);
    CHECK(w.ship.hull > hull);   // o casco novo já vem consertado
}

// Tiro perfurante: um tiro atravessa o primeiro inimigo e acerta o segundo.
static void evolution_pierce_hits_two(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    Vector3 a = { 0.0f, 0.0f, w.planet.params.radius * 2.5f };
    Vector3 b = Vector3Add(a, (Vector3){ 8.0f, 0.0f, 0.0f });
    world_spawn(&w, &t, ENEMY_SIPHON, 1, a);   // vida alta: sobrevivem ao tiro (e nenhum Escudeiro por perto)
    world_spawn(&w, &t, ENEMY_DIGGER, 1, b);
    w.evo.rank[EVO_PIERCE] = 1;
    float hp_a = w.enemies.items[0].hp, hp_b = w.enemies.items[1].hp;
    w.enemies.items[1].pos = b;

    // (de lado, por fora do planeta pequeno dos testes)
    Vector3 from = Vector3Add(a, (Vector3){ -30.0f, 0.0f, 0.0f });
    Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, from, (Vector3){ t.weapon_speed, 0.0f, 0.0f }, 2.0f);
    s->damage = 1.0f;
    s->pierce = w.evo.rank[EVO_PIERCE];
    for (int i = 0; i < 20; i++) {
        projectiles_move(&w.shots, 1.0f / (float)t.sim_hz);
        combat_resolve(&w, &t);
    }
    CHECK(w.enemies.items[0].hp < hp_a);
    CHECK(w.enemies.items[1].hp < hp_b);
}

// Reação em cadeia: o abatido explode e fere o vizinho.
static void evolution_chain_hurts_neighbors(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    Vector3 a = { 0.0f, 0.0f, w.planet.params.radius * 2.5f };
    world_spawn(&w, &t, ENEMY_SWARM, 1, a);
    world_spawn(&w, &t, ENEMY_SHIELDER, 1, Vector3Add(a, (Vector3){ 4.0f, 0.0f, 0.0f }));
    w.enemies.items[1].pos = Vector3Add(a, (Vector3){ 4.0f, 0.0f, 0.0f });
    w.evo.rank[EVO_CHAIN] = 2;
    float hp = w.enemies.items[1].hp;
    enemies_damage(&w.enemies, 0, 100.0f, DEATH_PLAYER);
    combat_sweep(&w, &t);
    CHECK(w.enemies.items[1].hp < hp);
    CHECK(saw_event(&w, EV_BLAST));
}

// Roubo de vida e XP por abate.
static void evolution_kills_give_xp_and_heal(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_BOMBER, 1, (Vector3){ 0.0f, 0.0f, w.planet.params.radius * 2.5f });
    w.evo.rank[EVO_LIFESTEAL] = 1;
    w.ship.hull = 50.0f;
    enemies_damage(&w.enemies, 0, 1000.0f, DEATH_PLAYER);
    combat_sweep(&w, &t);
    CHECK_NEAR(w.ship.hull, 50.0f + t.lifesteal_hull, 1e-3);
    CHECK_NEAR(w.evo.essence[ENEMY_BOMBER], t.xp_kill[ENEMY_BOMBER], 1e-3);
}

// Sinergias: só entram no sorteio com as duas linhas, e não contam para a
// linha dominante (a cor da nave).
static void synergy_unlocks_with_both_lines(void)
{
    Evolution e;
    evolution_reset(&e);
    CHECK(evolution_is_synergy(EVO_SYN_SUICIDE) && !evolution_is_synergy(EVO_ESCORT));
    CHECK(evolution_weight(&e, &tun, EVO_SYN_SUICIDE) == 0.0f);
    e.rank[EVO_SPREAD] = 1;   // só o Enxame
    CHECK(evolution_weight(&e, &tun, EVO_SYN_SUICIDE) == 0.0f);
    e.rank[EVO_CHAIN] = 1;    // ... e o Kamikaze
    CHECK_NEAR(evolution_weight(&e, &tun, EVO_SYN_SUICIDE), tun.cards_synergy_weight, 1e-6);
    CHECK(evolution_weight(&e, &tun, EVO_SYN_POISE) == 0.0f);   // outras sinergias, não
    e.rank[EVO_SYN_SUICIDE] = 1;
    CHECK(evolution_weight(&e, &tun, EVO_SYN_SUICIDE) == 0.0f);   // nível único
    e.rank[EVO_SYN_SALVAGE] = 1;
    CHECK(evolution_dominant_line(&e) == ENEMY_SWARM || evolution_dominant_line(&e) == ENEMY_KAMIKAZE);
}

// Drones suicidas: com um inimigo por perto, um drone sai da nave, persegue e explode nele.
static void synergy_suicide_drone_hunts(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_SIPHON, 1, Vector3Add(w.ship.pos, (Vector3){ 30.0f, 0.0f, 20.0f }));
    float hp = w.enemies.items[0].hp;
    w.evo.rank[EVO_SYN_SUICIDE] = 1;
    for (int i = 0; i < seconds(6.0f) && !saw_event(&w, EV_BLAST); i++) run(&w, keys(0u), 1);
    CHECK(saw_event(&w, EV_SUICIDE));
    CHECK(saw_event(&w, EV_BLAST));
    CHECK(!pool_alive(&w.enemies.pool, 0) || w.enemies.items[0].hp < hp);
}

// Desmanche: um abate deixa minério (mais dos grandes).
static void synergy_salvage_drops_ore(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_BOMBER, 1, (Vector3){ 0.0f, 0.0f, w.planet.params.radius * 2.5f });
    w.evo.rank[EVO_SYN_SALVAGE] = 1;
    CHECK(rocks_ore_alive(&w.rocks) == 0);
    enemies_damage(&w.enemies, 0, 1000.0f, DEATH_PLAYER);
    combat_sweep(&w, &t);
    CHECK(rocks_ore_alive(&w.rocks) == t.salvage_big);
}

// Mira blindada: com o escudo cheio, o tiro vale mais; com o escudo gasto, não.
static float bolt_damage(World *w, const Tuning *t, Vector3 target)
{
    float before = w->enemies.items[0].hp;
    Vector3 from = Vector3Add(target, (Vector3){ -30.0f, 0.0f, 0.0f });
    Shot *s = projectiles_spawn(&w->shots, SHOT_BOLT, from, (Vector3){ t->weapon_speed, 0.0f, 0.0f }, 2.0f);
    s->damage = 1.0f;
    s->source = BOLT_PLAYER;
    for (int i = 0; i < 20; i++) {
        projectiles_move(&w->shots, 1.0f / (float)t->sim_hz);
        combat_resolve(w, t);
    }
    return before - w->enemies.items[0].hp;
}

static void synergy_poise_hits_harder(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    Vector3 a = { 0.0f, 0.0f, w.planet.params.radius * 2.5f };
    world_spawn(&w, &t, ENEMY_SIPHON, 1, a);
    w.enemies.items[0].pos = a;
    w.evo.rank[EVO_SYN_POISE] = 1;
    w.ship.shield = t.ship_shield_max;
    CHECK_NEAR(bolt_damage(&w, &t, a), t.poise_mult, 1e-4);
    w.ship.shield = t.ship_shield_max * 0.5f;
    CHECK_NEAR(bolt_damage(&w, &t, a), 1.0f, 1e-4);
}

// Feixe restaurador: quebrar uma rocha a tiros conserta a maior cratera.
static void synergy_restore_mends_a_scar(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    float r = w.planet.params.radius;
    world_impact(&w, &t, (Vector3){ r * 3.0f, 0.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, 4.0f, 3.0f);
    int64_t hurt = w.planet.volume;
    Rock *k = world_spawn_rock(&w, &t, ROCK_FRAGMENT, (Vector3){ 0.0f, r + 15.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f });
    CHECK(k != NULL);
    if (k == NULL) return;
    int index = (int)(k - w.rocks.items);
    w.evo.rank[EVO_SYN_RESTORE] = 1;
    world_damage_rock(&w, &t, index, 1000.0f, k->pos, (Vector3){ 0.0f, 1.0f, 0.0f });
    CHECK(saw_event(&w, EV_RESTORE_BEAM));
    CHECK(repair_active_jobs(&w.repair) > 0);
    CHECK(rocks_ore_alive(&w.rocks) > 0);   // o minério continua saindo
    run(&w, keys(0u), seconds(t.repair_time + 0.5f));
    CHECK(w.planet.volume > hurt);
}

// Reflexão: com chance certa, o tiro inimigo volta como tiro da nave.
static void evolution_reflect_returns_shots(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.reflect_chance = 1.0f;
    w.evo.rank[EVO_REFLECT] = 1;
    float shield = w.ship.shield;
    Vector3 from = Vector3Add(w.ship.pos, (Vector3){ 30.0f, 0.0f, 0.0f });
    Shot *s = projectiles_spawn(&w.shots, SHOT_PLASMA, from, (Vector3){ -t.swarm_shot_speed, 0.0f, 0.0f }, 3.0f);
    s->damage = 10.0f;
    for (int i = 0; i < 60 && !saw_event(&w, EV_REFLECT); i++) {
        projectiles_move(&w.shots, 1.0f / (float)t.sim_hz);
        combat_resolve(&w, &t);
    }
    CHECK(saw_event(&w, EV_REFLECT));
    CHECK(w.ship.shield == shield);
    CHECK(w.shots.pool.live == 1 && w.shots.items[0].kind == SHOT_BOLT && w.shots.items[0].vel.x > 0.0f);
}

// Drones de escolta: nascem com a evolução, seguem a nave e derrubam quem chega perto.
static void evolution_escorts_fight(void)
{
    static World w;
    make_world(&w);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_set_evolution(&w, &t, EVO_ESCORT, 2);
    world_set_evolution(&w, &t, EVO_FIGHTERS, 1);
    run(&w, keys(0u), 2);
    CHECK(allies_alive(&w.allies) == 3);
    world_spawn(&w, &t, ENEMY_SWARM, 1, Vector3Add(w.ship.pos, (Vector3){ 0.0f, 0.0f, 30.0f }));
    for (int i = 0; i < seconds(10.0f) && enemies_alive(&w.enemies) > 0; i++) run(&w, keys(0u), 1);
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(saw_event(&w, EV_ALLY_SHOT));
    CHECK(w.score.kills == 1);
    for (int i = 0; i < ALLIES_MAX; i++) {
        if (w.allies.items[i].alive && w.allies.items[i].kind == ALLY_ESCORT) {
            CHECK(Vector3Distance(w.allies.items[i].pos, w.ship.pos) < tun.escort_orbit * 2.5f);
        }
    }
}

// Uma partida inteira jogada pelo piloto automático escolhe cartas e evolui.
static void evolution_autopilot_evolves(void)
{
    static World w;
    static WavePlan plan;
    char err[160];
    arena_reset(&arena);
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(world_init(&w, &tun, 2u, &arena));
    world_set_plan(&w, &plan);
    InputTracker tracker;
    input_init(&tracker);
    for (int i = 0; i < seconds(150.0f) && w.state == GAME_PLAYING; i++) {
        InputSample sample = { 0u, 0.0f, 0.0f };
        autopilot_drive(&w, world_tuning(&w), &sample);
        input_feed(&tracker, &sample);
        InputState st = input_tick(&tracker);
        world_tick(&w, &st, &tun);
    }
    CHECK(w.evo.level >= 3);
    CHECK(evolution_total_ranks(&w.evo) >= 2);
    CHECK(!w.evo.choosing || w.evo.pending > 0);
}

void suite_evolution(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(evolution_levels_grow);
    RUN(evolution_cards_follow_essence);
    RUN(evolution_maxed_cards_disappear);
    RUN(evolution_level_up_freezes_and_picks);
    RUN(evolution_numbers_change_the_ship);
    RUN(evolution_pierce_hits_two);
    RUN(evolution_chain_hurts_neighbors);
    RUN(evolution_kills_give_xp_and_heal);
    RUN(evolution_reflect_returns_shots);
    RUN(evolution_escorts_fight);
    RUN(evolution_autopilot_evolves);
    RUN(synergy_unlocks_with_both_lines);
    RUN(synergy_suicide_drone_hunts);
    RUN(synergy_salvage_drops_ore);
    RUN(synergy_poise_hits_harder);
    RUN(synergy_restore_mends_a_scar);
    arena_free(&arena);
}
