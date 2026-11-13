#include "raylib.h"

#include "ui/hud.h"

#include <math.h>
#include <stdio.h>

#include "core/vmath.h"
#include "game/combat.h"

#define BAR_WIDTH    400
#define BAR_HEIGHT   14
#define BAR_TOP      18
#define CORE_WIDTH   230
#define CORE_HEIGHT  8
#define LABEL_SIZE   20
#define SMALL_SIZE   10
#define BANNER_SIZE  54

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
static const Color PRIZE_COLOR         = { 255, 214, 110, 255 };

// Cores de cada tipo de inimigo nos marcadores (as mesmas do brilho deles).
static const Color ENEMY_COLORS[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]  = { 196, 130, 255, 255 },
    [ENEMY_BOMBER] = { 255, 150, 70, 255 },
};
static const char *const ENEMY_NAMES[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]  = "Enxame",
    [ENEMY_BOMBER] = "Bombardeiro",
};
static const Color BOMB_COLOR = { 255, 70, 50, 255 };

static Color with_alpha(Color c, float alpha)
{
    c.a = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * (float)c.a);
    return c;
}

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

// Texto com sombra: legível sobre o planeta claro e sobre o espaço escuro.
static void text(const char *s, int x, int y, int size, Color color)
{
    DrawText(s, x + 2, y + 2, size, with_alpha((Color){ 0, 0, 0, 200 }, (float)color.a / 255.0f));
    DrawText(s, x, y, size, color);
}

static void text_centered(const char *s, int cx, int y, int size, Color color)
{
    text(s, cx - MeasureText(s, size) / 2, y, size, color);
}

// 12450 -> "12.450"
static const char *format_points(int64_t value)
{
    static char out[32];
    char digits[24];
    snprintf(digits, sizeof digits, "%lld", (long long)(value < 0 ? 0 : value));
    int len = 0, count = 0;
    while (digits[count] != '\0') count++;
    for (int i = 0; i < count; i++) {
        if (i > 0 && (count - i) % 3 == 0) out[len++] = '.';
        out[len++] = digits[i];
    }
    out[len] = '\0';
    return out;
}

static float seconds(const World *w, const Tuning *t)
{
    return (float)w->tick / (float)t->sim_hz;
}

static float event_age(const World *w, const Tuning *t, const GameEvent *e)
{
    return (float)(w->tick - e->tick) / (float)t->sim_hz;
}

// Projeta um ponto do mundo na tela. Devolve false se ele está atrás da câmera.
static bool project(Camera3D camera, Vector3 pos, Vector2 *out)
{
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    if (Vector3DotProduct(Vector3Subtract(pos, camera.position), forward) <= 0.0f) return false;
    *out = GetWorldToScreen(pos, camera);
    return true;
}

// ---------------------------------------------------------------------------
//  Planeta
// ---------------------------------------------------------------------------

static void draw_planet(const World *w, const Tuning *t, int center_x)
{
    float hp = planet_hp(&w->planet);
    float core = planet_core_hp(&w->planet);

    // 0 no ponto de colapso, 1 com o planeta intacto.
    float health = (hp - t->defeat_planet_hp) / (1.0f - t->defeat_planet_hp + 1e-6f);
    Color color = health_color(health);
    if (!w->collapsed && hp < t->defeat_planet_hp + DANGER_MARGIN) {
        // Perto do fim, a barra pulsa para chamar atenção.
        float blink = 0.5f + 0.5f * sinf(seconds(w, t) * BLINK_SPEED);
        color = mix_color(color, MARKER_COLOR, blink * 0.5f);
    }

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

    // Núcleo (só depois de atingido).
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
}

// ---------------------------------------------------------------------------
//  Nave
// ---------------------------------------------------------------------------
#define SHIP_BAR_WIDTH  210
#define SHIP_BAR_HEIGHT 10
#define SHIP_ROW        22
#define SHIP_LEFT       24
#define SHIP_BOTTOM     64       // distância da última barra até a borda de baixo
#define SHIP_INFO_WIDTH 164      // largura da coluna de velocidade e altitude, ao lado das barras
#define HULL_LOW        0.25f    // o casco pisca em vermelho abaixo desta fração
#define HIT_TIME        0.35f    // (mesma duração do aviso de dano em game/combat.c)

static const Color HULL_COLOR   = { 230, 232, 238, 255 };
static const Color SHIELD_COLOR = { 90, 200, 255, 255 };
static const Color ENERGY_COLOR = { 250, 205, 90, 255 };
static const Color GRAZE_COLOR  = { 120, 220, 255, 255 };
static const Color SLING_COLOR  = { 200, 165, 255, 255 };
static const Color LOCK_COLOR   = { 255, 120, 90, 255 };

static void ship_bar(int x, int y, const char *label, float value, float max, Color color)
{
    DrawText(label, x, y - 5, LABEL_SIZE, color);
    draw_bar(x + 96, y, SHIP_BAR_WIDTH, SHIP_BAR_HEIGHT, (max > 0.0f) ? value / max : 0.0f, color);
}

static void draw_reticle(int cx, int cy, Color color)
{
    DrawCircleLines(cx, cy, 9.0f, color);
    DrawLine(cx - 16, cy, cx - 6, cy, color);
    DrawLine(cx + 6, cy, cx + 16, cy, color);
    DrawLine(cx, cy - 16, cx, cy - 6, color);
    DrawLine(cx, cy + 6, cx, cy + 16, color);
}

// Um "X" em volta da mira: o tiro acertou (ou destruiu) alguma coisa.
static void draw_hit_marker(int cx, int cy, float strength, bool kill)
{
    if (strength <= 0.0f) return;
    float inner = kill ? 10.0f : 8.0f, outer = kill ? 22.0f : 15.0f;
    Color c = with_alpha(kill ? (Color){ 255, 110, 90, 255 } : WHITE, strength);
    for (int i = 0; i < 4; i++) {
        float a = PI * 0.25f + (float)i * PI * 0.5f;
        Vector2 from = { (float)cx + cosf(a) * inner, (float)cy + sinf(a) * inner };
        Vector2 to = { (float)cx + cosf(a) * outer, (float)cy + sinf(a) * outer };
        DrawLineEx(from, to, kill ? 3.0f : 2.0f, c);
    }
}

// Cantos de um quadrado em volta do alvo travado.
static void draw_brackets(Vector2 p, float half, Color c)
{
    float arm = half * 0.45f;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            Vector2 corner = { p.x + (float)sx * half, p.y + (float)sy * half };
            DrawLineEx(corner, (Vector2){ corner.x - (float)sx * arm, corner.y }, 2.0f, c);
            DrawLineEx(corner, (Vector2){ corner.x, corner.y - (float)sy * arm }, 2.0f, c);
        }
    }
}

static void draw_ship(const World *w, const Tuning *t, Camera3D camera)
{
    const Ship *s = &w->ship;
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();

    if (!s->alive) {
        const char *msg = TextFormat("NAVE DESTRUÍDA   renascendo em %.1f s", (double)fmaxf(s->respawn_left, 0.0f));
        int width = MeasureText(msg, LABEL_SIZE);
        DrawRectangle((screen_w - width) / 2 - 14, screen_h / 2 + 90, width + 28, LABEL_SIZE + 16, FRAME_COLOR);
        DrawText(msg, (screen_w - width) / 2, screen_h / 2 + 98, LABEL_SIZE, DYING);
        const char *warn = "o planeta está sem defesa";
        text_centered(warn, screen_w / 2, screen_h / 2 + 130, LABEL_SIZE, with_alpha(DIM_COLOR, 0.9f));
        return;
    }

    // Barras: casco, escudo e energia, no canto de baixo à esquerda.
    int x = SHIP_LEFT;
    int y = screen_h - SHIP_BOTTOM - SHIP_ROW * 2;
    DrawRectangle(x - 10, y - 12, 96 + SHIP_BAR_WIDTH + SHIP_INFO_WIDTH + 20, SHIP_ROW * 3 + 12, FRAME_COLOR);

    Color hull = HULL_COLOR;
    if (s->hull < t->ship_hull_max * HULL_LOW && sinf(seconds(w, t) * BLINK_SPEED * 2.0f) > 0.0f) hull = DYING;
    ship_bar(x, y, "CASCO", s->hull, t->ship_hull_max, hull);
    ship_bar(x, y + SHIP_ROW, "ESCUDO", s->shield, t->ship_shield_max, SHIELD_COLOR);
    ship_bar(x, y + SHIP_ROW * 2, "ENERGIA", s->energy, t->ship_energy_max, s->boost_spent ? DIM_COLOR : ENERGY_COLOR);

    // Velocidade e altitude, ao lado das barras.
    int info_x = x + 96 + SHIP_BAR_WIDTH + 28;
    float speed = Vector3Length(Vector3Add(s->vel, s->fall));
    DrawText(TextFormat("vel %3.0f", (double)speed), info_x, y - 5, LABEL_SIZE, TEXT_COLOR);
    DrawText(TextFormat("alt %3.0f", (double)s->altitude), info_x, y - 5 + SHIP_ROW, LABEL_SIZE, TEXT_COLOR);
    // Uma palavra para o que a nave está fazendo de especial.
    if (s->sling > 0.0f)       DrawText("ESTILINGUE", info_x, y - 5 + SHIP_ROW * 2, LABEL_SIZE, SLING_COLOR);
    else if (s->in_atmosphere) DrawText("RASANTE", info_x, y - 5 + SHIP_ROW * 2, LABEL_SIZE, GRAZE_COLOR);
    else if (s->drifting)      DrawText("DRIFT", info_x, y - 5 + SHIP_ROW * 2, LABEL_SIZE, ENERGY_COLOR);

    // Mira: onde os canhões se cruzam. Como a câmera vem atrás com atraso,
    // esse ponto nem sempre é o centro da tela.
    Vector3 aim = Vector3Add(s->pos, Vector3Scale(ship_forward(s), COMBAT_AIM_DISTANCE));
    Vector2 p;
    if (project(camera, aim, &p)) {
        int cx = (int)p.x, cy = (int)p.y;
        draw_reticle(cx, cy, MARKER_COLOR);
        if (w->kill_marker > 0.0f) draw_hit_marker(cx, cy, w->kill_marker / 0.3f, true);
        else                      draw_hit_marker(cx, cy, w->hit_marker / 0.12f, false);
    }

    // Trava da ajuda de mira: cantos em volta do alvo que puxa os tiros.
    AimAssist assist;
    if (combat_aim_assist(w, t, &assist)) {
        Vector3 target = (assist.enemy >= 0) ? w->enemies.items[assist.enemy].pos : w->shots.items[assist.bomb].pos;
        float radius = (assist.enemy >= 0) ? t->enemy[w->enemies.items[assist.enemy].type].radius : t->bomber_bomb_radius;
        Vector2 at;
        if (project(camera, target, &at)) {
            Vector2 edge;
            Vector3 side = Vector3Add(target, Vector3Scale(Vector3Normalize(Vector3CrossProduct(
                                                               Vector3Subtract(camera.target, camera.position), camera.up)),
                                                           radius));
            float half = 14.0f;
            if (project(camera, side, &edge)) half = fmaxf(fabsf(edge.x - at.x) * 1.3f, 12.0f);
            draw_brackets(at, half, LOCK_COLOR);
        }
    }
}

// Borda da tela tingida por um instante quando a nave leva dano: azul se o
// escudo segurou, vermelho se chegou no casco.
static void draw_damage_vignette(const World *w)
{
    const Ship *s = &w->ship;
    if (s->hit_flash <= 0.0f) return;
    float strength = Clamp(s->hit_flash / HIT_TIME, 0.0f, 1.0f) * (s->hit_shield ? 0.35f : 0.6f);
    Color edge = with_alpha(s->hit_shield ? (Color){ 60, 150, 255, 255 } : (Color){ 255, 40, 30, 255 }, strength);
    Color clear = { edge.r, edge.g, edge.b, 0 };
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int band_x = sw / 7, band_y = sh / 6;
    DrawRectangleGradientV(0, 0, sw, band_y, edge, clear);
    DrawRectangleGradientV(0, sh - band_y, sw, band_y, clear, edge);
    DrawRectangleGradientH(0, 0, band_x, sh, edge, clear);
    DrawRectangleGradientH(sw - band_x, 0, band_x, sh, clear, edge);
}

// ---------------------------------------------------------------------------
//  Pontos e hordas
// ---------------------------------------------------------------------------
#define SCORE_RIGHT   24
#define SCORE_TOP     14
#define SCORE_SIZE    36
#define WAVE_LEFT     24
#define BANNER_TIME   2.6f    // segundos que o aviso de horda fica na tela
#define CLEAR_TIME    2.4f
#define POPUP_TIME    1.1f    // segundos que os pontos de um abate sobem na tela
#define POPUP_RISE    46.0f

static void draw_score(const World *w, const Tuning *t, const HudContext *ctx)
{
    const Score *sc = &w->score;
    int sw = GetScreenWidth();
    int right = sw - SCORE_RIGHT;
    const char *points = format_points(sc->points);
    int width = MeasureText(points, SCORE_SIZE);
    DrawRectangle(right - 250, SCORE_TOP - 6, 260, 84, FRAME_COLOR);
    DrawText("PONTOS", right - 240, SCORE_TOP, LABEL_SIZE, DIM_COLOR);
    text(points, right - width, SCORE_TOP + 20, SCORE_SIZE, TEXT_COLOR);

    // Combo: multiplicador e o tempo que falta para ele acabar.
    if (sc->chain > 0) {
        float left = Clamp(sc->chain_left / t->score_combo_window, 0.0f, 1.0f);
        const char *mult = TextFormat("x%.2g", (double)sc->multiplier);
        Color c = mix_color(TEXT_COLOR, PRIZE_COLOR, (sc->multiplier - 1.0f) / fmaxf(t->score_combo_max - 1.0f, 1e-3f));
        DrawText(mult, right - 240, SCORE_TOP + 26, LABEL_SIZE + 6, c);
        DrawRectangle(right - 240, SCORE_TOP + 60, (int)(80.0f * left), 4, c);
    }
    const char *best = TextFormat("recorde %s", format_points(ctx->best_score > sc->points ? ctx->best_score : sc->points));
    DrawText(best, right - MeasureText(best, SMALL_SIZE + 4), SCORE_TOP + 62, SMALL_SIZE + 4, DIM_COLOR);
}

static int enemies_of(const World *w, EnemyType type)
{
    return w->enemies.alive_by_type[type];
}

static void draw_wave_info(const World *w, const Tuning *t)
{
    const WaveDirector *d = &w->director;
    if (d->phase == DIRECTOR_OFF || w->plan.count == 0) return;
    int x = WAVE_LEFT, y = SCORE_TOP;
    DrawRectangle(x - 10, y - 6, 250, 62, FRAME_COLOR);

    int shown = (d->phase == DIRECTOR_DONE) ? w->plan.count : d->wave + 1;
    DrawText("HORDA", x, y, LABEL_SIZE, DIM_COLOR);
    DrawText(TextFormat("%d / %d", shown, w->plan.count), x + 80, y - 2, LABEL_SIZE + 4, TEXT_COLOR);

    if (d->phase == DIRECTOR_PAUSE) {
        DrawText(TextFormat("chega em %.0f s", (double)ceilf(d->timer)), x, y + 28, LABEL_SIZE, PRIZE_COLOR);
    } else if (d->phase == DIRECTOR_ACTIVE) {
        int line_x = x;
        for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
            int count = enemies_of(w, (EnemyType)type);
            if (count == 0) continue;
            const char *item = TextFormat("%s %d", ENEMY_NAMES[type], count);
            DrawText(item, line_x, y + 28, LABEL_SIZE - 2, ENEMY_COLORS[type]);
            line_x += MeasureText(item, LABEL_SIZE - 2) + 16;
        }
        if (line_x == x) DrawText("chegando...", x, y + 28, LABEL_SIZE - 2, DIM_COLOR);
    }
    (void)t;
}

// Avisos grandes no meio da tela: começo e fim de cada horda.
static void draw_banners(const World *w, const Tuning *t)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) break;
        float age = event_age(w, t, e);
        if (age > BANNER_TIME) break;   // os eventos estão em ordem: daqui para trás, todos são mais velhos
        if (e->type != EV_WAVE_START && e->type != EV_WAVE_CLEAR) continue;

        float total = (e->type == EV_WAVE_START) ? BANNER_TIME : CLEAR_TIME;
        float fade = fminf(fminf(age / 0.25f, (total - age) / 0.5f), 1.0f);
        if (fade <= 0.0f) continue;
        int y = sh / 2 - 130;

        if (e->type == EV_WAVE_START) {
            int number = (int)e->value;
            DrawRectangle(0, y - 14, sw, BANNER_SIZE + 64, with_alpha((Color){ 10, 4, 18, 255 }, fade * 0.55f));
            text_centered(TextFormat("HORDA %d", number), sw / 2, y, BANNER_SIZE, with_alpha(TEXT_COLOR, fade));
            if (number >= 1 && number <= w->plan.count) {
                const WaveDef *wave = &w->plan.waves[number - 1];
                char line[96] = "";
                int len = 0;
                for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
                    int count = wave_enemy_count(wave, (EnemyType)type);
                    if (count == 0) continue;
                    len += snprintf(line + len, sizeof line - (size_t)len, "%s%s x%d", len > 0 ? "   " : "",
                                    ENEMY_NAMES[type], count);
                }
                text_centered(line, sw / 2, y + BANNER_SIZE + 8, LABEL_SIZE, with_alpha(PRIZE_COLOR, fade));
            }
        } else {
            text_centered("HORDA VENCIDA", sw / 2, y, BANNER_SIZE - 10, with_alpha(HEALTHY, fade));
            if (e->value > 0.0f) {
                text_centered(TextFormat("+%s  planeta intacto", format_points((int64_t)e->value)), sw / 2,
                              y + BANNER_SIZE, LABEL_SIZE, with_alpha(PRIZE_COLOR, fade));
            }
        }
        break;   // um aviso de cada vez
    }
}

// Pontos ganhos subindo de onde cada inimigo explodiu.
static void draw_popups(const World *w, const Tuning *t, Camera3D camera)
{
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) break;
        float age = event_age(w, t, e);
        if (age > POPUP_TIME) break;
        if ((e->type != EV_KILL && e->type != EV_BOMB_DOWN) || e->value <= 0.0f) continue;
        Vector2 p;
        if (!project(camera, e->pos, &p)) continue;
        float life = age / POPUP_TIME;
        Color c = (e->type == EV_BOMB_DOWN) ? BOMB_COLOR : (e->sub == ENEMY_BOMBER ? PRIZE_COLOR : TEXT_COLOR);
        int size = (e->sub == ENEMY_BOMBER && e->type == EV_KILL) ? 30 : 20;
        text_centered(TextFormat("+%s", format_points((int64_t)e->value)), (int)p.x, (int)(p.y - 24.0f - POPUP_RISE * life),
                      size, with_alpha(c, 1.0f - life * life));
    }
}

// ---------------------------------------------------------------------------
//  Marcadores de inimigos
// ---------------------------------------------------------------------------
#define EDGE_MARGIN  46.0f   // as setas ficam a esta distância das bordas dos lados
#define EDGE_TOP     112.0f  // ... e abaixo dos painéis do topo
#define EDGE_BOTTOM  70.0f
#define ARROW_SIZE   12.0f

// Seta na borda da tela apontando para algo fora dela.
static void edge_arrow(Camera3D camera, Vector3 pos, float size, Color color)
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    Vector3 to = Vector3Subtract(pos, camera.position);
    float x = Vector3DotProduct(to, right), y = -Vector3DotProduct(to, up);   // na tela, y cresce para baixo
    if (fabsf(x) < 1e-4f && fabsf(y) < 1e-4f) y = 1.0f;
    float angle = atan2f(y, x);

    // O ponto da borda (de um retângulo recuado, que não cobre os painéis)
    // na direção do alvo.
    float half_w = sw * 0.5f - EDGE_MARGIN, half_h = (sh - EDGE_TOP - EDGE_BOTTOM) * 0.5f;
    float center_y = EDGE_TOP + half_h;
    float c = cosf(angle), s = sinf(angle);
    float scale = fminf(half_w / fmaxf(fabsf(c), 1e-4f), half_h / fmaxf(fabsf(s), 1e-4f));
    Vector2 tip = { sw * 0.5f + c * scale, center_y + s * scale };

    Vector2 dir = { c, s }, side = { -s, c };
    Vector2 a = { tip.x + dir.x * size, tip.y + dir.y * size };
    Vector2 b = { tip.x - dir.x * size * 0.6f + side.x * size * 0.8f, tip.y - dir.y * size * 0.6f + side.y * size * 0.8f };
    Vector2 d = { tip.x - dir.x * size * 0.6f - side.x * size * 0.8f, tip.y - dir.y * size * 0.6f - side.y * size * 0.8f };
    DrawTriangle(a, d, b, color);
    DrawTriangleLines(a, d, b, with_alpha((Color){ 0, 0, 0, 255 }, 0.6f));
}

// Losango sobre um inimigo distante, para ele não sumir contra o planeta.
static void diamond(Vector2 p, float size, Color color)
{
    Vector2 top = { p.x, p.y - size }, right = { p.x + size, p.y };
    Vector2 bottom = { p.x, p.y + size }, left = { p.x - size, p.y };
    DrawLineEx(top, right, 2.0f, color);
    DrawLineEx(right, bottom, 2.0f, color);
    DrawLineEx(bottom, left, 2.0f, color);
    DrawLineEx(left, top, 2.0f, color);
}

// Visível na tela (com uma pequena folga nas bordas).
static bool on_screen(Vector2 p)
{
    const float slack = 16.0f;
    return p.x >= slack && p.y >= slack && p.x <= (float)GetScreenWidth() - slack
        && p.y <= (float)GetScreenHeight() - slack;
}

static void draw_markers(const World *w, const Tuning *t, Camera3D camera)
{
    float now = seconds(w, t);
    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        bool arming = en->arming > 0.0f;
        Color color = ENEMY_COLORS[en->type];
        if (arming) color = mix_color(color, BOMB_COLOR, 0.5f + 0.5f * sinf(now * 14.0f));
        float big = (en->type == ENEMY_BOMBER) ? 1.5f : 1.0f;

        Vector2 p;
        if (project(camera, en->pos, &p) && on_screen(p)) {
            float distance = Vector3Distance(camera.position, en->pos);
            if (en->type == ENEMY_BOMBER) {
                // Vida do Bombardeiro, embaixo dele.
                float fill = en->hp / t->enemy[ENEMY_BOMBER].hp;
                draw_bar((int)p.x - 30, (int)p.y + 26, 60, 5, fill, color);
                if (arming) text_centered("BOMBA", (int)p.x, (int)p.y - 66, 18, BOMB_COLOR);
            }
            if (distance > t->combat_marker_distance) diamond((Vector2){ p.x, p.y - 18.0f * big }, 5.0f * big, color);
        } else {
            edge_arrow(camera, en->pos, ARROW_SIZE * big, with_alpha(color, 0.9f));
        }
    }

    // Bombas caindo fora da tela também ganham seta: é preciso correr.
    const Projectiles *s = &w->shots;
    for (int i = 0; i < s->pool.capacity; i++) {
        if (!pool_alive(&s->pool, i) || s->items[i].kind != SHOT_BOMB) continue;
        Vector2 p;
        if (project(camera, s->items[i].pos, &p) && on_screen(p)) continue;
        edge_arrow(camera, s->items[i].pos, ARROW_SIZE * 1.3f, with_alpha(BOMB_COLOR, 0.6f + 0.4f * sinf(now * 16.0f)));
    }
}

// ---------------------------------------------------------------------------
//  Fim da partida
// ---------------------------------------------------------------------------
#define END_DELAY 1.2f   // segundos entre o fim e o painel (para ver a explosão final)

static void draw_end(const World *w, const Tuning *t, const HudContext *ctx)
{
    if (w->state == GAME_PLAYING) return;
    float since = (float)(w->tick - w->state_tick) / (float)t->sim_hz;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    bool won = (w->state == GAME_WON);

    // Primeiro só o título, sobre uma faixa; depois o resumo.
    float band = Clamp(since / 0.4f, 0.0f, 1.0f);
    Color tint = won ? (Color){ 4, 20, 14, 255 } : (Color){ 24, 2, 2, 255 };
    DrawRectangle(0, sh / 2 - 210, sw, 420, with_alpha(tint, band * 0.78f));
    const char *title = won ? "PLANETA DEFENDIDO" : "COLAPSO PLANETÁRIO";
    text_centered(title, sw / 2, sh / 2 - 190, BANNER_SIZE, with_alpha(won ? HEALTHY : DYING, band));
    if (!won) {
        const char *why = (planet_core_hp(&w->planet) < t->defeat_core_hp) ? "o núcleo foi destruído"
                                                                            : "o planeta perdeu massa demais";
        text_centered(why, sw / 2, sh / 2 - 126, LABEL_SIZE, with_alpha(TEXT_COLOR, band));
    }

    float panel = Clamp((since - END_DELAY) / 0.5f, 0.0f, 1.0f);
    if (panel <= 0.0f) return;
    const Score *sc = &w->score;
    int y = sh / 2 - 86;
    text_centered(format_points(sc->points), sw / 2, y, 60, with_alpha(TEXT_COLOR, panel));
    y += 66;
    if (ctx->new_best) {
        float pulse = 0.75f + 0.25f * sinf(since * 6.0f);
        text_centered("NOVO RECORDE!", sw / 2, y, LABEL_SIZE + 4, with_alpha(PRIZE_COLOR, panel * pulse));
    } else {
        text_centered(TextFormat("recorde %s", format_points(ctx->best_score)), sw / 2, y, LABEL_SIZE,
                      with_alpha(DIM_COLOR, panel));
    }
    y += 40;

    int reached = won ? w->plan.count : w->director.wave + 1;
    const char *lines[4];
    lines[0] = TextFormat("planeta salvo   %.1f%%", (double)planet_hp(&w->planet) * 100.0);
    lines[1] = TextFormat("horda   %d de %d", reached, w->plan.count);
    lines[2] = TextFormat("abates   %d Enxame   %d Bombardeiro   %d bombas", sc->kills_by_type[ENEMY_SWARM],
                          sc->kills_by_type[ENEMY_BOMBER], sc->bombs_down);
    lines[3] = TextFormat("maior combo   %d   bônus de planeta   %s", sc->best_chain, format_points(sc->planet_bonus));
    for (int i = 0; i < 4; i++) {
        text_centered(lines[i], sw / 2, y, LABEL_SIZE, with_alpha(TEXT_COLOR, panel));
        y += 28;
    }
    float blink = 0.6f + 0.4f * sinf(since * 4.0f);
    text_centered("R para jogar de novo", sw / 2, y + 16, LABEL_SIZE + 2, with_alpha(PRIZE_COLOR, panel * blink));
}

// ---------------------------------------------------------------------------

void hud_draw(const World *w, const Tuning *t, const HudContext *ctx)
{
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    bool playing = (w->cam_mode == CAM_SHIP && w->ship_active);

    if (playing) {
        draw_damage_vignette(w);
        draw_markers(w, t, ctx->camera);
        draw_popups(w, t, ctx->camera);
    }
    draw_planet(w, t, ctx->center_x);
    if (w->director.phase != DIRECTOR_OFF) {
        draw_score(w, t, ctx);
        draw_wave_info(w, t);
    }

    if (playing) {
        draw_ship(w, t, ctx->camera);
        if (w->state == GAME_PLAYING) draw_banners(w, t);   // no fim, quem fala é o painel
    } else if (w->cam_mode == CAM_FREE) {
        int cx = screen_w / 2, cy = screen_h / 2;
        DrawLine(cx - 10, cy, cx - 3, cy, MARKER_COLOR);
        DrawLine(cx + 3, cy, cx + 10, cy, MARKER_COLOR);
        DrawLine(cx, cy - 10, cx, cy - 3, MARKER_COLOR);
        DrawLine(cx, cy + 3, cx, cy + 10, MARKER_COLOR);
        if (w->ship_active) text_centered("PAUSA (F2 volta ao jogo)", screen_w / 2, 120, LABEL_SIZE, PRIZE_COLOR);
    }

    // Fim da partida (ou o colapso, nos roteiros sem hordas).
    if (w->director.phase != DIRECTOR_OFF) {
        draw_end(w, t, ctx);
    } else if (w->collapsed) {
        const char *banner = "COLAPSO PLANETÁRIO";
        const char *reason = (planet_core_hp(&w->planet) < t->defeat_core_hp) ? "o núcleo foi destruído"
                                                                              : "o planeta perdeu massa demais";
        int band_y = screen_h / 2 - 70;
        DrawRectangle(0, band_y, screen_w, 140, (Color){ 20, 0, 0, 190 });
        text_centered(banner, screen_w / 2, band_y + 26, BANNER_SIZE - 4, DYING);
        text_centered(reason, screen_w / 2, band_y + 90, LABEL_SIZE, TEXT_COLOR);
    }
}
