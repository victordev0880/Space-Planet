#include "test.h"
#include "test_util.h"

#include "game/ship.h"
#include "voxel/damage.h"

static Tuning tun;
static Arena  arena;
static Planet planet;
static float  dt;

static void setup(Ship *s)
{
    arena_reset(&arena);
    PlanetParams pp = tuning_planet_params(&tun);
    if (!planet_init(&planet, &pp, &arena)) test_fail(__FILE__, __LINE__, "planet_init falhou");
    ship_spawn(s, &tun, &planet);
}

static InputState input(uint32_t held, uint32_t pressed, float dx, float dy)
{
    InputState in = { held, pressed, 0u, dx, dy };
    return in;
}

// Roda `count` passos com o mesmo input e junta o que aconteceu.
static ShipEvents run(Ship *s, InputState in, int count)
{
    ShipEvents all;
    memset(&all, 0, sizeof all);
    for (int i = 0; i < count; i++) {
        ShipEvents ev = ship_tick(s, &in, &planet, NULL, &tun, dt);
        in.pressed = 0u;   // a borda de "apertou" vale só no primeiro passo
        if (ev.hit && ev.hit_speed > all.hit_speed) {
            all.hit = true;
            all.hit_speed = ev.hit_speed;
            all.hit_normal = ev.hit_normal;
            all.hit_point = ev.hit_point;
        }
        all.dashed |= ev.dashed;
        all.died |= ev.died;
        all.respawned |= ev.respawned;
    }
    return all;
}

static int seconds(float s)
{
    return (int)(s * (float)tun.sim_hz);
}

// Põe a nave parada em `pos`, com o nariz apontado para `forward`.
static void place(Ship *s, Vector3 pos, Vector3 forward, Vector3 up)
{
    s->pos = pos;
    s->pos_prev = pos;
    s->vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->fall = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->turn = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->rot = quat_look(forward, up);
    s->rot_prev = s->rot;
}

// Nave longe do planeta e de lado para ele, com espaço livre à frente: para
// os testes de voo que não querem o planeta no caminho.
static void setup_far(Ship *s)
{
    setup(s);
    Vector3 away = Vector3Normalize(s->pos);
    place(s, s->pos, Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f }, away), away);
}

static void quat_look_points_where_asked(void)
{
    Vector3 forwards[4] = { { 0, 0, -1 }, { 1, 0, 0 }, { 0.3f, -0.8f, 0.5f }, { 0, 1, 0 } };
    Vector3 ups[4] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };   // o último é paralelo à frente
    for (int i = 0; i < 4; i++) {
        Quaternion q = quat_look(forwards[i], ups[i]);
        Vector3 f = quat_forward(q), r = quat_right(q), u = quat_up(q);
        CHECK_NEAR(Vector3DotProduct(f, Vector3Normalize(forwards[i])), 1.0f, 1e-4);
        // Os três eixos formam um sistema ortogonal, de mão direita.
        CHECK_NEAR(Vector3DotProduct(f, r), 0.0f, 1e-4);
        CHECK_NEAR(Vector3DotProduct(f, u), 0.0f, 1e-4);
        CHECK_NEAR(Vector3DotProduct(Vector3CrossProduct(f, u), r), 1.0f, 1e-4);
        if (i < 3) CHECK(Vector3DotProduct(u, ups[i]) > 0.0f);   // o topo vai para o lado pedido
    }
}

static void ship_spawns_whole_facing_the_planet(void)
{
    Ship s;
    setup(&s);
    CHECK(s.alive);
    CHECK(s.hull == tun.ship_hull_max && s.shield == tun.ship_shield_max && s.energy == tun.ship_energy_max);
    CHECK_NEAR(Vector3Length(s.pos), planet.params.radius * tun.ship_spawn_distance, 1e-3);
    CHECK_NEAR(Vector3Length(s.vel), 0.0f, 1e-6);
    CHECK_NEAR(s.altitude, planet.params.radius * (tun.ship_spawn_distance - 1.0f), 1e-3);
    CHECK(!s.in_atmosphere && s.graze == 0.0f);

    // De frente para o planeta, com o nariz um pouco acima do centro dele e
    // o topo virado para o norte.
    Vector3 to_planet = Vector3Negate(Vector3Normalize(s.pos));
    CHECK_NEAR(Vector3DotProduct(ship_forward(&s), to_planet), cosf(tun.ship_spawn_pitch * DEG2RAD), 1e-4);
    CHECK(ship_forward(&s).y > to_planet.y);
    CHECK(ship_up(&s).y > 0.5f);
    CHECK_NEAR(ship_right(&s).y, 0.0f, 1e-4);   // sem rolagem
}

static void ship_place_puts_it_where_asked(void)
{
    Ship s;
    setup(&s);
    s.hull = 1.0f;
    s.alive = false;

    Vector3 pos = { 0.0f, planet.params.radius + 1.0f, 0.0f };
    ship_place(&s, &tun, &planet, pos, (Vector3){ 0.0f, 0.0f, -3.0f }, 12.0f);
    CHECK(s.alive && s.hull == tun.ship_hull_max);
    CHECK_NEAR(Vector3Distance(s.pos, pos), 0.0f, 1e-6);
    CHECK_NEAR(Vector3Distance(s.pos, s.pos_prev), 0.0f, 1e-6);
    CHECK(ship_forward(&s).z < -0.999f);
    CHECK(ship_up(&s).y > 0.999f);                       // topo para longe do planeta
    CHECK_NEAR(Vector3DotProduct(s.vel, ship_forward(&s)), 12.0f, 1e-3);
    CHECK_NEAR(s.altitude, 1.0f, 1e-3);
    CHECK(s.in_atmosphere && s.graze == 1.0f);

    // Sem direção, não gera orientação inválida.
    ship_place(&s, &tun, &planet, pos, (Vector3){ 0.0f, 0.0f, 0.0f }, 0.0f);
    CHECK_NEAR(QuaternionLength(s.rot), 1.0f, 1e-4);
}

static void ship_accelerates_up_to_max_speed(void)
{
    Ship s;
    setup_far(&s);
    Vector3 forward = ship_forward(&s);

    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(0.5f));
    float early = Vector3Length(s.vel);
    CHECK(early > 1.0f);
    CHECK_NEAR(Vector3DotProduct(Vector3Normalize(s.vel), forward), 1.0f, 0.02f);   // anda para onde aponta

    // O motor leva a nave até a velocidade máxima, e nunca além dela.
    float fastest = 0.0f;
    InputState thrust = input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f);
    for (int i = 0; i < seconds(2.5f); i++) {
        ship_tick(&s, &thrust, &planet, NULL, &tun, dt);
        if (Vector3Length(s.vel) > fastest) fastest = Vector3Length(s.vel);
    }
    float cruise = Vector3Length(s.vel);
    CHECK(cruise > early);
    CHECK(fastest <= tun.ship_speed_max * 1.001f);
    CHECK(cruise >= tun.ship_speed_max * 0.97f);

    // Soltando o acelerador, o arrasto freia aos poucos; o freio (S), bem mais
    // rápido (em um segundo ele já pode ter virado ré: conta só o avanço).
    Ship coasting = s, braking = s;
    run(&coasting, input(0u, 0u, 0.0f, 0.0f), seconds(1.0f));
    run(&braking, input(ACT_BIT(ACT_BACK), 0u, 0.0f, 0.0f), seconds(1.0f));
    CHECK(Vector3Length(coasting.vel) < cruise);
    CHECK(Vector3DotProduct(braking.vel, forward) < Vector3Length(coasting.vel) * 0.5f);
}

static void ship_mouse_and_keys_turn_it(void)
{
    Ship s;
    setup_far(&s);
    Vector3 forward0 = ship_forward(&s), right0 = ship_right(&s), up0 = ship_up(&s);

    // Mouse para a direita: o nariz vai para a direita.
    Ship yaw = s;
    run(&yaw, input(0u, 0u, 6.0f, 0.0f), seconds(0.5f));
    CHECK(Vector3DotProduct(ship_forward(&yaw), right0) > 0.2f);
    CHECK(Vector3DotProduct(ship_forward(&yaw), forward0) < 0.99f);

    // Mouse para cima (dy negativo): o nariz sobe.
    Ship pitch = s;
    run(&pitch, input(0u, 0u, 0.0f, -6.0f), seconds(0.5f));
    CHECK(Vector3DotProduct(ship_forward(&pitch), up0) > 0.2f);

    // E rola para a direita: o topo tomba para a direita, o nariz fica.
    Ship roll = s;
    run(&roll, input(ACT_BIT(ACT_ROLL_RIGHT), 0u, 0.0f, 0.0f), seconds(0.3f));
    CHECK(Vector3DotProduct(ship_up(&roll), right0) > 0.2f);
    CHECK_NEAR(Vector3DotProduct(ship_forward(&roll), forward0), 1.0f, 1e-3);

    // O giro nunca passa do limite, por mais que o mouse corra.
    Ship fast = s;
    run(&fast, input(0u, 0u, 100000.0f, 0.0f), 1);
    float turned = acosf(Clamp(Vector3DotProduct(ship_forward(&fast), forward0), -1.0f, 1.0f));
    CHECK(turned <= tun.ship_turn_rate * DEG2RAD * dt * 1.01f);

    // A orientação continua sendo um quatérnion de comprimento 1.
    run(&fast, input(ACT_BIT(ACT_ROLL_LEFT), 0u, 9.0f, -7.0f), seconds(20.0f));
    CHECK_NEAR(QuaternionLength(fast.rot), 1.0f, 1e-4);
}

static void ship_heading_follows_the_nose(void)
{
    Ship s;
    setup_far(&s);
    uint32_t forward = ACT_BIT(ACT_FORWARD);

    run(&s, input(forward, 0u, 0.0f, 0.0f), seconds(2.0f));
    Vector3 before = Vector3Normalize(s.vel);

    // Vira cerca de 90 graus acelerando: o voo acompanha a curva.
    run(&s, input(forward, 0u, 15.0f, 0.0f), seconds(0.9f));
    run(&s, input(forward, 0u, 0.0f, 0.0f), seconds(2.0f));
    CHECK(Vector3DotProduct(ship_forward(&s), before) < 0.5f);   // o nariz virou mesmo
    CHECK_NEAR(Vector3DotProduct(Vector3Normalize(s.vel), ship_forward(&s)), 1.0f, 0.02f);
}

static void ship_drift_keeps_the_heading(void)
{
    Ship s;
    setup_far(&s);

    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(2.0f));
    Vector3 heading = Vector3Normalize(s.vel);
    float speed = Vector3Length(s.vel);

    // Com o drift apertado, o nariz gira e a nave continua indo reto.
    run(&s, input(ACT_BIT(ACT_DRIFT), 0u, 20.0f, 0.0f), seconds(0.7f));
    CHECK(s.drifting);
    CHECK(Vector3DotProduct(ship_forward(&s), heading) < 0.3f);              // nariz virou bastante
    CHECK(Vector3DotProduct(Vector3Normalize(s.vel), heading) > 0.98f);      // rumo quase igual
    CHECK(Vector3Length(s.vel) > speed * 0.9f);                              // e quase sem perder velocidade

    // Soltando o drift, a aderência volta e o rumo vai atrás do nariz.
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(2.5f));
    CHECK(!s.drifting);
    CHECK_NEAR(Vector3DotProduct(Vector3Normalize(s.vel), ship_forward(&s)), 1.0f, 0.03f);
}

static void ship_boost_is_faster_and_costs_energy(void)
{
    Ship s;
    setup_far(&s);
    uint32_t boost = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST);

    run(&s, input(boost, 0u, 0.0f, 0.0f), seconds(2.0f));
    CHECK(s.boosting);
    CHECK(Vector3Length(s.vel) > tun.ship_speed_max * 1.2f);
    CHECK(s.energy < tun.ship_energy_max);
    CHECK(s.boost_blend > 0.9f);

    // A energia acaba e o boost desliga sozinho. Com a tecla ainda apertada
    // ele não volta: nada de ligar e desligar sem parar.
    bool restarted = false;
    InputState held = input(boost, 0u, 0.0f, 0.0f);
    int exhausted_at = -1;
    for (int i = 0; i < seconds(tun.ship_energy_max / tun.ship_boost_cost + 3.0f); i++) {
        ship_tick(&s, &held, &planet, NULL, &tun, dt);
        if (!s.boosting && exhausted_at < 0) exhausted_at = i;
        if (s.boosting && exhausted_at >= 0) restarted = true;
    }
    CHECK(exhausted_at >= 0);
    CHECK(!restarted);
    CHECK(!s.boosting && s.boost_spent);
    CHECK(s.energy > tun.ship_energy_max * 0.3f);   // recarregando, mesmo com a tecla apertada

    // Sem boost, a velocidade volta ao limite normal.
    CHECK(Vector3Length(s.vel) <= tun.ship_speed_max * 1.05f);

    // Soltou e apertou de novo: o boost volta.
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), 1);
    CHECK(!s.boost_spent);
    run(&s, input(boost, 0u, 0.0f, 0.0f), 2);
    CHECK(s.boosting);
}

static void ship_dash_is_an_impulse_with_cooldown(void)
{
    Ship s;
    setup_far(&s);
    Vector3 right = ship_right(&s);
    uint32_t dash = ACT_BIT(ACT_DASH);

    // Dash para o lado (D apertado).
    ShipEvents ev = run(&s, input(ACT_BIT(ACT_RIGHT), dash, 0.0f, 0.0f), 1);
    CHECK(ev.dashed);
    CHECK(Vector3DotProduct(s.vel, right) > tun.ship_dash_speed * 0.8f);
    CHECK_NEAR(s.energy, tun.ship_energy_max - tun.ship_dash_cost, 1.0f);
    CHECK(ship_invulnerable(&s));

    // Invulnerável enquanto dura.
    float hull = s.hull, shield = s.shield;
    CHECK(!ship_damage(&s, &tun, 500.0f));
    CHECK(s.hull == hull && s.shield == shield);

    // Um segundo dash logo em seguida não sai.
    ev = run(&s, input(0u, dash, 0.0f, 0.0f), 1);
    CHECK(!ev.dashed);

    // Depois do intervalo, sai de novo, e a invulnerabilidade do primeiro já acabou.
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_dash_cooldown + tun.ship_dash_time + 0.1f));
    CHECK(!ship_invulnerable(&s));
    ev = run(&s, input(0u, dash, 0.0f, 0.0f), 1);
    CHECK(ev.dashed);

    // Sem energia, não há dash.
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_dash_cooldown + 0.1f));
    s.energy = tun.ship_dash_cost * 0.5f;
    ev = run(&s, input(0u, dash, 0.0f, 0.0f), 1);
    CHECK(!ev.dashed);
}

static void ship_shield_absorbs_before_the_hull(void)
{
    Ship s;
    setup(&s);

    CHECK(!ship_damage(&s, &tun, tun.ship_shield_max * 0.5f));
    CHECK_NEAR(s.shield, tun.ship_shield_max * 0.5f, 1e-3);
    CHECK(s.hull == tun.ship_hull_max);

    CHECK(!ship_damage(&s, &tun, tun.ship_shield_max));   // estoura o escudo e sobra para o casco
    CHECK(s.shield == 0.0f);
    CHECK_NEAR(s.hull, tun.ship_hull_max - tun.ship_shield_max * 0.5f, 1e-3);

    // O escudo espera um tempo sem dano e só então recarrega; o casco, nunca.
    float hull = s.hull;
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_shield_delay * 0.5f));
    CHECK(s.shield == 0.0f);
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_shield_delay + 1.0f));
    CHECK(s.shield > 0.0f);
    CHECK(s.hull == hull);
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_shield_max / tun.ship_shield_regen + 1.0f));
    CHECK(s.shield == tun.ship_shield_max);
}

static void ship_dies_and_comes_back(void)
{
    Ship s;
    setup(&s);
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 3.0f, 0.0f), seconds(1.0f));
    Vector3 spawn = Vector3Scale(Vector3Normalize((Vector3){ 0.2f, 0.35f, 1.0f }),
                                 planet.params.radius * tun.ship_spawn_distance);

    CHECK(ship_damage(&s, &tun, 100000.0f));
    CHECK(!s.alive && s.hull == 0.0f);
    CHECK(!ship_damage(&s, &tun, 10.0f));   // já destruída: mais dano não muda nada

    // Destruída, ignora os controles e fica onde está.
    Vector3 wreck = s.pos;
    ShipEvents ev = run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 5.0f, 0.0f), seconds(tun.ship_respawn_time * 0.5f));
    CHECK(!s.alive && !ev.respawned);
    CHECK_NEAR(Vector3Distance(s.pos, wreck), 0.0f, 1e-6);

    ev = run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(tun.ship_respawn_time * 0.5f + 0.2f));
    CHECK(ev.respawned && s.alive);
    CHECK(s.hull == tun.ship_hull_max && s.shield == tun.ship_shield_max);
    CHECK_NEAR(Vector3Distance(s.pos, spawn), 0.0f, 0.01f);
    CHECK_NEAR(Vector3Distance(s.pos, s.pos_prev), 0.0f, 1e-6);   // sem interpolar o salto
}

static void ship_gravity_is_gentle_and_local(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;

    // Parada perto do planeta, de nariz de lado e com os estabilizadores
    // desligados (drift): afunda devagar, reto para baixo. A queda não vira
    // impulso para a frente.
    Vector3 start = { 0.0f, radius * 1.5f, 0.0f };
    place(&s, start, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    float fastest_fall = 0.0f;
    InputState loose = input(ACT_BIT(ACT_DRIFT), 0u, 0.0f, 0.0f);
    for (int i = 0; i < seconds(1.5f); i++) {
        ship_tick(&s, &loose, &planet, NULL, &tun, dt);
        if (Vector3Length(s.fall) > fastest_fall) fastest_fall = Vector3Length(s.fall);
    }
    CHECK(s.fall.y < 0.0f && s.pos.y < start.y);
    CHECK_NEAR(s.pos.x, 0.0f, 1e-3);
    CHECK_NEAR(Vector3Length(s.vel), 0.0f, 1e-5);
    // A queda tem uma velocidade limite baixa.
    CHECK(fastest_fall <= tun.ship_gravity / tun.ship_fall_drag);

    // Longe, nada puxa.
    place(&s, (Vector3){ 0.0f, radius * (tun.ship_gravity_range + 0.5f), 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f },
          (Vector3){ 0.0f, 1.0f, 0.0f });
    run(&s, input(0u, 0u, 0.0f, 0.0f), seconds(1.0f));
    CHECK_NEAR(Vector3Length(s.vel), 0.0f, 1e-5);
    CHECK_NEAR(Vector3Length(s.fall), 0.0f, 1e-5);

    // O motor vence a gravidade com folga.
    CHECK(tun.ship_thrust > tun.ship_gravity * 3.0f);
}

// S freia e, já quase parada, dá ré, mais devagar que para a frente.
static void ship_brakes_then_reverses(void)
{
    Ship s;
    setup_far(&s);
    Vector3 forward = ship_forward(&s);
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(2.0f));
    CHECK(Vector3DotProduct(s.vel, forward) > tun.ship_speed_max * 0.8f);

    // Primeiro freia: ainda anda para a frente, mais devagar, sem ré.
    InputState back = input(ACT_BIT(ACT_BACK), 0u, 0.0f, 0.0f);
    float before = Vector3DotProduct(s.vel, forward);
    run(&s, back, 10);
    CHECK(Vector3DotProduct(s.vel, forward) > 0.0f);
    CHECK(Vector3DotProduct(s.vel, forward) < before);
    CHECK(!s.reversing);

    // Segurando, para e passa a andar de ré, até o limite de ré.
    float fastest_back = 0.0f;
    for (int i = 0; i < seconds(4.0f); i++) {
        ship_tick(&s, &back, &planet, NULL, &tun, dt);
        fastest_back = fmaxf(fastest_back, -Vector3DotProduct(s.vel, forward));
    }
    CHECK(s.reversing);
    CHECK(-Vector3DotProduct(s.vel, forward) > tun.ship_reverse_max * 0.95f);
    CHECK(fastest_back <= tun.ship_reverse_max * 1.001f);
    CHECK(tun.ship_reverse_max < tun.ship_speed_max * 0.75f);   // ré serve para manobrar e recuar, mais lenta que a frente
    CHECK_NEAR(Vector3DotProduct(ship_forward(&s), forward), 1.0f, 1e-3);   // o nariz continua para a frente
}

// Sem comandos e devagar, a nave para e fica no lugar, mesmo perto do planeta.
static void ship_holds_still_when_idle(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;
    Vector3 start = { 0.0f, radius + 8.0f, 0.0f };
    InputState idle = input(0u, 0u, 0.0f, 0.0f);

    // Devagar: os estabilizadores param a nave e ela não afunda.
    ship_place(&s, &tun, &planet, start, (Vector3){ 1.0f, 0.0f, 0.0f }, 10.0f);
    run(&s, idle, seconds(3.0f));
    CHECK(Vector3Length(Vector3Add(s.vel, s.fall)) < 0.3f);
    CHECK(s.hover > 0.95f);
    float altitude = s.altitude;
    run(&s, idle, seconds(5.0f));
    CHECK_NEAR(s.altitude, altitude, 0.05f);   // pairando
    CHECK(Vector3Length(Vector3Add(s.vel, s.fall)) < 0.05f);

    // Rápido: soltar tudo deixa a nave seguir pela inércia.
    Ship fast;
    setup_far(&fast);
    run(&fast, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(2.0f));
    float cruise = Vector3Length(fast.vel);
    run(&fast, idle, seconds(1.0f));
    CHECK(Vector3Length(fast.vel) > cruise * 0.6f);
    CHECK(fast.hover == 0.0f);
}

static void ship_never_ends_up_inside_the_ground(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;

    // Mergulha de cima, acelerando, direto contra o planeta.
    place(&s, (Vector3){ 0.0f, radius + 25.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f });
    bool inside = false, hit = false;
    float hardest = 0.0f;
    InputState in = input(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST), 0u, 0.0f, 0.0f);
    for (int i = 0; i < seconds(4.0f) && s.alive; i++) {
        ShipEvents ev = ship_tick(&s, &in, &planet, NULL, &tun, dt);
        if (ev.hit) {
            hit = true;
            if (ev.hit_speed > hardest) hardest = ev.hit_speed;
            CHECK(Vector3DotProduct(ev.hit_normal, Vector3Normalize(s.pos)) > 0.3f);   // a normal aponta para fora
        }
        if (planet_sample(&planet, s.pos) >= VOXEL_ISO) inside = true;
    }
    CHECK(hit);
    CHECK(!inside);                    // o centro da nave nunca fica dentro da matéria
    CHECK(hardest > tun.ship_crash_min);
    // Bateu forte: perdeu escudo ou casco (ou foi destruída).
    CHECK(!s.alive || s.shield < tun.ship_shield_max || s.hull < tun.ship_hull_max);
}

static void ship_gentle_touch_does_no_damage(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;

    // Solta a nave parada pouco acima do solo, com os estabilizadores
    // desligados (drift), e deixa a gravidade pousá-la.
    place(&s, (Vector3){ 0.0f, radius + 6.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    ShipEvents ev = run(&s, input(ACT_BIT(ACT_DRIFT), 0u, 0.0f, 0.0f), seconds(8.0f));
    CHECK(ev.hit);
    CHECK(ev.hit_speed < tun.ship_crash_min);
    CHECK(s.alive && s.hull == tun.ship_hull_max && s.shield == tun.ship_shield_max);
    CHECK(planet_sample(&planet, s.pos) < VOXEL_ISO);
    CHECK(s.altitude < 6.0f);
}

static void ship_flies_inside_craters(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;

    // Um poço largo, reto para baixo a partir do topo do planeta.
    for (int i = 0; i < 5; i++) {
        planet_carve(&planet, (Vector3){ 0.0f, radius + 2.0f - (float)i * 3.0f, 0.0f }, 4.5f);
    }

    // Dentro do poço, bem abaixo de onde ficava o chão: não há colisão.
    Vector3 inside = { 0.0f, radius - 6.0f, 0.0f };
    CHECK(planet_sample(&planet, inside) < VOXEL_ISO);
    place(&s, inside, (Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f });
    ShipEvents ev = run(&s, input(0u, 0u, 0.0f, 0.0f), 3);
    CHECK(!ev.hit);
    CHECK(Vector3Length(s.pos) < radius - 5.0f);   // continua lá embaixo
    CHECK(!s.in_atmosphere && s.graze == 0.0f);    // no fundo de um poço não há voo rasante

    // Indo de lado, bate na parede do poço e é devolvida para dentro dele.
    place(&s, inside, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });
    ev = run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(1.5f));
    CHECK(ev.hit);
    CHECK(planet_sample(&planet, s.pos) < VOXEL_ISO);
    CHECK(fabsf(s.pos.x) < 4.5f);
}

// Voo rasante: dentro da atmosfera, voar "reto" contorna o planeta.
static void ship_low_flight_follows_the_curvature(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;

    // O planeta de teste é pequeno, e a atmosfera dele acabaria abaixo do
    // topo das montanhas. Aqui ela é mais espessa, para caber um voo acima
    // do relevo e ainda dentro do ar.
    Tuning on = tun;
    on.atmo_height = 0.45f;
    float altitude = planet.params.land_height + 1.5f;
    CHECK(altitude < radius * on.atmo_height);

    Vector3 start = { 0.0f, radius + altitude, 0.0f };
    InputState fly = input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f);

    // Com o recurso ligado: três segundos de voo nivelado e a altitude quase
    // não muda, embora a nave já tenha dado boa parte da volta no planeta.
    ship_place(&s, &on, &planet, start, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f);
    CHECK(s.graze == 1.0f);
    float highest = 0.0f, lowest = 1e9f, farthest = 1.0f;
    for (int i = 0; i < seconds(3.0f); i++) {
        ShipEvents ev = ship_tick(&s, &fly, &planet, NULL, &on, dt);
        CHECK(!ev.hit);
        if (s.altitude > highest) highest = s.altitude;
        if (s.altitude < lowest) lowest = s.altitude;
        float side = Vector3DotProduct(Vector3Normalize(s.pos), Vector3Normalize(start));
        if (side < farthest) farthest = side;
    }
    CHECK(highest < altitude + 0.5f);
    CHECK(lowest > altitude - 1.5f);          // afunda um pouco só enquanto ganha velocidade
    CHECK(farthest < -0.5f);                  // chegou ao outro lado do planeta
    // A nave continua nivelada: o topo aponta para longe do planeta.
    CHECK(Vector3DotProduct(ship_up(&s), Vector3Normalize(s.pos)) > 0.98f);
    CHECK_NEAR(QuaternionLength(s.rot), 1.0f, 1e-4);

    // Desligado, o mesmo voo sai pela tangente e ganha altura.
    Tuning off = on;
    off.ship_hug = 0.0f;
    ship_place(&s, &off, &planet, start, (Vector3){ 1.0f, 0.0f, 0.0f }, 0.0f);
    for (int i = 0; i < seconds(3.0f); i++) ship_tick(&s, &fly, &planet, NULL, &off, dt);
    CHECK(s.altitude > altitude + 20.0f);
}

// Fora da atmosfera o voo é livre: reto é reto.
static void ship_high_flight_is_straight(void)
{
    Ship s;
    setup(&s);
    float radius = planet.params.radius;
    float air = radius * tun.atmo_height;

    // O efeito some aos poucos acima da atmosfera, e também abaixo do nível do mar.
    Vector3 dir = { 1.0f, 0.0f, 0.0f };
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius + air * 0.5f, 0.0f }, dir, 0.0f);
    CHECK(s.graze == 1.0f);
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius + air * (1.0f + tun.ship_hug_fade * 0.5f), 0.0f }, dir, 0.0f);
    CHECK_NEAR(s.graze, 0.5f, 1e-3);
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius + air * (1.0f + tun.ship_hug_fade) + 0.1f, 0.0f }, dir, 0.0f);
    CHECK(s.graze == 0.0f);
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius - air * 0.5f, 0.0f }, dir, 0.0f);
    CHECK_NEAR(s.graze, 0.5f, 1e-3);
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius - air * 1.1f, 0.0f }, dir, 0.0f);
    CHECK(s.graze == 0.0f);

    // Lá em cima, o nariz não gira sozinho.
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius * 2.0f, 0.0f }, dir, 0.0f);
    Vector3 forward = ship_forward(&s);
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(2.0f));
    CHECK(s.pos.x > 20.0f);
    CHECK_NEAR(Vector3DotProduct(ship_forward(&s), forward), 1.0f, 1e-3);

    // Subindo na vertical de dentro da atmosfera, também não: a vertical não muda.
    ship_place(&s, &tun, &planet, (Vector3){ 0.0f, radius + air * 0.8f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, 0.0f);
    forward = ship_forward(&s);
    run(&s, input(ACT_BIT(ACT_FORWARD), 0u, 0.0f, 0.0f), seconds(0.5f));
    CHECK_NEAR(Vector3DotProduct(ship_forward(&s), forward), 1.0f, 1e-4);
    CHECK_NEAR(s.pos.x, 0.0f, 1e-3);
}

static void ship_same_inputs_same_flight(void)
{
    Ship a, b;
    setup(&a);
    b = a;

    InputState steps[4] = {
        input(ACT_BIT(ACT_FORWARD), 0u, 2.0f, -1.0f),
        input(ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BOOST), ACT_BIT(ACT_DASH), -4.0f, 0.5f),
        input(ACT_BIT(ACT_DRIFT) | ACT_BIT(ACT_ROLL_LEFT), 0u, 9.0f, 3.0f),
        input(ACT_BIT(ACT_BACK) | ACT_BIT(ACT_LEFT), 0u, 0.0f, 0.0f),
    };
    for (int i = 0; i < 4; i++) {
        run(&a, steps[i], seconds(1.0f));
        run(&b, steps[i], seconds(1.0f));
    }
    CHECK(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z);
    CHECK(a.vel.x == b.vel.x && a.vel.y == b.vel.y && a.vel.z == b.vel.z);
    CHECK(a.fall.x == b.fall.x && a.fall.y == b.fall.y && a.fall.z == b.fall.z);
    CHECK(a.rot.x == b.rot.x && a.rot.y == b.rot.y && a.rot.z == b.rot.z && a.rot.w == b.rot.w);
    CHECK(a.energy == b.energy && a.hull == b.hull);
    CHECK(Vector3Distance(a.pos, a.pos_prev) > 0.0f || Vector3Length(a.vel) < 1e-3f);
}

void suite_ship(void)
{
    tun = test_tuning();
    dt = 1.0f / (float)tun.sim_hz;
    CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    RUN(quat_look_points_where_asked);
    RUN(ship_spawns_whole_facing_the_planet);
    RUN(ship_place_puts_it_where_asked);
    RUN(ship_accelerates_up_to_max_speed);
    RUN(ship_mouse_and_keys_turn_it);
    RUN(ship_heading_follows_the_nose);
    RUN(ship_drift_keeps_the_heading);
    RUN(ship_boost_is_faster_and_costs_energy);
    RUN(ship_dash_is_an_impulse_with_cooldown);
    RUN(ship_shield_absorbs_before_the_hull);
    RUN(ship_dies_and_comes_back);
    RUN(ship_gravity_is_gentle_and_local);
    RUN(ship_brakes_then_reverses);
    RUN(ship_holds_still_when_idle);
    RUN(ship_never_ends_up_inside_the_ground);
    RUN(ship_gentle_touch_does_no_damage);
    RUN(ship_flies_inside_craters);
    RUN(ship_low_flight_follows_the_curvature);
    RUN(ship_high_flight_is_straight);
    RUN(ship_same_inputs_same_flight);
    arena_free(&arena);
}
