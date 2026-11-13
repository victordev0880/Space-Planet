#include "test.h"
#include "test_util.h"

#include "game/moon.h"
#include "game/ship.h"

static Tuning tun;
static Arena  arena;
static Planet planet;
static Moon   moon;
static float  dt;

static void setup(Ship *s)
{
    arena_reset(&arena);
    PlanetParams pp = tuning_planet_params(&tun);
    if (!planet_init(&planet, &pp, &arena)) test_fail(__FILE__, __LINE__, "planet_init falhou");
    moon_init(&moon, &tun, planet.params.radius);
    ship_spawn(s, &tun, &planet);
}

static InputState input(uint32_t held)
{
    InputState in = { held, 0u, 0u, 0.0f, 0.0f };
    return in;
}

static int seconds(float s)
{
    return (int)(s * (float)tun.sim_hz);
}

// Direção qualquer perpendicular a `v`.
static Vector3 sideways(Vector3 v)
{
    Vector3 hint = (fabsf(v.y) > 0.9f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    return Vector3Normalize(Vector3CrossProduct(v, hint));
}

static void moon_circles_the_planet(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;
    float orbit = tun.moon_orbit_radius * radius;

    CHECK_NEAR(moon.radius, tun.moon_radius * radius, 1e-4);
    CHECK_NEAR(Vector3Length(moon.pos), orbit, orbit * 1e-4f);
    CHECK_NEAR(Vector3Distance(moon.pos, moon.pos_prev), 0.0f, 1e-6);
    // A Lua fica longe o bastante para não encostar no planeta.
    CHECK(orbit - moon.radius > radius * 1.5f);

    // Um quarto de volta: sempre à mesma distância, e a posição anterior
    // acompanha um passo atrás.
    Vector3 start = moon.pos;
    int quarter = seconds(tun.moon_orbit_period * 0.25f);
    float nearest = 1e9f, farthest = 0.0f;
    for (int i = 0; i < quarter; i++) {
        Vector3 before = moon.pos;
        moon_tick(&moon, &tun, radius, dt);
        CHECK_NEAR(Vector3Distance(moon.pos_prev, before), 0.0f, 1e-6);
        float distance = Vector3Length(moon.pos);
        if (distance < nearest) nearest = distance;
        if (distance > farthest) farthest = distance;
    }
    CHECK_NEAR(nearest, orbit, orbit * 1e-3f);
    CHECK_NEAR(farthest, orbit, orbit * 1e-3f);
    // Um quarto de volta é um ângulo reto visto do planeta.
    CHECK_NEAR(Vector3DotProduct(Vector3Normalize(start), Vector3Normalize(moon.pos)), 0.0f, 0.02f);

    // A volta inteira termina onde começou.
    for (int i = quarter; i < seconds(tun.moon_orbit_period); i++) moon_tick(&moon, &tun, radius, dt);
    CHECK(Vector3Distance(moon.pos, start) < orbit * 0.02f);

    // A órbita é inclinada: a Lua sai do plano do equador.
    Tuning flat = tun;
    flat.moon_orbit_tilt = 0.0f;
    CHECK_NEAR(moon_orbit_point(&flat, radius, 1.0f).y, 0.0f, 1e-4);
    CHECK(fabsf(moon_orbit_point(&tun, radius, 0.0f).y) > 1.0f);
}

static void ship_cannot_enter_the_moon(void)
{
    Ship s;
    setup(&s);

    // De longe, reto para o centro da Lua, com boost.
    Vector3 away = sideways(moon.pos);
    Vector3 start = Vector3Add(moon.pos, Vector3Scale(away, moon.radius + 40.0f));
    ship_place(&s, &tun, &planet, start, Vector3Negate(away), 0.0f);

    bool inside = false, hit = false;
    float hardest = 0.0f;
    InputState dive = input(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST));
    for (int i = 0; i < seconds(3.0f) && s.alive; i++) {
        moon_tick(&moon, &tun, planet.params.radius, dt);
        ShipEvents ev = ship_tick(&s, &dive, &planet, &moon, &tun, dt);
        if (ev.hit) {
            hit = true;
            if (ev.hit_speed > hardest) hardest = ev.hit_speed;
            // A normal aponta para fora da Lua, e o ponto fica na superfície dela.
            CHECK(Vector3DotProduct(ev.hit_normal, Vector3Subtract(s.pos, moon.pos)) > 0.0f);
            CHECK_NEAR(Vector3Distance(ev.hit_point, moon.pos), moon.radius, 0.05f);
        }
        if (Vector3Distance(s.pos, moon.pos) < moon.radius) inside = true;
    }
    CHECK(hit);
    CHECK(!inside);
    CHECK(hardest > tun.ship_crash_min);
    CHECK(!s.alive || s.shield < tun.ship_shield_max || s.hull < tun.ship_hull_max);
}

static void moon_pulls_gently(void)
{
    Ship s;
    setup(&s);

    // Parada perto da Lua: é puxada para ela, devagar, e pousa sem dano.
    Vector3 away = sideways(moon.pos);
    Vector3 start = Vector3Add(moon.pos, Vector3Scale(away, moon.radius + 6.0f));
    ship_place(&s, &tun, &planet, start, sideways(away), 0.0f);

    InputState idle = input(ACT_BIT(ACT_DRIFT));   // estabilizadores desligados
    ShipEvents all;
    memset(&all, 0, sizeof all);
    for (int i = 0; i < seconds(6.0f); i++) {
        ShipEvents ev = ship_tick(&s, &idle, &planet, &moon, &tun, dt);   // a Lua fica parada neste teste
        if (ev.hit && ev.hit_speed > all.hit_speed) all = ev;
    }
    CHECK(s.near_moon);
    CHECK(all.hit);
    CHECK(all.hit_speed < tun.ship_crash_min);
    CHECK(s.alive && s.hull == tun.ship_hull_max && s.shield == tun.ship_shield_max);
    CHECK(Vector3Distance(s.pos, moon.pos) < moon.radius + tun.ship_radius + 0.5f);
    CHECK(s.sling == 0.0f);   // parada, não arma o estilingue

    // Fora do alcance, nada puxa.
    Vector3 far = Vector3Add(moon.pos, Vector3Scale(away, moon.radius * (tun.moon_sling_range + 0.5f)));
    ship_place(&s, &tun, &planet, far, sideways(away), 0.0f);
    for (int i = 0; i < seconds(1.0f); i++) ship_tick(&s, &idle, &planet, &moon, &tun, dt);
    CHECK(!s.near_moon);
    CHECK_NEAR(Vector3Length(s.fall), 0.0f, 1e-5);
    CHECK_NEAR(Vector3Distance(s.pos, far), 0.0f, 1e-3);
}

// Voa em linha reta, acelerando, e devolve a maior velocidade atingida.
// `with_moon` decide se a Lua existe para a nave.
static float fly_past(Ship *s, Vector3 start, Vector3 heading, bool with_moon, float duration)
{
    ship_place(s, &tun, &planet, start, heading, tun.ship_speed_max);
    InputState fly = input(ACT_BIT(ACT_FORWARD));
    float fastest = 0.0f;
    for (int i = 0; i < seconds(duration); i++) {
        ship_tick(s, &fly, &planet, with_moon ? &moon : NULL, &tun, dt);
        if (Vector3Length(s->vel) > fastest) fastest = Vector3Length(s->vel);
    }
    return fastest;
}

static void moon_slingshot_gives_speed(void)
{
    Ship s;
    setup(&s);

    // Passa a meio caminho entre a superfície da Lua e o fim do alcance.
    float reach = moon.radius * tun.moon_sling_range;
    Vector3 away = sideways(moon.pos);
    Vector3 heading = sideways(away);
    Vector3 start = Vector3Add(moon.pos, Vector3Add(Vector3Scale(away, (moon.radius + reach) * 0.5f),
                                                    Vector3Scale(heading, -(reach + 20.0f))));
    float seconds_to_pass = (reach * 2.0f + 40.0f) / tun.ship_speed_max;

    // Sem Lua: a velocidade máxima normal.
    float plain = fly_past(&s, start, heading, false, seconds_to_pass);
    CHECK(plain <= tun.ship_speed_max * 1.001f);
    CHECK(s.sling == 0.0f && !s.near_moon);

    // Com a Lua: o estilingue arma e a nave passa bem do limite normal.
    float slung = fly_past(&s, start, heading, true, seconds_to_pass);
    CHECK(slung > tun.ship_speed_max * 1.3f);
    CHECK(slung <= tun.ship_speed_max * tun.moon_sling_boost * 1.001f);
    CHECK(s.sling > 0.0f);
    CHECK(s.sling_blend > 0.5f);
    CHECK(s.alive && s.hull == tun.ship_hull_max);   // passou sem bater
    CHECK(!s.near_moon);                             // e já saiu de perto

    // O ganho dura alguns segundos e acaba: a velocidade volta ao normal.
    InputState fly = input(ACT_BIT(ACT_FORWARD));
    for (int i = 0; i < seconds(tun.moon_sling_time + 3.0f); i++) ship_tick(&s, &fly, &planet, &moon, &tun, dt);
    CHECK(s.sling <= 0.0f);
    CHECK(Vector3Length(s.vel) <= tun.ship_speed_max * 1.02f);
    CHECK(s.sling_blend < 0.05f);

    // Devagar demais, o estilingue não arma.
    ship_place(&s, &tun, &planet, start, heading, 0.0f);
    Tuning slow = tun;
    slow.ship_thrust = 2.0f;
    for (int i = 0; i < seconds(2.0f); i++) ship_tick(&s, &fly, &planet, &moon, &slow, dt);
    Vector3 inside = Vector3Add(moon.pos, Vector3Scale(away, (moon.radius + reach) * 0.5f));
    ship_place(&s, &tun, &planet, inside, heading, tun.ship_speed_max * tun.moon_sling_speed * 0.5f);
    ship_tick(&s, &fly, &planet, &moon, &slow, dt);
    CHECK(s.near_moon && s.sling == 0.0f);
}

void suite_moon(void)
{
    tun = test_tuning();
    dt = 1.0f / (float)tun.sim_hz;
    CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    RUN(moon_circles_the_planet);
    RUN(ship_cannot_enter_the_moon);
    RUN(moon_pulls_gently);
    RUN(moon_slingshot_gives_speed);
    arena_free(&arena);
}
