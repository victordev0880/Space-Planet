// Testes do marco 9: rochas (fragmentos e meteoros), raio trator, conserto,
// mineração e defesas planetárias.
#include "test.h"
#include "test_util.h"

#include "core/input.h"
#include "game/world.h"
#include "tools/autopilot.h"
#include "voxel/damage.h"
#include "voxel/terraform.h"

static Tuning tun;
static Arena  arena;

static InputState keys(uint32_t held, uint32_t pressed)
{
    InputState in = { held, pressed, 0u, 0.0f, 0.0f };
    return in;
}

static void run(World *w, const Tuning *t, int ticks)
{
    InputState in = keys(0u, 0u);
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

static void make_world(World *w, const Tuning *t)
{
    arena_reset(&arena);
    if (!world_init(w, t, 11u, &arena)) test_fail(__FILE__, __LINE__, "world_init falhou");
}

// Ponto do solo na direção `dir`.
static Vector3 ground(const World *w, Vector3 dir)
{
    dir = Vector3Normalize(dir);
    Vector3 hit = { 0.0f, 0.0f, 0.0f };
    float far = w->planet.params.radius * 3.0f;
    planet_raycast(&w->planet, Vector3Scale(dir, far), Vector3Negate(dir), far, &hit);
    return hit;
}

// --- voxel/terraform ---------------------------------------------------------

// Consertar devolve exatamente o planeta original, nunca passa dele, e
// consertar onde nada foi destruído não muda nada.
static void restore_returns_the_original(void)
{
    static World w;
    make_world(&w, &tun);
    Planet *p = &w.planet;
    Vector3 at = ground(&w, (Vector3){ 0.3f, 0.8f, 0.5f });
    CarveResult carve = planet_carve(p, at, 4.0f);
    CHECK(carve.removed > 0);
    CHECK(p->volume < p->initial_volume);
    // A cratera mexe até VOXEL_ISO / VOXEL_DENSITY_PER_UNIT (~4) além do raio dela.
    CHECK(planet_missing(p, at, 9.0f) == carve.removed);

    RestoreResult res = planet_restore(p, at, 9.0f);
    CHECK(res.restored == carve.removed);
    CHECK(res.chunks_touched > 0);
    CHECK(p->volume == p->initial_volume);
    size_t count = (size_t)p->size * (size_t)p->size * (size_t)p->size;
    CHECK(memcmp(p->density, p->original_density, count) == 0);
    CHECK(memcmp(p->material, p->original_material, count) == 0);

    RestoreResult again = planet_restore(p, at, 9.0f);   // já está como era
    CHECK(again.restored == 0 && again.chunks_touched == 0);
}

// A esfera conserta só dentro dela (a borda mistura um pouco).
static void restore_stays_inside_the_sphere(void)
{
    static World w;
    make_world(&w, &tun);
    Planet *p = &w.planet;
    Vector3 at = ground(&w, (Vector3){ -0.6f, 0.2f, 0.7f });
    CarveResult carve = planet_carve(p, at, 5.0f);
    RestoreResult res = planet_restore(p, at, 1.5f);   // pequena demais para tudo
    CHECK(res.restored > 0 && res.restored < carve.removed);
    CHECK(planet_missing(p, at, 10.0f) == carve.removed - res.restored);
}

// --- fragmentos ------------------------------------------------------------------

// Explosões grandes arrancam fragmentos, que sobem até a órbita; as pequenas, não.
static void big_impacts_eject_fragments(void)
{
    static World w;
    Tuning t = tun;
    t.fragment_volume = 40.0f;   // o planeta dos testes é pequeno: crateras pequenas
    t.fragment_min_radius = 3.0f;
    make_world(&w, &t);

    Vector3 dir = { 0.2f, 0.9f, 0.3f };
    world_impact(&w, &t, Vector3Scale(dir, 100.0f), Vector3Negate(dir), 2.0f, 1.5f);   // pequena
    CHECK(rocks_count(&w.rocks, ROCK_FRAGMENT) == 0);
    world_impact(&w, &t, Vector3Scale(dir, 100.0f), Vector3Negate(dir), 5.0f, 4.0f);   // grande
    int made = rocks_count(&w.rocks, ROCK_FRAGMENT);
    CHECK(made >= 1 && made <= t.fragment_per_impact);
    CHECK(saw_event(&w, EV_FRAGMENT));

    run(&w, &t, seconds(t.fragment_eject_time + 0.5f));
    float radius = w.planet.params.radius;
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w.rocks.items[i];
        if (!k->alive) continue;
        CHECK(k->state == ROCK_ORBIT);
        float altitude = Vector3Length(k->pos) - radius;
        CHECK(altitude >= t.fragment_alt_min - 0.5f && altitude <= t.fragment_alt_max + 0.5f);
    }
    // O cinturão tem teto.
    t.fragment_max = made;
    world_impact(&w, &t, Vector3Scale(dir, -100.0f), dir, 5.0f, 4.0f);
    CHECK(rocks_count(&w.rocks, ROCK_FRAGMENT) == made);
}

// Um meteoro passa e, se ninguém pegar, vai embora.
static void meteor_passes_and_leaves(void)
{
    static World w;
    make_world(&w, &tun);
    world_place_ship(&w, &tun, (Vector3){ 0.0f, -60.0f, 0.0f }, (Vector3){ 0.0f, -60.0f, 10.0f }, 0.0f);
    float r = w.planet.params.radius;
    Rock *k = world_spawn_rock(&w, &tun, ROCK_METEOR, (Vector3){ -r * 3.0f, r * 1.8f, 0.0f }, (Vector3){ 40.0f, 0.0f, 0.0f });
    CHECK(k != NULL && k->kind == ROCK_METEOR && saw_event(&w, EV_METEOR));
    run(&w, &tun, seconds(20.0f));
    CHECK(rocks_alive(&w.rocks) == 0);
}

// --- raio trator -------------------------------------------------------------------

// Nave parada sobre o planeta, de nariz para o espaço (+x), longe do chão.
static void park_ship(World *w, Vector3 *pos)
{
    float r = w->planet.params.radius;
    *pos = (Vector3){ 0.0f, r + 30.0f, 0.0f };
    world_place_ship(w, &tun, *pos, Vector3Add(*pos, (Vector3){ 1.0f, 0.0f, 0.0f }), 0.0f);
}

static void tractor_grabs_only_in_front(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    // Uma rocha atrás da nave: o raio não pega.
    world_spawn_rock(&w, &tun, ROCK_METEOR, Vector3Add(pos, (Vector3){ -20.0f, 0.0f, 0.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });
    InputState f = keys(ACT_BIT(ACT_TRACTOR), ACT_BIT(ACT_TRACTOR));
    world_tick(&w, &f, &tun);
    CHECK(rocks_towed(&w.rocks) == NULL && saw_event(&w, EV_TRACTOR_MISS));
    run(&w, &tun, 1);

    // À frente, ao alcance: pega, e a nave fica mais lenta.
    world_clear_enemies(&w);
    world_spawn_rock(&w, &tun, ROCK_METEOR, Vector3Add(pos, (Vector3){ 25.0f, 0.0f, 0.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });
    float speed_free = world_tuning(&w)->ship_speed_max;
    world_tick(&w, &f, &tun);
    CHECK(rocks_towed(&w.rocks) != NULL && saw_event(&w, EV_TRACTOR_GRAB));
    run(&w, &tun, 1);
    CHECK(world_tuning(&w)->ship_speed_max < speed_free);

    // A rocha vem para baixo e à frente da nave e fica lá.
    run(&w, &tun, seconds(4.0f));
    const Rock *k = rocks_towed(&w.rocks);
    CHECK(k != NULL);
    if (k != NULL) {
        Vector3 rel = Vector3Subtract(k->pos, w.ship.pos);
        CHECK(Vector3DotProduct(rel, ship_forward(&w.ship)) > 0.0f);            // à frente
        CHECK(Vector3DotProduct(rel, Vector3Normalize(w.ship.pos)) < -2.0f);    // abaixo
        CHECK(Vector3Length(rel) < tun.tractor_distance + tun.tractor_drop + 3.0f * k->radius);
    }

    // F de novo: solta.
    world_tick(&w, &f, &tun);
    CHECK(rocks_towed(&w.rocks) == NULL && saw_event(&w, EV_TRACTOR_RELEASE));
}

// Longe demais (um teletransporte, uma pancada), o raio arrebenta e a rocha cai.
static void tether_breaks_when_stretched(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    world_spawn_rock(&w, &tun, ROCK_FRAGMENT, Vector3Add(pos, (Vector3){ 20.0f, 0.0f, 0.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });
    InputState f = keys(ACT_BIT(ACT_TRACTOR), ACT_BIT(ACT_TRACTOR));
    world_tick(&w, &f, &tun);
    CHECK(rocks_towed(&w.rocks) != NULL);
    Vector3 far = { 0.0f, -(w.planet.params.radius + 60.0f), 0.0f };
    world_place_ship(&w, &tun, far, Vector3Add(far, (Vector3){ 1.0f, 0.0f, 0.0f }), 0.0f);
    run(&w, &tun, 2);
    CHECK(rocks_towed(&w.rocks) == NULL && saw_event(&w, EV_TRACTOR_BREAK));
}

// --- conserto ------------------------------------------------------------------------

// Solta sobre uma cratera: a rocha cai, o planeta volta, a cicatriz some e há
// pontos e o aviso dos habitantes.
static void dropped_rock_repairs_the_crater(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 up = Vector3Normalize((Vector3){ 0.1f, 1.0f, 0.15f });
    world_impact(&w, &tun, Vector3Scale(up, 100.0f), Vector3Negate(up), 4.0f, 3.0f);
    int64_t hurt = w.planet.volume;
    CHECK(hurt < w.planet.initial_volume);
    bool scar = false;
    for (int i = 0; i < REPAIR_SCARS; i++) scar = scar || w.repair.scars[i].used;
    CHECK(scar);

    // Uma rocha logo acima da cratera, já caindo.
    Rock *k = world_spawn_rock(&w, &tun, ROCK_FRAGMENT, Vector3Scale(up, w.planet.params.radius + 12.0f),
                               (Vector3){ 0.0f, 0.0f, 0.0f });
    CHECK(k != NULL);
    if (k == NULL) return;
    k->state = ROCK_FALLING;
    k->fill = 6.0f;
    int64_t points = w.score.points;
    for (int i = 0; i < seconds(4.0f) && !saw_event(&w, EV_REPAIR); i++) run(&w, &tun, 1);
    CHECK(saw_event(&w, EV_DEPOSIT) && saw_event(&w, EV_REPAIR));
    CHECK(w.rocks.delivered == 1);
    CHECK(w.planet.volume > hurt);
    CHECK(w.score.points > points);
    run(&w, &tun, seconds(1.0f));   // as cicatrizes são conferidas aos poucos
    for (int i = 0; i < REPAIR_SCARS; i++) CHECK(!w.repair.scars[i].used);
}

// Impactos próximos viram uma cicatriz só; distantes, cicatrizes separadas.
static void scars_merge_nearby_impacts(void)
{
    Repair r;
    repair_reset(&r);
    repair_note_damage(&r, (Vector3){ 0.0f, 20.0f, 0.0f }, 4.0f);
    repair_note_damage(&r, (Vector3){ 2.0f, 20.0f, 0.0f }, 4.0f);
    repair_note_damage(&r, (Vector3){ 0.0f, -20.0f, 0.0f }, 4.0f);
    int used = 0;
    for (int i = 0; i < REPAIR_SCARS; i++) used += r.scars[i].used ? 1 : 0;
    CHECK(used == 2);
}

// O piloto automático (sem inimigos) busca a rocha e conserta a cratera.
static void autopilot_fetches_and_repairs(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 up = Vector3Normalize((Vector3){ 0.0f, 1.0f, 0.2f });
    world_impact(&w, &tun, Vector3Scale(up, 100.0f), Vector3Negate(up), 4.5f, 3.5f);
    int64_t hurt = w.planet.volume;
    float r = w.planet.params.radius;
    Vector3 pos = Vector3Scale(up, r + 25.0f);
    world_place_ship(&w, &tun, pos, Vector3Add(pos, (Vector3){ 1.0f, 0.0f, 0.0f }), 0.0f);
    world_spawn_rock(&w, &tun, ROCK_FRAGMENT, Vector3Add(pos, (Vector3){ 30.0f, 0.0f, 10.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });

    InputTracker tracker;
    input_init(&tracker);
    InputSample sample = { 0u, 0.0f, 0.0f };
    for (int i = 0; i < seconds(40.0f) && w.rocks.delivered == 0; i++) {
        autopilot_drive(&w, &tun, &sample);
        input_feed(&tracker, &sample);
        InputState in = input_tick(&tracker);
        world_tick(&w, &in, &tun);
    }
    CHECK(w.rocks.delivered >= 1);
    run(&w, &tun, seconds(2.0f));
    CHECK(w.planet.volume > hurt);
}

// Nas calmarias chegam meteoros.
static void meteors_come_in_the_calm(void)
{
    static World w;
    static WavePlan plan;
    make_world(&w, &tun);
    static Config c;   // grande demais para a pilha
    CHECK(config_parse(&c, "wave.1.pause = 30\nwave.1.group.1 = swarm 1 0 0 60\n"));
    char err[160];
    CHECK(waves_parse(&plan, &c, err, sizeof err));
    world_set_plan(&w, &plan);
    run(&w, &tun, seconds(1.5f + tun.meteor_calm_gap * (float)tun.meteor_calm_count));
    CHECK(rocks_count(&w.rocks, ROCK_METEOR) == tun.meteor_calm_count);
}

// --- mineração ---------------------------------------------------------------------

// Tiros da nave quebram a rocha em cristais, que a nave atrai e coleta.
static void bolts_mine_rocks_into_ore(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    world_spawn_rock(&w, &tun, ROCK_METEOR, Vector3Add(pos, (Vector3){ 30.0f, 0.0f, 0.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });
    InputState fire = keys(ACT_BIT(ACT_FIRE), 0u);
    for (int i = 0; i < seconds(6.0f) && rocks_alive(&w.rocks) > 0; i++) world_tick(&w, &fire, &tun);
    CHECK(rocks_alive(&w.rocks) == 0);
    CHECK(saw_event(&w, EV_ROCK_SHOT) && saw_event(&w, EV_ROCK_MINED));
    CHECK(rocks_ore_alive(&w.rocks) + w.ore == tun.ore_meteor);
    // A nave avança um pouco e os cristais vêm até ela.
    InputState go = keys(ACT_BIT(ACT_FORWARD), 0u);
    for (int i = 0; i < seconds(1.0f); i++) world_tick(&w, &go, &tun);
    run(&w, &tun, seconds(4.0f));
    CHECK(w.ore == tun.ore_meteor && w.ore_total == tun.ore_meteor);
    CHECK(saw_event(&w, EV_ORE));
}

// Os tiros dos aliados (e das torres) não quebram rochas: passam por elas.
static void ally_bolts_pass_through_rocks(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    world_spawn_rock(&w, &tun, ROCK_FRAGMENT, Vector3Add(pos, (Vector3){ 20.0f, 0.0f, 0.0f }), (Vector3){ 0.0f, 0.0f, 0.0f });
    float hp = 0.0f;
    for (int i = 0; i < ROCKS_MAX; i++) if (w.rocks.items[i].alive) hp = w.rocks.items[i].hp;
    for (int i = 0; i < 10; i++) {
        Shot *s = projectiles_spawn(&w.shots, SHOT_BOLT, Vector3Add(pos, (Vector3){ 5.0f, 0.0f, 0.0f }),
                                    (Vector3){ tun.weapon_speed, 0.0f, 0.0f }, 1.0f);
        if (s != NULL) {
            s->damage = 1.0f;
            s->source = BOLT_ALLY;
        }
        run(&w, &tun, 6);
    }
    for (int i = 0; i < ROCKS_MAX; i++) if (w.rocks.items[i].alive) CHECK(w.rocks.items[i].hp == hp);
    CHECK(!saw_event(&w, EV_ROCK_SHOT));
}

// --- defesas -------------------------------------------------------------------------

static InputState press(Action a)
{
    return keys(ACT_BIT(a), ACT_BIT(a));
}

// Comprar custa minério, respeita o limite e recusa sem minério.
static void buying_costs_ore_and_has_limits(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    InputState buy = press(ACT_BUY1);
    world_tick(&w, &buy, &tun);   // sem minério
    CHECK(defenses_count(&w.defenses, DEF_TURRET) == 0 && saw_event(&w, EV_BUY_FAIL));

    w.ore = tun.turret_cost * (tun.turret_max + 1);
    for (int k = 0; k < tun.turret_max + 1; k++) {
        world_tick(&w, &buy, &tun);
        run(&w, &tun, 1);
    }
    CHECK(defenses_count(&w.defenses, DEF_TURRET) == tun.turret_max);
    CHECK(w.ore == tun.turret_cost);   // a última compra foi recusada pelo limite
    CHECK(saw_event(&w, EV_BUY));
}

// A torre atira num inimigo ao alcance e o derruba.
static void turret_shoots_enemies(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    w.ore = tun.turret_cost;
    InputState buy = press(ACT_BUY1);
    world_tick(&w, &buy, &tun);
    const Defense *turret = NULL;
    for (int i = 0; i < DEFENSES_MAX; i++) if (w.defenses.items[i].alive) turret = &w.defenses.items[i];
    CHECK(turret != NULL);
    if (turret == NULL) return;
    // Um drone parado perto da torre (e longe da nave, que não atira).
    world_place_ship(&w, &tun, Vector3Scale(pos, -1.0f), Vector3Add(Vector3Scale(pos, -1.0f), (Vector3){ 1.0f, 0.0f, 0.0f }), 0.0f);
    world_spawn(&w, &tun, ENEMY_SWARM, 1, Vector3Add(turret->pos, Vector3Scale(Vector3Normalize(turret->pos), 25.0f)));
    for (int i = 0; i < seconds(10.0f) && enemies_alive(&w.enemies) > 0; i++) run(&w, &tun, 1);
    CHECK(saw_event(&w, EV_TURRET_SHOT));
    CHECK(enemies_alive(&w.enemies) == 0);
}

// O escudo segura as crateras dentro dele (até a vida acabar) e não as de fora.
static void shield_absorbs_craters_inside(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    w.ore = tun.shield_cost;
    InputState buy = press(ACT_BUY2);
    world_tick(&w, &buy, &tun);
    CHECK(defenses_count(&w.defenses, DEF_SHIELD) == 1);
    Vector3 up = Vector3Normalize(pos);
    int64_t volume = w.planet.volume;
    world_impact(&w, &tun, Vector3Scale(up, 100.0f), Vector3Negate(up), 3.0f, 2.0f);
    CHECK(w.planet.volume == volume && saw_event(&w, EV_SHIELD_BLOCK));
    // Do outro lado do planeta, a cratera sai.
    world_impact(&w, &tun, Vector3Scale(up, -100.0f), up, 3.0f, 2.0f);
    CHECK(w.planet.volume < volume);
    // Golpes em cima até a vida acabar: o escudo cai e o próximo cava.
    for (int i = 0; i < 100 && defenses_count(&w.defenses, DEF_SHIELD) > 0; i++) {
        world_impact(&w, &tun, Vector3Scale(up, 100.0f), Vector3Negate(up), 3.0f, 2.0f);
    }
    CHECK(defenses_count(&w.defenses, DEF_SHIELD) == 0 && saw_event(&w, EV_SHIELD_DOWN));
    int64_t before = w.planet.volume;
    world_impact(&w, &tun, Vector3Scale(up, 100.0f), Vector3Negate(up), 3.0f, 2.0f);
    CHECK(w.planet.volume < before);
}

// O satélite derruba uma bomba que passa perto dele.
static void satellite_intercepts_bombs(void)
{
    static World w;
    make_world(&w, &tun);
    Vector3 pos;
    park_ship(&w, &pos);
    w.ore = tun.satellite_cost;
    InputState buy = press(ACT_BUY3);
    world_tick(&w, &buy, &tun);
    const Defense *sat = NULL;
    for (int i = 0; i < DEFENSES_MAX; i++) if (w.defenses.items[i].alive) sat = &w.defenses.items[i];
    CHECK(sat != NULL);
    if (sat == NULL) return;
    Vector3 at = Vector3Add(sat->pos, Vector3Scale(Vector3Normalize(sat->pos), 10.0f));
    Shot *bomb = projectiles_spawn(&w.shots, SHOT_BOMB, at, Vector3Scale(Vector3Normalize(at), -5.0f), 20.0f);
    CHECK(bomb != NULL);
    if (bomb != NULL) {
        bomb->hp = 5.0f;
        bomb->target = Vector3Scale(Vector3Normalize(at), w.planet.params.radius);
    }
    run(&w, &tun, seconds(1.0f));
    CHECK(projectiles_count(&w.shots, SHOT_BOMB) == 0);
    CHECK(saw_event(&w, EV_INTERCEPT));
}

void suite_rocks(void)
{
    tun = test_tuning();
    CHECK(arena_init(&arena, 8u * 1024u * 1024u));
    RUN(restore_returns_the_original);
    RUN(restore_stays_inside_the_sphere);
    RUN(big_impacts_eject_fragments);
    RUN(meteor_passes_and_leaves);
    RUN(tractor_grabs_only_in_front);
    RUN(tether_breaks_when_stretched);
    RUN(dropped_rock_repairs_the_crater);
    RUN(scars_merge_nearby_impacts);
    RUN(autopilot_fetches_and_repairs);
    RUN(meteors_come_in_the_calm);
    RUN(bolts_mine_rocks_into_ore);
    RUN(ally_bolts_pass_through_rocks);
    RUN(buying_costs_ore_and_has_limits);
    RUN(turret_shoots_enemies);
    RUN(shield_absorbs_craters_inside);
    RUN(satellite_intercepts_bombs);
    arena_free(&arena);
}
