// TERRA FRATURADA - ponto de entrada.
//
// O laço principal faz sempre as mesmas quatro coisas por quadro:
//   1. lê o input (teclado/mouse reais ou o roteiro do autoplay);
//   2. avança a simulação em passos fixos;
//   3. trata as ações do aplicativo (overlay, recarga do tuning);
//   4. desenha, interpolando entre o passo anterior e o atual.

#include "raylib.h"
#include "rlgl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "audio/audio.h"
#include "core/arena.h"
#include "core/input.h"
#include "core/loop.h"
#include "game/score.h"
#include "game/tuning.h"
#include "game/waves.h"
#include "game/world.h"
#include "platform/platform_input.h"
#include "ui/cards.h"
#include "render/render.h"
#include "tools/autopilot.h"
#include "tools/autoplay.h"
#include "ui/debug_overlay.h"
#include "ui/hud.h"

static const char *const CRATER_NAMES[CRATER_SIZE_COUNT] = { "pequena", "media", "grande", "enorme" };

#define WINDOW_TITLE  "Terra Fraturada"
#define DATA_DIR      "data"
#define TUNING_PATH   DATA_DIR "/" TUNING_FILE
#define WAVES_PATH    DATA_DIR "/waves.txt"
#define SAVE_DIR      "save"
#define BEST_PATH     SAVE_DIR "/highscore.dat"
#define CAPTURES_DIR  "captures"
#define REPORT_PATH   CAPTURES_DIR "/report.txt"

#define EXIT_OK       0
#define EXIT_PROBLEMS 1   // o autoplay terminou, mas registrou erros
#define EXIT_USAGE    2   // argumentos ou arquivos de dados inválidos

// Memória da partida (grade de voxels do planeta), reservada uma única vez.
// Uma grade de 128 pontos por eixo usa 4 MB; a de 256, o máximo aceito, 32 MB.
#define WORLD_ARENA_BYTES ((size_t)40 * 1024 * 1024)

typedef struct {
    const char *autoplay_path;   // NULL = jogo normal
    bool        realtime;        // autoplay com relógio real e vsync
    bool        no_shaders;      // desenha sem shaders próprios
    bool        verbose;
    bool        mute;            // sem som (o autoplay é sempre mudo)
} Options;

// ---------------------------------------------------------------------------
//  Log: conta avisos e erros (do raylib e nossos) para o relatório do autoplay
// ---------------------------------------------------------------------------
static int  log_warnings = 0;
static int  log_errors = 0;
static char log_first_problem[200] = "";
static bool log_verbose = false;

static void on_trace_log(int level, const char *text, va_list args)
{
    char message[512];
    vsnprintf(message, sizeof message, text, args);

    if (level >= LOG_WARNING) {
        if (level >= LOG_ERROR) log_errors++;
        else                    log_warnings++;
        if (log_first_problem[0] == '\0') {
            snprintf(log_first_problem, sizeof log_first_problem, "%.190s", message);
        }
    } else if (!log_verbose) {
        return;
    }

    const char *tag = (level >= LOG_ERROR) ? "ERRO" : (level == LOG_WARNING) ? "AVISO" : "INFO";
    printf("%s: %s\n", tag, message);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
static void print_usage(void)
{
    printf("Uso: terra [opcoes]\n"
           "  --autoplay <roteiro>   executa um roteiro de inputs, salva screenshots em\n"
           "                         captures/ e grava captures/report.txt\n"
           "  --realtime             com --autoplay: usa o relogio real e o vsync, como\n"
           "                         no jogo normal (o padrao e um passo por quadro)\n"
           "  --no-shaders           desenha sem os shaders de assets/shaders (mais simples)\n"
           "  --mute                 sem som\n"
           "  --verbose              mostra as mensagens informativas do raylib\n"
           "  --help                 mostra esta ajuda\n");
}

static bool parse_options(int argc, char **argv, Options *opt)
{
    opt->autoplay_path = NULL;
    opt->realtime = false;
    opt->no_shaders = false;
    opt->verbose = false;
    opt->mute = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--autoplay") == 0 && i + 1 < argc) {
            opt->autoplay_path = argv[++i];
        } else if (strcmp(argv[i], "--realtime") == 0) {
            opt->realtime = true;
        } else if (strcmp(argv[i], "--no-shaders") == 0) {
            opt->no_shaders = true;
        } else if (strcmp(argv[i], "--mute") == 0) {
            opt->mute = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            opt->verbose = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            return false;
        } else {
            fprintf(stderr, "ERRO: argumento invalido ou incompleto: %s\n\n", argv[i]);
            print_usage();
            return false;
        }
    }
    if (opt->realtime && opt->autoplay_path == NULL) {
        fprintf(stderr, "ERRO: --realtime so vale junto com --autoplay\n\n");
        print_usage();
        return false;
    }
    return true;
}

// Salva o quadro atual em captures/<nome>.png. Chamar depois de desenhar tudo
// e antes de EndDrawing, enquanto a imagem ainda está no buffer de desenho.
//
// Não usa TakeScreenshot() porque ele ignora a pasta do nome (grava sempre no
// diretório atual) e lê um tamanho errado em telas com escala de DPI.
static bool save_screenshot(const char *name)
{
    rlDrawRenderBatchActive();   // envia o que ainda está na fila de desenho
    int width = GetRenderWidth();
    int height = GetRenderHeight();
    Image image = { rlReadScreenPixels(width, height), width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    bool ok = ExportImage(image, TextFormat("%s/%s.png", CAPTURES_DIR, name));
    UnloadImage(image);
    if (!ok) TraceLog(LOG_ERROR, "autoplay: nao consegui salvar o screenshot \"%s\"", name);
    return ok;
}

// F5: relê data/tuning.txt, data/enemies.txt e data/waves.txt. Se algum
// estiver errado, mantém os valores antigos e mostra o motivo na tela. Se a
// receita do planeta mudou, ele é gerado de novo (o que desfaz qualquer
// destruição: é uma tecla de debug). As hordas novas valem na próxima partida.
static void reload_data(Tuning *tuning, WavePlan *plan, FixedLoop *loop, World *world, Renderer *renderer,
                        Audio *audio, bool muted, DebugOverlay *overlay)
{
    char err[160];
    static WavePlan fresh;
    if (!tuning_load(tuning, DATA_DIR, err, sizeof err)) {
        TraceLog(LOG_ERROR, "%s", err);
        debug_overlay_error(overlay, err);
        return;
    }
    if (!waves_load(&fresh, WAVES_PATH, err, sizeof err)) {
        TraceLog(LOG_ERROR, "%s: %s", WAVES_PATH, err);
        debug_overlay_error(overlay, TextFormat("waves.txt: %s", err));
        return;
    }
    *plan = fresh;
    loop->dt = 1.0 / (double)tuning->sim_hz;
    loop->max_steps = tuning->sim_max_steps;
    bool regenerated = world_apply_tuning(world, tuning);
    render_apply_tuning(renderer, world, tuning);
    audio_apply_tuning(audio, tuning, muted);
    debug_overlay_clear_error(overlay);
    debug_overlay_notify(overlay, regenerated ? "dados recarregados, planeta gerado de novo" : "dados recarregados",
                         world->tick + (uint64_t)(tuning->sim_hz * 2));
}

int main(int argc, char **argv)
{
    Options opt;
    if (!parse_options(argc, argv, &opt)) return EXIT_USAGE;

    // static: structs grandes ficam fora da pilha.
    static Autoplay script;
    bool autoplay = (opt.autoplay_path != NULL);
    if (autoplay && !autoplay_load_file(&script, opt.autoplay_path)) {
        fprintf(stderr, "ERRO em %s: %s\n", opt.autoplay_path, script.error);
        return EXIT_USAGE;
    }

    log_verbose = opt.verbose;
    SetTraceLogCallback(on_trace_log);

    // Os dados são procurados a partir do diretório atual; se o jogo foi
    // aberto de outro lugar, tenta a pasta do executável.
    if (!FileExists(TUNING_PATH)) ChangeDirectory(GetApplicationDirectory());

    static Tuning tuning;
    static WavePlan plan;
    char err[160];
    if (!tuning_load(&tuning, DATA_DIR, err, sizeof err)) {
        fprintf(stderr, "ERRO em %s/%s\n", DATA_DIR, err);
        return EXIT_USAGE;
    }
    if (!waves_load(&plan, WAVES_PATH, err, sizeof err)) {
        fprintf(stderr, "ERRO em %s: %s\n", WAVES_PATH, err);
        return EXIT_USAGE;
    }

    // --- janela -------------------------------------------------------------
    // No autoplay determinístico cada quadro roda um passo de lógica, sem vsync
    // nem limite de FPS: o resultado se repete e os tempos mostram o custo real.
    // Nos outros casos vale o relógio de verdade.
    bool one_step_per_frame = autoplay && !opt.realtime;

    unsigned int flags = 0;
    if (tuning.window_msaa) flags |= FLAG_MSAA_4X_HINT;
    if (tuning.window_vsync && !one_step_per_frame) flags |= FLAG_VSYNC_HINT;
    if (autoplay) flags |= FLAG_WINDOW_UNFOCUSED;   // não toma o foco de quem está usando o computador
    SetConfigFlags(flags);
    InitWindow(tuning.window_width, tuning.window_height, WINDOW_TITLE);
    if (!IsWindowReady()) {
        fprintf(stderr, "ERRO: nao consegui criar a janela\n");
        return EXIT_USAGE;
    }
    if (!one_step_per_frame && tuning.window_fps_cap > 0) SetTargetFPS(tuning.window_fps_cap);
    if (autoplay) MakeDirectory(CAPTURES_DIR);

    // --- estado -------------------------------------------------------------
    uint64_t seed = (autoplay && script.has_seed) ? script.seed : (uint64_t)tuning.sim_seed;

    static World world;
    static Renderer renderer;
    static DebugOverlay overlay;
    static Audio audio;
    static Arena world_arena;
    FixedLoop loop;
    InputTracker tracker;
    FrameStats stats;

    // Com vsync os quadros oscilam em torno de 16,7 ms; lá só conta como
    // estouro o quadro que perdeu uma atualização inteira do monitor.
    frame_stats_init(&stats, one_step_per_frame ? FRAME_BUDGET_MS : FRAME_BUDGET_MS * 1.5);

    double init_start = GetTime();
    bool world_ok = arena_init(&world_arena, WORLD_ARENA_BYTES) && world_init(&world, &tuning, seed, &world_arena);
    double world_init_ms = (GetTime() - init_start) * 1000.0;

    init_start = GetTime();
    bool render_ok = world_ok && render_init(&renderer, &world, &tuning, !opt.no_shaders);
    double render_init_ms = (GetTime() - init_start) * 1000.0;

    if (!world_ok || !render_ok) {
        fprintf(stderr, "ERRO: nao consegui criar o planeta (memoria insuficiente?)\n");
        CloseWindow();
        return EXIT_USAGE;
    }
    TraceLog(LOG_INFO, "inicio: mundo em %.0f ms, renderizacao em %.0f ms", world_init_ms, render_init_ms);
    double planet_mesh_ms = renderer.planet.rebuild_ms;
    world_set_plan(&world, &plan);
    bool muted = autoplay || opt.mute;   // o autoplay é mudo: não atrapalha quem está usando o computador
    audio_init(&audio, &world, &tuning, muted);
    debug_overlay_init(&overlay);
    fixed_loop_init(&loop, tuning.sim_hz, tuning.sim_max_steps);
    input_init(&tracker);

    // O recorde só é gravado em partidas de verdade, nunca no autoplay.
    int64_t best_score = autoplay ? 0 : score_load_best(BEST_PATH);
    bool new_best = false;
    GameState last_state = world.state;

    bool cursor_captured = false;
    int skip_mouse_frames = 0;
    int crater_size = 1;   // tamanho da cratera de debug (começa na média)
    int frame = 0;
    int screenshots = 0;
    int boss_downs = 0, boss_down_frames[AUTOPLAY_MAX_BOSS_DOWNS] = { 0 };
    double last_start = GetTime();

    // --- laço principal -----------------------------------------------------
    while (!WindowShouldClose()) {
        double start = GetTime();
        double frame_seconds = start - last_start;
        last_start = start;

        // 1. input
        InputSample sample;
        const char *shot = NULL;
        if (autoplay) {
            AutoplayFrame step;
            autoplay_step(&script, frame, &step);
            sample = step.input;
            shot = step.shot;
            autoplay_apply(&step, &world, &tuning);   // câmera, nave, inimigos e crateras pedidos pelo roteiro
            if (step.autopilot) autopilot_drive(&world, world_tuning(&world), &sample);
        } else {
            sample = platform_poll_input(cursor_captured && skip_mouse_frames == 0);
            if (skip_mouse_frames > 0) skip_mouse_frames--;
            // Cartas de evolução: um clique numa carta vale como a tecla dela.
            if (world.evo.choosing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int card = cards_hit(&world, GetMousePosition());
                if (card >= 0) sample.held |= ACT_BIT(ACT_CARD1 + card);
            }
        }
        input_feed(&tracker, &sample);

        // 2. simulação em passos fixos
        int steps = one_step_per_frame ? 1 : fixed_loop_advance(&loop, frame_seconds);
        uint32_t pressed = 0u;
        for (int i = 0; i < steps; i++) {
            InputState state = input_tick(&tracker);
            pressed |= state.pressed;
            world_tick(&world, &state, &tuning);
        }
        float alpha = one_step_per_frame ? 1.0f : (float)fixed_loop_alpha(&loop);
        double sim_ms = (GetTime() - start) * 1000.0;

        // 3. ações do aplicativo (não fazem parte da simulação)
        if (pressed & ACT_BIT(ACT_OVERLAY)) overlay.visible = !overlay.visible;
        if (pressed & ACT_BIT(ACT_WIREFRAME)) renderer.wireframe = !renderer.wireframe;
        if (pressed & ACT_BIT(ACT_RELOAD)) {
            reload_data(&tuning, &plan, &loop, &world, &renderer, &audio, muted, &overlay);
        }

        // Fim da partida: guarda o recorde; R começa outra.
        if (world.state != last_state && world.state != GAME_PLAYING) {
            new_best = world.score.points > best_score;
            if (new_best && !autoplay) {
                best_score = world.score.points;
                MakeDirectory(SAVE_DIR);
                if (!score_save_best(BEST_PATH, best_score)) TraceLog(LOG_WARNING, "nao consegui gravar %s", BEST_PATH);
            }
        }
        last_state = world.state;
        uint64_t notice_until = world.tick + (uint64_t)(tuning.sim_hz * 2);
        if ((pressed & ACT_BIT(ACT_RESET)) && world.state != GAME_PLAYING) {
            world.plan = plan;   // hordas relidas com F5 valem a partir daqui
            world_restart(&world, &tuning);
            render_apply_tuning(&renderer, &world, &tuning);
            new_best = false;
            last_state = world.state;
            pressed &= ~ACT_BIT(ACT_RESET);
        }

        // Ferramentas de debug do planeta destrutível: só com o F1 aberto,
        // para nenhuma tecla de teste disparar no meio de uma partida.
        bool debug_tools = overlay.visible && !autoplay;
        if (debug_tools && (pressed & ACT_BIT(ACT_RESET))) {
            world_reset_planet(&world, &tuning);
            render_apply_tuning(&renderer, &world, &tuning);
            debug_overlay_notify(&overlay, "planeta restaurado", notice_until);
        }
        if (debug_tools && (pressed & ACT_BIT(ACT_DEBUG_SIZE))) {
            crater_size = (crater_size + 1) % CRATER_SIZE_COUNT;
            debug_overlay_notify(&overlay,
                                 TextFormat("cratera %s: boca %.1f, profundidade %.1f", CRATER_NAMES[crater_size],
                                            (double)tuning.crater_radius[crater_size],
                                            (double)tuning.crater_depth[crater_size]),
                                 notice_until);
        }
        if (debug_tools && (pressed & ACT_BIT(ACT_DEBUG_CRATER))) {
            // Na câmera de jogo o impacto sai da nave, para onde o nariz
            // aponta (a mira). Nas outras, sai da câmera na direção do ponto
            // sob o cursor, ou do centro da tela se o cursor está preso.
            Vector3 origin, dir;
            if (world.cam_mode == CAM_SHIP && world.ship_active) {
                origin = world.ship.pos;
                dir = ship_forward(&world.ship);
            } else {
                Vector2 point = GetMousePosition();
                if (cursor_captured) {
                    point = (Vector2){ (float)GetScreenWidth() * 0.5f, (float)GetScreenHeight() * 0.5f };
                }
                Ray ray = GetScreenToWorldRay(point, render_camera(&world, &tuning, alpha));
                origin = ray.position;
                dir = ray.direction;
            }
            world_impact(&world, &tuning, origin, dir, tuning.crater_radius[crater_size],
                         tuning.crater_depth[crater_size]);
        }

        // O mouse fica preso à janela enquanto pilota a nave ou a câmera livre
        // (e solto enquanto as cartas de evolução estão na tela).
        bool want_capture = !autoplay && world.cam_mode != CAM_ORBIT && !world.evo.choosing;
        if (want_capture != cursor_captured) {
            if (want_capture) DisableCursor();
            else              EnableCursor();
            cursor_captured = want_capture;
            skip_mouse_frames = 2;   // o raylib reporta um salto ao capturar/soltar
        }

        // 4. desenho
        DebugInfo info = { 0 };
        info.steps = steps;
        info.alpha = alpha;
        info.star_count = renderer.stars.count;
        info.city_light_count = renderer.cities.count;
        info.planet_meshes = renderer.planet.mesh_count;
        info.planet_chunks = renderer.planet.chunk_count;
        info.planet_vertices = renderer.planet.vertex_total;
        info.planet_triangles = renderer.planet.triangle_total;
        info.seed = seed;
        info.autoplay_script = autoplay ? GetFileName(opt.autoplay_path) : NULL;
        info.autoplay_frame = frame;
        info.autoplay_frames = script.frames;

        // Daqui em diante, o tuning com as evoluções (barras, alcance da arma).
        const Tuning *play_tuning = world_tuning(&world);
        BeginDrawing();
        render_scene(&renderer, &world, play_tuning, alpha, overlay.visible);

        // Preenchidos depois de render_scene, que é quem remalha os chunks.
        info.remeshed = renderer.planet.remeshed_last_frame;
        info.remesh_backlog = renderer.planet.backlog;
        info.remesh_ms = (float)renderer.planet.remesh_ms_last_frame;
        info.particles = world.particles.count;
        info.shaded = renderer.planet.shaded;

        // Com o overlay aberto, o HUD se desloca para a direita dele.
        HudContext hud = { 0 };
        hud.center_x = GetScreenWidth() / 2;
        if (overlay.visible) hud.center_x = (DEBUG_OVERLAY_RIGHT + GetScreenWidth()) / 2;
        hud.camera = render_camera(&world, &tuning, alpha);
        hud.best_score = best_score;
        hud.new_best = new_best;
        hud_draw(&world, play_tuning, &hud);
        cards_draw(&world, play_tuning, GetTime());
        debug_overlay_draw(&overlay, &world, &info);
        audio_update(&audio, &world, play_tuning, hud.camera);
        rlDrawRenderBatchActive();
        double cpu_ms = (GetTime() - start) * 1000.0;

        if (shot != NULL && save_screenshot(shot)) screenshots++;
        EndDrawing();

        double frame_ms = (GetTime() - start) * 1000.0;
        debug_overlay_record_frame(&overlay, (float)frame_ms, (float)cpu_ms);

        // Quadros de aquecimento e quadros com screenshot (gravar o PNG é
        // lento) ficam fora das medições.
        if (autoplay && frame >= tuning.autoplay_warmup_frames && shot == NULL) {
            frame_stats_add(&stats, frame, frame_ms, cpu_ms);
            frame_stats_add_sim(&stats, sim_ms);
        }
        // Quadro em que cada boss caiu (o relatório lista os primeiros).
        if (autoplay && world.score.bosses > boss_downs && boss_downs < AUTOPLAY_MAX_BOSS_DOWNS) {
            boss_down_frames[boss_downs++] = frame;
        }

        frame++;
        if (autoplay && frame >= script.frames) break;
    }

    // --- fim ----------------------------------------------------------------
    int exit_code = EXIT_OK;
    if (autoplay) {
        if (frame < script.frames) {
            TraceLog(LOG_ERROR, "autoplay interrompido no quadro %d de %d", frame, script.frames);
        }

        AutoplayReport report = { 0 };
        report.script = opt.autoplay_path;
        report.realtime = opt.realtime;
        report.frames_planned = script.frames;
        report.frames_run = frame;
        report.ticks = world.tick;
        report.seed = seed;
        report.state_hash = world_hash(&world);
        report.screenshots = screenshots;
        report.warmup_frames = tuning.autoplay_warmup_frames;
        report.stats = stats;
        report.world_init_ms = world_init_ms;
        report.render_init_ms = render_init_ms;
        report.planet_mesh_ms = planet_mesh_ms;
        report.planet_meshes = renderer.planet.mesh_count;
        report.planet_triangles = renderer.planet.triangle_total;
        report.planet_hp = planet_hp(&world.planet);
        report.core_hp = planet_core_hp(&world.planet);
        report.collapsed = world.collapsed;
        report.impacts = (int)world.impact_count;
        report.particles_peak = world.particles.peak;
        report.remeshed_total = renderer.planet.remeshed_total;
        report.remesh_ms_total = renderer.planet.remesh_ms_total;
        report.remeshed_max_frame = renderer.planet.remeshed_max_frame;
        report.remesh_backlog_max = renderer.planet.backlog_max;
        report.remesh_ms_max = renderer.planet.remesh_ms_max;
        report.shaded = renderer.planet.shaded;
        report.score = world.score.points;
        report.kills = world.score.kills;
        report.wave = (world.director.phase == DIRECTOR_DONE) ? world.plan.count : world.director.wave + 1;
        report.waves_total = world.plan.count;
        report.waves_on = world.director.phase != DIRECTOR_OFF;
        report.game_state = (world.state == GAME_WON) ? "vitoria" : (world.state == GAME_LOST) ? "derrota" : "jogando";
        report.enemies_alive = enemies_alive(&world.enemies);
        report.boss_active = boss_active(&world.boss);
        report.boss_hp = world.boss.hp / fmaxf(world.boss.hp_max, 1e-3f);
        report.boss_phase = world.boss.phase;
        report.boss_downs = boss_downs;
        for (int i = 0; i < boss_downs; i++) report.boss_down_frames[i] = boss_down_frames[i];
        report.ability_owned = world.ability.owned;
        report.rocks_delivered = world.rocks.delivered;
        report.rocks_alive = rocks_alive(&world.rocks);
        report.planet_restored = (float)((double)world.repair.restored_total / (double)world.planet.initial_volume);
        for (int i = 0; i < REPAIR_SCARS; i++) report.scars_open += world.repair.scars[i].used ? 1 : 0;
        report.warnings = log_warnings;
        report.errors = log_errors;
        report.first_problem = log_first_problem;

        if (!autoplay_write_report(REPORT_PATH, &report)) {
            TraceLog(LOG_ERROR, "autoplay: nao consegui gravar %s", REPORT_PATH);
        }
        printf("autoplay: %d/%d quadros, %d screenshots, %d avisos, %d erros, hash %016llx -> %s\n",
               frame, script.frames, screenshots, log_warnings, log_errors,
               (unsigned long long)report.state_hash, REPORT_PATH);
        if (log_errors > 0) exit_code = EXIT_PROBLEMS;
    }

    audio_shutdown(&audio);
    render_shutdown(&renderer);
    CloseWindow();
    arena_free(&world_arena);
    return exit_code;
}
