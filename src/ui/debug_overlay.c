#include "raylib.h"

#include "ui/debug_overlay.h"

#include <stdio.h>
#include <string.h>

#include "core/vmath.h"

#define FONT_SIZE     20
#define LINE_HEIGHT   24
#define MARGIN        12
#define PADDING       10
#define PANEL_WIDTH   640

#define GRAPH_BAR     3       // largura de cada barra, em pixels
#define GRAPH_HEIGHT  44
#define GRAPH_MAX_MS  33.4f   // topo do gráfico = 2 quadros de 60 FPS
#define TARGET_MS     16.67f

static const Color PANEL_COLOR = { 0, 0, 0, 170 };
static const Color TEXT_COLOR  = { 215, 222, 235, 255 };
static const Color DIM_COLOR   = { 130, 140, 160, 255 };
static const Color GOOD_COLOR  = { 90, 210, 120, 255 };
static const Color WARN_COLOR  = { 235, 200, 80, 255 };
static const Color BAD_COLOR   = { 240, 90, 80, 255 };
static const Color HINT_COLOR  = { 190, 198, 214, 255 };
static const Color HINT_SHADOW = { 0, 0, 0, 200 };

void debug_overlay_init(DebugOverlay *o)
{
    memset(o, 0, sizeof *o);
}

void debug_overlay_record_frame(DebugOverlay *o, float frame_ms, float cpu_ms)
{
    o->frame_ms[o->head] = frame_ms;
    o->cpu_ms[o->head] = cpu_ms;
    o->head = (o->head + 1) % OVERLAY_HISTORY;
    if (o->filled < OVERLAY_HISTORY) o->filled++;
}

void debug_overlay_notify(DebugOverlay *o, const char *text, uint64_t until_tick)
{
    snprintf(o->notice, sizeof o->notice, "%s", text);
    o->notice_is_error = false;
    o->notice_until_tick = until_tick;
}

void debug_overlay_error(DebugOverlay *o, const char *text)
{
    snprintf(o->notice, sizeof o->notice, "%s", text);
    o->notice_is_error = true;
}

void debug_overlay_clear_error(DebugOverlay *o)
{
    if (o->notice_is_error) {
        o->notice[0] = '\0';
        o->notice_is_error = false;
    }
}

// Valor do meio de uma lista: metade dos quadros foi mais rápida, metade mais lenta.
static float median(const float *values, int count)
{
    if (count <= 0) return 0.0f;

    // Ordena uma cópia por inserção; com 120 valores o custo é desprezível.
    float sorted[OVERLAY_HISTORY];
    for (int i = 0; i < count; i++) {
        float value = values[i];
        int j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = value;
    }
    return sorted[count / 2];
}

static Color frame_color(float ms)
{
    if (ms <= TARGET_MS * 1.05f) return GOOD_COLOR;
    if (ms <= TARGET_MS * 1.5f) return WARN_COLOR;
    return BAD_COLOR;
}

static void draw_graph(const DebugOverlay *o, int x, int y)
{
    int width = OVERLAY_HISTORY * GRAPH_BAR;
    int base = y + GRAPH_HEIGHT;

    DrawRectangle(x, y, width, GRAPH_HEIGHT, (Color){ 255, 255, 255, 14 });

    for (int i = 0; i < o->filled; i++) {
        // Do quadro mais antigo (esquerda) para o mais novo (direita).
        int index = (o->head - o->filled + i + OVERLAY_HISTORY) % OVERLAY_HISTORY;
        float ms = o->frame_ms[index];
        float ratio = ms / GRAPH_MAX_MS;
        if (ratio > 1.0f) ratio = 1.0f;
        int height = (int)(ratio * GRAPH_HEIGHT);
        if (height < 1) height = 1;
        int bar_x = x + (OVERLAY_HISTORY - o->filled + i) * GRAPH_BAR;
        DrawRectangle(bar_x, base - height, GRAPH_BAR - 1, height, frame_color(ms));
    }

    // Linha dos 16,7 ms: barras acima dela são quadros abaixo de 60 FPS.
    int target_y = base - (int)(TARGET_MS / GRAPH_MAX_MS * GRAPH_HEIGHT);
    DrawLine(x, target_y, x + width, target_y, DIM_COLOR);
    DrawText("16.7 ms", x + width + 8, target_y - 5, 10, DIM_COLOR);
}

static void draw_notice(const DebugOverlay *o, const World *w)
{
    if (o->notice[0] == '\0') return;
    if (!o->notice_is_error && w->tick >= o->notice_until_tick) return;

    Color color = o->notice_is_error ? BAD_COLOR : GOOD_COLOR;
    int width = MeasureText(o->notice, FONT_SIZE);
    int x = (GetScreenWidth() - width) / 2;
    int y = GetScreenHeight() - 84;
    DrawRectangle(x - PADDING, y - 6, width + PADDING * 2, FONT_SIZE + 12, PANEL_COLOR);
    DrawText(o->notice, x, y, FONT_SIZE, color);
}

// Linha de controles no pé da tela. Na partida, aparece só antes da primeira
// horda (depois o jogador já sabe e ela só atrapalha) ou com o F1 aberto.
static void draw_hint(const DebugOverlay *o, const World *w)
{
    const char *hint = NULL;
    if (w->evo.choosing) return;   // as cartas de evolução têm as próprias instruções
    bool first_moments = w->director.phase == DIRECTOR_OFF
                      || (w->director.phase == DIRECTOR_PAUSE && w->director.wave == 0);
    if (o->visible) {
        hint = (w->cam_mode == CAM_FREE)
                   ? "WASD mover   mouse olhar   Shift rápido   C cratera   V tamanho   R restaura   F2 voltar"
                   : "C cratera onde a nave mira   V tamanho   R restaura o planeta   F2 pausa   F3 wireframe   F5 recarrega";
    } else if (w->cam_mode == CAM_FREE) {
        hint = "WASD mover   mouse olhar   Shift rápido   F2 volta ao jogo   F1 debug";
    } else if (w->cam_mode == CAM_SHIP && first_moments) {
        hint = "clique atira   W acelera   S freia e dá ré   sem teclas a nave paira   A/D lado   Q/E rola\n"
               "Shift boost   Espaço dash   Ctrl drift   Enter chama a próxima horda";
    } else if (w->cam_mode == CAM_ORBIT) {
        hint = "F1 debug   F2 câmera livre   F3 wireframe";
    }
    if (hint == NULL) return;

    // Uma ou duas linhas ('\n'), cada uma centralizada. Uma linha fica no pé
    // da tela, embaixo do painel da nave; duas não cabem ali sem cobrir o
    // painel, então sobem para logo acima dele.
    char lines[2][160] = { "", "" };
    const char *split = strchr(hint, '\n');
    int count = 1;
    if (split != NULL) {
        snprintf(lines[0], sizeof lines[0], "%.*s", (int)(split - hint), hint);
        snprintf(lines[1], sizeof lines[1], "%s", split + 1);
        count = 2;
    } else {
        snprintf(lines[0], sizeof lines[0], "%s", hint);
    }
    for (int i = 0; i < count; i++) {
        int width = MeasureText(lines[i], FONT_SIZE);
        int x = (GetScreenWidth() - width) / 2;
        int y = (count == 1) ? GetScreenHeight() - 36 : GetScreenHeight() - 196 + i * (FONT_SIZE + 8);
        // Sombra escura por baixo: a dica passa por cima do planeta, que é claro.
        DrawText(lines[i], x + 2, y + 2, FONT_SIZE, HINT_SHADOW);
        DrawText(lines[i], x, y, FONT_SIZE, HINT_COLOR);
    }
}

void debug_overlay_draw(const DebugOverlay *o, const World *w, const DebugInfo *info)
{
    draw_hint(o, w);
    draw_notice(o, w);
    if (!o->visible) return;

    // Quadro típico (mediana) e pior quadro da janela de histórico. A mediana,
    // e não a média, porque um único quadro travado (um F5, por exemplo)
    // puxaria a média para cima por dois segundos inteiros e daria a impressão
    // errada de que o jogo ficou lento.
    float typical = median(o->frame_ms, o->filled);
    float cpu_typical = median(o->cpu_ms, o->filled);
    float worst = 0.0f;
    for (int i = 0; i < o->filled; i++) {
        if (o->frame_ms[i] > worst) worst = o->frame_ms[i];
    }
    float fps = (typical > 0.0f) ? 1000.0f / typical : 0.0f;

    int lines = (info->autoplay_script != NULL) ? 10 : 9;
    int panel_height = PADDING * 2 + lines * LINE_HEIGHT + GRAPH_HEIGHT + 8;
    DrawRectangle(MARGIN, MARGIN, PANEL_WIDTH, panel_height, PANEL_COLOR);

    int x = MARGIN + PADDING;
    int y = MARGIN + PADDING;

    DrawText(TextFormat("FPS %.0f", fps), x, y, FONT_SIZE, frame_color(typical));
    DrawText(TextFormat("quadro %.2f ms   pior %.2f ms   cpu %.2f ms", typical, worst, cpu_typical),
             x + 110, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    DrawText(TextFormat("tick %llu   passos/quadro %d   alpha %.2f",
                        (unsigned long long)w->tick, info->steps, info->alpha),
             x, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    CamPose pose = w->view;
    const char *mode = (w->cam_mode == CAM_FREE) ? "livre" : (w->cam_mode == CAM_SHIP) ? "nave" : "orbita";
    DrawText(TextFormat("camera %s   pos %.1f %.1f %.1f   fov %.0f", mode, pose.pos.x, pose.pos.y, pose.pos.z,
                        (double)pose.fov),
             x, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    const Ship *s = &w->ship;
    DrawText(TextFormat("nave vel %.1f   alt %.1f   casco %.0f   escudo %.0f   energia %.0f",
                        (double)Vector3Length(Vector3Add(s->vel, s->fall)), (double)s->altitude, (double)s->hull,
                        (double)s->shield, (double)s->energy),
             x, y, FONT_SIZE, w->ship_active ? TEXT_COLOR : DIM_COLOR);
    y += LINE_HEIGHT;

    DrawText(TextFormat("planeta %.2f%% de massa   volume %lld",
                        (double)planet_hp(&w->planet) * 100.0, (long long)w->planet.volume),
             x, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    static const char *const PHASES[] = { "sem hordas", "calmaria", "em curso", "vencidas" };
    DrawText(TextFormat("inimigos %d   tiros %d   horda %d %s   congelado %d",
                        enemies_alive(&w->enemies), w->shots.pool.live, w->director.wave + 1,
                        PHASES[w->director.phase], w->hitstop),
             x, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    DrawText(TextFormat("chunks %d/%d   vertices %d   triangulos %d",
                        info->planet_meshes, info->planet_chunks, info->planet_vertices, info->planet_triangles),
             x, y, FONT_SIZE, TEXT_COLOR);
    y += LINE_HEIGHT;

    // Remalhamento: fica amarelo enquanto há chunks na fila.
    DrawText(TextFormat("remalha %d/quadro   fila %d   %.2f ms   particulas %d",
                        info->remeshed, info->remesh_backlog, (double)info->remesh_ms, info->particles),
             x, y, FONT_SIZE, (info->remesh_backlog > 0) ? WARN_COLOR : TEXT_COLOR);
    y += LINE_HEIGHT;

    DrawText(TextFormat("estrelas %d   luzes %d   hash %08x   %s",
                        info->star_count, info->city_light_count,
                        (unsigned int)(world_hash(w) & 0xffffffffu), info->shaded ? "com shader" : "sem shader"),
             x, y, FONT_SIZE, DIM_COLOR);
    y += LINE_HEIGHT;

    if (info->autoplay_script != NULL) {
        DrawText(TextFormat("AUTOPLAY %s   quadro %d/%d   seed %llu",
                            info->autoplay_script, info->autoplay_frame, info->autoplay_frames,
                            (unsigned long long)info->seed),
                 x, y, FONT_SIZE, WARN_COLOR);
        y += LINE_HEIGHT;
    }

    draw_graph(o, x, y + 4);
}
