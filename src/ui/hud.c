#include "raylib.h"

#include "ui/hud.h"

#include <math.h>
#include <stdio.h>

#include "core/vmath.h"
#include "game/combat.h"
#include "ui/font.h"

#define BAR_WIDTH    400
#define BAR_HEIGHT   14
#define BAR_TOP      18
#define CORE_WIDTH   230
#define CORE_HEIGHT  8
#define LABEL_SIZE   20
#define SMALL_SIZE   10
#define BANNER_SIZE  54

// Faixa do topo ocupada pelos painéis (horda, planeta, pontos) e pelos
// alertas: marcadores, etiquetas e pontos que sobem não entram nela.
#define PANELS_BOTTOM 112.0f

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
static const Color BOSS_COLOR          = { 255, 84, 60, 255 };
static const Color ABILITY_COLOR       = { 120, 220, 255, 255 };
static const Color CAPACITOR_COLOR     = { 255, 210, 80, 255 };
static const Color CORE_OPEN_COLOR     = { 255, 110, 230, 255 };
static const Color TRACTOR_COLOR       = { 110, 255, 210, 255 };
static const Color METEOR_COLOR        = { 255, 150, 70, 255 };
static const Color FRAGMENT_COLOR      = { 210, 196, 180, 255 };
static const Color REPAIR_COLOR        = { 120, 255, 170, 255 };
static const Color ORE_COLOR           = { 255, 206, 90, 255 };

// Cores de cada tipo de inimigo nos marcadores (as do brilho da variação
// principal deles, um pouco mais claras para ler sobre o planeta).
static const Color ENEMY_COLORS[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = { 196, 130, 255, 255 },
    [ENEMY_BOMBER]   = { 255, 150, 70, 255 },
    [ENEMY_KAMIKAZE] = { 255, 96, 72, 255 },
    [ENEMY_DIGGER]   = { 255, 186, 80, 255 },
    [ENEMY_SIPHON]   = { 120, 255, 190, 255 },
    [ENEMY_SNIPER]   = { 255, 110, 140, 255 },
    [ENEMY_SHIELDER] = { 110, 230, 255, 255 },
    [ENEMY_CARRIER]  = { 214, 150, 255, 255 },
};
static const char *const ENEMY_NAMES[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = "Enxame",
    [ENEMY_BOMBER]   = "Bombardeiro",
    [ENEMY_KAMIKAZE] = "Kamikaze",
    [ENEMY_DIGGER]   = "Escavador",
    [ENEMY_SIPHON]   = "Sifão",
    [ENEMY_SNIPER]   = "Atirador",
    [ENEMY_SHIELDER] = "Escudeiro",
    [ENEMY_CARRIER]  = "Porta-drones",
};
static const Color BOMB_COLOR = { 255, 70, 50, 255 };

static void text(const char *s, int x, int y, int size, Color color);
static Color with_alpha(Color c, float alpha);

static bool gamepad_hints = false;

void hud_set_gamepad(bool on) { gamepad_hints = on; }
bool hud_gamepad(void) { return gamepad_hints; }

const char *hud_keys(const char *keyboard, const char *gamepad)
{
    return gamepad_hints ? gamepad : keyboard;
}

void hud_dpad_arrow(int x, int y, int size, int dir, Color color)
{
    float s = (float)size, fx = (float)x, fy = (float)y;
    Vector2 a, b, c;
    if (dir == 0) {        // esquerda
        a = (Vector2){ fx + s * 0.2f, fy + s * 0.5f };
        b = (Vector2){ fx + s * 0.85f, fy + s * 0.85f };
        c = (Vector2){ fx + s * 0.85f, fy + s * 0.15f };
    } else if (dir == 1) { // cima
        a = (Vector2){ fx + s * 0.5f, fy + s * 0.15f };
        b = (Vector2){ fx + s * 0.15f, fy + s * 0.8f };
        c = (Vector2){ fx + s * 0.85f, fy + s * 0.8f };
    } else {               // direita
        a = (Vector2){ fx + s * 0.8f, fy + s * 0.5f };
        b = (Vector2){ fx + s * 0.15f, fy + s * 0.15f };
        c = (Vector2){ fx + s * 0.15f, fy + s * 0.85f };
    }
    DrawTriangle(a, b, c, color);   // vértices no sentido anti-horário
}

Color hud_enemy_color(EnemyType type)
{
    return ((int)type >= 0 && type < ENEMY_TYPE_COUNT) ? ENEMY_COLORS[type] : WHITE;
}

const char *hud_enemy_name(EnemyType type)
{
    return ((int)type >= 0 && type < ENEMY_TYPE_COUNT) ? ENEMY_NAMES[type] : "";
}

void hud_type_list(const int counts[ENEMY_TYPE_COUNT], bool count_first, int cx, int y, int size, float alpha)
{
    char items[ENEMY_TYPE_COUNT][32];
    int types[ENEMY_TYPE_COUNT], n = 0, width = 0;
    const int gap = size + 2;
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
        if (counts[type] <= 0) continue;
        if (count_first) snprintf(items[n], sizeof items[n], "%d %s", counts[type], ENEMY_NAMES[type]);
        else             snprintf(items[n], sizeof items[n], "%s x%d", ENEMY_NAMES[type], counts[type]);
        types[n] = type;
        width += ui_measure_text(items[n], size) + (n > 0 ? gap : 0);
        n++;
    }
    int x = cx - width / 2;
    for (int k = 0; k < n; k++) {
        text(items[k], x, y, size, with_alpha(ENEMY_COLORS[types[k]], alpha));
        x += ui_measure_text(items[k], size) + gap;
    }
}

// Os grandões ganham marcador maior e barra de vida.
static bool is_big(EnemyType type)
{
    return type == ENEMY_BOMBER || type == ENEMY_CARRIER;
}

static bool has_health_bar(const Tuning *t, EnemyType type)
{
    return t->enemy[type].hp >= 12.0f;
}

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
    ui_draw_text(s, x + 2, y + 2, size, with_alpha((Color){ 0, 0, 0, 200 }, (float)color.a / 255.0f));
    ui_draw_text(s, x, y, size, color);
}

static void text_centered(const char *s, int cx, int y, int size, Color color)
{
    text(s, cx - ui_measure_text(s, size) / 2, y, size, color);
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

// Habitantes, a partir de bilhões: "7,48 bilhões", "1 bilhão", "640 milhões".
// (A vida do planeta é o volume que resta; os habitantes são só o nome dela.)
static const char *format_population(float billions)
{
    static char out[32];
    if (billions < 0.0f) billions = 0.0f;
    if (billions >= 1.995f) {
        snprintf(out, sizeof out, "%.2f bilhões", (double)billions);
    } else if (billions >= 0.995f) {
        snprintf(out, sizeof out, "%.2f bilhão", (double)billions);
    } else {
        snprintf(out, sizeof out, "%.0f milhões", (double)(billions * 1000.0f));
    }
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '.') *c = ',';   // vírgula decimal, como se escreve em português
    }
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
    const char *people = TextFormat("%s de habitantes", format_population(hp * t->planet_population));
    DrawRectangle(x - 10, y - 8, BAR_WIDTH + 20, LABEL_SIZE + BAR_HEIGHT + 34, FRAME_COLOR);
    ui_draw_text("PLANETA", x, y, LABEL_SIZE, TEXT_COLOR);
    ui_draw_text(people, x + BAR_WIDTH - ui_measure_text(people, LABEL_SIZE), y, LABEL_SIZE, color);

    int bar_y = y + LABEL_SIZE + 6;
    draw_bar(x, bar_y, BAR_WIDTH, BAR_HEIGHT, hp, color);

    // Marca do ponto de extinção.
    int marker_x = x + (int)((float)BAR_WIDTH * t->defeat_planet_hp);
    DrawRectangle(marker_x - 1, bar_y - 4, 2, BAR_HEIGHT + 8, MARKER_COLOR);
    ui_draw_text("extinção", marker_x - ui_measure_text("extinção", SMALL_SIZE) / 2, bar_y + BAR_HEIGHT + 5, SMALL_SIZE,
             DIM_COLOR);

    // Núcleo (só depois de atingido).
    if (core < CORE_SHOW_BELOW) {
        int core_y = bar_y + BAR_HEIGHT + 28;
        const char *core_text = TextFormat("NÚCLEO %.0f%%", (double)core * 100.0);
        DrawRectangle(x - 10, core_y - 6, BAR_WIDTH + 20, LABEL_SIZE + 12, FRAME_COLOR);
        ui_draw_text(core_text, x, core_y, LABEL_SIZE, CORE_COLOR);
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
#define LEAD_ANGLE      25.0f    // mostra onde mirar no inimigo mais perto do nariz, até este ângulo

static const Color HULL_COLOR   = { 230, 232, 238, 255 };
static const Color SHIELD_COLOR = { 90, 200, 255, 255 };
static const Color ENERGY_COLOR = { 250, 205, 90, 255 };
static const Color GRAZE_COLOR  = { 120, 220, 255, 255 };
static const Color SLING_COLOR  = { 200, 165, 255, 255 };
static const Color LOCK_COLOR   = { 255, 120, 90, 255 };
static const Color XP_COLOR     = { 255, 214, 110, 255 };
#define LEVEL_FLASH 0.8f   // segundos de brilho na barra de nível depois de subir

static void ship_bar(int x, int y, const char *label, float value, float max, Color color)
{
    ui_draw_text(label, x, y - 5, LABEL_SIZE, color);
    draw_bar(x + 96, y, SHIP_BAR_WIDTH, SHIP_BAR_HEIGHT, (max > 0.0f) ? value / max : 0.0f, color);
}

// Nível e XP (com um brilho logo depois de subir), e ao lado um quadradinho
// por nível de evolução, na cor da linha de cada uma.
static void draw_level(const World *w, const Tuning *t, int x, int y, int pips_x)
{
    const Evolution *e = &w->evo;
    float need = evolution_xp_needed(t, e->level);
    float since_up = 99.0f;
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *ev = events_find(&w->events, id);
        if (ev == NULL) break;
        float age = event_age(w, t, ev);
        if (age > LEVEL_FLASH) break;
        if (ev->type == EV_LEVEL_UP) {
            since_up = age;
            break;
        }
    }
    Color c = (since_up < LEVEL_FLASH) ? mix_color(XP_COLOR, WHITE, 1.0f - since_up / LEVEL_FLASH) : XP_COLOR;
    ship_bar(x, y, TextFormat("NÍVEL %d", e->level), e->xp, need, c);

    int px = pips_x;
    const int pip = 9, gap = 3;
    for (int i = 0; i < EVO_COUNT; i++) {
        Color line = ENEMY_COLORS[evolution_info((EvolutionId)i)->line];
        for (int r = 0; r < e->rank[i] && px + pip <= pips_x + SHIP_INFO_WIDTH - 10; r++) {
            DrawRectangle(px, y, pip, pip, line);
            px += pip + gap;
        }
    }
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
        int width = ui_measure_text(msg, LABEL_SIZE);
        DrawRectangle((screen_w - width) / 2 - 14, screen_h / 2 + 90, width + 28, LABEL_SIZE + 16, FRAME_COLOR);
        ui_draw_text(msg, (screen_w - width) / 2, screen_h / 2 + 98, LABEL_SIZE, DYING);
        const char *warn = "o planeta está sem defesa";
        text_centered(warn, screen_w / 2, screen_h / 2 + 130, LABEL_SIZE, with_alpha(DIM_COLOR, 0.9f));
        return;
    }

    // Barras: casco, escudo, energia e nível, no canto de baixo à esquerda.
    int x = SHIP_LEFT;
    int y = screen_h - SHIP_BOTTOM - SHIP_ROW * 3;
    DrawRectangle(x - 10, y - 12, 96 + SHIP_BAR_WIDTH + SHIP_INFO_WIDTH + 20, SHIP_ROW * 4 + 12, FRAME_COLOR);

    Color hull = HULL_COLOR;
    if (s->hull < t->ship_hull_max * HULL_LOW && sinf(seconds(w, t) * BLINK_SPEED * 2.0f) > 0.0f) hull = DYING;
    ship_bar(x, y, "CASCO", s->hull, t->ship_hull_max, hull);
    ship_bar(x, y + SHIP_ROW, "ESCUDO", s->shield, t->ship_shield_max, SHIELD_COLOR);
    ship_bar(x, y + SHIP_ROW * 2, "ENERGIA", s->energy, t->ship_energy_max, s->boost_spent ? DIM_COLOR : ENERGY_COLOR);
    draw_level(w, t, x, y + SHIP_ROW * 3, x + 96 + SHIP_BAR_WIDTH + 28);

    // Núcleo Rompe-Mundos: pronto, carregando ou em recarga, logo acima do painel.
    const Ability *ab = &w->ability;
    if (ab->owned) {
        int ay = y - 44;
        DrawRectangle(x - 10, ay - 6, 96 + SHIP_BAR_WIDTH + 20, 28, FRAME_COLOR);
        ui_draw_text("RAIO", x, ay, LABEL_SIZE - 2, ABILITY_COLOR);
        float ready = 1.0f - ab->cooldown / fmaxf(t->ability_cooldown, 1e-3f);
        Color c = (ab->cooldown <= 0.0f) ? ABILITY_COLOR : with_alpha(ABILITY_COLOR, 0.45f);
        draw_bar(x + 96, ay + 4, SHIP_BAR_WIDTH, 8, (ab->charge > 0.0f) ? ab->charge / t->ability_charge : ready, c);
        if (ab->cooldown <= 0.0f && ab->charge <= 0.0f) {
            ui_draw_text(hud_keys("botão direito", "LT"), x + 96 + SHIP_BAR_WIDTH - ui_measure_text(hud_keys("botão direito", "LT"), SMALL_SIZE + 2), ay - 12,
                     SMALL_SIZE + 2, DIM_COLOR);
        }
    }

    // Velocidade e altitude, ao lado das barras.
    int info_x = x + 96 + SHIP_BAR_WIDTH + 28;
    float speed = Vector3Length(Vector3Add(s->vel, s->fall));
    ui_draw_text(TextFormat("vel %3.0f", (double)speed), info_x, y - 5, LABEL_SIZE, TEXT_COLOR);
    ui_draw_text(TextFormat("alt %3.0f", (double)s->altitude), info_x, y - 5 + SHIP_ROW, LABEL_SIZE, TEXT_COLOR);
    // Uma palavra para o que a nave está fazendo de especial.
    int status_y = y - 5 + SHIP_ROW * 2;
    if (s->sling > 0.0f)       ui_draw_text("ESTILINGUE", info_x, status_y, LABEL_SIZE, SLING_COLOR);
    else if (s->drifting)      ui_draw_text("DRIFT", info_x, status_y, LABEL_SIZE, ENERGY_COLOR);
    else if (s->reversing)     ui_draw_text("RÉ", info_x, status_y, LABEL_SIZE, HULL_COLOR);
    else if (s->hover > 0.6f)  ui_draw_text("PAIRANDO", info_x, status_y, LABEL_SIZE, DIM_COLOR);
    else if (s->in_atmosphere) ui_draw_text("RASANTE", info_x, status_y, LABEL_SIZE, GRAZE_COLOR);

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

    // Antecipação: onde mirar no inimigo mais perto do nariz. Ele se move e o
    // tiro leva um tempo para chegar; com a mira em cima do marcador, acerta.
    int lead = combat_lead_target(w, t, LEAD_ANGLE);
    if (lead >= 0) {
        const Enemy *en = &w->enemies.items[lead];
        Vector3 point = combat_lead_point(w, t, en->pos, en->vel);
        Vector2 at_enemy, at_lead;
        if (project(camera, point, &at_lead) && project(camera, en->pos, &at_enemy)) {
            Color c = ENEMY_COLORS[en->type];
            if (Vector2Distance(at_enemy, at_lead) > 8.0f) DrawLineEx(at_enemy, at_lead, 1.5f, with_alpha(c, 0.45f));
            DrawCircleLines((int)at_lead.x, (int)at_lead.y, 7.0f, with_alpha(c, 0.95f));
            DrawCircle((int)at_lead.x, (int)at_lead.y, 2.5f, with_alpha(c, 0.95f));
        }
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
    int width = ui_measure_text(points, SCORE_SIZE);
    DrawRectangle(right - 250, SCORE_TOP - 6, 260, 84, FRAME_COLOR);
    ui_draw_text("PONTOS", right - 240, SCORE_TOP, LABEL_SIZE, DIM_COLOR);
    text(points, right - width, SCORE_TOP + 20, SCORE_SIZE, TEXT_COLOR);

    // Combo: multiplicador e o tempo que falta para ele acabar.
    if (sc->chain > 0) {
        float left = Clamp(sc->chain_left / t->score_combo_window, 0.0f, 1.0f);
        const char *mult = TextFormat("x%.2g", (double)sc->multiplier);
        Color c = mix_color(TEXT_COLOR, PRIZE_COLOR, (sc->multiplier - 1.0f) / fmaxf(t->score_combo_max - 1.0f, 1e-3f));
        ui_draw_text(mult, right - 240, SCORE_TOP + 26, LABEL_SIZE + 6, c);
        DrawRectangle(right - 240, SCORE_TOP + 60, (int)(80.0f * left), 4, c);
    }
    const char *best = TextFormat("recorde %s", format_points(ctx->best_score > sc->points ? ctx->best_score : sc->points));
    ui_draw_text(best, right - ui_measure_text(best, SMALL_SIZE + 4), SCORE_TOP + 62, SMALL_SIZE + 4, DIM_COLOR);
}

static int enemies_of(const World *w, EnemyType type)
{
    return w->enemies.alive_by_type[type];
}

#define WAVE_PANEL_WIDTH 330   // largura do painel da horda (os nomes quebram de linha)
#define WAVE_ITEM_SIZE   16
#define WAVE_LINE        20

static void draw_wave_info(const World *w, const Tuning *t)
{
    const WaveDirector *d = &w->director;
    if (d->phase == DIRECTOR_OFF || w->plan.count == 0) return;
    int x = WAVE_LEFT, y = SCORE_TOP;
    int shown = (d->phase == DIRECTOR_DONE) ? w->plan.count : d->wave + 1;

    // Primeiro mede quantas linhas os inimigos vivos ocupam, para o painel
    // crescer junto.
    int lines = 1, line_x = 0;
    bool boss = boss_active(&w->boss);
    const char *boss_item = boss ? boss_name((BossType)w->boss.type) : NULL;
    if (d->phase == DIRECTOR_ACTIVE) {
        if (boss) line_x = ui_measure_text(boss_item, WAVE_ITEM_SIZE) + 14;
        for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
            int count = enemies_of(w, (EnemyType)type);
            if (count == 0) continue;
            int width = ui_measure_text(TextFormat("%s %d", ENEMY_NAMES[type], count), WAVE_ITEM_SIZE);
            if (line_x > 0 && line_x + width > WAVE_PANEL_WIDTH - 20) {
                lines++;
                line_x = 0;
            }
            line_x += width + 14;
        }
    } else if (d->phase == DIRECTOR_PAUSE) {
        lines = 2;
    }
    DrawRectangle(x - 10, y - 6, WAVE_PANEL_WIDTH, 40 + lines * WAVE_LINE + 4, FRAME_COLOR);
    ui_draw_text("HORDA", x, y, LABEL_SIZE, DIM_COLOR);
    ui_draw_text(TextFormat("%d / %d", shown, w->plan.count), x + 80, y - 2, LABEL_SIZE + 4, TEXT_COLOR);

    int row_y = y + 30;
    if (d->phase == DIRECTOR_PAUSE) {
        ui_draw_text(TextFormat("próxima horda em %.0f s", (double)ceilf(d->timer)), x, row_y, LABEL_SIZE - 2, PRIZE_COLOR);
        // A calmaria pode ser encurtada: quem já está pronto chama a horda.
        float blink = 0.65f + 0.35f * sinf(seconds(w, t) * 3.0f);
        ui_draw_text(hud_keys("Enter: chamar agora", "Y: chamar agora"), x, row_y + WAVE_LINE + 2, WAVE_ITEM_SIZE, with_alpha(DIM_COLOR, blink));
    } else if (d->phase == DIRECTOR_ACTIVE) {
        line_x = x;
        bool any = false;
        if (boss) {
            ui_draw_text(boss_item, line_x, row_y, WAVE_ITEM_SIZE, BOSS_COLOR);
            line_x += ui_measure_text(boss_item, WAVE_ITEM_SIZE) + 14;
            any = true;
        }
        for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
            int count = enemies_of(w, (EnemyType)type);
            if (count == 0) continue;
            const char *item = TextFormat("%s %d", ENEMY_NAMES[type], count);
            int width = ui_measure_text(item, WAVE_ITEM_SIZE);
            if (line_x > x && line_x + width > x + WAVE_PANEL_WIDTH - 20) {
                row_y += WAVE_LINE;
                line_x = x;
            }
            ui_draw_text(item, line_x, row_y, WAVE_ITEM_SIZE, ENEMY_COLORS[type]);
            line_x += width + 14;
            any = true;
        }
        if (!any) ui_draw_text("chegando...", x, row_y, WAVE_ITEM_SIZE, DIM_COLOR);
    }
}

// Avisos de ameaça logo abaixo da barra do planeta: o que está ferindo o
// planeta (ou mirando na nave) agora, para o jogador decidir o que caçar.
#define ALERT_SIZE 18

// Logo abaixo do painel do planeta (que cresce quando o núcleo aparece) vem a
// barra do boss, se houver um, e depois os avisos.
#define BOSS_PANEL_HEIGHT 58
#define METEOR_NOTE_TIME  6.0f   // segundos do aviso de meteoro novo

static bool boss_bar_shown(const World *w)
{
    return boss_active(&w->boss) && w->boss.state != BOSS_DYING;
}

static int below_planet(const World *w)
{
    int y = BAR_TOP + LABEL_SIZE + BAR_HEIGHT + 40;
    if (planet_core_hp(&w->planet) < CORE_SHOW_BELOW) y += LABEL_SIZE + 22;
    return y;
}

static void draw_alerts(const World *w, const Tuning *t, int center_x)
{
    const Enemies *e = &w->enemies;
    float digging = -1.0f;
    int draining = 0, aiming = 0;
    bool locked = false;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        if (en->type == ENEMY_DIGGER && en->state == DIGGER_DRILL) {
            digging = fmaxf(digging, digger_progress(en, &w->planet));
        } else if (en->type == ENEMY_SIPHON && en->state == SIPHON_DRAIN) {
            draining++;
        } else if (en->type == ENEMY_SNIPER && en->arming > 0.0f) {
            aiming++;
            if (en->arming <= t->sniper_lock_time) locked = true;
        }
    }

    float now = seconds(w, t);
    int y = below_planet(w) + (boss_bar_shown(w) ? BOSS_PANEL_HEIGHT + 4 : 0);

    // O boss vem antes de tudo: é o que mais fere o planeta.
    const Boss *b = &w->boss;
    if (boss_vulnerable(b)) {
        if (b->action == BREAKER_CHARGE) {
            float left = b->timer;
            float urgency = 1.0f - left / fmaxf(t->breaker_charge[b->phase - 1], 1e-3f);
            // Pisca clareando (nunca escurecendo): tem de ser lido sobre o espaço.
            float blink = (sinf(now * (6.0f + 18.0f * urgency)) > 0.0f) ? 0.5f : 0.0f;
            text_centered(TextFormat("CORTE EM %.0f s: DESTRUA OS CAPACITORES (%d/%d)", (double)ceilf(left),
                                     BOSS_CAPACITORS - boss_capacitors_left(b), BOSS_CAPACITORS),
                          center_x, y, ALERT_SIZE + 6, mix_color(BOSS_COLOR, WHITE, blink));
            y += ALERT_SIZE + 12;
        } else if (b->action == BREAKER_FIRE) {
            text_centered("O LASER ESTÁ CORTANDO O PLANETA!", center_x, y, ALERT_SIZE + 6,
                          mix_color(BOSS_COLOR, WHITE, 0.25f + 0.25f * sinf(now * 25.0f)));
            y += ALERT_SIZE + 12;
        } else if (b->action == BREAKER_STAGGER) {
            text_centered(TextFormat("NÚCLEO EXPOSTO: ATAQUE O NÚCLEO! (%.0f s)", (double)ceilf(b->timer)), center_x, y,
                          ALERT_SIZE + 6, mix_color(CORE_OPEN_COLOR, WHITE, 0.25f + 0.25f * sinf(now * 8.0f)));
            y += ALERT_SIZE + 12;
        }
    }
    if (digging >= 0.0f) {
        float blink = 0.7f + 0.3f * sinf(now * (4.0f + 10.0f * digging));
        text_centered(TextFormat("ESCAVADOR CAVANDO RUMO AO NÚCLEO  %.0f%%", (double)digging * 100.0), center_x, y,
                      ALERT_SIZE, with_alpha(ENEMY_COLORS[ENEMY_DIGGER], blink));
        y += ALERT_SIZE + 6;
    }
    if (draining > 0) {
        const char *msg = (draining > 1) ? TextFormat("%d SIFÕES DRENANDO O PLANETA", draining) : "SIFÃO DRENANDO O PLANETA";
        text_centered(msg, center_x, y, ALERT_SIZE, with_alpha(ENEMY_COLORS[ENEMY_SIPHON], 0.75f + 0.25f * sinf(now * 5.0f)));
        y += ALERT_SIZE + 6;
    }
    if (aiming > 0 && w->ship.alive) {
        const char *msg = locked ? "ATIRADOR TRAVOU: DESVIE!" : "ATIRADOR MIRANDO EM VOCÊ";
        float blink = locked ? (sinf(now * 30.0f) > 0.0f ? 1.0f : 0.4f) : 0.8f;
        text_centered(msg, center_x, y, ALERT_SIZE + (locked ? 4 : 0), with_alpha(ENEMY_COLORS[ENEMY_SNIPER], blink));
        y += ALERT_SIZE + 10;
    }

    // Rochas: o que fazer com a que está no raio, e o aviso de meteoro novo.
    if (rocks_towed(&w->rocks) != NULL) {
        text_centered(hud_keys("REBOCANDO: solte (F) em cima de uma cratera", "REBOCANDO: solte (X) em cima de uma cratera"), center_x, y, ALERT_SIZE, with_alpha(TRACTOR_COLOR, 0.9f));
    } else if (rocks_count(&w->rocks, ROCK_METEOR) > 0) {
        for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
            const GameEvent *ev = events_find(&w->events, id);
            if (ev == NULL || event_age(w, t, ev) > METEOR_NOTE_TIME) break;
            if (ev->type != EV_METEOR) continue;
            text_centered(hud_keys("METEORO À VISTA: puxe com F e leve até uma cratera", "METEORO À VISTA: puxe com X e leve até uma cratera"), center_x, y, ALERT_SIZE,
                          with_alpha(METEOR_COLOR, 0.75f + 0.25f * sinf(now * 5.0f)));
            break;
        }
    }
}

// Quanto tempo cada aviso grande fica na tela (0 = não é aviso grande).
static float banner_time(EventType type)
{
    switch (type) {
    case EV_WAVE_START: return BANNER_TIME;
    case EV_WAVE_CLEAR: return CLEAR_TIME;
    case EV_BOSS_ENTER: return 4.2f;
    case EV_BOSS_PHASE: return 2.2f;
    case EV_BOSS_DOWN:  return 3.2f;
    case EV_REWARD:     return 5.5f;
    default:            return 0.0f;
    }
}
#define BANNER_LONGEST 5.5f

// Avisos de boss: chegada, fase, destruição e a recompensa.
static void boss_banner(const World *w, const Tuning *t, const GameEvent *e, float age, float fade, int y)
{
    int sw = GetScreenWidth();
    const char *name = boss_name((BossType)e->sub);
    switch ((EventType)e->type) {
    case EV_BOSS_ENTER: {
        DrawRectangle(0, y - 24, sw, BANNER_SIZE + 104, with_alpha((Color){ 30, 4, 4, 255 }, fade * 0.6f));
        float blink = (sinf(age * 12.0f) > 0.0f) ? 1.0f : 0.45f;
        text_centered("ALERTA", sw / 2, y - 14, LABEL_SIZE + 10, with_alpha(BOSS_COLOR, fade * blink));
        text_centered(name, sw / 2, y + 18, BANNER_SIZE + 6, with_alpha(BOSS_COLOR, fade));
        text_centered("quando ele carregar o canhão, destrua os 4 capacitores", sw / 2, y + BANNER_SIZE + 34,
                      LABEL_SIZE, with_alpha(TEXT_COLOR, fade));
        break;
    }
    case EV_BOSS_PHASE:
        text_centered(TextFormat("%s  -  FASE %d", name, (int)e->value), sw / 2, y, BANNER_SIZE - 14,
                      with_alpha(BOSS_COLOR, fade));
        break;
    case EV_BOSS_DOWN:
        text_centered(TextFormat("%s DESTRUÍDO", name), sw / 2, y, BANNER_SIZE - 10, with_alpha(PRIZE_COLOR, fade));
        text_centered(TextFormat("+%s", format_points((int64_t)e->value)), sw / 2, y + BANNER_SIZE, LABEL_SIZE + 4,
                      with_alpha(PRIZE_COLOR, fade));
        break;
    default:   // EV_REWARD
        text_centered("NÚCLEO ROMPE-MUNDOS ADQUIRIDO", sw / 2, y, BANNER_SIZE - 16, with_alpha(ABILITY_COLOR, fade));
        text_centered(TextFormat(hud_keys("segure o botão direito: um raio que atravessa tudo (recarga de %.0f s)", "segure LT: um raio que atravessa tudo (recarga de %.0f s)"),
                                 (double)t->ability_cooldown),
                      sw / 2, y + BANNER_SIZE - 6, LABEL_SIZE, with_alpha(TEXT_COLOR, fade));
        break;
    }
    (void)w;
}

// Avisos grandes no meio da tela: começo e fim de cada horda, e os do boss.
static void draw_banners(const World *w, const Tuning *t)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) break;
        float age = event_age(w, t, e);
        if (age > BANNER_LONGEST) break;   // os eventos estão em ordem: daqui para trás, todos são mais velhos
        float total = banner_time((EventType)e->type);
        if (total <= 0.0f || age > total) continue;

        float fade = fminf(fminf(age / 0.25f, (total - age) / 0.5f), 1.0f);
        if (fade <= 0.0f) continue;
        int y = sh / 2 - 130;

        if (e->type != EV_WAVE_START && e->type != EV_WAVE_CLEAR) {
            boss_banner(w, t, e, age, fade, y);
        } else if (e->type == EV_WAVE_START) {
            int number = (int)e->value;
            DrawRectangle(0, y - 14, sw, BANNER_SIZE + 64, with_alpha((Color){ 10, 4, 18, 255 }, fade * 0.55f));
            text_centered(TextFormat("HORDA %d", number), sw / 2, y, BANNER_SIZE, with_alpha(TEXT_COLOR, fade));
            if (number >= 1 && number <= w->plan.count) {
                // A composição da horda, cada tipo na sua cor.
                int counts[ENEMY_TYPE_COUNT];
                for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
                    counts[type] = wave_enemy_count(&w->plan.waves[number - 1], (EnemyType)type);
                }
                hud_type_list(counts, false, sw / 2, y + BANNER_SIZE + 8, LABEL_SIZE, fade);
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
        if (p.y - 24.0f - POPUP_RISE * life < PANELS_BOTTOM) continue;
        bool big = (e->type == EV_KILL && is_big((EnemyType)e->sub));
        Color c = (e->type == EV_BOMB_DOWN) ? BOMB_COLOR : (big ? PRIZE_COLOR : TEXT_COLOR);
        int size = big ? 30 : 20;
        text_centered(TextFormat("+%s", format_points((int64_t)e->value)), (int)p.x, (int)(p.y - 24.0f - POPUP_RISE * life),
                      size, with_alpha(c, 1.0f - life * life));
    }
}

// ---------------------------------------------------------------------------
//  Marcadores de inimigos
// ---------------------------------------------------------------------------
#define EDGE_MARGIN  46.0f   // as setas ficam a esta distância das bordas dos lados
#define EDGE_TOP     PANELS_BOTTOM   // ... e abaixo dos painéis do topo
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

// Etiqueta sobre um inimigo que está fazendo algo perigoso agora (NULL se nada).
static const char *enemy_label(const World *w, const Tuning *t, const Enemy *en, Color *color)
{
    switch ((EnemyType)en->type) {
    case ENEMY_BOMBER:
        *color = BOMB_COLOR;
        return (en->arming > 0.0f) ? "BOMBA" : NULL;
    case ENEMY_CARRIER:
        *color = ENEMY_COLORS[ENEMY_CARRIER];
        return (en->arming > 0.0f) ? "LANÇANDO" : NULL;
    case ENEMY_DIGGER:
        *color = ENEMY_COLORS[ENEMY_DIGGER];
        return (en->state == DIGGER_DRILL)
                   ? TextFormat("ESCAVANDO %.0f%%", (double)digger_progress(en, &w->planet) * 100.0)
                   : NULL;
    case ENEMY_SIPHON:
        *color = ENEMY_COLORS[ENEMY_SIPHON];
        return (en->state == SIPHON_DRAIN) ? "DRENANDO" : NULL;
    case ENEMY_SNIPER:
        *color = ENEMY_COLORS[ENEMY_SNIPER];
        if (en->arming <= 0.0f) return NULL;
        return (en->arming <= t->sniper_lock_time) ? "TRAVOU" : "MIRANDO";
    case ENEMY_KAMIKAZE:
        *color = ENEMY_COLORS[ENEMY_KAMIKAZE];
        return (en->state == KAMIKAZE_HUNT) ? "!" : NULL;
    default:
        return NULL;
    }
}

static void draw_markers(const World *w, const Tuning *t, Camera3D camera)
{
    float now = seconds(w, t);
    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        EnemyType type = (EnemyType)en->type;
        Color label_color = WHITE;
        const char *label = enemy_label(w, t, en, &label_color);
        Color color = ENEMY_COLORS[type];
        if (label != NULL) color = mix_color(color, label_color, 0.5f + 0.5f * sinf(now * 14.0f));
        float big = is_big(type) ? 1.5f : 1.0f;

        Vector2 p;
        if (project(camera, en->pos, &p) && on_screen(p)) {
            float distance = Vector3Distance(camera.position, en->pos);
            float lift = 18.0f * big;
            if (has_health_bar(t, type) && (is_big(type) || en->hp < t->enemy[type].hp)) {
                // Vida dos mais resistentes, embaixo deles.
                int width = is_big(type) ? 60 : 40;
                draw_bar((int)p.x - width / 2, (int)p.y + 26, width, 5, en->hp / t->enemy[type].hp, color);
            }
            // (não sobe para a faixa dos painéis e avisos do topo)
            if (label != NULL && p.y - lift - 30.0f > EDGE_TOP) {
                text_centered(label, (int)p.x, (int)(p.y - lift - 30.0f), 18, label_color);
            }
            if (type == ENEMY_SHIELDER) {
                // O Escudeiro é alvo prioritário: um círculo, não um losango.
                if (distance > t->combat_marker_distance * 0.5f) {
                    DrawCircleLines((int)p.x, (int)(p.y - lift), 7.0f, color);
                    DrawCircleLines((int)p.x, (int)(p.y - lift), 4.0f, color);
                }
            } else if (distance > t->combat_marker_distance) {
                diamond((Vector2){ p.x, p.y - lift }, 5.0f * big, color);
            }
        } else {
            // Fora da tela: quem está ferindo o planeta (ou mirando) pisca.
            float alpha = (label != NULL) ? 0.6f + 0.4f * sinf(now * 12.0f) : 0.85f;
            edge_arrow(camera, en->pos, ARROW_SIZE * big * (label != NULL ? 1.25f : 1.0f), with_alpha(color, alpha));
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

// As evoluções escolhidas, "Nome N" na cor da linha, centralizadas em até
// duas linhas. Devolve a altura usada.
static int evolution_list(const World *w, int cx, int y, int size, float alpha)
{
    char items[EVO_COUNT][40];
    int ids[EVO_COUNT], n = 0;
    for (int i = 0; i < EVO_COUNT; i++) {
        if (w->evo.rank[i] == 0) continue;
        snprintf(items[n], sizeof items[n], "%s %d", evolution_info((EvolutionId)i)->name, w->evo.rank[i]);
        ids[n++] = i;
    }
    const int gap = size + 4;
    int max_width = GetScreenWidth() - 120;
    int start = 0, height = 0;
    while (start < n) {
        // Quantos cabem nesta linha.
        int width = 0, end = start;
        while (end < n) {
            int item = ui_measure_text(items[end], size) + (end > start ? gap : 0);
            if (end > start && width + item > max_width) break;
            width += item;
            end++;
        }
        int x = cx - width / 2;
        for (int k = start; k < end; k++) {
            Color c = ENEMY_COLORS[evolution_info((EvolutionId)ids[k])->line];
            text(items[k], x, y + height, size, with_alpha(c, alpha));
            x += ui_measure_text(items[k], size) + gap;
        }
        height += size + 6;
        start = end;
    }
    return height;
}
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
    DrawRectangle(0, sh / 2 - 215, sw, 495, with_alpha(tint, band * 0.78f));
    const char *title = won ? "PLANETA DEFENDIDO" : "COLAPSO PLANETÁRIO";
    text_centered(title, sw / 2, sh / 2 - 190, BANNER_SIZE, with_alpha(won ? HEALTHY : DYING, band));
    if (!won) {
        const char *why = (planet_core_hp(&w->planet) < t->defeat_core_hp) ? "o núcleo foi destruído"
                                                                            : "a vida no planeta não se sustenta mais";
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
    Color body = with_alpha(TEXT_COLOR, panel);
    text_centered(TextFormat("%s de habitantes salvos      horda   %d de %d",
                             format_population(planet_hp(&w->planet) * t->planet_population), reached, w->plan.count),
                  sw / 2, y, LABEL_SIZE, body);
    y += 28;
    text_centered(TextFormat("abates   %d      bombas derrubadas   %d", sc->kills, sc->bombs_down), sw / 2, y,
                  LABEL_SIZE, body);
    y += 26;

    hud_type_list(sc->kills_by_type, true, sw / 2, y, LABEL_SIZE - 4, panel);   // abates por tipo
    y += 30;
    text_centered(TextFormat("maior combo   %d   bônus de planeta   %s", sc->best_chain, format_points(sc->planet_bonus)),
                  sw / 2, y, LABEL_SIZE, body);
    y += 28;
    if (w->rocks.delivered > 0) {
        float restored = (float)((double)w->repair.restored_total / (double)w->planet.initial_volume);
        text_centered(TextFormat("rochas levadas ao planeta   %d      %s de habitantes devolvidos", w->rocks.delivered,
                                 format_population(restored * t->planet_population)),
                      sw / 2, y, LABEL_SIZE, with_alpha(REPAIR_COLOR, panel));
        y += 28;
    }
    if (evolution_total_ranks(&w->evo) > 0) {
        text_centered(TextFormat("nível %d   evoluções:", w->evo.level), sw / 2, y, LABEL_SIZE - 4, with_alpha(DIM_COLOR, panel));
        y += 22;
        y += evolution_list(w, sw / 2, y, LABEL_SIZE - 4, panel);
    }
    float blink = 0.6f + 0.4f * sinf(since * 4.0f);
    text_centered(hud_keys("R joga de novo      Esc volta ao menu", "A joga de novo      B volta ao menu"), sw / 2, y + 16, LABEL_SIZE + 2,
                  with_alpha(PRIZE_COLOR, panel * blink));
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Boss
// ---------------------------------------------------------------------------
#define BOSS_BAR_WIDTH  480

// Barra de vida no alto, logo abaixo da do planeta, com a fase e os
// capacitores; e na tela, marcadores nos capacitores carregados e no núcleo
// aberto.
static void draw_boss(const World *w, const Tuning *t, Camera3D camera)
{
    const Boss *b = &w->boss;
    if (!boss_bar_shown(w)) return;
    int sw = GetScreenWidth();
    float now = seconds(w, t);

    int top = below_planet(w);
    int x = sw / 2 - BOSS_BAR_WIDTH / 2, y = top + 30;
    DrawRectangle(x - 12, top, BOSS_BAR_WIDTH + 24, BOSS_PANEL_HEIGHT, FRAME_COLOR);
    ui_draw_text(boss_name((BossType)b->type), x, top + 6, LABEL_SIZE, BOSS_COLOR);
    const char *phase = TextFormat("FASE %d", b->phase);
    ui_draw_text(phase, x + BOSS_BAR_WIDTH - ui_measure_text(phase, LABEL_SIZE), top + 6, LABEL_SIZE, TEXT_COLOR);
    // Na entrada, a barra enche (e ele ainda é invulnerável).
    float fill = b->hp / fmaxf(b->hp_max, 1e-3f);
    if (b->state == BOSS_ENTER) fill *= 1.0f - Clamp(b->timer / t->breaker_enter_time, 0.0f, 1.0f);
    bool open = boss_core_factor(b, t) >= 0.99f;
    Color bar = open ? mix_color(CORE_OPEN_COLOR, WHITE, 0.3f + 0.3f * sinf(now * 8.0f)) : BOSS_COLOR;
    draw_bar(x, y, BOSS_BAR_WIDTH, 12, fill, bar);
    // Marcas das fases.
    for (int k = 0; k < 2; k++) {
        float mark = (k == 0) ? t->breaker_phase_2 : t->breaker_phase_3;
        int mx = x + (int)((float)BOSS_BAR_WIDTH * mark);
        DrawRectangle(mx - 1, y - 3, 2, 18, MARKER_COLOR);
    }
    // Capacitores: um quadradinho por capacitor, aceso enquanto carregado.
    if (boss_capacitors_armed(b)) {
        int px = sw / 2 - (BOSS_CAPACITORS * 22) / 2;
        for (int k = 0; k < BOSS_CAPACITORS; k++) {
            Color c = (b->cap_hp[k] > 0.0f) ? CAPACITOR_COLOR : with_alpha(DIM_COLOR, 0.5f);
            DrawRectangle(px + k * 22, y + 17, 16, 7, c);
        }
    }

    // Marcadores na tela.
    if (!boss_vulnerable(b)) {
        Vector2 p;
        if (!project(camera, b->pos, &p) || !on_screen(p)) edge_arrow(camera, b->pos, ARROW_SIZE * 2.0f, BOSS_COLOR);
        return;
    }
    bool any_visible = false;
    if (boss_capacitors_armed(b)) {
        for (int k = 0; k < BOSS_CAPACITORS; k++) {
            if (b->cap_hp[k] <= 0.0f) continue;
            Vector2 p;
            Vector3 pos = boss_point(b, t, BREAKER_CAPACITOR[k]);
            if (project(camera, pos, &p) && on_screen(p)) {
                diamond(p, 9.0f + 2.0f * sinf(now * 10.0f), CAPACITOR_COLOR);
                any_visible = true;
            }
        }
    }
    Vector3 core = boss_point(b, t, BREAKER_CORE);
    Vector2 cp;
    if (open && project(camera, core, &cp) && on_screen(cp)) {
        DrawCircleLines((int)cp.x, (int)cp.y, 14.0f + 3.0f * sinf(now * 8.0f), CORE_OPEN_COLOR);
        text_centered("NÚCLEO", (int)cp.x, (int)cp.y - 34, 18, CORE_OPEN_COLOR);
        any_visible = true;
    }
    Vector2 bp;
    if (!any_visible && (!project(camera, b->pos, &bp) || !on_screen(bp))) {
        edge_arrow(camera, b->pos, ARROW_SIZE * 2.0f, with_alpha(BOSS_COLOR, 0.7f + 0.3f * sinf(now * 6.0f)));
    }
}

// ---------------------------------------------------------------------------
//  Rochas e conserto
// ---------------------------------------------------------------------------
#define FRAGMENT_MARK_RANGE 140.0f   // fragmentos mais perto que isto ganham um marcador
#define REPAIR_POPUP_TIME   2.8f
#define TRACTOR_NOTE_TIME   1.0f
#define SCAR_MARKS          8        // cicatrizes marcadas ao mesmo tempo, as maiores
#define SCAR_MARK_MAX       90.0f    // raio máximo do círculo de uma cicatriz na tela (pixels)
#define POPUP_EDGE          160      // o texto dos habitantes não chega mais perto que isto das bordas

// A cicatriz para onde levar a rocha: grande e perto dela.
static int best_scar(const World *w, Vector3 from)
{
    int best = -1;
    float best_score = 0.0f;
    for (int i = 0; i < REPAIR_SCARS; i++) {
        const Scar *s = &w->repair.scars[i];
        if (!s->used) continue;
        float score = Vector3Distance(from, repair_scar_point(&w->planet, s)) - s->radius * 3.0f;
        if (best < 0 || score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

// Tamanho na tela (pixels) de algo de tamanho `size` a `distance` da câmera.
static float screen_size(Camera3D camera, float size, float distance)
{
    float half_fov = camera.fovy * 0.5f * DEG2RAD;
    return size * (float)GetScreenHeight() / (2.0f * tanf(half_fov) * fmaxf(distance, 1.0f));
}

static void draw_rocks(const World *w, const Tuning *t, Camera3D camera)
{
    const Rocks *r = &w->rocks;
    const Ship *s = &w->ship;
    float now = seconds(w, t);
    const Rock *towed = rocks_towed(r);
    int grab = (towed == NULL && s->alive) ? rocks_find_grab(r, s, t) : -1;

    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &r->items[i];
        if (!k->alive || k->state == ROCK_TOWED || k->state == ROCK_FALLING) continue;
        Vector2 p;
        bool visible = project(camera, k->pos, &p) && on_screen(p);
        if (i == grab && visible) {
            // Ao alcance do raio: colchetes e a tecla.
            float half = fmaxf(screen_size(camera, k->radius * 1.4f, Vector3Distance(camera.position, k->pos)), 12.0f);
            draw_brackets(p, half, TRACTOR_COLOR);
            text_centered(hud_keys("F  puxa", "X  puxa"), (int)p.x, (int)(p.y + half + 6.0f), 18, TRACTOR_COLOR);
            continue;
        }
        if (k->kind == ROCK_METEOR) {
            if (visible) {
                diamond((Vector2){ p.x, p.y - 22.0f }, 6.0f, METEOR_COLOR);
                text_centered("METEORO", (int)p.x, (int)p.y - 46, 16, METEOR_COLOR);
            } else {
                edge_arrow(camera, k->pos, ARROW_SIZE * 1.3f, with_alpha(METEOR_COLOR, 0.7f + 0.3f * sinf(now * 5.0f)));
            }
        } else if (visible && Vector3Distance(s->pos, k->pos) < FRAGMENT_MARK_RANGE) {
            DrawCircleLines((int)p.x, (int)p.y, 9.0f, with_alpha(FRAGMENT_COLOR, 0.6f));
        }
    }

    // Rebocando: as crateras (as maiores), e a melhor em destaque.
    if (towed != NULL) {
        int best = best_scar(w, towed->pos);
        int order[REPAIR_SCARS], count = 0;
        for (int i = 0; i < REPAIR_SCARS; i++) if (w->repair.scars[i].used) order[count++] = i;
        for (int a = 1; a < count; a++) {   // as maiores primeiro (poucas: ordenação simples)
            int v = order[a], b = a - 1;
            while (b >= 0 && w->repair.scars[order[b]].radius < w->repair.scars[v].radius) {
                order[b + 1] = order[b];
                b--;
            }
            order[b + 1] = v;
        }
        if (count > SCAR_MARKS) count = SCAR_MARKS;
        bool best_seen = false;
        for (int n = 0; n < count; n++) {
            const Scar *scar = &w->repair.scars[order[n]];
            Vector3 point = repair_scar_point(&w->planet, scar);
            Vector3 to_eye = Vector3Subtract(camera.position, point);
            if (Vector3DotProduct(scar->dir, to_eye) <= 0.0f) continue;   // do outro lado do planeta
            Vector2 p;
            if (!project(camera, point, &p) || !on_screen(p)) continue;
            float radius = Clamp(screen_size(camera, scar->radius, Vector3Length(to_eye)), 10.0f, SCAR_MARK_MAX);
            bool is_best = (order[n] == best);
            float pulse = is_best ? 0.75f + 0.25f * sinf(now * 6.0f) : 0.5f;
            DrawCircleLines((int)p.x, (int)p.y, radius, with_alpha(REPAIR_COLOR, pulse));
            if (is_best) {
                DrawCircleLines((int)p.x, (int)p.y, radius * 0.6f, with_alpha(REPAIR_COLOR, pulse * 0.6f));
                text_centered("CRATERA", (int)p.x, (int)(p.y - radius - 22.0f), 16, REPAIR_COLOR);
                best_seen = true;
            }
        }
        if (best >= 0 && !best_seen) {
            edge_arrow(camera, repair_scar_point(&w->planet, &w->repair.scars[best]), ARROW_SIZE * 1.3f, REPAIR_COLOR);
        }
    }

    // Avisos curtos perto da mira, e os habitantes que voltaram subindo de
    // onde o planeta foi consertado.
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int stacked = 0;   // textos de conserto já desenhados: os mais velhos sobem, para não se sobreporem
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) break;
        float age = event_age(w, t, e);
        if (age > REPAIR_POPUP_TIME) break;
        if ((e->type == EV_TRACTOR_MISS || e->type == EV_TRACTOR_BREAK) && age < TRACTOR_NOTE_TIME) {
            const char *msg = (e->type == EV_TRACTOR_MISS) ? "NENHUMA ROCHA À FRENTE" : "O RAIO ARREBENTOU";
            text_centered(msg, sw / 2, sh / 2 + 40, 18, with_alpha(TRACTOR_COLOR, 1.0f - age / TRACTOR_NOTE_TIME));
        } else if (e->type == EV_REPAIR) {
            Vector2 p;
            if (!project(camera, e->pos, &p)) continue;
            float life = age / REPAIR_POPUP_TIME;
            float y = Clamp(p.y - 30.0f - POPUP_RISE * 1.6f * life, (float)PANELS_BOTTOM, (float)(sh - 120));
            y -= (float)(stacked++ * 30);
            int x = (int)Clamp(p.x, (float)POPUP_EDGE, (float)(sw - POPUP_EDGE));
            Color c = with_alpha(REPAIR_COLOR, 1.0f - life * life);
            if (e->value <= 0.0f) {
                text_centered("aqui não havia o que consertar", x, (int)y, 18, with_alpha(DIM_COLOR, 1.0f - life));
            } else {
                text_centered(TextFormat("+%s de habitantes", format_population(e->value * t->planet_population)), x,
                              (int)y, 26, c);
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Minério e defesas
// ---------------------------------------------------------------------------
#define SHOP_WIDTH   300
#define SHOP_ROW     24
#define SHOP_BOTTOM  22
#define SHOP_NOTE_TIME 1.6f
#define ORE_FLASH_TIME 0.35f

// Canto de baixo à direita: o minério e as três defesas (tecla, preço e
// quantas já há), apagadas quando não dá para comprar.
static void draw_shop(const World *w, const Tuning *t)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int x = sw - SHOP_WIDTH - 14, y = sh - SHOP_BOTTOM - SHOP_ROW * 4 - 8;
    float now = seconds(w, t);

    // Coletou minério há pouco? O número clareia.
    float glow = 0.0f;
    const char *note = NULL;
    Color note_color = TEXT_COLOR;
    float note_alpha = 0.0f;
    for (uint32_t id = w->events.count; id >= 1 && id + EVENTS_CAPACITY > w->events.count; id--) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) break;
        float age = event_age(w, t, e);
        if (age > SHOP_NOTE_TIME) break;
        if (e->type == EV_ORE && age < ORE_FLASH_TIME) glow = fmaxf(glow, 1.0f - age / ORE_FLASH_TIME);
        if (note == NULL && e->type == EV_BUY) {
            note = (e->sub == DEF_TURRET) ? "TORRE ORBITAL INSTALADA"
                 : (e->sub == DEF_SHIELD) ? "ESCUDO LOCAL ERGUIDO"
                                          : "SATÉLITE EM ÓRBITA";
            note_color = ORE_COLOR;
            note_alpha = 1.0f - age / SHOP_NOTE_TIME;
        } else if (note == NULL && e->type == EV_BUY_FAIL) {
            note = (e->value > 0.5f) ? "LIMITE DESSA DEFESA" : "MINÉRIO INSUFICIENTE";
            note_color = DYING;
            note_alpha = 1.0f - age / SHOP_NOTE_TIME;
        }
    }

    DrawRectangle(x - 10, y - 8, SHOP_WIDTH + 20, SHOP_ROW * 4 + 14, FRAME_COLOR);
    // A confirmação (ou a recusa) da compra aparece no lugar do título, por um instante.
    if (note != NULL && note_alpha > 0.0f) {
        ui_draw_text(note, x, y + 2, LABEL_SIZE - 4, with_alpha(note_color, fminf(note_alpha * 2.0f, 1.0f)));
    } else {
        ui_draw_text("MINÉRIO", x, y, LABEL_SIZE, with_alpha(ORE_COLOR, 0.85f));
    }
    const char *amount = TextFormat("%d", w->ore);
    int size = LABEL_SIZE + 4 + (int)(glow * 6.0f);
    ui_draw_text(amount, x + SHOP_WIDTH - ui_measure_text(amount, size), y - 2 - (int)(glow * 3.0f), size,
             mix_color(ORE_COLOR, WHITE, glow));
    for (int k = 0; k < DEF_TYPE_COUNT; k++) {
        DefenseType type = (DefenseType)k;
        int ry = y + SHOP_ROW * (k + 1) + 2;
        int cost = defense_cost(t, type), max = defense_max(t, type), have = defenses_count(&w->defenses, type);
        bool full = have >= max, cheap = w->ore >= cost;
        Color c = (full || !cheap) ? DIM_COLOR : TEXT_COLOR;
        Color key = (full || !cheap) ? DIM_COLOR : PRIZE_COLOR;
        if (hud_gamepad()) hud_dpad_arrow(x - 2, ry, 16, k, key);
        else               ui_draw_text(TextFormat("%d", 4 + k), x, ry, LABEL_SIZE - 2, key);
        ui_draw_text(defense_name(type), x + 24, ry, LABEL_SIZE - 2, c);
        const char *right = full ? TextFormat("%d/%d", have, max) : TextFormat("%d    %d/%d", cost, have, max);
        ui_draw_text(right, x + SHOP_WIDTH - ui_measure_text(right, LABEL_SIZE - 2), ry, LABEL_SIZE - 2,
                 full ? DIM_COLOR : (cheap ? ORE_COLOR : DIM_COLOR));
    }
    (void)now;
}

void hud_draw(const World *w, const Tuning *t, const HudContext *ctx)
{
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    bool playing = (w->cam_mode == CAM_SHIP && w->ship_active);

    if (playing) {
        draw_damage_vignette(w);
        draw_markers(w, t, ctx->camera);
        if (!w->evo.choosing) draw_rocks(w, t, ctx->camera);
        draw_popups(w, t, ctx->camera);
    }
    draw_planet(w, t, ctx->center_x);
    if (playing && w->state == GAME_PLAYING) draw_alerts(w, t, ctx->center_x);
    if (w->director.phase != DIRECTOR_OFF) {
        draw_score(w, t, ctx);
        draw_wave_info(w, t);
    }

    if (playing && !w->evo.choosing) {   // com as cartas na tela, o painel da nave sai do caminho
        if (w->state == GAME_PLAYING) {  // no fim, quem fala é o painel do resultado
            draw_boss(w, t, ctx->camera);
            draw_ship(w, t, ctx->camera);
            draw_shop(w, t);
            draw_banners(w, t);
        }
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
                                                                              : "a vida no planeta não se sustenta mais";
        int band_y = screen_h / 2 - 70;
        DrawRectangle(0, band_y, screen_w, 140, (Color){ 20, 0, 0, 190 });
        text_centered(banner, screen_w / 2, band_y + 26, BANNER_SIZE - 4, DYING);
        text_centered(reason, screen_w / 2, band_y + 90, LABEL_SIZE, TEXT_COLOR);
    }
}
