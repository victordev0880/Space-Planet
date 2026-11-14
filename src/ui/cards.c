#include "raylib.h"

#include "ui/cards.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/vmath.h"
#include "ui/font.h"
#include "ui/hud.h"

// Marco 12 (pedido do Victor: "opções com menos texto e mais simples"): cada
// carta é um ícone grande, o nome e uma linha só do que ela dá agora. Os
// detalhes (raios, alcances) ficaram nos dados.
#define CARD_W      232
#define CARD_H      292
#define CARD_GAP    26
#define CARD_LIFT   10       // a carta sob o mouse sobe este tanto
#define OPEN_TIME   0.26     // segundos da animação de entrada de cada carta
#define OPEN_STAGGER 0.07    // ... e o atraso entre uma carta e a seguinte
#define TITLE_SIZE  44
#define NAME_SIZE   24
#define TEXT_SIZE   18
#define SMALL_SIZE  14
#define ICON_R      44.0f    // raio do medalhão do ícone

static const Color PANEL     = { 12, 14, 24, 236 };
static const Color TEXT      = { 232, 236, 246, 255 };
static const Color DIM       = { 140, 148, 170, 255 };
static const Color HIGHLIGHT = { 255, 214, 110, 255 };
static const Color SYNERGY   = { 255, 196, 64, 255 };   // ouro: as cartas de sinergia

static Color alpha_of(Color c, float a)
{
    c.a = (unsigned char)(Clamp(a, 0.0f, 1.0f) * (float)c.a);
    return c;
}

static Color lighter(Color c, float u)
{
    return (Color){ (unsigned char)Lerp((float)c.r, 255.0f, u), (unsigned char)Lerp((float)c.g, 255.0f, u),
                    (unsigned char)Lerp((float)c.b, 255.0f, u), c.a };
}

static void shadowed(const char *s, int x, int y, int size, Color c)
{
    ui_draw_text(s, x + 2, y + 2, size, alpha_of((Color){ 0, 0, 0, 220 }, (float)c.a / 255.0f));
    ui_draw_text(s, x, y, size, c);
}

static void centered(const char *s, int cx, int y, int size, Color c)
{
    shadowed(s, cx - ui_measure_text(s, size) / 2, y, size, c);
}

static Rectangle card_rect(int index, int count)
{
    int total = count * CARD_W + (count - 1) * CARD_GAP;
    int x0 = (GetScreenWidth() - total) / 2;
    int y0 = GetScreenHeight() / 2 - CARD_H / 2 + 10;
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

const char *cards_effect(const Tuning *t, EvolutionId id, int rank)
{
    static char out[96];
    (void)rank;
    switch (id) {
    case EVO_ESCORT:        snprintf(out, sizeof out, "+1 drone que atira junto"); break;
    case EVO_SPREAD:        snprintf(out, sizeof out, "+2 tiros em leque"); break;
    case EVO_BLAST_DASH:    snprintf(out, sizeof out, "O dash explode (+%.0f)", (double)t->blast_dash_damage); break;
    case EVO_CHAIN:         snprintf(out, sizeof out, "Abatidos explodem (+%.0f)", (double)t->chain_damage); break;
    case EVO_HEAVY_HULL:    snprintf(out, sizeof out, "+%.0f de casco", (double)t->heavy_hull_hull); break;
    case EVO_WARHEADS:      snprintf(out, sizeof out, "Tiros ferem em volta"); break;
    case EVO_PIERCE:        snprintf(out, sizeof out, "Tiros atravessam +1"); break;
    case EVO_ARMOR_BREAK:   snprintf(out, sizeof out, "+%.0f%% nos grandes", (double)(t->armor_break_bonus * 100.0f)); break;
    case EVO_LIFESTEAL:     snprintf(out, sizeof out, "Abate cura %.0f de casco", (double)t->lifesteal_hull); break;
    case EVO_ENERGY_SIPHON: snprintf(out, sizeof out, "Acerto dá energia"); break;
    case EVO_CRIT:          snprintf(out, sizeof out, "+%.0f%% de crítico", (double)(t->crit_chance * 100.0f)); break;
    case EVO_LONG_RANGE:    snprintf(out, sizeof out, "+%.0f%% de alcance", (double)(t->long_range_speed * 100.0f)); break;
    case EVO_BIG_SHIELD:    snprintf(out, sizeof out, "+%.0f de escudo", (double)t->big_shield_shield); break;
    case EVO_REFLECT:       snprintf(out, sizeof out, "Escudo devolve tiros"); break;
    case EVO_FIGHTERS:      snprintf(out, sizeof out, "+1 caça aliado"); break;
    case EVO_HANGAR:        snprintf(out, sizeof out, "Aliados mais rápidos"); break;
    case EVO_SYN_SUICIDE:   snprintf(out, sizeof out, "Drones caçam e explodem"); break;
    case EVO_SYN_RESTORE:   snprintf(out, sizeof out, "Minerar conserta o planeta"); break;
    case EVO_SYN_SALVAGE:   snprintf(out, sizeof out, "Abates deixam minério"); break;
    case EVO_SYN_POISE:     snprintf(out, sizeof out, "Escudo cheio: dano x%.1f", (double)t->poise_mult); break;
    default:                out[0] = '\0'; break;
    }
    return out;
}

// Maiúsculas, inclusive as letras acentuadas (UTF-8: em "Ã" = C3 83 e "ã" =
// C3 A3, a minúscula é a maiúscula + 0x20 no segundo byte, de à a þ, menos ÷).
static void upper(char *s)
{
    for (unsigned char *c = (unsigned char *)s; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (unsigned char)(*c - 'a' + 'A');
        } else if (*c == 0xC3 && c[1] >= 0xA0 && c[1] <= 0xBE && c[1] != 0xB7) {
            c[1] = (unsigned char)(c[1] - 0x20);
            c++;
        }
    }
}

// --- ícones -------------------------------------------------------------------
// Desenhados com formas simples, em volta de (x, y), num quadrado de lado ~2r.

static Vector2 V2(float x, float y) { return (Vector2){ x, y }; }

static void ship_glyph(float x, float y, float s, Color c)
{
    DrawTriangle(V2(x, y - s), V2(x - s * 0.8f, y + s * 0.7f), V2(x + s * 0.8f, y + s * 0.7f), c);
}

static void burst(float x, float y, float r, int rays, Color c)
{
    for (int k = 0; k < rays; k++) {
        float a = (float)k / (float)rays * 2.0f * PI;
        DrawLineEx(V2(x + cosf(a) * r * 0.45f, y + sinf(a) * r * 0.45f), V2(x + cosf(a) * r, y + sinf(a) * r), 3.0f, c);
    }
}

static void draw_icon(EvolutionId id, float x, float y, float r, Color c, float now)
{
    Color soft = alpha_of(c, 0.55f);
    switch (id) {
    case EVO_ESCORT:   // a nave e dois drones em órbita
        ship_glyph(x, y, r * 0.35f, c);
        for (int k = 0; k < 2; k++) {
            float a = now * 1.6f + (float)k * PI;
            DrawCircleV(V2(x + cosf(a) * r * 0.7f, y + sinf(a) * r * 0.45f), r * 0.12f, c);
        }
        DrawEllipseLines((int)x, (int)y, r * 0.7f, r * 0.45f, soft);
        break;
    case EVO_SPREAD:   // três tiros em leque
        for (int k = -1; k <= 1; k++) {
            float a = -PI * 0.5f + (float)k * 0.38f;
            DrawLineEx(V2(x, y + r * 0.55f), V2(x + cosf(a) * r * 0.85f, y + r * 0.55f + sinf(a) * r * 1.1f), 5.0f, c);
        }
        break;
    case EVO_BLAST_DASH:   // seta com explosão na ponta
        DrawLineEx(V2(x - r * 0.75f, y + r * 0.3f), V2(x + r * 0.1f, y - r * 0.05f), 6.0f, soft);
        burst(x + r * 0.35f, y - r * 0.15f, r * 0.5f, 8, c);
        break;
    case EVO_CHAIN:   // três explosões ligadas
        for (int k = 0; k < 3; k++) {
            float px = x + ((float)k - 1.0f) * r * 0.62f, py = y + ((k == 1) ? -r * 0.3f : r * 0.25f);
            burst(px, py, r * 0.3f, 6, c);
            if (k < 2) DrawLineEx(V2(px, py), V2(x + (float)k * r * 0.62f, y + ((k == 0) ? -r * 0.3f : r * 0.25f)), 2.0f, soft);
        }
        break;
    case EVO_HEAVY_HULL:   // placa de blindagem
        DrawPoly(V2(x, y), 6, r * 0.75f, 30.0f, soft);
        DrawPolyLinesEx(V2(x, y), 6, r * 0.75f, 30.0f, 4.0f, c);
        DrawLineEx(V2(x - r * 0.4f, y), V2(x + r * 0.4f, y), 4.0f, c);
        break;
    case EVO_WARHEADS:   // tiro e anel de explosão
        DrawCircleV(V2(x, y), r * 0.22f, c);
        DrawRing(V2(x, y), r * 0.55f, r * 0.68f, 0.0f, 360.0f, 32, soft);
        burst(x, y, r * 0.9f, 8, alpha_of(c, 0.7f));
        break;
    case EVO_PIERCE:   // flecha atravessando duas barras
        DrawRectangleRec((Rectangle){ x - r * 0.35f, y - r * 0.6f, r * 0.14f, r * 1.2f }, soft);
        DrawRectangleRec((Rectangle){ x + r * 0.2f, y - r * 0.6f, r * 0.14f, r * 1.2f }, soft);
        DrawLineEx(V2(x - r * 0.85f, y), V2(x + r * 0.6f, y), 5.0f, c);
        DrawTriangle(V2(x + r * 0.9f, y), V2(x + r * 0.55f, y - r * 0.22f), V2(x + r * 0.55f, y + r * 0.22f), c);
        break;
    case EVO_ARMOR_BREAK:   // placa rachada
        DrawRectangleLinesEx((Rectangle){ x - r * 0.6f, y - r * 0.6f, r * 1.2f, r * 1.2f }, 4.0f, c);
        DrawLineEx(V2(x - r * 0.1f, y - r * 0.6f), V2(x + r * 0.1f, y - r * 0.1f), 3.0f, c);
        DrawLineEx(V2(x + r * 0.1f, y - r * 0.1f), V2(x - r * 0.15f, y + r * 0.2f), 3.0f, c);
        DrawLineEx(V2(x - r * 0.15f, y + r * 0.2f), V2(x + r * 0.05f, y + r * 0.6f), 3.0f, c);
        break;
    case EVO_LIFESTEAL:   // cruz de cura
        DrawRectangleRec((Rectangle){ x - r * 0.18f, y - r * 0.65f, r * 0.36f, r * 1.3f }, c);
        DrawRectangleRec((Rectangle){ x - r * 0.65f, y - r * 0.18f, r * 1.3f, r * 0.36f }, c);
        break;
    case EVO_ENERGY_SIPHON: {   // raio de energia
        Vector2 bolt[] = { V2(x + r * 0.15f, y - r * 0.8f), V2(x - r * 0.35f, y + r * 0.1f), V2(x, y + r * 0.1f),
                           V2(x - r * 0.15f, y + r * 0.8f), V2(x + r * 0.4f, y - r * 0.15f), V2(x + r * 0.05f, y - r * 0.15f) };
        for (int k = 0; k < 6; k++) DrawLineEx(bolt[k], bolt[(k + 1) % 6], 4.0f, c);
        break;
    }
    case EVO_CRIT:   // mira com brilho
        DrawRing(V2(x, y), r * 0.45f, r * 0.55f, 0.0f, 360.0f, 32, c);
        for (int k = 0; k < 4; k++) {
            float a = (float)k * PI * 0.5f;
            DrawLineEx(V2(x + cosf(a) * r * 0.6f, y + sinf(a) * r * 0.6f), V2(x + cosf(a) * r * 0.9f, y + sinf(a) * r * 0.9f), 4.0f, c);
        }
        DrawPoly(V2(x, y), 4, r * 0.2f, now * 90.0f, lighter(c, 0.5f));
        break;
    case EVO_LONG_RANGE:   // tiro longo até uma mira distante
        DrawLineEx(V2(x - r * 0.85f, y + r * 0.5f), V2(x + r * 0.45f, y - r * 0.3f), 4.0f, c);
        DrawRing(V2(x + r * 0.55f, y - r * 0.38f), r * 0.2f, r * 0.28f, 0.0f, 360.0f, 24, c);
        break;
    case EVO_BIG_SHIELD:   // arco de escudo diante da nave
        ship_glyph(x, y + r * 0.25f, r * 0.3f, soft);
        DrawRing(V2(x, y + r * 0.2f), r * 0.65f, r * 0.82f, 200.0f, 340.0f, 24, c);
        break;
    case EVO_REFLECT:   // escudo devolvendo um tiro
        DrawRing(V2(x - r * 0.25f, y), r * 0.6f, r * 0.75f, -60.0f, 60.0f, 16, c);
        DrawLineEx(V2(x + r * 0.9f, y - r * 0.6f), V2(x + r * 0.42f, y), 4.0f, soft);
        DrawLineEx(V2(x + r * 0.42f, y), V2(x + r * 0.9f, y + r * 0.6f), 4.0f, c);
        break;
    case EVO_FIGHTERS:   // dois caças
        ship_glyph(x - r * 0.38f, y + r * 0.15f, r * 0.3f, c);
        ship_glyph(x + r * 0.38f, y - r * 0.15f, r * 0.3f, c);
        break;
    case EVO_HANGAR:   // caça com linhas de velocidade
        ship_glyph(x + r * 0.15f, y, r * 0.38f, c);
        for (int k = -1; k <= 1; k++) {
            DrawLineEx(V2(x - r * 0.85f, y + (float)k * r * 0.25f), V2(x - r * 0.35f, y + (float)k * r * 0.25f), 3.0f, soft);
        }
        break;
    case EVO_SYN_SUICIDE:   // drone com rastro e explosão
        DrawLineEx(V2(x - r * 0.8f, y + r * 0.45f), V2(x, y), 4.0f, soft);
        burst(x + r * 0.25f, y - r * 0.15f, r * 0.55f, 8, c);
        break;
    case EVO_SYN_RESTORE:   // rocha virando terra (seta para o chão)
        DrawPoly(V2(x, y - r * 0.35f), 5, r * 0.3f, 10.0f, soft);
        DrawLineEx(V2(x, y - r * 0.05f), V2(x, y + r * 0.45f), 4.0f, c);
        DrawTriangle(V2(x, y + r * 0.7f), V2(x - r * 0.2f, y + r * 0.4f), V2(x + r * 0.2f, y + r * 0.4f), c);
        DrawRing(V2(x, y + r * 1.6f), r * 0.85f, r * 0.95f, 225.0f, 315.0f, 16, c);
        break;
    case EVO_SYN_SALVAGE:   // cristais de minério
        for (int k = 0; k < 3; k++) {
            float px = x + ((float)k - 1.0f) * r * 0.45f, py = y + ((k == 1) ? -r * 0.15f : r * 0.2f);
            DrawPoly(V2(px, py), 4, r * 0.28f, 45.0f, (k == 1) ? c : soft);
        }
        break;
    case EVO_SYN_POISE:   // escudo cheio e mira
        DrawRing(V2(x, y), r * 0.7f, r * 0.82f, 0.0f, 360.0f, 32, soft);
        DrawRing(V2(x, y), r * 0.25f, r * 0.33f, 0.0f, 360.0f, 24, c);
        DrawLineEx(V2(x - r * 0.5f, y), V2(x + r * 0.5f, y), 3.0f, c);
        DrawLineEx(V2(x, y - r * 0.5f), V2(x, y + r * 0.5f), 3.0f, c);
        break;
    default:
        break;
    }
}

// --- cartas ---------------------------------------------------------------------

static void card_key(int x, int y, int index, float a)
{
    if (hud_gamepad()) {
        hud_dpad_arrow(x + CARD_W - 32, y + 12, 18, index, alpha_of(TEXT, a));   // a seta do direcional
    } else {
        const char *key = TextFormat("%d", index + 1);
        Rectangle box = { (float)(x + CARD_W - 34), (float)(y + 10), 24.0f, 24.0f };
        DrawRectangleRoundedLines(box, 0.3f, 6, alpha_of(DIM, a));
        centered(key, x + CARD_W - 22, y + 13, SMALL_SIZE + 4, alpha_of(TEXT, a));
    }
}

static void draw_card(const World *w, const Tuning *t, int index, float appear, bool hover, float now)
{
    int id = w->evo.choices[index];
    if (id < 0) return;
    const EvolutionInfo *info = evolution_info((EvolutionId)id);
    bool synergy = info->synergy;
    Color line = synergy ? SYNERGY : hud_enemy_color(info->line);
    int next = w->evo.rank[id] + 1;
    int max = t->evo_max[id];

    Rectangle r = card_rect(index, w->evo.choice_count);
    float ease = 1.0f - (1.0f - appear) * (1.0f - appear);
    r.y += (1.0f - ease) * 60.0f - (hover ? (float)CARD_LIFT : 0.0f);
    float a = ease;
    int x = (int)r.x, y = (int)r.y, cx = x + CARD_W / 2;

    // Fundo, com a cor da linha subindo de baixo e a borda acesa.
    DrawRectangleRounded(r, 0.08f, 8, alpha_of(PANEL, a));
    DrawRectangleGradientV(x + 4, y + CARD_H / 2, CARD_W - 8, CARD_H / 2 - 6, alpha_of((Color){ line.r, line.g, line.b, 0 }, a),
                           alpha_of((Color){ line.r, line.g, line.b, 60 }, a));
    float pulse = synergy ? 0.6f + 0.4f * sinf(now * 4.0f) : 1.0f;
    DrawRectangleRoundedLinesEx(r, 0.08f, 8, hover ? 3.5f : 2.0f, alpha_of(line, a * (hover ? 1.0f : 0.75f * pulse)));

    // A linha (ou "SINERGIA") no alto, e a tecla.
    char header[48];
    snprintf(header, sizeof header, "%s", synergy ? "sinergia" : hud_enemy_name(info->line));
    upper(header);
    shadowed(header, x + 16, y + 15, SMALL_SIZE + 2, alpha_of(line, a));
    card_key(x, y, index, a);

    // O ícone, num medalhão.
    float iy = (float)y + 112.0f;
    DrawCircleV(V2((float)cx, iy), ICON_R + 8.0f, alpha_of((Color){ line.r, line.g, line.b, 26 }, a));
    DrawCircleV(V2((float)cx, iy), ICON_R, alpha_of((Color){ 6, 8, 16, 255 }, a));
    DrawRing(V2((float)cx, iy), ICON_R, ICON_R + 2.5f, 0.0f, 360.0f, 48, alpha_of(line, a));
    draw_icon((EvolutionId)id, (float)cx, iy, ICON_R * 0.72f, alpha_of(lighter(line, 0.15f), a), now);

    // Nome e a única linha de efeito.
    centered(info->name, cx, y + 178, NAME_SIZE, alpha_of(TEXT, a));
    centered(cards_effect(t, (EvolutionId)id, next), cx, y + 212, TEXT_SIZE, alpha_of(lighter(line, 0.35f), a));

    // Níveis: um tracinho por nível; o novo, aceso.
    if (!synergy && max > 1) {
        int pip = 18, gap = 6;
        int width = max * pip + (max - 1) * gap;
        for (int k = 0; k < max; k++) {
            Rectangle box = { (float)(cx - width / 2 + k * (pip + gap)), (float)(y + CARD_H - 30), (float)pip, 5.0f };
            Color fill = (k < next - 1) ? alpha_of(line, a * 0.8f) : (k == next - 1) ? alpha_of(HIGHLIGHT, a) : alpha_of(DIM, a * 0.35f);
            DrawRectangleRec(box, fill);
        }
    } else if (next == 1) {
        centered("NOVA", cx, y + CARD_H - 36, SMALL_SIZE, alpha_of(HIGHLIGHT, a));
    }
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
    centered(TextFormat("NÍVEL %d", w->evo.level), sw / 2, top - 96, TITLE_SIZE, alpha_of(HIGHLIGHT, title));
    centered(hud_keys("escolha uma  (1, 2, 3 ou clique)", "escolha uma  (direcional)"), sw / 2, top - 44, TEXT_SIZE,
             alpha_of(DIM, title));

    int hover = cards_hit(w, GetMousePosition());
    for (int i = 0; i < w->evo.choice_count; i++) {
        float appear = Clamp((since - (float)(OPEN_STAGGER * i)) / (float)OPEN_TIME, 0.0f, 1.0f);
        draw_card(w, t, i, appear, i == hover, (float)time);
    }
}
