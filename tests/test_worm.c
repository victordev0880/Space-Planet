// Testes do marco 11: o Verme do Núcleo e o Túnel.
#include "test.h"
#include "test_util.h"

#include "core/input.h"
#include "game/combat.h"
#include "game/world.h"

#define ID_TAIL  WORM_NODULES
#define ID_MOUTH (WORM_NODULES + 1)

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

// O planeta dos testes é pequeno (raio ~17): o Verme encolhe junto.
static Tuning small(void)
{
    Tuning t = tun;
    t.worm_scale = 1.2f;
    t.worm_spacing = 1.1f;
    t.worm_depth = 5.0f;
    t.worm_tunnel_radius = 2.0f;
    t.worm_tunnel_spacing = 1.5f;
    t.worm_tunnel_guard = 5.0f;
    t.worm_breach_height = 14.0f;
    t.worm_breach_radius = 2.5f;
    t.worm_breach_depth = 1.2f;
    t.worm_breach_blast = 4.0f;
    t.worm_core_bite = 2.0f;
    t.worm_swim_depth = 1.0f;
    t.worm_furrow_radius = 1.6f;
    t.worm_speed = 14.0f;
    t.worm_swim_speed = 16.0f;
    t.tunnel_altitude = 6.0f;
    return t;
}

// Mundo sem hordas, a nave longe (bem acima do planeta) e o Verme chegando por +y.
static void make_world(World *w, const Tuning *t)
{
    arena_reset(&arena);
    if (!world_init(w, t, 33u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
    Vector3 far = { 0.0f, -w->planet.params.radius * 6.0f, 0.0f };
    world_place_ship(w, t, far, Vector3Add(far, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    world_spawn_boss(w, t, BOSS_WORM, (Vector3){ 0.0f, 1.0f, 0.0f });
}

static void until_action(World *w, const Tuning *t, int action, float limit)
{
    for (int i = 0; i < seconds(limit) && !(w->boss.state == BOSS_FIGHT && w->boss.action == action); i++) {
        run(w, t, keys(0u), 1);
    }
}

// Ele mergulha no planeta e cava um túnel: o planeta perde massa.
static void worm_dives_and_digs(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    int64_t volume = w.planet.volume;
    CHECK(w.boss.state == BOSS_ENTER);
    run(&w, &t, keys(0u), seconds(t.worm_enter_time + 1.0f));
    CHECK(w.boss.state == BOSS_FIGHT);
    CHECK(worm_underground(&w.boss, &w.planet));
    CHECK(w.planet.volume < volume);
    CHECK(w.boss.worm.tunnel_count > 0);
    CHECK(saw_event(&w, EV_WORM_DIVE));
}

// O aviso vem antes da saída; no salto a boca abre (dano inteiro).
static void worm_breaches_with_warning(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    until_action(&w, &t, WORM_RUMBLE, 25.0f);
    CHECK(w.boss.action == WORM_RUMBLE && saw_event(&w, EV_WORM_RUMBLE));
    CHECK(boss_core_factor(&w.boss, &t) < 0.1f);   // boca fechada debaixo da terra
    uint32_t impacts = w.impact_count;
    until_action(&w, &t, WORM_BREACH, 5.0f);
    CHECK(w.boss.action == WORM_BREACH && saw_event(&w, EV_WORM_BREACH));
    CHECK(w.impact_count > impacts);   // a cratera da saída
    CHECK_NEAR(boss_core_factor(&w.boss, &t), 1.0f, 1e-6);
    float hp = w.boss.hp;
    CHECK_NEAR(boss_hit(&w.boss, &t, ID_MOUTH, 10.0f, &w.events, (uint32_t)w.tick), 10.0f, 1e-4);
    CHECK_NEAR(w.boss.hp, hp - 10.0f, 1e-3);

    // No ar, os nódulos ficam à mostra: cada um destruído tira uma fatia da vida.
    run(&w, &t, keys(0u), seconds(t.worm_breach_time * 0.4f));
    int open = -1;
    for (int n = 0; n < WORM_NODULES; n++) {
        if (w.boss.worm.nodule_open[n]) open = n;
    }
    CHECK(open >= 0);
    if (open >= 0) {
        hp = w.boss.hp;
        boss_hit(&w.boss, &t, open, 1000.0f, &w.events, (uint32_t)w.tick);
        CHECK(saw_event(&w, EV_WORM_NODULE));
        CHECK_NEAR(w.boss.hp, hp - t.worm_nodule_damage * w.boss.hp_max, 1e-2);
    }
}

// O túnel desmorona depois de um tempo, mas não com a nave dentro dele.
static void tunnel_collapses_unless_ship_inside(void)
{
    static World w;
    Tuning t = small();
    t.worm_tunnel_life = 2.0f;
    make_world(&w, &t);
    run(&w, &t, keys(0u), seconds(t.worm_enter_time + 0.5f));
    CHECK(w.boss.worm.tunnel_count > 0);
    // A nave para dentro do trecho mais velho.
    Vector3 oldest = w.boss.worm.tunnel[w.boss.worm.tunnel_first];
    w.ship.pos = w.ship.pos_prev = oldest;
    w.ship.vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    run(&w, &t, keys(0u), seconds(t.worm_tunnel_life + 0.5f));
    CHECK(planet_sample(&w.planet, oldest) < VOXEL_ISO);   // ainda aberto: a nave está ali

    // Sem a nave, desmorona: o planeta recupera massa.
    Vector3 far = { 0.0f, -w.planet.params.radius * 6.0f, 0.0f };
    world_place_ship(&w, &t, far, Vector3Add(far, (Vector3){ 0.0f, 0.0f, 1.0f }), 0.0f);
    run(&w, &t, keys(0u), seconds(1.0f));
    CHECK(planet_sample(&w.planet, oldest) >= VOXEL_ISO);
}

// Acertado o ferrão debaixo da terra, ele sai na hora, atordoado.
static void tail_hit_makes_it_panic(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    until_action(&w, &t, WORM_BURROW, 10.0f);
    run(&w, &t, keys(0u), seconds(0.5f));
    CHECK(w.boss.action == WORM_BURROW);
    w.boss.worm.tail_open = true;   // a nave está no túnel, atrás dele
    CHECK(boss_hit(&w.boss, &t, ID_TAIL, 1000.0f, &w.events, (uint32_t)w.tick) > 0.0f);
    run(&w, &t, keys(0u), 1);
    CHECK(w.boss.action == WORM_RUMBLE && w.boss.worm.panic);
    CHECK(saw_event(&w, EV_BOSS_STAGGER));
    until_action(&w, &t, WORM_BREACH, 3.0f);
    CHECK_NEAR(w.boss.worm.arc_total, t.worm_panic_time, 1e-4);   // um salto lento
}

// Da fase 2 em diante, ele passa pelo núcleo e o morde.
static void phase_two_bites_the_core(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    until_action(&w, &t, WORM_BURROW, 10.0f);
    boss_set_health(&w.boss, &t, 0.5f, &w.events, (uint32_t)w.tick);
    CHECK(w.boss.phase == 2);
    for (int i = 0; i < seconds(60.0f) && !saw_event(&w, EV_WORM_CORE); i++) run(&w, &t, keys(0u), 1);
    CHECK(saw_event(&w, EV_WORM_CORE));
    run(&w, &t, keys(0u), 2);
    CHECK(planet_core_hp(&w.planet) < 1.0f);
}

// Morto, ele dá o Túnel: a nave atravessa o planeta.
static void tunnel_ability_crosses_the_planet(void)
{
    static World w;
    Tuning t = small();
    make_world(&w, &t);
    run(&w, &t, keys(0u), seconds(t.worm_enter_time + 0.5f));
    boss_set_health(&w.boss, &t, 0.0f, &w.events, (uint32_t)w.tick);
    run(&w, &t, keys(0u), seconds(4.5f));
    CHECK(!boss_active(&w.boss) && w.tunnel.owned);
    for (int i = 0; i < 10 && (w.evo.choosing || w.evo.pending > 0); i++) {
        run(&w, &t, keys(0u), seconds(0.6f));
        run(&w, &t, press(ACT_BIT(ACT_CARD1)), 1);
    }
    Vector3 before = Vector3Normalize(w.ship.pos);
    run(&w, &t, press(ACT_BIT(ACT_ABILITY3)), 1);
    CHECK(saw_event(&w, EV_TUNNEL));
    CHECK(Vector3DotProduct(Vector3Normalize(w.ship.pos), before) < -0.99f);   // do outro lado
    CHECK(w.tunnel.cooldown > 0.0f && ship_invulnerable(&w.ship));
}

void suite_worm(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(worm_dives_and_digs);
    RUN(worm_breaches_with_warning);
    RUN(tunnel_collapses_unless_ship_inside);
    RUN(tail_hit_makes_it_panic);
    RUN(phase_two_bites_the_core);
    RUN(tunnel_ability_crosses_the_planet);
    arena_free(&arena);
}
