#include "raylib.h"

#include "ui/menus.h"

#include <math.h>
#include <stdio.h>

#include "core/vmath.h"
#include "ui/font.h"

static const Color INK       = { 2, 3, 8, 255 };   // o mesmo fundo do espaço
static const Color TEXT      = { 228, 232, 242, 255 };
static const Color DIM       = { 140, 150, 172, 255 };
static const Color ACCENT    = { 255, 214, 110, 255 };
static const Color LAVA      = { 255, 128, 48, 255 };
static const Color LAVA_GLOW = { 255, 196, 90, 255 };
static const Color PANEL     = { 6, 10, 22, 230 };
static const Color EDGE      = { 70, 84, 120, 255 };

#define ITEM_SIZE     30     // texto dos botões
#define ROW_SIZE      22     // texto das linhas de opções e controles
#define HEADING_SIZE  60
#define GLOW_SPEED    12.0f  // quão rápido o destaque acompanha o cursor
#define OPEN_TIME     0.35f  // segundos do surgimento de uma tela
#define TIP_TIME      7.0f   // segundos de cada dica na pausa

typedef enum {
    BTN_PLAY, BTN_RESUME, BTN_RESTART, BTN_CONTROLS, BTN_OPTIONS, BTN_TO_TITLE, BTN_QUIT, BTN_BACK
} Button;

typedef struct {
    Rectangle rect;
    const char *label;
    bool      setting;   // linha de opção (id = SettingId) ou botão (id = Button)
    int       id;
} Item;

// ---------------------------------------------------------------------------
//  Disposição dos itens (a mesma para o mouse e para o desenho)
// ---------------------------------------------------------------------------

static float sw(void) { return (float)GetScreenWidth(); }
static float sh(void) { return (float)GetScreenHeight(); }

static float title_left(void) { return fmaxf(70.0f, sw() * 0.085f); }

static float options_width(void) { return fminf(820.0f, sw() - 80.0f); }

static void add(Item *items, int *n, Rectangle r, const char *label, bool setting, int id)
{
    if (*n >= MENU_MAX_ITEMS) return;
    items[*n] = (Item){ r, label, setting, id };
    (*n)++;
}

static int build_items(Screen screen, Item items[MENU_MAX_ITEMS])
{
    int n = 0;
    switch (screen) {
    case SCREEN_TITLE: {
        static const char *const LABELS[] = { "JOGAR", "CONTROLES", "OPÇÕES", "SAIR" };
        static const Button IDS[] = { BTN_PLAY, BTN_CONTROLS, BTN_OPTIONS, BTN_QUIT };
        float y = fmaxf(sh() * 0.50f, 330.0f);
        for (int i = 0; i < 4; i++) add(items, &n, (Rectangle){ title_left() - 18.0f, y + 54.0f * (float)i, 340.0f, 46.0f },
                                        LABELS[i], false, IDS[i]);
        break;
    }
    case SCREEN_PAUSE: {
        static const char *const LABELS[] = { "CONTINUAR", "RECOMEÇAR", "CONTROLES", "OPÇÕES", "MENU PRINCIPAL",
                                              "SAIR DO JOGO" };
        static const Button IDS[] = { BTN_RESUME, BTN_RESTART, BTN_CONTROLS, BTN_OPTIONS, BTN_TO_TITLE, BTN_QUIT };
        float y = sh() * 0.5f - 130.0f;
        for (int i = 0; i < 6; i++) add(items, &n, (Rectangle){ sw() * 0.5f - 190.0f, y + 50.0f * (float)i, 380.0f, 44.0f },
                                        LABELS[i], false, IDS[i]);
        break;
    }
    case SCREEN_OPTIONS: {
        float width = options_width();
        float x = (sw() - width) * 0.5f;
        float y = fmaxf(sh() * 0.5f - 250.0f, 120.0f);
        for (int i = 0; i < SET_COUNT; i++) {
            add(items, &n, (Rectangle){ x, y + 44.0f * (float)i, width, 38.0f }, settings_label((SettingId)i), true, i);
        }
        add(items, &n, (Rectangle){ sw() * 0.5f - 150.0f, y + 44.0f * (float)SET_COUNT + 24.0f, 300.0f, 44.0f },
            "VOLTAR", false, BTN_BACK);
        break;
    }
    case SCREEN_CONTROLS:
        add(items, &n, (Rectangle){ sw() * 0.5f - 150.0f, sh() - 92.0f, 300.0f, 44.0f }, "VOLTAR", false, BTN_BACK);
        break;
    default:
        break;
    }
    return n;
}

// Trilho do slider dentro de uma linha de opção.
static Rectangle slider_rect(Rectangle row)
{
    return (Rectangle){ row.x + row.width * 0.52f, row.y + row.height * 0.5f - 4.0f, row.width * 0.32f, 8.0f };
}

// ---------------------------------------------------------------------------
//  Navegação
// ---------------------------------------------------------------------------

void menu_open(Menu *m, Screen screen)
{
    Screen parent = m->parent;
    bool gamepad = m->gamepad;
    *m = (Menu){ 0 };
    m->screen = screen;
    m->parent = parent;
    m->gamepad = gamepad;
    m->dragging = -1;
}

bool menu_freezes_game(const Menu *m)
{
    if (m->screen == SCREEN_PAUSE) return true;
    if (m->screen == SCREEN_OPTIONS || m->screen == SCREEN_CONTROLS) return m->parent == SCREEN_PAUSE;
    return false;
}

// Abre opções ou controles lembrando de onde veio (e em qual item estava).
static void open_child(Menu *m, Screen child)
{
    Screen from = m->screen;
    menu_open(m, child);
    m->parent = from;
}

static void open_parent(Menu *m)
{
    Screen child = m->screen;
    menu_open(m, m->parent);
    // Volta com o cursor no item que abriu a tela filha.
    Item items[MENU_MAX_ITEMS];
    int n = build_items(m->screen, items);
    Button wanted = (child == SCREEN_OPTIONS) ? BTN_OPTIONS : BTN_CONTROLS;
    for (int i = 0; i < n; i++) {
        if (!items[i].setting && items[i].id == (int)wanted) m->cursor = i;
    }
    for (int i = 0; i < MENU_MAX_ITEMS; i++) m->glow[i] = (i == m->cursor) ? 1.0f : 0.0f;
}

static MenuCommand go_back(Menu *m)
{
    m->sound = MENU_SOUND_BACK;
    switch (m->screen) {
    case SCREEN_OPTIONS:
        open_parent(m);
        m->sound = MENU_SOUND_BACK;
        return MENU_SETTINGS_DONE;
    case SCREEN_CONTROLS:
        open_parent(m);
        m->sound = MENU_SOUND_BACK;
        return MENU_NONE;
    case SCREEN_PAUSE:
        return MENU_RESUME;
    default:
        m->sound = MENU_SOUND_NONE;
        return MENU_NONE;
    }
}

static MenuCommand press_button(Menu *m, Button b)
{
    m->sound = MENU_SOUND_CONFIRM;
    switch (b) {
    case BTN_PLAY:     return MENU_PLAY;
    case BTN_RESUME:   return MENU_RESUME;
    case BTN_RESTART:  return MENU_RESTART;
    case BTN_TO_TITLE: return MENU_TO_TITLE;
    case BTN_QUIT:     return MENU_QUIT;
    case BTN_CONTROLS: open_child(m, SCREEN_CONTROLS); return MENU_NONE;
    case BTN_OPTIONS:  open_child(m, SCREEN_OPTIONS); return MENU_NONE;
    case BTN_BACK:     return go_back(m);
    }
    return MENU_NONE;
}

static void set_from_mouse(Settings *s, SettingId id, Rectangle row, float mouse_x)
{
    Rectangle track = slider_rect(row);
    settings_set_fraction(s, id, Clamp((mouse_x - track.x) / track.width, 0.0f, 1.0f));
}

MenuCommand menu_update(Menu *m, const MenuInput *in, Settings *settings, float dt)
{
    m->sound = MENU_SOUND_NONE;
    if (m->screen == SCREEN_GAME) return MENU_NONE;
    m->age += dt;
    if (in->gamepad) m->gamepad = true;
    else if (in->mouse_moved || in->click) m->gamepad = false;

    Item items[MENU_MAX_ITEMS];
    int n = build_items(m->screen, items);
    if (n == 0) return MENU_NONE;
    if (m->cursor >= n) m->cursor = n - 1;

    // O destaque desliza até o item do cursor.
    float blend = 1.0f - expf(-GLOW_SPEED * dt);
    for (int i = 0; i < n; i++) m->glow[i] += (((i == m->cursor) ? 1.0f : 0.0f) - m->glow[i]) * blend;

    // Arrastando um slider: segue o mouse até soltar.
    if (m->dragging >= 0) {
        if (!in->mouse_down) {
            m->dragging = -1;
            return MENU_NONE;
        }
        set_from_mouse(settings, (SettingId)m->dragging, items[m->dragging].rect, in->mouse.x);
        return MENU_SETTINGS;
    }

    // Mouse em cima de um item: ele vira o escolhido.
    int under = -1;
    for (int i = 0; i < n; i++) {
        if (CheckCollisionPointRec(in->mouse, items[i].rect)) under = i;
    }
    if ((in->mouse_moved || in->click) && under >= 0 && under != m->cursor) {
        m->cursor = under;
        m->sound = MENU_SOUND_MOVE;
    }

    if (in->up || in->down) {
        m->cursor = (m->cursor + (in->down ? 1 : n - 1)) % n;
        m->sound = MENU_SOUND_MOVE;
    }
    if (in->back) return go_back(m);

    const Item *it = &items[m->cursor];
    bool clicked = in->click && under == m->cursor;
    if (it->setting) {
        SettingId id = (SettingId)it->id;
        if (in->left || in->right) {
            settings_step(settings, id, in->right ? +1 : -1);
            m->sound = MENU_SOUND_MOVE;
            return MENU_SETTINGS;
        }
        if (settings_is_toggle(id) && (in->confirm || clicked)) {
            settings_step(settings, id, +1);
            m->sound = MENU_SOUND_CONFIRM;
            return MENU_SETTINGS;
        }
        if (clicked && !settings_is_toggle(id)) {
            m->dragging = it->id;
            set_from_mouse(settings, id, it->rect, in->mouse.x);
            m->sound = MENU_SOUND_MOVE;
            return MENU_SETTINGS;
        }
        return MENU_NONE;
    }
    if (in->confirm || clicked) return press_button(m, (Button)it->id);
    return MENU_NONE;
}

// ---------------------------------------------------------------------------
//  Desenho
// ---------------------------------------------------------------------------

static Color fade(Color c, float alpha)
{
    c.a = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * (float)c.a);
    return c;
}

static Color mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)Lerp(a.r, b.r, t), (unsigned char)Lerp(a.g, b.g, t),
                    (unsigned char)Lerp(a.b, b.b, t), (unsigned char)Lerp(a.a, b.a, t) };
}

static void text_at(const char *s, float x, float y, int size, Color c)
{
    ui_draw_text_shadow(s, (int)x, (int)y, size, c, size >= 40 ? 3 : 2);
}

static void text_centered(const char *s, float cx, float y, int size, Color c)
{
    text_at(s, cx - (float)ui_measure_text(s, size) * 0.5f, y, size, c);
}

// Y para centralizar as maiúsculas de um texto de tamanho `size` numa faixa.
static float middle(Rectangle r, int size)
{
    return r.y + r.height * 0.5f - (float)size * 0.45f;
}

static void draw_button(const Item *it, float glow, float alpha, bool centered)
{
    Rectangle r = it->rect;
    if (glow > 0.01f) {
        Rectangle bar = r;
        if (centered) {
            bar.width = r.width * (0.6f + 0.4f * glow);
            bar.x = r.x + (r.width - bar.width) * 0.5f;
        } else {
            bar.width = r.width * (0.5f + 0.5f * glow);
        }
        DrawRectangleGradientH((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height,
                               fade(ACCENT, 0.22f * glow * alpha), fade(ACCENT, (centered ? 0.22f : 0.0f) * glow * alpha));
        DrawRectangle((int)bar.x, (int)bar.y, 4, (int)bar.height, fade(ACCENT, glow * alpha));
    }
    Color c = fade(mix(TEXT, ACCENT, glow), alpha);
    if (centered) text_centered(it->label, r.x + r.width * 0.5f, middle(r, ITEM_SIZE), ITEM_SIZE, c);
    else          text_at(it->label, r.x + 18.0f + 10.0f * glow, middle(r, ITEM_SIZE), ITEM_SIZE, c);
}

static void draw_hint(const Menu *m, const char *keys, const char *pad, float alpha)
{
    const char *s = m->gamepad ? pad : keys;
    text_centered(s, sw() * 0.5f, sh() - 34.0f, 18, fade(DIM, alpha));
}

// O nome do jogo, com uma rachadura de lava atravessando "FRATURADA".
static void draw_logo(float x, float y, float alpha, float time)
{
    const int size = 100;
    text_at("TERRA", x, y, size, fade(TEXT, alpha));
    float y2 = y + (float)size * 0.86f;
    // Brilho por trás da palavra quente.
    for (int k = 3; k >= 1; k--) {
        float spread = (float)k * 2.0f;
        ui_draw_text("FRATURADA", (int)(x - spread), (int)(y2 - spread * 0.5f), size, fade(LAVA, alpha * 0.06f));
        ui_draw_text("FRATURADA", (int)(x + spread), (int)(y2 + spread * 0.5f), size, fade(LAVA, alpha * 0.06f));
    }
    text_at("FRATURADA", x, y2, size, fade(LAVA, alpha));

    // A rachadura: uma linha em zigue-zague da cor do fundo, com um fio de
    // lava pulsando dentro.
    static const float CRACK[] = { 0.0f, 0.42f, 0.30f, 0.62f, 0.47f, 0.36f, 0.58f, 0.70f, 0.52f, 0.40f, 0.55f, 0.48f };
    const int points = (int)(sizeof CRACK / sizeof CRACK[0]);
    float width = (float)ui_measure_text("FRATURADA", size);
    float top = y2 + (float)size * 0.10f, height = (float)size * 0.70f;
    float pulse = 0.75f + 0.25f * sinf(time * 2.4f);
    Vector2 line[sizeof CRACK / sizeof CRACK[0]];
    for (int i = 0; i < points; i++) {
        float t = (float)i / (float)(points - 1);
        line[i] = (Vector2){ x - 6.0f + t * (width + 12.0f), top + height * (i == 0 ? 0.5f : CRACK[i]) };
    }
    for (int i = 0; i + 1 < points; i++) DrawLineEx(line[i], line[i + 1], 8.0f, fade(INK, alpha));
    for (int i = 0; i < points; i++) DrawCircleV(line[i], 4.0f, fade(INK, alpha));   // juntas sem falhas
    for (int i = 0; i + 1 < points; i++) DrawLineEx(line[i], line[i + 1], 2.4f, fade(LAVA_GLOW, alpha * pulse));
}

static void draw_title(const Menu *m, const MenuView *view, const Item *items, int n)
{
    float alpha = Clamp(m->age / 0.8f, 0.0f, 1.0f);
    // Escurece o lado esquerdo, onde ficam o nome e os botões.
    DrawRectangleGradientH(0, 0, (int)(sw() * 0.62f), (int)sh(), fade((Color){ 0, 0, 0, 215 }, alpha),
                           (Color){ 0, 0, 0, 0 });
    float x = title_left();
    draw_logo(x, fmaxf(sh() * 0.13f, 40.0f), alpha, m->age);
    text_at("a Terra está sendo despedaçada. você é a última defesa.", x + 4.0f,
            fmaxf(sh() * 0.13f, 40.0f) + 196.0f, 20, fade(DIM, alpha));

    for (int i = 0; i < n; i++) {
        float appear = Clamp((m->age - 0.25f - 0.07f * (float)i) / OPEN_TIME, 0.0f, 1.0f);
        Item it = items[i];
        it.rect.x -= 30.0f * (1.0f - appear);
        draw_button(&it, m->glow[i], appear, false);
    }
    if (view->best_score > 0) {
        char best[48];
        snprintf(best, sizeof best, "RECORDE  %lld", (long long)view->best_score);
        text_at(best, x, items[n - 1].rect.y + 76.0f, 20, fade(ACCENT, alpha * 0.85f));
    }
    draw_hint(m, "W/S ou setas escolhem   ·   Enter confirma", "direcional escolhe   ·   A confirma", alpha);
    const char *credit = "feito em C com raylib";
    text_at(credit, sw() - (float)ui_measure_text(credit, 16) - 24.0f, sh() - 34.0f, 16, fade(DIM, alpha * 0.7f));
}

static const char *const TIPS[] = {
    "Voar baixo, dentro da atmosfera, recarrega o escudo duas vezes mais rápido e acelera a arma.",
    "O Bombardeiro é blindado na frente: acerte o motor, atrás dele.",
    "Puxe uma rocha com F e solte em cima de uma cratera: o terreno volta e os habitantes também.",
    "Tiros quebram rochas em minério. Com minério, compre torres, escudos e satélites (4, 5 e 6).",
    "O Escudeiro deixa os aliados imunes aos tiros: derrube ele primeiro.",
    "O Escavador cava um túnel até o núcleo. Entre no túnel atrás dele.",
    "Passar perto da Lua dá um estilingue: velocidade extra por alguns segundos.",
    "No Rompe-Mundos, destrua os quatro capacitores antes do disparo para abrir o núcleo.",
};

static void draw_pause(const Menu *m, const World *w, const Item *items, int n)
{
    float alpha = Clamp(m->age / 0.2f, 0.0f, 1.0f);
    DrawRectangle(0, 0, (int)sw(), (int)sh(), fade((Color){ 0, 2, 10, 215 }, alpha));
    float top = items[0].rect.y;
    text_centered("PAUSA", sw() * 0.5f, top - 120.0f, HEADING_SIZE + 12, fade(TEXT, alpha));

    char info[128];
    if (w->director.phase != DIRECTOR_OFF) {
        int wave = (w->director.phase == DIRECTOR_DONE) ? w->plan.count : w->director.wave + 1;
        snprintf(info, sizeof info, "horda %d de %d   ·   planeta %.0f%%   ·   %lld pontos", wave, w->plan.count,
                 (double)planet_hp(&w->planet) * 100.0, (long long)w->score.points);
    } else {
        snprintf(info, sizeof info, "planeta %.0f%%", (double)planet_hp(&w->planet) * 100.0);
    }
    text_centered(info, sw() * 0.5f, top - 42.0f, 20, fade(DIM, alpha));

    for (int i = 0; i < n; i++) draw_button(&items[i], m->glow[i], alpha, true);

    int count = (int)(sizeof TIPS / sizeof TIPS[0]);
    int tip = (int)(GetTime() / TIP_TIME) % count;
    float y = items[n - 1].rect.y + 76.0f;
    DrawRectangleGradientV(0, (int)(y - 14.0f), (int)sw(), 36, fade((Color){ 0, 0, 0, 0 }, alpha),
                           fade((Color){ 0, 0, 0, 150 }, alpha));
    DrawRectangle(0, (int)(y + 22.0f), (int)sw(), 40, fade((Color){ 0, 0, 0, 150 }, alpha));
    text_centered("DICA", sw() * 0.5f, y, 16, fade(ACCENT, alpha * 0.8f));
    text_centered(TIPS[tip], sw() * 0.5f, y + 24.0f, 20, fade(TEXT, alpha * 0.9f));
    draw_hint(m, "Esc volta ao jogo", "Start ou B volta ao jogo", alpha);
}

static void draw_panel(Rectangle r, float alpha)
{
    DrawRectangleRec(r, fade(PANEL, alpha));
    DrawRectangleLinesEx(r, 1.0f, fade(EDGE, alpha * 0.8f));
}

static void draw_options(const Menu *m, const Settings *s, const Item *items, int n)
{
    float alpha = Clamp(m->age / 0.2f, 0.0f, 1.0f);
    DrawRectangle(0, 0, (int)sw(), (int)sh(), fade((Color){ 0, 2, 10, 190 }, alpha));
    Rectangle first = items[0].rect;
    Rectangle panel = { first.x - 30.0f, first.y - 96.0f, first.width + 60.0f,
                        items[n - 1].rect.y + items[n - 1].rect.height + 26.0f - (first.y - 96.0f) };
    draw_panel(panel, alpha);
    text_centered("OPÇÕES", sw() * 0.5f, first.y - 80.0f, HEADING_SIZE - 12, fade(TEXT, alpha));

    for (int i = 0; i < n; i++) {
        const Item *it = &items[i];
        if (!it->setting) {
            draw_button(it, m->glow[i], alpha, true);
            continue;
        }
        SettingId id = (SettingId)it->id;
        Rectangle r = it->rect;
        float g = m->glow[i];
        if (g > 0.01f) {
            DrawRectangleRec(r, fade(ACCENT, 0.12f * g * alpha));
            DrawRectangle((int)r.x, (int)r.y, 4, (int)r.height, fade(ACCENT, g * alpha));
        }
        Color label = fade(mix(TEXT, ACCENT, g), alpha);
        text_at(it->label, r.x + 18.0f, middle(r, ROW_SIZE), ROW_SIZE, label);

        const char *value = settings_value_text(s, id);
        float value_x = r.x + r.width - 18.0f - (float)ui_measure_text(value, ROW_SIZE);
        if (settings_is_toggle(id)) {
            bool on = settings_fraction(s, id) > 0.5f;
            Rectangle pill = { slider_rect(r).x, r.y + 8.0f, 54.0f, r.height - 16.0f };
            DrawRectangleRounded(pill, 1.0f, 8, fade(on ? (Color){ 70, 200, 120, 255 } : (Color){ 60, 66, 84, 255 }, alpha));
            float knob = on ? pill.x + pill.width - pill.height * 0.5f : pill.x + pill.height * 0.5f;
            DrawCircle((int)knob, (int)(pill.y + pill.height * 0.5f), pill.height * 0.5f - 3.0f, fade(TEXT, alpha));
        } else {
            Rectangle track = slider_rect(r);
            float f = settings_fraction(s, id);
            DrawRectangleRec(track, fade((Color){ 40, 46, 62, 255 }, alpha));
            DrawRectangle((int)track.x, (int)track.y, (int)(track.width * f), (int)track.height,
                          fade(mix(DIM, ACCENT, 0.4f + 0.6f * g), alpha));
            DrawCircle((int)(track.x + track.width * f), (int)(track.y + track.height * 0.5f), 9.0f,
                       fade(mix(TEXT, ACCENT, g), alpha));
        }
        text_at(value, value_x, middle(r, ROW_SIZE), ROW_SIZE, fade(g > 0.5f ? TEXT : DIM, alpha));
    }
    draw_hint(m, "W/S escolhem   ·   A/D ou setas ajustam   ·   Esc volta e grava",
              "direcional escolhe e ajusta   ·   A liga e desliga   ·   B volta e grava", alpha);
}

static const char *const CONTROLS[][3] = {
    { "Mirar",                          "mouse",                      "analógico direito" },
    { "Atirar",                         "botão esquerdo",             "RT" },
    { "Raio do núcleo (segurar)",       "botão direito",              "LT" },
    { "Acelerar  /  frear e ré",        "W  /  S",                    "analógico esquerdo" },
    { "Deslocar de lado",               "A  /  D",                    "analógico esquerdo" },
    { "Rolar",                          "Q  /  E",                    "LB  /  RB" },
    { "Boost",                          "Shift",                      "L3 (apertar o analógico)" },
    { "Dash",                           "Espaço",                     "A" },
    { "Drift",                          "Ctrl",                       "B" },
    { "Raio trator",                    "F",                          "X" },
    { "Chamar a próxima horda",         "Enter",                      "Y" },
    { "Comprar torre, escudo, satélite", "4  /  5  /  6",             "direcional esq. / cima / dir." },
    { "Escolher carta de evolução",     "1  /  2  /  3  ou clique",   "direcional esq. / cima / dir." },
    { "Pausa",                          "Esc",                        "Start" },
};

static void draw_controls(const Menu *m, const Item *items, int n)
{
    float alpha = Clamp(m->age / 0.2f, 0.0f, 1.0f);
    DrawRectangle(0, 0, (int)sw(), (int)sh(), fade((Color){ 0, 2, 10, 190 }, alpha));
    int rows = (int)(sizeof CONTROLS / sizeof CONTROLS[0]);
    float line = fminf(30.0f, (sh() - 290.0f) / (float)rows);
    float width = fminf(1000.0f, sw() - 60.0f);
    float x = (sw() - width) * 0.5f;
    float top = fmaxf((sh() - (float)rows * line - 200.0f) * 0.5f, 20.0f);
    Rectangle panel = { x - 20.0f, top, width + 40.0f, items[0].rect.y - top - 18.0f };
    draw_panel(panel, alpha);
    text_centered("CONTROLES", sw() * 0.5f, top + 16.0f, HEADING_SIZE - 12, fade(TEXT, alpha));

    float col[3] = { x + 10.0f, x + width * 0.38f, x + width * 0.66f };
    float y = top + 92.0f;
    const char *heads[3] = { "AÇÃO", "TECLADO E MOUSE", "CONTROLE" };
    for (int c = 0; c < 3; c++) text_at(heads[c], col[c], y, 16, fade(ACCENT, alpha * 0.9f));
    y += 28.0f;
    for (int r = 0; r < rows; r++) {
        if (r % 2 == 0) DrawRectangle((int)x, (int)(y - 4.0f), (int)width, (int)line, fade((Color){ 255, 255, 255, 10 }, alpha));
        text_at(CONTROLS[r][0], col[0], y, 20, fade(TEXT, alpha));
        text_at(CONTROLS[r][1], col[1], y, 20, fade(m->gamepad ? DIM : TEXT, alpha));
        text_at(CONTROLS[r][2], col[2], y, 20, fade(m->gamepad ? TEXT : DIM, alpha));
        y += line;
    }
    for (int i = 0; i < n; i++) draw_button(&items[i], m->glow[i], alpha, true);
    draw_hint(m, "Esc volta", "B volta", alpha);
}

void menu_draw(const Menu *m, const World *w, const Settings *settings, const MenuView *view)
{
    Item items[MENU_MAX_ITEMS];
    int n = build_items(m->screen, items);
    switch (m->screen) {
    case SCREEN_TITLE:    if (n > 0) draw_title(m, view, items, n); break;
    case SCREEN_PAUSE:    if (n > 0) draw_pause(m, w, items, n); break;
    case SCREEN_OPTIONS:  if (n > 0) draw_options(m, settings, items, n); break;
    case SCREEN_CONTROLS: if (n > 0) draw_controls(m, items, n); break;
    default: break;
    }
}
