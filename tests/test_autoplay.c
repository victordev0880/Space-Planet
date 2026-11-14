#include "test.h"
#include "test_util.h"

#include <stdio.h>

#include "game/world.h"
#include "tools/autopilot.h"
#include "tools/autoplay.h"

static Autoplay ap;   // static: a struct é grande demais para a pilha

static void autoplay_parses_a_script(void)
{
    const char *text =
        "# comentario\n"
        "frames 100\n"
        "seed 42\n"
        "at 50 shot meio      # comentario no fim\n"
        "at 0  hold forward\n"
        "at 10 look 3 -1.5\n"
        "at 50 release forward\n"
        "at 20 press overlay\r\n";
    bool ok = autoplay_parse(&ap, text);
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);
    CHECK(ap.frames == 100);
    CHECK(ap.has_seed && ap.seed == 42u);
    CHECK(ap.event_count == 5);

    // Ordenados por quadro; no mesmo quadro vale a ordem do arquivo.
    for (int i = 1; i < ap.event_count; i++) CHECK(ap.events[i - 1].frame <= ap.events[i].frame);
    CHECK(ap.events[0].cmd == AP_HOLD && ap.events[0].action == ACT_FORWARD);
    CHECK(ap.events[1].cmd == AP_LOOK);
    CHECK_NEAR(ap.events[1].dy, -1.5f, 1e-6);
    CHECK(ap.events[3].cmd == AP_SHOT && ap.events[4].cmd == AP_RELEASE);
}

static void autoplay_plays_inputs_frame_by_frame(void)
{
    const char *text =
        "frames 60\n"
        "at 0  hold forward\n"
        "at 5  press overlay\n"
        "at 10 look 3 -1\n"
        "at 20 shot foto\n"
        "at 30 release forward\n"
        "at 30 look 0 0\n";
    CHECK(autoplay_parse(&ap, text));

    uint32_t fwd = ACT_BIT(ACT_FORWARD);
    uint32_t overlay = ACT_BIT(ACT_OVERLAY);
    int shots = 0;

    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        InputSample s = f.input;
        const char *shot = f.shot;
        CHECK(!f.has_cam);

        if (frame < 30) CHECK((s.held & fwd) != 0u);
        else            CHECK((s.held & fwd) == 0u);

        // "press" dura exatamente um quadro.
        if (frame == 5) CHECK((s.held & overlay) != 0u);
        else            CHECK((s.held & overlay) == 0u);

        if (frame >= 10 && frame < 30) CHECK(s.look_dx == 3.0f && s.look_dy == -1.0f);
        else                           CHECK(s.look_dx == 0.0f && s.look_dy == 0.0f);

        if (shot != NULL) {
            shots++;
            CHECK(frame == 20);
            CHECK_STR(shot, "foto");
        }
    }
    CHECK(shots == 1);
}

static void autoplay_cam_command_places_the_camera(void)
{
    const char *text =
        "frames 20\n"
        "at 3 cam 10 -20.5 30\n"
        "at 7 cam 1 2 3 4 5 6\n"
        "at 7 shot foto\n";
    bool ok = autoplay_parse(&ap, text);
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);

    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        CHECK(f.has_cam == (frame == 3 || frame == 7));   // vale só no quadro pedido
        if (frame == 3) {
            CHECK(f.cam_pos.x == 10.0f && f.cam_pos.y == -20.5f && f.cam_pos.z == 30.0f);
            CHECK(f.cam_target.x == 0.0f && f.cam_target.y == 0.0f && f.cam_target.z == 0.0f);
        }
        if (frame == 7) {
            CHECK(f.cam_pos.x == 1.0f && f.cam_target.x == 4.0f && f.cam_target.z == 6.0f);
            CHECK(f.shot != NULL);   // câmera e foto no mesmo quadro
        }
    }
}

static void autoplay_crater_commands(void)
{
    const char *text =
        "frames 20\n"
        "at 4 crater 0 2 0 5 3.5\n"
        "at 4 crater 1 0 0 2 1\n"
        "at 9 barrage 30 3 2\n";
    bool ok = autoplay_parse(&ap, text);
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);

    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        CHECK(f.crater_count == ((frame == 4) ? 2 : 0));     // valem só no quadro pedido
        CHECK(f.barrage_count == ((frame == 9) ? 30 : 0));
        if (frame == 4) {
            CHECK(f.craters[0].dir.y == 2.0f && f.craters[0].rim_radius == 5.0f && f.craters[0].depth == 3.5f);
            CHECK(f.craters[1].dir.x == 1.0f && f.craters[1].rim_radius == 2.0f);
        }
        if (frame == 9) CHECK(f.barrage_rim_radius == 3.0f && f.barrage_depth == 2.0f);
    }
}

// Os comandos de cratera de um quadro realmente escavam o planeta.
static void autoplay_apply_digs_the_planet(void)
{
    static Arena arena;
    if (arena.base == NULL) CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    arena_reset(&arena);

    Tuning t = test_tuning();
    World w;
    CHECK(world_init(&w, &t, 1u, &arena));
    int64_t volume = w.planet.volume;

    CHECK(autoplay_parse(&ap, "frames 10\nat 2 crater 0 1 0 4 3\nat 5 barrage 8 2 1.5\nat 7 cam 0 0 50\n"));
    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        autoplay_apply(&f, &w, &t);
        if (frame == 1) CHECK(w.planet.volume == volume && w.impact_count == 0u);
        if (frame == 2) CHECK(w.planet.volume < volume && w.impact_count == 1u);
        if (frame == 5) CHECK(w.impact_count == 9u);
        if (frame == 7) CHECK(w.cam_mode == CAM_FREE && w.view.pos.z == 50.0f);
    }

    // A cratera do quadro 2 ficou no topo do planeta, onde foi mandada.
    const Impact *first = world_find_impact(&w, 1u);
    CHECK(first != NULL);
    if (first != NULL) {
        CHECK_NEAR(first->radius, 4.0f, 1e-6);
        CHECK(first->pos.y > w.planet.params.radius - 1.0f);
        CHECK_NEAR(first->pos.x, 0.0f, 0.05f);
    }
}

// "view" troca entre a câmera de jogo e a de apresentação.
static void autoplay_view_command_switches_cameras(void)
{
    static Arena arena;
    if (arena.base == NULL) CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    arena_reset(&arena);

    Tuning t = test_tuning();
    World w;
    CHECK(world_init(&w, &t, 1u, &arena));
    CHECK(w.cam_mode == CAM_SHIP);

    bool ok = autoplay_parse(&ap, "frames 10\nat 2 view orbit\nat 6 view ship\n");
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);
    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        CHECK(f.has_view == (frame == 2 || frame == 6));   // vale só no quadro pedido
        autoplay_apply(&f, &w, &t);
        if (frame < 2)               CHECK(w.cam_mode == CAM_SHIP && w.ship_active);
        if (frame >= 2 && frame < 6) CHECK(w.cam_mode == CAM_ORBIT && !w.ship_active);
        if (frame >= 6)              CHECK(w.cam_mode == CAM_SHIP && w.ship_active);
    }
}

// "ship" põe a nave onde o roteiro manda, apontada para o alvo.
static void autoplay_ship_command_places_the_ship(void)
{
    static Arena arena;
    if (arena.base == NULL) CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    arena_reset(&arena);

    Tuning t = test_tuning();
    World w;
    CHECK(world_init(&w, &t, 1u, &arena));
    world_set_view(&w, &t, CAM_ORBIT);
    w.ship.hull = 1.0f;

    bool ok = autoplay_parse(&ap, "frames 6\nat 2 ship 0 60 0 0 60 -10\nat 4 ship 0 0 70 0 0 0 25\nat 4 cam 0 5 80\n");
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);
    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        CHECK(f.has_ship == (frame == 2 || frame == 4));   // vale só no quadro pedido
        autoplay_apply(&f, &w, &t);

        if (frame == 2) {
            // Na posição, inteira, parada, de nariz para o alvo e de topo para
            // longe do planeta; e a câmera de jogo assumiu.
            CHECK(w.cam_mode == CAM_SHIP && w.ship_active);
            CHECK_NEAR(Vector3Distance(w.ship.pos, (Vector3){ 0.0f, 60.0f, 0.0f }), 0.0f, 1e-5);
            CHECK(ship_forward(&w.ship).z < -0.999f);
            CHECK(ship_up(&w.ship).y > 0.999f);
            CHECK_NEAR(Vector3Length(w.ship.vel), 0.0f, 1e-6);
            CHECK_NEAR(w.ship.hull, t.ship_hull_max, 1e-6);
            CHECK(Vector3Distance(w.view.pos, w.ship.pos) < t.chase_distance * 2.0f);
        }
        if (frame == 4) {
            // Com velocidade, e com um "cam" no mesmo quadro: a nave fica
            // posta e a câmera livre a fotografa de fora.
            CHECK(w.cam_mode == CAM_FREE && w.cam_return == CAM_SHIP && w.ship_active);
            CHECK(ship_forward(&w.ship).z < -0.999f);
            CHECK_NEAR(Vector3DotProduct(w.ship.vel, ship_forward(&w.ship)), 25.0f, 1e-3);
            CHECK(w.view.pos.z == 80.0f);
        }
    }
}

// "waves", "spawn" e "autopilot".
static void autoplay_combat_commands(void)
{
    static Arena arena;
    if (arena.base == NULL) CHECK(arena_init(&arena, 2u * 1024u * 1024u));
    arena_reset(&arena);

    Tuning t = test_tuning();
    static World w;
    CHECK(world_init(&w, &t, 1u, &arena));
    static WavePlan plan;
    plan.count = 1;
    plan.waves[0].pause = 1.0f;
    plan.waves[0].group_count = 1;
    plan.waves[0].groups[0] = (WaveGroup){ ENEMY_SWARM, 2, 0.0f, 0.0f, 0.0f };
    world_set_plan(&w, &plan);
    CHECK(w.director.phase == DIRECTOR_PAUSE);

    bool ok = autoplay_parse(&ap,
                             "frames 8\n"
                             "at 1 waves off\n"
                             "at 2 spawn swarm 3 0 60 0\n"
                             "at 2 spawn bomber 1 60 0 0\n"
                             "at 3 autopilot on\n"
                             "at 5 autopilot off\n"
                             "at 6 waves on\n");
    if (!ok) test_fail(__FILE__, __LINE__, ap.error);
    CHECK(ok);
    for (int frame = 0; frame < ap.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&ap, frame, &f);
        CHECK(f.autopilot == (frame >= 3 && frame < 5));   // vale até ser desligado
        CHECK(f.spawn_count == ((frame == 2) ? 2 : 0));
        autoplay_apply(&f, &w, &t);
        if (frame == 1) CHECK(w.director.phase == DIRECTOR_OFF);
        if (frame == 2) {
            CHECK(w.enemies.alive_by_type[ENEMY_SWARM] == 3 && w.enemies.alive_by_type[ENEMY_BOMBER] == 1);
        }
        if (frame == 6) CHECK(w.director.phase == DIRECTOR_PAUSE && w.director.wave == 0);   // recomeçou
    }
}

static void expect_error(const char *text, const char *fragment)
{
    CHECK(!autoplay_parse(&ap, text));
    if (strstr(ap.error, fragment) == NULL) test_fail(__FILE__, __LINE__, ap.error);
    test_checks++;
}

static void autoplay_rejects_bad_scripts(void)
{
    expect_error("at 0 hold forward\n", "frames");
    expect_error("frames 10\nat 0 hold voar\n", "linha 2");
    expect_error("frames 10\nat 0 hold voar\n", "voar");
    expect_error("frames 10\nat 10 hold forward\n", "quadro 10");
    expect_error("frames 10\nat -1 hold forward\n", "quadro invalido");
    expect_error("frames 10\nat 2 shot a\nat 2 shot b\n", "ja existe");
    expect_error("frames 10\nat 2 shot ../fora\n", "shot");
    expect_error("frames 10\nat 2 pular\n", "comando desconhecido");
    expect_error("frames 10\nat 2 look 1\n", "look");
    expect_error("frames 0\n", "frames");
    expect_error("frames 10\nvoar 3\n", "instrucao desconhecida");
    expect_error("frames 10\nat 1 hold forward extra\n", "esperado");
    expect_error("frames 10\nat 1 cam 1 2\n", "cam");
    expect_error("frames 10\nat 1 cam 1 2 3 4\n", "cam");
    expect_error("frames 10\nat 1 cam 1 dois 3\n", "cam");
    expect_error("frames 10\nat 1 cam 1 2 3 4 5 6 7\n", "cam");
    expect_error("frames 10\nat 1 cam 1 2 3 4 5 6 7 8\n", "palavras demais");
    expect_error("frames 10\nat 1 ship 1 2 3\n", "ship");
    expect_error("frames 10\nat 1 ship 1 2 3 1 2 3\n", "alvo");
    expect_error("frames 10\nat 1 ship 1 2 3 0 0 0 -5\n", "negativa");
    expect_error("frames 10\nat 1 crater 0 1 0 5\n", "crater");
    expect_error("frames 10\nat 1 crater 0 0 0 5 3\n", "direcao");
    expect_error("frames 10\nat 1 crater 0 1 0 -5 3\n", "positivos");
    expect_error("frames 10\nat 1 crater 0 1 0 5 0\n", "positivos");
    expect_error("frames 10\nat 1 barrage 0 3 2\n", "quantidade");
    expect_error("frames 10\nat 1 barrage 99999 3 2\n", "quantidade");
    expect_error("frames 10\nat 1 barrage 5 3\n", "barrage");
    expect_error("frames 10\nat 1 waves talvez\n", "waves");
    expect_error("frames 10\nat 1 autopilot\n", "autopilot");
    expect_error("frames 10\nat 1 spawn dragao 2 0 60 0\n", "dragao");
    expect_error("frames 10\nat 1 spawn swarm 0 0 60 0\n", "quantidade");
    expect_error("frames 10\nat 1 spawn swarm 2 0 0 0\n", "centro");
    expect_error("frames 10\nat 1 spawn swarm 2 0 60\n", "spawn");
    expect_error("frames 10\nat 1 view\n", "view");
    expect_error("frames 10\nat 1 view livre\n", "view");
    expect_error("frames 10\nat 1 view ship orbit\n", "view");
}

static void autoplay_skips_utf8_bom(void)
{
    CHECK(autoplay_parse(&ap, "\xEF\xBB\xBF" "frames 10\nat 0 hold forward\n"));
    CHECK(ap.frames == 10 && ap.event_count == 1);
}

static void autoplay_missing_file_is_an_error(void)
{
    CHECK(!autoplay_load_file(&ap, "roteiros/nao_existe.txt"));
    CHECK(ap.error[0] != '\0');
}

// Roda um roteiro inteiro pela mesma cadeia do jogo (roteiro -> tracker ->
// mundo), um passo de lógica por quadro, e devolve o hash final.
static uint64_t play(const char *path, const Tuning *t)
{
    static Autoplay script;
    if (!autoplay_load_file(&script, path)) {
        test_fail(__FILE__, __LINE__, script.error);
        return 0u;
    }

    static Arena arena;
    if (arena.base == NULL && !arena_init(&arena, 2u * 1024u * 1024u)) return 0u;
    arena_reset(&arena);

    // Como no jogo: com as hordas de data/waves.txt.
    static WavePlan plan;
    char err[160];
    if (!waves_load(&plan, "data/waves.txt", err, sizeof err)) {
        test_fail(__FILE__, __LINE__, err);
        return 0u;
    }

    static World w;
    InputTracker tracker;
    if (!world_init(&w, t, script.has_seed ? script.seed : 1u, &arena)) return 0u;
    world_set_plan(&w, &plan);
    input_init(&tracker);
    for (int frame = 0; frame < script.frames; frame++) {
        AutoplayFrame f;
        autoplay_step(&script, frame, &f);
        autoplay_apply(&f, &w, t);
        if (f.autopilot) autopilot_drive(&w, t, &f.input);
        input_feed(&tracker, &f.input);
        InputState st = input_tick(&tracker);
        world_tick(&w, &st, t);
    }
    return world_hash(&w);
}

// Todos os roteiros que acompanham o jogo precisam ser válidos e dar sempre
// o mesmo resultado.
static void autoplay_shipped_scripts_are_deterministic(void)
{
    static const char *const SCRIPTS[] = {
        "roteiros/fundacao.txt", "roteiros/planeta.txt", "roteiros/desempenho.txt", "roteiros/cratera.txt",
        "roteiros/voo.txt",      "roteiros/combate.txt", "roteiros/inimigos.txt", "roteiros/modelos.txt",
        "roteiros/evolucao.txt", "roteiros/onda1.txt",   "roteiros/boss1.txt",   "roteiros/conserto.txt", "roteiros/defesas.txt",
        "roteiros/menus.txt",    "roteiros/radar.txt",   "roteiros/hive1.txt",   "roteiros/worm1.txt",   "roteiros/devourer1.txt",
        "roteiros/online_anfitriao.txt", "roteiros/online_cliente.txt",
    };
    Tuning t = test_tuning();

    for (int i = 0; i < (int)(sizeof SCRIPTS / sizeof SCRIPTS[0]); i++) {
        uint64_t first = play(SCRIPTS[i], &t);
        uint64_t second = play(SCRIPTS[i], &t);
        CHECK(first != 0u);
        CHECK(first == second);
    }
}

static void autoplay_frame_stats_track_the_worst_frame(void)
{
    FrameStats s;
    frame_stats_init(&s, FRAME_BUDGET_MS);
    frame_stats_add(&s, 10, 4.0, 1.0);
    frame_stats_add(&s, 11, 20.0, 3.0);
    frame_stats_add(&s, 12, 6.0, 2.0);
    CHECK(s.frames == 3);
    CHECK_NEAR(s.sum_ms, 30.0, 1e-9);
    CHECK_NEAR(s.worst_ms, 20.0, 1e-9);
    CHECK(s.worst_frame == 11);
    CHECK_NEAR(s.worst_cpu_ms, 3.0, 1e-9);
    CHECK(s.over_budget == 1);   // só o quadro de 20 ms passa de 16,7 ms
}

static void autoplay_percentile_ignores_rare_spikes(void)
{
    FrameStats s;
    frame_stats_init(&s, FRAME_BUDGET_MS);
    CHECK(frame_stats_percentile(&s, 0.99) == 0.0);

    // 198 quadros bons e 2 travadas: 1% de quadros ruins não mexe no p99.
    for (int i = 0; i < 198; i++) frame_stats_add(&s, i, 2.1, 1.0);
    frame_stats_add(&s, 198, 30.0, 1.0);
    frame_stats_add(&s, 199, 400.0, 1.0);

    CHECK(s.over_budget == 2);
    CHECK(s.slow_frames[0] == 198 && s.slow_frames[1] == 199);   // quais quadros estouraram
    CHECK_NEAR(s.worst_ms, 400.0, 1e-9);

    // A lista guarda só os primeiros; a contagem continua.
    for (int i = 0; i < 50; i++) frame_stats_add(&s, 1000 + i, 99.0, 1.0);
    CHECK(s.over_budget == 52);
    CHECK(s.slow_frames[FRAME_SLOW_LIST - 1] == 1000 + FRAME_SLOW_LIST - 3);
    frame_stats_init(&s, FRAME_BUDGET_MS);
    for (int i = 0; i < 198; i++) frame_stats_add(&s, i, 2.1, 1.0);
    frame_stats_add(&s, 198, 30.0, 1.0);
    frame_stats_add(&s, 199, 400.0, 1.0);
    double p50 = frame_stats_percentile(&s, 0.50);
    double p99 = frame_stats_percentile(&s, 0.99);
    CHECK(p50 >= 2.1 && p50 <= 2.1 + FRAME_HIST_STEP_MS);
    CHECK(p99 >= 2.1 && p99 <= 2.1 + FRAME_HIST_STEP_MS);

    // Pedindo todos os quadros, a travada aparece (e vale o valor exato).
    CHECK_NEAR(frame_stats_percentile(&s, 1.0), 400.0, 1e-9);
    double p995 = frame_stats_percentile(&s, 0.995);
    CHECK(p995 >= 30.0 && p995 <= 30.0 + FRAME_HIST_STEP_MS);
}

static bool file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof line, f) != NULL) {
        if (strstr(line, needle) != NULL) { found = true; break; }
    }
    fclose(f);
    return found;
}

static void autoplay_writes_the_report(void)
{
    const char *path = "build/test_report.txt";
    AutoplayReport r;
    memset(&r, 0, sizeof r);
    r.script = "roteiros/teste.txt";
    r.frames_planned = 100;
    r.frames_run = 100;
    r.ticks = 100u;
    r.seed = 42u;
    r.state_hash = 0xabcdef0123456789ULL;
    r.screenshots = 2;
    r.warmup_frames = 10;
    frame_stats_init(&r.stats, FRAME_BUDGET_MS);
    frame_stats_add(&r.stats, 10, 5.0, 1.0);    // 200 FPS
    frame_stats_add(&r.stats, 11, 20.0, 2.0);   //  50 FPS
    r.errors = 1;
    r.first_problem = "algo deu errado";

    CHECK(autoplay_write_report(path, &r));
    CHECK(file_contains(path, "mode = deterministico"));
    CHECK(file_contains(path, "frames_run = 100"));
    CHECK(file_contains(path, "state_hash = abcdef0123456789"));
    CHECK(file_contains(path, "fps_avg = 80.0"));      // média de 12,5 ms
    CHECK(file_contains(path, "fps_min = 50.0"));
    CHECK(file_contains(path, "frame_ms_worst = 20.000"));
    CHECK(file_contains(path, "frames_over_budget = 1"));
    CHECK(file_contains(path, "slow_frames = 11"));
    CHECK(file_contains(path, "errors = 1"));
    CHECK(file_contains(path, "first_problem = algo deu errado"));
    remove(path);

    CHECK(!autoplay_write_report("pasta_que_nao_existe/report.txt", &r));
}

void suite_autoplay(void)
{
    RUN(autoplay_parses_a_script);
    RUN(autoplay_plays_inputs_frame_by_frame);
    RUN(autoplay_cam_command_places_the_camera);
    RUN(autoplay_crater_commands);
    RUN(autoplay_apply_digs_the_planet);
    RUN(autoplay_view_command_switches_cameras);
    RUN(autoplay_ship_command_places_the_ship);
    RUN(autoplay_combat_commands);
    RUN(autoplay_rejects_bad_scripts);
    RUN(autoplay_skips_utf8_bom);
    RUN(autoplay_missing_file_is_an_error);
    RUN(autoplay_shipped_scripts_are_deterministic);
    RUN(autoplay_frame_stats_track_the_worst_frame);
    RUN(autoplay_percentile_ignores_rare_spikes);
    RUN(autoplay_writes_the_report);
}
