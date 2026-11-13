#include "test.h"
#include "test_util.h"

#include <stdio.h>

#include "core/pool.h"
#include "game/combat.h"
#include "game/score.h"
#include "game/waves.h"
#include "game/world.h"
#include "tools/autopilot.h"
#include "voxel/damage.h"

static Tuning tun;
static Arena  arena;
static Config cfg;
static char   err[160];

// ---------------------------------------------------------------------------
//  Pool
// ---------------------------------------------------------------------------

static void pool_hands_out_slots_and_handles(void)
{
    Arena a;
    CHECK(arena_init(&a, 4096));
    Pool p;
    CHECK(pool_init(&p, 4, &a));

    int s0 = pool_acquire(&p), s1 = pool_acquire(&p);
    CHECK(s0 == 0 && s1 == 1 && p.live == 2);
    Handle h0 = pool_handle(&p, s0);
    CHECK(h0.value != 0u && pool_resolve(&p, h0) == 0);

    // Liberada e reocupada por outro objeto: o handle velho não vale mais.
    pool_release(&p, s0);
    CHECK(pool_resolve(&p, h0) == -1);
    int again = pool_acquire(&p);
    CHECK(again == 0);
    CHECK(pool_resolve(&p, h0) == -1);
    CHECK(pool_resolve(&p, pool_handle(&p, again)) == 0);

    // Cheio.
    CHECK(pool_acquire(&p) >= 0 && pool_acquire(&p) >= 0);
    CHECK(pool_acquire(&p) == -1);
    CHECK(pool_resolve(&p, (Handle){ 0u }) == -1);

    // Liberar duas vezes não estraga a contagem.
    pool_release(&p, 3);
    pool_release(&p, 3);
    CHECK(p.live == 3);

    // Limpar invalida todos os handles e recomeça a ordem.
    Handle h1 = pool_handle(&p, 1);
    pool_clear(&p);
    CHECK(p.live == 0 && pool_resolve(&p, h1) == -1);
    CHECK(pool_acquire(&p) == 0);
    arena_free(&a);
}

// ---------------------------------------------------------------------------
//  Hordas
// ---------------------------------------------------------------------------

static bool parse(const char *text, WavePlan *plan)
{
    return config_parse(&cfg, text) && waves_parse(plan, &cfg, err, sizeof err);
}

static void waves_real_file_is_valid(void)
{
    static WavePlan plan;
    bool ok = waves_load(&plan, "data/waves.txt", err, sizeof err);
    if (!ok) test_fail(__FILE__, __LINE__, err);
    CHECK(ok);
    CHECK(plan.count >= 3);
    CHECK(plan.waves[0].pause > 0.0f);
    int bombers = 0;
    for (int i = 0; i < plan.count; i++) {
        CHECK(plan.waves[i].group_count > 0);
        bombers += wave_enemy_count(&plan.waves[i], ENEMY_BOMBER);
    }
    CHECK(bombers > 0);
}

static void waves_parse_reads_groups(void)
{
    static WavePlan plan;
    const char *text =
        "wave.1.pause = 3\n"
        "wave.1.group.1 = swarm 6 0 40 10\n"
        "wave.1.group.2 = bomber 1 2.5 -90 -20\n"
        "wave.2.pause = 4.5\n"
        "wave.2.group.1 = swarm 2 0 0 90\n";
    bool ok = parse(text, &plan);
    if (!ok) test_fail(__FILE__, __LINE__, err);
    CHECK(ok);
    CHECK(plan.count == 2);
    CHECK(plan.waves[0].group_count == 2 && plan.waves[1].group_count == 1);
    const WaveGroup *g = &plan.waves[0].groups[1];
    CHECK(g->type == ENEMY_BOMBER && g->count == 1);
    CHECK_NEAR(g->delay, 2.5f, 1e-6);
    CHECK_NEAR(plan.waves[1].pause, 4.5f, 1e-6);
    CHECK(wave_enemy_count(&plan.waves[0], ENEMY_SWARM) == 6);

    // Direção: a elevação 90 é o polo norte; o azimute 90, o eixo +X.
    Vector3 up = wave_group_direction(&plan.waves[1].groups[0]);
    CHECK_NEAR(up.y, 1.0f, 1e-5);
    WaveGroup side = { ENEMY_SWARM, 1, 0.0f, 90.0f, 0.0f };
    CHECK_NEAR(wave_group_direction(&side).x, 1.0f, 1e-5);
}

static void expect_wave_error(const char *text, const char *fragment)
{
    static WavePlan plan;
    plan.count = 77;
    CHECK(!parse(text, &plan));
    CHECK(plan.count == 77);   // com erro, o plano antigo fica
    if (strstr(err, fragment) == NULL) test_fail(__FILE__, __LINE__, err);
    test_checks++;
}

static void waves_parse_rejects_mistakes(void)
{
    expect_wave_error("wave.1.group.1 = swarm 6 0 0 0\n", "wave.1.pause");
    expect_wave_error("wave.1.pause = 1\n", "grupo");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = swarm 6 0 0 0\nwave.3.pause = 1\n"
                      "wave.3.group.1 = swarm 6 0 0 0\n", "horda 2");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.2 = swarm 6 0 0 0\n", "wave.1.group.1");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = dragao 6 0 0 0\n", "dragao");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = swarm 0 0 0 0\n", "fora da faixa");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = swarm 6 0 0 95\n", "fora da faixa");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = swarm 6 0 0\n", "esperado");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.1 = swarm 6 0 0 0 9\n", "esperado");
    expect_wave_error("wave.1.pause = -1\nwave.1.group.1 = swarm 6 0 0 0\n", "pause");
    expect_wave_error("wave.1.pausa = 1\n", "chave desconhecida");
    expect_wave_error("wave.0.pause = 1\n", "hordas vao de 1");
    expect_wave_error("wave.1.pause = 1\nwave.1.group.9 = swarm 1 0 0 0\n", "grupos vao de 1");
}

static void director_runs_the_sequence(void)
{
    static WavePlan plan;
    CHECK(parse("wave.1.pause = 1\n"
                "wave.1.group.1 = swarm 3 0 0 0\n"
                "wave.1.group.2 = swarm 2 0.5 90 0\n"
                "wave.2.pause = 2\n"
                "wave.2.group.1 = bomber 1 0 0 0\n",
                &plan));

    WaveDirector d;
    director_start(&d, &plan);
    CHECK(d.phase == DIRECTOR_PAUSE && d.wave == 0);

    const float dt = 0.1f;
    int alive = 0, started = 0, cleared = 0, finished = 0, spawned = 0;
    int first_spawn_step = -1;
    for (int i = 0; i < 100 && d.phase != DIRECTOR_DONE; i++) {
        DirectorStep step = director_tick(&d, &plan, alive, dt);
        if (step.wave_started) started++;
        if (step.wave_cleared) cleared++;
        if (step.finished) finished++;
        for (int g = 0; g < step.spawn_count; g++) {
            alive += step.spawn[g]->count;
            spawned += step.spawn[g]->count;
            if (first_spawn_step < 0) first_spawn_step = i;
        }
        // Os inimigos morrem um segundo depois de a horda inteira entrar.
        if (d.phase == DIRECTOR_ACTIVE && d.timer > 1.6f) alive = 0;
    }
    CHECK(first_spawn_step >= 9 && first_spawn_step <= 10);   // depois da calmaria de 1 s
    CHECK(started == 2 && cleared == 2 && finished == 1);
    CHECK(spawned == 6);
    CHECK(d.phase == DIRECTOR_DONE);

    // Com inimigos vivos, a horda não acaba.
    director_start(&d, &plan);
    for (int i = 0; i < 200; i++) director_tick(&d, &plan, 5, dt);
    CHECK(d.phase == DIRECTOR_ACTIVE && d.wave == 0);

    // Plano vazio: o diretor não faz nada.
    static WavePlan empty;
    empty.count = 0;
    director_start(&d, &empty);
    CHECK(d.phase == DIRECTOR_OFF);
    DirectorStep none = director_tick(&d, &empty, 0, dt);
    CHECK(!none.wave_started && !none.finished && none.spawn_count == 0);
}

// ---------------------------------------------------------------------------
//  Pontos
// ---------------------------------------------------------------------------

static void score_combo_builds_and_resets(void)
{
    Score s;
    score_reset(&s);
    CHECK(s.multiplier == 1.0f && s.points == 0);

    CHECK(score_kill(&s, &tun, 100.0f) == 100);
    CHECK(score_kill(&s, &tun, 100.0f) == (int)lroundf(100.0f * (1.0f + tun.score_combo_step)));
    CHECK(s.chain == 2 && s.kills == 2);

    // O multiplicador tem teto.
    for (int i = 0; i < 100; i++) score_kill(&s, &tun, 100.0f);
    CHECK_NEAR(s.multiplier, tun.score_combo_max, 1e-5);
    CHECK(score_kill(&s, &tun, 100.0f) == (int)lroundf(100.0f * tun.score_combo_max));

    // Tempo demais sem abater: o combo acaba.
    score_tick(&s, tun.score_combo_window * 0.5f);
    CHECK(s.chain > 0);
    score_tick(&s, tun.score_combo_window);
    CHECK(s.chain == 0 && s.multiplier == 1.0f);
    CHECK(s.best_chain == 103);

    int64_t before = s.points;
    score_bonus(&s, 1000);
    CHECK(s.points == before + 1000 && s.chain == 0);
}

static void score_best_survives_in_a_file(void)
{
    const char *path = "build/test_best.dat";
    remove(path);
    CHECK(score_load_best(path) == 0);
    CHECK(score_save_best(path, 123456));
    CHECK(score_load_best(path) == 123456);
    remove(path);
    CHECK(!score_save_best("pasta_que_nao_existe/best.dat", 1));
}

// ---------------------------------------------------------------------------
//  Combate
// ---------------------------------------------------------------------------

static InputState input(uint32_t held, float dx, float dy)
{
    InputState in = { held, 0u, 0u, dx, dy };
    return in;
}

static void run(World *w, const Tuning *t, InputState in, int ticks)
{
    for (int i = 0; i < ticks; i++) world_tick(w, &in, t);
}

// Mundo sem hordas, com a nave parada longe de tudo, no lado oposto ao
// ponto `away_from`.
static void make_world(World *w, Vector3 away_from)
{
    arena_reset(&arena);
    if (!world_init(w, &tun, 7u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    float radius = w->planet.params.radius;
    Vector3 spot = Vector3Scale(Vector3Normalize(Vector3Negate(away_from)), radius * 4.0f);
    world_place_ship(w, &tun, spot, Vector3Add(spot, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
}

static int first_enemy(const World *w)
{
    for (int i = 0; i < w->enemies.pool.capacity; i++) {
        if (pool_alive(&w->enemies.pool, i)) return i;
    }
    return -1;
}

static bool saw_event(const World *w, EventType type)
{
    for (uint32_t id = 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e != NULL && e->type == type) return true;
    }
    return false;
}

// Um tiro "à mão": sai de `from` em direção a `to`.
static void shoot(World *w, Vector3 from, Vector3 to, float speed)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(to, from));
    Shot *s = projectiles_spawn(&w->shots, SHOT_BOLT, from, Vector3Scale(dir, speed), 5.0f);
    if (s != NULL) s->damage = tun.weapon_damage;
}

// Só a parte do combate de um passo: projéteis andam, colisões, mortes.
static void resolve(World *w, const Tuning *t, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        projectiles_move(&w->shots, 1.0f / (float)t->sim_hz);
        combat_resolve(w, t);
        combat_sweep(w, t);
    }
}

static void bolts_destroy_a_drone_and_score(void)
{
    static World w;
    Vector3 spot = { 0.0f, 60.0f, 0.0f };
    make_world(&w, spot);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    CHECK(world_spawn(&w, &t, ENEMY_SWARM, 1, spot) == 1);
    int index = first_enemy(&w);
    Vector3 at = w.enemies.items[index].pos;
    int64_t volume = w.planet.volume;

    for (int i = 0; i < (int)t.enemy[ENEMY_SWARM].hp; i++) {
        float hp = w.enemies.items[index].hp;
        shoot(&w, Vector3Add(at, (Vector3){ -30.0f, 0.0f, 0.0f }), at, t.weapon_speed);
        resolve(&w, &t, 10);
        CHECK(w.shots.pool.live == 0);   // o tiro some ao acertar
        if (i + 1 < (int)t.enemy[ENEMY_SWARM].hp) {
            CHECK_NEAR(w.enemies.items[index].hp, hp - t.weapon_damage, 1e-5);
            CHECK(w.enemies.items[index].flash > 0.0f);
            CHECK(w.hit_marker > 0.0f);
        }
    }
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(w.score.kills == 1 && w.score.kills_by_type[ENEMY_SWARM] == 1);
    CHECK(w.score.points == (int64_t)t.enemy[ENEMY_SWARM].points);
    CHECK(saw_event(&w, EV_HIT) && saw_event(&w, EV_KILL));
    CHECK(w.planet.volume == volume);   // ninguém tocou no planeta
    CHECK(w.particles.count > 20);      // explosão
}

static void bolts_do_not_dig_the_planet(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    float radius = w.planet.params.radius;
    int64_t volume = w.planet.volume;

    shoot(&w, (Vector3){ 0.0f, radius + 20.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }, tun.weapon_speed);
    resolve(&w, &tun, 20);
    CHECK(w.shots.pool.live == 0);
    CHECK(w.planet.volume == volume && w.impact_count == 0u);
    CHECK(saw_event(&w, EV_SPARK));
}

static void fast_bolts_never_pass_through(void)
{
    static World w;
    Vector3 spot = { 0.0f, 60.0f, 0.0f };
    make_world(&w, spot);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_SWARM, 1, spot);
    int index = first_enemy(&w);
    Vector3 at = w.enemies.items[index].pos;

    // A 3000 por segundo, o tiro anda 50 em um passo: muito mais que o drone.
    shoot(&w, Vector3Add(at, (Vector3){ -40.0f, 0.0f, 0.0f }), at, 3000.0f);
    resolve(&w, &t, 3);
    CHECK(w.enemies.items[index].hp < t.enemy[ENEMY_SWARM].hp);
}

static void bomber_armor_and_weak_spot(void)
{
    static World w;
    Vector3 spot = { 0.0f, 80.0f, 0.0f };
    make_world(&w, spot);
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_BOMBER, 1, spot);
    int index = first_enemy(&w);
    Enemy *en = &w.enemies.items[index];
    en->rot = quat_look((Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });   // nariz para +X
    Vector3 weak = enemy_weak_spot(en, &t);
    CHECK(weak.x < en->pos.x - 1.0f);   // o motor fica atrás

    // De frente: a blindagem segura quase tudo.
    float hp = en->hp;
    shoot(&w, Vector3Add(en->pos, (Vector3){ 40.0f, 0.0f, 0.0f }), en->pos, t.weapon_speed);
    resolve(&w, &t, 10);
    CHECK_NEAR(en->hp, hp - t.weapon_damage * t.bomber_armor, 1e-4);
    CHECK(saw_event(&w, EV_ARMOR_HIT) && !saw_event(&w, EV_WEAK_HIT));

    // Por trás, no motor: dano multiplicado.
    hp = en->hp;
    shoot(&w, Vector3Add(weak, (Vector3){ -40.0f, 0.0f, 0.0f }), weak, t.weapon_speed);
    resolve(&w, &t, 10);
    CHECK_NEAR(en->hp, hp - t.weapon_damage * t.bomber_weak_mult, 1e-4);
    CHECK(saw_event(&w, EV_WEAK_HIT));
    CHECK(t.bomber_weak_mult > t.bomber_armor * 4.0f);   // vale muito a pena dar a volta
}

// Bombardeiro em órbita: marca o alvo no solo, espera o aviso, solta a bomba,
// e a bomba cai em cima da marca.
static void bomber_marks_the_target_then_bombs_it(void)
{
    static World w;
    arena_reset(&arena);
    CHECK(world_init(&w, &tun, 7u, &arena));
    float radius = w.planet.params.radius;
    Vector3 spot = { radius + tun.bomber_altitude, 0.0f, 0.0f };
    // A nave fica longe, do outro lado, fora do alcance da torreta.
    world_place_ship(&w, &tun, (Vector3){ -radius * 5.0f, 0.0f, 0.0f }, (Vector3){ -radius * 5.0f, 0.0f, 10.0f }, 0.0f);

    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    world_spawn(&w, &t, ENEMY_BOMBER, 1, spot);
    Enemy *en = &w.enemies.items[first_enemy(&w)];
    en->state = BOMBER_ORBIT;
    en->timer = 0.05f;
    int64_t volume = w.planet.volume;

    // Armando: o alvo aparece como evento, sobre a superfície.
    run(&w, &t, input(0u, 0.0f, 0.0f), 6);
    CHECK(en->arming > 0.0f);
    CHECK(saw_event(&w, EV_BOMB_ARMING));
    Vector3 target = en->target;
    CHECK_NEAR(Vector3Length(target), radius, tun.planet_land_height + 1.5f);
    CHECK(projectiles_count(&w.shots, SHOT_BOMB) == 0);   // ainda não soltou

    // Depois do aviso, a bomba sai...
    run(&w, &t, input(0u, 0.0f, 0.0f), (int)(t.bomber_bomb_warning * (float)t.sim_hz) + 2);
    CHECK(saw_event(&w, EV_BOMB_DROP));
    CHECK(projectiles_count(&w.shots, SHOT_BOMB) == 1);
    CHECK(w.planet.volume == volume);

    // ... e cai na marca.
    run(&w, &t, input(0u, 0.0f, 0.0f), t.sim_hz * 5);
    CHECK(saw_event(&w, EV_BOMB_IMPACT));
    CHECK(w.planet.volume < volume);
    const Impact *impact = world_find_impact(&w, 1u);
    CHECK(impact != NULL);
    if (impact != NULL) CHECK(Vector3Distance(impact->pos, target) < 2.0f);
}

static void bombs_can_be_shot_down(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    float radius = w.planet.params.radius;
    int64_t volume = w.planet.volume;

    Vector3 start = { 0.0f, radius + 30.0f, 0.0f };
    Shot *b = projectiles_spawn(&w.shots, SHOT_BOMB, start, (Vector3){ 0.0f, -1.0f, 0.0f }, 20.0f);
    b->target = (Vector3){ 0.0f, radius, 0.0f };
    b->hp = tun.bomber_bomb_hp;
    b->crater_radius = tun.bomber_crater_radius;
    b->crater_depth = tun.bomber_crater_depth;

    for (int i = 0; i < (int)ceilf(tun.bomber_bomb_hp / tun.weapon_damage); i++) {
        shoot(&w, Vector3Add(start, (Vector3){ -30.0f, -1.0f, 0.0f }), Vector3Add(start, (Vector3){ 0.0f, -1.0f, 0.0f }),
              tun.weapon_speed);
        resolve(&w, &tun, 8);
    }
    CHECK(projectiles_count(&w.shots, SHOT_BOMB) == 0);
    CHECK(saw_event(&w, EV_BOMB_DOWN));
    CHECK(w.score.bombs_down == 1 && w.score.points == (int64_t)tun.bomber_bomb_points);
    resolve(&w, &tun, 60);
    CHECK(w.planet.volume == volume);
}

// Um bando faz passadas de ataque: muitas crateras pequenas, sem bater no chão.
// As distâncias do Enxame foram pensadas para o planeta de verdade, então
// este teste usa o planeta em tamanho real (e não o pequeno dos outros).
static void swarm_attack_runs_dig_small_craters(void)
{
    static World w;
    Tuning real;
    memset(&real, 0, sizeof real);
    CHECK(tuning_load(&real, "data", err, sizeof err));
    arena_reset(&arena);
    CHECK(world_init(&w, &real, 7u, &arena));
    Vector3 from = { 0.3f, 1.0f, 0.2f };
    float radius = w.planet.params.radius;
    Vector3 spot = Vector3Scale(Vector3Normalize(Vector3Negate(from)), radius * 4.0f);
    world_place_ship(&w, &real, spot, Vector3Add(spot, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    int64_t volume = w.planet.volume;
    world_spawn(&w, &real, ENEMY_SWARM, 4, Vector3Scale(Vector3Normalize(from), radius * real.enemies_spawn_distance));

    float lowest = 1e9f;
    for (int i = 0; i < real.sim_hz * 14; i++) {
        run(&w, &real, input(0u, 0.0f, 0.0f), 1);
        for (int k = 0; k < w.enemies.pool.capacity; k++) {
            if (!pool_alive(&w.enemies.pool, k)) continue;
            float altitude = Vector3Length(w.enemies.items[k].pos) - radius;
            if (altitude < lowest) lowest = altitude;
        }
    }
    CHECK(w.impact_count >= 6u);
    CHECK(w.planet.volume < volume);
    CHECK(enemies_alive(&w.enemies) == 4);     // nenhum bateu
    CHECK(!saw_event(&w, EV_CRASH));
    CHECK(lowest > real.planet_land_height);   // passaram rente, mas acima do relevo
    CHECK(w.enemies.flocks[w.enemies.items[first_enemy(&w)].flock].pass > 0);   // e o alvo andou

    // Os tiros caíram perto de onde o bando chegou, não do outro lado do planeta.
    const Impact *impact = world_find_impact(&w, 1u);
    CHECK(impact != NULL);
    if (impact != NULL) CHECK(Vector3DotProduct(Vector3Normalize(impact->pos), Vector3Normalize(from)) > 0.6f);
}

static void enemy_shots_hurt_the_ship_but_not_during_dash(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    Ship *s = &w.ship;
    float shield = s->shield;

    Shot *p = projectiles_spawn(&w.shots, SHOT_PLASMA, Vector3Add(s->pos, (Vector3){ 20.0f, 0.0f, 0.0f }),
                                (Vector3){ -55.0f, 0.0f, 0.0f }, 3.0f);
    p->damage = 6.0f;
    resolve(&w, &tun, 30);
    CHECK_NEAR(s->shield, shield - 6.0f, 1e-4);
    CHECK(s->hull == tun.ship_hull_max);   // o escudo segurou
    CHECK(s->hit_flash > 0.0f && s->hit_shield);
    CHECK(saw_event(&w, EV_PLAYER_HIT));
    CHECK(w.shots.pool.live == 0);

    // Durante o dash, o tiro passa pela nave sem fazer nada.
    s->dash_left = 1.0f;
    shield = s->shield;
    p = projectiles_spawn(&w.shots, SHOT_PLASMA, Vector3Add(s->pos, (Vector3){ 20.0f, 0.0f, 0.0f }),
                          (Vector3){ -55.0f, 0.0f, 0.0f }, 3.0f);
    p->damage = 6.0f;
    resolve(&w, &tun, 30);
    CHECK(s->shield == shield);

    // Golpe forte: congela o jogo por um instante.
    s->dash_left = 0.0f;
    combat_damage_ship(&w, &tun, 40.0f, s->pos);
    CHECK(w.hitstop == (int)lroundf(tun.combat_hitstop_player * (float)tun.sim_hz));
}

static void ramming_a_drone_destroys_it(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    Ship *s = &w.ship;
    world_spawn(&w, &t, ENEMY_SWARM, 1, Vector3Add(s->pos, (Vector3){ 1.0f, 0.0f, 0.0f }));
    float shield = s->shield;
    resolve(&w, &t, 1);
    CHECK(enemies_alive(&w.enemies) == 0);
    CHECK(w.score.kills == 1);
    CHECK_NEAR(s->shield, shield - t.enemy[ENEMY_SWARM].ram_damage, 1e-4);
}

// Hit-stop: a partida para por alguns passos, e o que o jogador fez nesse
// tempo vale assim que ela volta.
static void hitstop_freezes_and_keeps_the_input(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    run(&w, &tun, input(ACT_BIT(ACT_FORWARD), 0.0f, 0.0f), 30);
    w.hitstop = 3;
    Vector3 pos = w.ship.pos;
    Quaternion rot = w.ship.rot;
    uint64_t tick = w.tick;

    InputState turn = input(ACT_BIT(ACT_FORWARD), 4.0f, 0.0f);
    turn.pressed = ACT_BIT(ACT_DASH);
    run(&w, &tun, turn, 3);
    CHECK(w.tick == tick + 3);   // o tempo corre
    CHECK(Vector3Distance(w.ship.pos, pos) == 0.0f && Vector3Distance(w.ship.pos_prev, pos) == 0.0f);
    CHECK(QuaternionEquals(w.ship.rot, rot));
    CHECK_NEAR(w.frozen_input.look_dx, 12.0f, 1e-5);
    CHECK((w.frozen_input.pressed & ACT_BIT(ACT_DASH)) != 0u);

    // Voltou: o dash apertado durante o congelamento acontece agora.
    run(&w, &tun, input(ACT_BIT(ACT_FORWARD), 0.0f, 0.0f), 1);
    CHECK(Vector3Distance(w.ship.pos, pos) > 0.0f);
    CHECK(ship_invulnerable(&w.ship));
    CHECK(w.frozen_input.look_dx == 0.0f);
}

static void aim_assist_picks_what_the_nose_points_at(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    Tuning t = tun;
    t.enemies_spawn_spread = 0.0f;
    const Ship *s = &w.ship;
    Vector3 forward = ship_forward(s), right = ship_right(s);

    // Um inimigo um pouco fora do nariz, outro bem de lado.
    world_spawn(&w, &t, ENEMY_SWARM, 1, Vector3Add(s->pos, Vector3Add(Vector3Scale(forward, 80.0f), Vector3Scale(right, 3.0f))));
    world_spawn(&w, &t, ENEMY_SWARM, 1, Vector3Add(s->pos, Vector3Add(Vector3Scale(forward, 40.0f), Vector3Scale(right, 40.0f))));
    AimAssist aim;
    CHECK(combat_aim_assist(&w, &t, &aim));
    CHECK(aim.enemy == 0 && aim.bomb == -1);

    // Desligada, ou fora do alcance, não escolhe nada.
    t.weapon_assist_angle = 0.0f;
    CHECK(!combat_aim_assist(&w, &t, &aim) && aim.enemy == -1);
    t = tun;
    t.weapon_assist_range = 50.0f;
    CHECK(!combat_aim_assist(&w, &t, &aim));
}

static void the_gun_fires_at_its_rate(void)
{
    static World w;
    make_world(&w, (Vector3){ 0.0f, 1.0f, 0.0f });
    int shots_before = (int)w.events.count;
    run(&w, &tun, input(ACT_BIT(ACT_FIRE), 0.0f, 0.0f), 1);
    CHECK(w.ship.muzzle > 0.0f);   // o primeiro tiro sai na hora
    run(&w, &tun, input(ACT_BIT(ACT_FIRE), 0.0f, 0.0f), tun.sim_hz - 1);
    int fired = 0;
    for (uint32_t id = (uint32_t)shots_before + 1; id <= w.events.count; id++) {
        const GameEvent *e = events_find(&w.events, id);
        if (e != NULL && e->type == EV_SHOT) fired++;
    }
    CHECK(abs(fired - (int)tun.weapon_rate) <= 1);
    CHECK(projectiles_count(&w.shots, SHOT_BOLT) > 0);

    // Soltou o gatilho: para de atirar.
    run(&w, &tun, input(0u, 0.0f, 0.0f), tun.sim_hz * 2);
    CHECK(projectiles_count(&w.shots, SHOT_BOLT) == 0);
}

// ---------------------------------------------------------------------------
//  A partida
// ---------------------------------------------------------------------------

static void waves_play_out_to_victory(void)
{
    static World w;
    static WavePlan plan;
    make_world(&w, (Vector3){ 0.0f, 0.0f, 1.0f });
    CHECK(parse("wave.1.pause = 0.5\nwave.1.group.1 = swarm 2 0 0 0\n", &plan));
    world_set_plan(&w, &plan);

    run(&w, &tun, input(0u, 0.0f, 0.0f), tun.sim_hz);
    CHECK(saw_event(&w, EV_WAVE_START));
    CHECK(enemies_alive(&w.enemies) == 2);
    CHECK(w.state == GAME_PLAYING);

    for (int i = 0; i < w.enemies.pool.capacity; i++) {
        if (pool_alive(&w.enemies.pool, i)) enemies_damage(&w.enemies, i, 1000.0f, DEATH_PLAYER);
    }
    run(&w, &tun, input(0u, 0.0f, 0.0f), 2);
    CHECK(w.state == GAME_WON);
    CHECK(saw_event(&w, EV_WAVE_CLEAR) && saw_event(&w, EV_GAME_WON));
    CHECK(w.score.planet_bonus == (int64_t)tun.score_planet_bonus);   // planeta intacto
    CHECK(w.score.kills == 2);
}

static void collapse_loses_and_restart_starts_over(void)
{
    static World w;
    static WavePlan plan;
    make_world(&w, (Vector3){ 0.0f, 0.0f, 1.0f });
    CHECK(parse("wave.1.pause = 0.5\nwave.1.group.1 = swarm 2 0 0 0\n", &plan));
    world_set_plan(&w, &plan);
    run(&w, &tun, input(0u, 0.0f, 0.0f), tun.sim_hz);

    // Arranca o planeta pelo meio.
    planet_carve(&w.planet, (Vector3){ 0.0f, 0.0f, 0.0f }, w.planet.params.radius * 0.9f);
    run(&w, &tun, input(0u, 0.0f, 0.0f), 1);
    CHECK(w.state == GAME_LOST && saw_event(&w, EV_GAME_LOST));

    // Depois do fim, abates não contam mais.
    int index = first_enemy(&w);
    if (index >= 0) enemies_damage(&w.enemies, index, 1000.0f, DEATH_PLAYER);
    run(&w, &tun, input(0u, 0.0f, 0.0f), 2);
    CHECK(w.score.points == 0);

    world_restart(&w, &tun);
    CHECK(w.state == GAME_PLAYING && !w.collapsed);
    CHECK(planet_hp(&w.planet) > 0.999f);
    CHECK(enemies_alive(&w.enemies) == 0 && w.shots.pool.live == 0);
    CHECK(w.score.points == 0 && w.score.kills == 0);
    CHECK(w.director.phase == DIRECTOR_PAUSE && w.director.wave == 0);
    CHECK(w.ship.alive && w.cam_mode == CAM_SHIP);
}

static void the_match_pauses_outside_the_game_camera(void)
{
    static World w;
    static WavePlan plan;
    Vector3 spot = { 0.0f, 60.0f, 0.0f };
    make_world(&w, spot);
    CHECK(parse("wave.1.pause = 3\nwave.1.group.1 = swarm 2 0 0 0\n", &plan));
    world_set_plan(&w, &plan);
    world_spawn(&w, &tun, ENEMY_SWARM, 3, spot);
    run(&w, &tun, input(0u, 0.0f, 0.0f), 10);

    // F2: tudo para.
    InputState f2 = input(0u, 0.0f, 0.0f);
    f2.pressed = ACT_BIT(ACT_FREECAM);
    run(&w, &tun, f2, 1);
    CHECK(w.cam_mode == CAM_FREE);
    Vector3 enemy = w.enemies.items[first_enemy(&w)].pos;
    float timer = w.director.timer;
    run(&w, &tun, input(0u, 0.0f, 0.0f), 60);
    CHECK(Vector3Distance(w.enemies.items[first_enemy(&w)].pos, enemy) == 0.0f);
    CHECK(w.director.timer == timer);

    // F2 de novo: tudo volta a andar.
    run(&w, &tun, f2, 1);
    run(&w, &tun, input(0u, 0.0f, 0.0f), 10);
    CHECK(Vector3Distance(w.enemies.items[first_enemy(&w)].pos, enemy) > 0.0f);
}

// O piloto automático (ferramenta de teste) consegue vencer uma horda
// pequena passando pela mesma cadeia de input do jogador.
static void autopilot_beats_a_small_horde(void)
{
    static World w;
    static WavePlan plan;
    arena_reset(&arena);
    CHECK(world_init(&w, &tun, 3u, &arena));
    CHECK(parse("wave.1.pause = 1\nwave.1.group.1 = swarm 3 0 11 19\n", &plan));
    world_set_plan(&w, &plan);

    InputTracker tracker;
    input_init(&tracker);
    for (int i = 0; i < tun.sim_hz * 60 && w.state == GAME_PLAYING; i++) {
        InputSample sample = { 0u, 0.0f, 0.0f };
        autopilot_drive(&w, &tun, &sample);
        input_feed(&tracker, &sample);
        InputState st = input_tick(&tracker);
        world_tick(&w, &st, &tun);
    }
    CHECK(w.state == GAME_WON);
    CHECK(w.score.kills == 3);
}

void suite_pool(void)
{
    RUN(pool_hands_out_slots_and_handles);
}

void suite_waves(void)
{
    tun = test_tuning();
    RUN(waves_real_file_is_valid);
    RUN(waves_parse_reads_groups);
    RUN(waves_parse_rejects_mistakes);
    RUN(director_runs_the_sequence);
    RUN(score_combo_builds_and_resets);
    RUN(score_best_survives_in_a_file);
}

void suite_combat(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(bolts_destroy_a_drone_and_score);
    RUN(bolts_do_not_dig_the_planet);
    RUN(fast_bolts_never_pass_through);
    RUN(bomber_armor_and_weak_spot);
    RUN(bomber_marks_the_target_then_bombs_it);
    RUN(bombs_can_be_shot_down);
    RUN(swarm_attack_runs_dig_small_craters);
    RUN(enemy_shots_hurt_the_ship_but_not_during_dash);
    RUN(ramming_a_drone_destroys_it);
    RUN(hitstop_freezes_and_keeps_the_input);
    RUN(aim_assist_picks_what_the_nose_points_at);
    RUN(the_gun_fires_at_its_rate);
    RUN(waves_play_out_to_victory);
    RUN(collapse_loses_and_restart_starts_over);
    RUN(the_match_pauses_outside_the_game_camera);
    RUN(autopilot_beats_a_small_horde);
    arena_free(&arena);
}
