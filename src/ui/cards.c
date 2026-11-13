#include "raylib.h"

#include "ui/cards.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vmath.h"
#include "ui/hud.h"

#define CARD_W      300
#define CARD_H      340
#define CARD_GAP    28
#define CARD_LIFT   8        // a carta sob o mouse sobe este tanto
#define OPEN_TIME   0.28     // segundos da animação de entrada de cada carta
#define OPEN_STAGGER 0.07    // ... e o atraso entre uma carta e a seguinte
#define TITLE_SIZE  44
#define NAME_SIZE   24
#define TEXT_SIZE   18
#define SMALL_SIZE  16

static const Color PANEL = { 16, 18, 30, 238 };
static const Color TEXT  = { 228, 232, 242, 255 };
static const Color DIM   = { 150, 158, 178, 255 };
static const Color HIGHLIGHT  = { 255, 214, 110, 255 };

static Color alpha_of(Color c, float a)
{
    c.a = (unsigned char)(Clamp(a, 0.0f, 1.0f) * (float)c.a);
    return c;
}

static void shadowed(const char *s, int x, int y, int size, Color c)
{
    DrawText(s, x + 2, y + 2, size, alpha_of((Color){ 0, 0, 0, 220 }, (float)c.a / 255.0f));
    DrawText(s, x, y, size, c);
}

static void centered(const char *s, int cx, int y, int size, Color c)
{
    shadowed(s, cx - MeasureText(s, size) / 2, y, size, c);
}

static Rectangle card_rect(int index, int count)
{
    int total = count * CARD_W + (count - 1) * CARD_GAP;
    int x0 = (GetScreenWidth() - total) / 2;
    int y0 = GetScreenHeight() / 2 - CARD_H / 2 + 20;
    return (Rectangle){ (float)(x0 + index * (CARD_W + CARD_GAP)), (float)y0, (float)CARD_W, (float)CARD_H };
}

int cards_hit(const World *w, Vector2 p)
{
    if (!w->evo.choosing) return -1;
    for (int i = 0; i < w->evo.choice_count; i++) {
        if (CheckCollisionPointRec(p, card_rect(i, w->evo.choice_count))) return i;
    }
    return -1;
}

const char *cards_describe(const Tuning *t, EvolutionId id, int rank)
{
    static char out[256];
    float r = (float)rank;
    switch (id) {
    case EVO_ESCORT:
        snprintf(out, sizeof out, "Um drone em órbita da nave\natira em quem chegar\na até %.0f de você.\n\nDrones: %d", (double)t->escort_range,
                 rank);
        break;
    case EVO_SPREAD:
        snprintf(out, sizeof out, "Cada disparo sai com\nmais %d tiros abertos\npara os lados (%.0f%% do dano).", rank * 2,
                 (double)(t->spread_damage * 100.0f));
        break;
    case EVO_BLAST_DASH:
        snprintf(out, sizeof out, "O dash explode em volta\nda nave: %.0f de dano\nnum raio de %.0f.", (double)(t->blast_dash_damage * r),
                 (double)t->blast_dash_radius);
        break;
    case EVO_CHAIN:
        snprintf(out, sizeof out, "Inimigos que você derruba\nexplodem: %.0f de dano\nem quem estiver a %.0f.", (double)(t->chain_damage * r),
                 (double)t->chain_radius);
        break;
    case EVO_HEAVY_HULL:
        snprintf(out, sizeof out, "+%.0f de casco,\njá consertado.\n\nCasco extra: %.0f", (double)t->heavy_hull_hull,
                 (double)(t->heavy_hull_hull * r));
        break;
    case EVO_WARHEADS:
        snprintf(out, sizeof out, "Cada acerto também fere\nquem está em volta:\n%.0f%% do dano num raio de %.0f.",
                 (double)(t->warheads_fraction * r * 100.0f), (double)t->warheads_radius);
        break;
    case EVO_PIERCE:
        snprintf(out, sizeof out, "Os tiros atravessam\n%d inimigo%s antes de parar.", rank, rank > 1 ? "s" : "");
        break;
    case EVO_ARMOR_BREAK:
        snprintf(out, sizeof out, "Ignora a blindagem\ndo Bombardeiro, e\n+%.0f%% de dano nos pesados.",
                 (double)(t->armor_break_bonus * r * 100.0f));
        break;
    case EVO_LIFESTEAL:
        snprintf(out, sizeof out, "Cada abate conserta\n%.0f de casco.", (double)(t->lifesteal_hull * r));
        break;
    case EVO_ENERGY_SIPHON:
        snprintf(out, sizeof out, "Cada acerto devolve\n%.0f de energia: mais\nboost e mais dash.", (double)(t->energy_siphon_energy * r));
        break;
    case EVO_CRIT:
        snprintf(out, sizeof out, "%.0f%% de chance de acerto\ncrítico (x%.0f de dano).\nDe longe, a chance dobra.",
                 (double)(t->crit_chance * r * 100.0f), (double)t->crit_mult);
        break;
    case EVO_LONG_RANGE:
        snprintf(out, sizeof out, "Tiros %.0f%% mais rápidos\ne de mais alcance;\najuda de mira maior.",
                 (double)(t->long_range_speed * r * 100.0f));
        break;
    case EVO_BIG_SHIELD:
        snprintf(out, sizeof out, "+%.0f de escudo e recarga\n%.0f%% mais rápida.", (double)t->big_shield_shield,
                 (double)(t->big_shield_regen * r * 100.0f));
        break;
    case EVO_REFLECT:
        snprintf(out, sizeof out, "Com escudo, %.0f%% de chance\nde devolver um tiro\ninimigo, com o dobro do dano.",
                 (double)(t->reflect_chance * r * 100.0f));
        break;
    case EVO_FIGHTERS:
        snprintf(out, sizeof out, "%d caça%s aliado%s caça%s\ninimigos até %.0f de\ndistância da nave.", rank, rank > 1 ? "s" : "",
                 rank > 1 ? "s" : "", rank > 1 ? "m" : "", (double)t->fighters_range);
        break;
    case EVO_HANGAR:
        snprintf(out, sizeof out, "+1 drone de escolta, e\nos aliados atiram %.0f%%\nmais rápido.", (double)(t->hangar_rate * r * 100.0f));
        break;
    default:
        out[0] = '\0';
        break;
    }
    return out;
}

// Texto de várias linhas (separadas por '\n'), uma embaixo da outra.
static void lines(const char *text, int x, int y, int size, int step, Color c)
{
    char line[96];
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (len > 0) shadowed(line, x, y, size, c);
        y += step;
        p += len;
        if (*p == '\n') p++;
    }
}

static void draw_card(const World *w, const Tuning *t, int index, float appear, bool hover)
{
    int id = w->evo.choices[index];
    if (id < 0) return;
    const EvolutionInfo *info = evolution_info((EvolutionId)id);
    Color line = hud_enemy_color(info->line);
    int next = w->evo.rank[id] + 1;
    int max = t->evo_max[id];

    Rectangle r = card_rect(index, w->evo.choice_count);
    float ease = 1.0f - (1.0f - appear) * (1.0f - appear);
    r.y += (1.0f - ease) * 60.0f - (hover ? (float)CARD_LIFT : 0.0f);
    float a = ease;
    int x = (int)r.x, y = (int)r.y, cx = x + CARD_W / 2;

    DrawRectangleRec(r, alpha_of(PANEL, a));
    DrawRectangle(x, y, CARD_W, 44, alpha_of((Color){ line.r, line.g, line.b, 70 }, a));
    DrawRectangleLinesEx(r, hover ? 4.0f : 2.0f, alpha_of(hover ? line : (Color){ line.r, line.g, line.b, 170 }, a));

    // Linha de origem e a tecla.
    char header[48];
    snprintf(header, sizeof header, "%s", hud_enemy_name(info->line));
    for (char *c = header; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
    }
    shadowed(header, x + 16, y + 13, SMALL_SIZE + 2, alpha_of(line, a));
    const char *key = TextFormat("[%d]", index + 1);
    shadowed(key, x + CARD_W - 16 - MeasureText(key, SMALL_SIZE + 2), y + 13, SMALL_SIZE + 2, alpha_of(TEXT, a));

    // Emblema: um círculo na cor da linha, com o nível novo dentro.
    int ey = y + 96;
    DrawCircle(cx, ey, 34.0f, alpha_of((Color){ line.r, line.g, line.b, 60 }, a));
    DrawCircleLines(cx, ey, 34.0f, alpha_of(line, a));
    DrawCircleLines(cx, ey, 28.0f, alpha_of((Color){ line.r, line.g, line.b, 120 }, a));
    centered(TextFormat("%d", next), cx, ey - 18, 36, alpha_of(TEXT, a));

    // Nome e nível.
    centered(info->name, cx, y + 146, NAME_SIZE, alpha_of(TEXT, a));
    const char *level = (next == 1) ? "NOVA" : TextFormat("NÍVEL %d", next);
    centered(level, cx, y + 176, SMALL_SIZE, alpha_of(next == 1 ? HIGHLIGHT : DIM, a));

    // Níveis: um quadradinho por nível, aceso até o novo.
    int pip = 14, pip_gap = 6;
    int pips_w = max * pip + (max - 1) * pip_gap;
    for (int k = 0; k < max; k++) {
        int px = cx - pips_w / 2 + k * (pip + pip_gap);
        Rectangle box = { (float)px, (float)(y + 198), (float)pip, (float)pip };
        if (k < next - 1)       DrawRectangleRec(box, alpha_of(line, a * 0.8f));
        else if (k == next - 1) DrawRectangleRec(box, alpha_of(HIGHLIGHT, a));
        DrawRectangleLinesEx(box, 1.0f, alpha_of(DIM, a));
    }

    lines(cards_describe(t, (EvolutionId)id, next), x + 22, y + 228, TEXT_SIZE, TEXT_SIZE + 5, alpha_of(TEXT, a * 0.92f));
}

void cards_draw(const World *w, const Tuning *t, double time)
{
    static bool was_open = false;
    static double opened = 0.0;
    if (!w->evo.choosing) {
        was_open = false;
        return;
    }
    if (!was_open) {
        opened = time;
        was_open = true;
    }
    float since = (float)(time - opened);
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, alpha_of((Color){ 4, 6, 14, 255 }, Clamp(since / 0.2f, 0.0f, 1.0f) * 0.62f));
    float title = Clamp(since / 0.2f, 0.0f, 1.0f);
    int top = (int)card_rect(0, 1).y;
    centered(TextFormat("NÍVEL %d", w->evo.level), sw / 2, top - 112, TITLE_SIZE, alpha_of(HIGHLIGHT, title));
    centered("escolha uma evolução  (1, 2, 3 ou clique)", sw / 2, top - 58, TEXT_SIZE, alpha_of(TEXT, title));

    int hover = cards_hit(w, GetMousePosition());
    for (int i = 0; i < w->evo.choice_count; i++) {
        float appear = Clamp((since - (float)(OPEN_STAGGER * i)) / (float)OPEN_TIME, 0.0f, 1.0f);
        draw_card(w, t, i, appear, i == hover);
    }

    // Essências: por que essas cartas apareceram.
    int y = top + CARD_H + 40;
    int counts[ENEMY_TYPE_COUNT];
    for (int k = 0; k < ENEMY_TYPE_COUNT; k++) counts[k] = (int)floorf(w->evo.essence[k] + 0.5f);
    centered("ESSÊNCIAS (quanto mais de uma linha, mais cartas dela)", sw / 2, y, SMALL_SIZE, alpha_of(DIM, title));
    hud_type_list(counts, true, sw / 2, y + 24, TEXT_SIZE, title);
}
