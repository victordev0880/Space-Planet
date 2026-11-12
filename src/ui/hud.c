#include "raylib.h"

#include "ui/hud.h"

#include <math.h>

#include "core/vmath.h"

#define BAR_WIDTH    400
#define BAR_HEIGHT   14
#define BAR_TOP      18
#define CORE_WIDTH   230
#define CORE_HEIGHT  8
#define LABEL_SIZE   20
#define SMALL_SIZE   10
#define BANNER_SIZE  50

// O núcleo só ganha barra depois de perder pelo menos esta fração.
#define CORE_SHOW_BELOW 0.999f

// A barra pisca quando a vida chega a esta distância do ponto de colapso.
#define DANGER_MARGIN 0.12f
#define BLINK_SPEED   6.0f

static const Color FRAME_COLOR  = { 0, 0, 0, 170 };
static const Color EMPTY_COLOR  = { 40, 44, 56, 220 };
static const Color TEXT_COLOR   = { 225, 230, 240, 255 };
static const Color DIM_COLOR    = { 150, 158, 176, 255 };
static const Color HEALTHY      = { 70, 200, 120, 255 };
static const Color WOUNDED      = { 235, 200, 70, 255 };
static const Color DYING        = { 235, 80, 60, 255 };
static const Color CORE_COLOR   = { 255, 190, 90, 255 };
static const Color MARKER_COLOR = { 255, 255, 255, 230 };

static Color mix_color(Color a, Color b, float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return (Color){
        (unsigned char)Lerp((float)a.r, (float)b.r, t),
        (unsigned char)Lerp((float)a.g, (float)b.g, t),
        (unsigned char)Lerp((float)a.b, (float)b.b, t),
        255,
    };
}

// Verde com o planeta inteiro, amarelo no meio do caminho, vermelho no colapso.
// `health` vai de 0 (no ponto de colapso) a 1 (intacto).
static Color health_color(float health)
{
    if (health > 0.5f) return mix_color(WOUNDED, HEALTHY, (health - 0.5f) * 2.0f);
    return mix_color(DYING, WOUNDED, health * 2.0f);
}

static void draw_bar(int x, int y, int width, int height, float fill, Color color)
{
    DrawRectangle(x, y, width, height, EMPTY_COLOR);
    DrawRectangle(x, y, (int)((float)width * Clamp(fill, 0.0f, 1.0f)), height, color);
}

void hud_draw(const World *w, const Tuning *t, int center_x, bool show_crosshair)
{
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    float hp = planet_hp(&w->planet);
    float core = planet_core_hp(&w->planet);
    float time = (float)w->tick / (float)t->sim_hz;

    // 0 no ponto de colapso, 1 com o planeta intacto.
    float health = (hp - t->defeat_planet_hp) / (1.0f - t->defeat_planet_hp + 1e-6f);
    Color color = health_color(health);
    if (!w->collapsed && hp < t->defeat_planet_hp + DANGER_MARGIN) {
        // Perto do fim, a barra pulsa para chamar atenção.
        float blink = 0.5f + 0.5f * sinf(time * BLINK_SPEED);
        color = mix_color(color, MARKER_COLOR, blink * 0.5f);
    }

    // --- barra do planeta: rótulos em cima, barra embaixo --------------------
    int x = center_x - BAR_WIDTH / 2;
    int y = BAR_TOP;
    const char *mass = TextFormat("massa restante %.1f%%", (double)hp * 100.0);
    DrawRectangle(x - 10, y - 8, BAR_WIDTH + 20, LABEL_SIZE + BAR_HEIGHT + 34, FRAME_COLOR);
    DrawText("PLANETA", x, y, LABEL_SIZE, TEXT_COLOR);
    DrawText(mass, x + BAR_WIDTH - MeasureText(mass, LABEL_SIZE), y, LABEL_SIZE, color);

    int bar_y = y + LABEL_SIZE + 6;
    draw_bar(x, bar_y, BAR_WIDTH, BAR_HEIGHT, hp, color);

    // Marca do ponto de colapso.
    int marker_x = x + (int)((float)BAR_WIDTH * t->defeat_planet_hp);
    DrawRectangle(marker_x - 1, bar_y - 4, 2, BAR_HEIGHT + 8, MARKER_COLOR);
    DrawText("colapso", marker_x - MeasureText("colapso", SMALL_SIZE) / 2, bar_y + BAR_HEIGHT + 5, SMALL_SIZE,
             DIM_COLOR);

    // --- barra do núcleo (só depois de atingido) ----------------------------
    if (core < CORE_SHOW_BELOW) {
        int core_y = bar_y + BAR_HEIGHT + 28;
        const char *core_text = TextFormat("NÚCLEO %.0f%%", (double)core * 100.0);
        DrawRectangle(x - 10, core_y - 6, BAR_WIDTH + 20, LABEL_SIZE + 12, FRAME_COLOR);
        DrawText(core_text, x, core_y, LABEL_SIZE, CORE_COLOR);

        int core_x = x + BAR_WIDTH - CORE_WIDTH;
        int core_bar_y = core_y + (LABEL_SIZE - CORE_HEIGHT) / 2;
        draw_bar(core_x, core_bar_y, CORE_WIDTH, CORE_HEIGHT, core, CORE_COLOR);
        int core_marker = core_x + (int)((float)CORE_WIDTH * t->defeat_core_hp);
        DrawRectangle(core_marker - 1, core_bar_y - 3, 2, CORE_HEIGHT + 6, MARKER_COLOR);
    }

    // --- mira ----------------------------------------------------------------
    if (show_crosshair) {
        int cx = screen_w / 2, cy = screen_h / 2;
        DrawLine(cx - 10, cy, cx - 3, cy, MARKER_COLOR);
        DrawLine(cx + 3, cy, cx + 10, cy, MARKER_COLOR);
        DrawLine(cx, cy - 10, cx, cy - 3, MARKER_COLOR);
        DrawLine(cx, cy + 3, cx, cy + 10, MARKER_COLOR);
    }

    // --- colapso --------------------------------------------------------------
    if (w->collapsed) {
        const char *banner = "COLAPSO PLANETÁRIO";
        const char *reason = (core < t->defeat_core_hp) ? "o núcleo foi destruído" : "o planeta perdeu massa demais";
        int banner_w = MeasureText(banner, BANNER_SIZE);
        int band_y = screen_h / 2 - 70;

        DrawRectangle(0, band_y, screen_w, 140, (Color){ 20, 0, 0, 190 });
        DrawText(banner, (screen_w - banner_w) / 2, band_y + 26, BANNER_SIZE, DYING);
        DrawText(reason, (screen_w - MeasureText(reason, LABEL_SIZE)) / 2, band_y + 90, LABEL_SIZE, TEXT_COLOR);
    }
}
