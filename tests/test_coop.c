// Testes do marco 12: a partida com vários jogadores (o online roda a mesma
// simulação em cada computador; aqui, duas cópias no mesmo processo).
#include "test.h"
#include "test_util.h"

#include <string.h>

#include "game/combat.h"
#include "game/waves.h"
#include "game/world.h"
#include "net/session.h"

static Tuning tun;
static Arena  arena_a, arena_b;

static int seconds(float s)
{
    return (int)(s * (float)tun.sim_hz);
}

// Comandos de teste, diferentes por jogador e por passo, mas sempre os
// mesmos para o mesmo (jogador, passo): voam, viram, atiram, soltam mísseis.
static InputState scripted(int player, uint32_t tick)
{
    InputState in;
    memset(&in, 0, sizeof in);
    uint32_t phase = (tick / 30u + (uint32_t)player * 7u) % 6u;
    in.held = ACT_BIT(ACT_FIRE);
    if (phase < 4u) in.held |= ACT_BIT(ACT_FORWARD);
    if (phase == 2u) in.held |= ACT_BIT(player == 0 ? ACT_LEFT : ACT_RIGHT);
    if (phase == 5u) in.held |= ACT_BIT(ACT_ALT_FIRE);
    if (tick % 97u == 0u) in.pressed |= ACT_BIT(ACT_DASH);
    in.look_dx = (player == 0 ? 2.0f : -3.0f) * ((phase % 2u) ? 1.0f : -0.5f);
    in.look_dy = (phase == 3u) ? 1.5f : -0.4f;
    return in;
}

static bool make_world(World *w, Arena *arena, const WavePlan *plan, int players, int local)
{
    arena_reset(arena);
    if (!world_init(w, &tun, 77u, arena)) return false;
    world_set_plan(w, plan);
    world_set_players(w, &tun, players, local);
    return true;
}

// O mesmo jogo, com os mesmos comandos, dá o mesmo resultado em qualquer
// computador, seja qual for o jogador daquela tela. É o que o lockstep exige.
static void coop_same_game_on_every_screen(void)
{
    static World a, b;
    static WavePlan plan;
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(make_world(&a, &arena_a, &plan, 2, 0));
    CHECK(make_world(&b, &arena_b, &plan, 2, 1));
    CHECK(world_hash(&a) == world_hash(&b));
    Vector3 start0 = a.players[0].ship.pos, start1 = a.players[1].ship.pos;
    CHECK(Vector3Distance(start0, start1) > 1.0f);   // nascem separados

    InputState in[SHIPS_MAX];
    bool same = true;
    for (int i = 0; i < seconds(40.0f) && same; i++) {
        for (int p = 0; p < SHIPS_MAX; p++) in[p] = scripted(p, (uint32_t)i);
        world_tick_all(&a, in, &tun);
        world_tick_all(&b, in, &tun);
        if (i % 60 == 0) same = world_hash(&a) == world_hash(&b);
    }
    CHECK(same);
    CHECK(world_hash(&a) == world_hash(&b));
    // As duas naves andaram, cada uma do seu jeito; cada tela mostra a sua.
    CHECK(Vector3Distance(a.players[0].ship.pos, start0) > 5.0f);
    CHECK(Vector3Distance(a.players[1].ship.pos, start1) > 5.0f);
    CHECK(Vector3Distance(a.players[0].ship.pos, a.players[1].ship.pos) > 1.0f);
    CHECK(Vector3Distance(a.ship.pos, a.players[0].ship.pos) < 1e-4f);
    CHECK(Vector3Distance(b.ship.pos, b.players[1].ship.pos) < 1e-4f);
    CHECK(a.score.kills > 0);   // e lutaram
}

// Mais jogadores, mais difícil: inimigos com mais vida, bosses com mais vida.
static void coop_scales_difficulty(void)
{
    static World w;
    static WavePlan plan;
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(make_world(&w, &arena_a, &plan, 3, 0));
    world_set_waves(&w, false);
    Vector3 spot = { w.planet.params.radius * 3.0f, 0.0f, 0.0f };
    world_spawn(&w, &tun, ENEMY_SWARM, 1, spot);
    int i = 0;
    while (i < w.enemies.pool.capacity && !pool_alive(&w.enemies.pool, i)) i++;
    CHECK(i < w.enemies.pool.capacity);
    if (i < w.enemies.pool.capacity) {
        CHECK_NEAR(w.enemies.items[i].hp, tun.enemy[ENEMY_SWARM].hp * (1.0f + 2.0f * tun.coop_hp), 1e-3);
    }
    world_spawn_boss(&w, &tun, BOSS_BREAKER, (Vector3){ 0.0f, 1.0f, 0.0f });
    CHECK_NEAR(w.boss.hp_max, tun.breaker_hp * (1.0f + 2.0f * tun.coop_boss_hp), 1e-2);
}

// O XP é do time: o abate de um evolui todos (cada um escolhe as próprias cartas).
static void coop_shares_xp(void)
{
    static World w;
    static WavePlan plan;
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(make_world(&w, &arena_a, &plan, 2, 0));
    world_set_waves(&w, false);
    world_spawn(&w, &tun, ENEMY_BOMBER, 1, (Vector3){ 0.0f, 0.0f, w.planet.params.radius * 2.5f });
    int i = 0;
    while (i < w.enemies.pool.capacity && !pool_alive(&w.enemies.pool, i)) i++;
    world_use_player(&w, 1);
    w.enemies.damage_owner = 1;
    enemies_damage(&w.enemies, i, 1e6f, DEATH_PLAYER);
    combat_sweep_for(&w, &tun, 1);
    world_use_player(&w, 0);
    CHECK(w.players[1].evo.essence[ENEMY_BOMBER] > 0.0f);
    CHECK(w.evo.essence[ENEMY_BOMBER] > 0.0f);
    CHECK(w.score.kills == 1);
}

// O raio trator é de cada um: a rocha do jogador 1 segue a nave dele.
static void coop_each_player_tows_own_rock(void)
{
    static World w;
    static WavePlan plan;
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(make_world(&w, &arena_a, &plan, 2, 0));
    world_set_waves(&w, false);
    // A nave 1 parada, com uma rocha bem na frente dela.
    world_use_player(&w, 1);
    Vector3 pos = w.ship.pos;
    Rock *k = world_spawn_rock(&w, &tun, ROCK_FRAGMENT, Vector3Add(pos, Vector3Scale(ship_forward(&w.ship), 12.0f)),
                               (Vector3){ 0.0f, 0.0f, 0.0f });
    CHECK(k != NULL);
    world_use_player(&w, 0);
    InputState in[SHIPS_MAX];
    memset(in, 0, sizeof in);
    in[1].held = in[1].pressed = ACT_BIT(ACT_TRACTOR);
    world_tick_all(&w, in, &tun);
    CHECK(w.players[1].towed >= 0 && w.players[0].towed < 0);
    // O jogador 0 voa; a rocha fica com a nave 1 (parada), e o 0 continua sem rocha.
    memset(in, 0, sizeof in);
    in[0].held = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_LEFT);
    for (int i = 0; i < seconds(2.0f); i++) world_tick_all(&w, in, &tun);
    CHECK(w.players[1].towed >= 0);
    if (w.players[1].towed >= 0) {
        const Rock *r = &w.rocks.items[w.players[1].towed];
        CHECK(Vector3Distance(r->pos, w.players[1].ship.pos) < tun.tractor_break + r->radius);
        CHECK(Vector3Distance(r->pos, w.players[0].ship.pos) > Vector3Distance(r->pos, w.players[1].ship.pos));
    }
    CHECK(w.players[0].towed < 0);
}

// A partida online começa igual em todos os computadores, mesmo que um deles
// tenha jogado sozinho antes (planeta ferido, relógio adiantado, inimigos).
static void coop_start_is_identical_after_any_history(void)
{
    static World a, b;
    static WavePlan plan;
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    CHECK(make_world(&a, &arena_a, &plan, 1, 0));
    CHECK(make_world(&b, &arena_b, &plan, 1, 0));
    InputState solo = scripted(0, 0);
    for (int i = 0; i < seconds(5.0f); i++) {
        solo = scripted(0, (uint32_t)i);
        world_tick(&b, &solo, &tun);
    }
    world_impact(&b, &tun, (Vector3){ 0.0f, 100.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, 5.0f, 4.0f);
    world_start_online(&a, &tun, 1234u, 3, 0);
    world_start_online(&b, &tun, 1234u, 3, 2);
    CHECK(a.tick == 0 && b.tick == 0 && b.player_count == 3 && b.local == 2);
    CHECK(world_hash(&a) == world_hash(&b));
    InputState in[SHIPS_MAX];
    for (int i = 0; i < seconds(6.0f); i++) {
        for (int p = 0; p < SHIPS_MAX; p++) in[p] = scripted(p, (uint32_t)i);
        world_tick_all(&a, in, &tun);
        world_tick_all(&b, in, &tun);
    }
    CHECK(world_hash(&a) == world_hash(&b));
}

// De ponta a ponta, pela rede de verdade (127.0.0.1): um anfitrião e um
// cliente entram na sala, começam, e jogam 10 s com 1 pacote em cada 4
// jogado fora. As duas partidas têm de terminar iguais.
#define TEST_PORT 27123

static void coop_over_the_network(void)
{
    static World worlds[2];
    static WavePlan plan;
    static NetSession net[2];
    char err[160];
    CHECK(waves_load(&plan, "data/waves.txt", err, sizeof err));
    udp_loopback_only(true);
    CHECK(session_host(&net[0], TEST_PORT));
    NetAddress addr;
    CHECK(udp_parse("127.0.0.1:27123", &addr));
    CHECK(session_join(&net[1], addr));
    double now = 0.0;
    for (int i = 0; i < 200 && !(net[1].phase == SESSION_LOBBY && net[0].players == 2); i++) {
        now += 0.05;
        session_poll(&net[0], now);
        session_poll(&net[1], now);
    }
    CHECK(net[1].phase == SESSION_LOBBY && net[1].local == 1 && net[0].players == 2);

    session_start(&net[0], 99u, now);
    for (int i = 0; i < 200 && net[1].phase != SESSION_PLAYING; i++) {
        now += 0.05;
        session_poll(&net[0], now);
        session_poll(&net[1], now);
    }
    CHECK(net[1].phase == SESSION_PLAYING && net[1].seed == 99u && net[1].players == 2);
    net[0].sock.drop_every = 4;
    net[1].sock.drop_every = 4;

    Arena *arenas[2] = { &arena_a, &arena_b };
    for (int k = 0; k < 2; k++) CHECK(make_world(&worlds[k], arenas[k], &plan, 2, net[k].local));
    const uint32_t goal = (uint32_t)seconds(10.0f);
    uint64_t hash_at_goal[2] = { 0u, 1u };
    for (int i = 0; i < 20000 && (worlds[0].tick < goal || worlds[1].tick < goal); i++) {
        now += 1.0 / 240.0;
        for (int k = 0; k < 2; k++) {
            NetSession *s = &net[k];
            session_poll(s, now);
            InputState mine = scripted(s->local, s->local_next);
            session_push(s, &mine, now);
            if (worlds[k].tick < goal && session_ready(s)) {
                InputState in[NET_MAX_PLAYERS];
                session_take(s, in);
                world_tick_all(&worlds[k], in, &tun);
                session_report_hash(s, (uint32_t)worlds[k].tick, world_hash(&worlds[k]));
                if (worlds[k].tick == goal) hash_at_goal[k] = world_hash(&worlds[k]);
            }
        }
    }
    CHECK(worlds[0].tick == goal && worlds[1].tick == goal);
    CHECK(hash_at_goal[0] == hash_at_goal[1]);
    CHECK(!net[0].desync && !net[1].desync);
    session_close(&net[1]);
    for (int i = 0; i < 10; i++) session_poll(&net[0], now += 0.05);
    CHECK(!net[0].joined[1]);   // o anfitrião viu o cliente sair
    session_close(&net[0]);
    udp_loopback_only(false);
}

void suite_coop(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena_a, 8u * 1024u * 1024u));
    CHECK(arena_init(&arena_b, 8u * 1024u * 1024u));
    RUN(coop_same_game_on_every_screen);
    RUN(coop_scales_difficulty);
    RUN(coop_shares_xp);
    RUN(coop_each_player_tows_own_rock);
    RUN(coop_start_is_identical_after_any_history);
    RUN(coop_over_the_network);
    arena_free(&arena_a);
    arena_free(&arena_b);
}
