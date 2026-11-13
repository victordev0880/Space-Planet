#include "ui/font.h"

#include <math.h>

#define BODY_PATH   "assets/fonts/Rajdhani-SemiBold.ttf"
#define TITLE_PATH  "assets/fonts/Rajdhani-Bold.ttf"
#define FIRST_CHAR  32
#define CHAR_COUNT  224      // 32 a 255: ASCII e Latin-1 (os acentos do português)

// Na fonte padrão do raylib, as maiúsculas têm 70% do `size` e começam a
// 10% dele. A fonte nova é escalada para cobrir o mesmo espaço.
#define DEFAULT_CAP 0.70f
#define DEFAULT_TOP 0.10f
#define TITLE_FROM  34       // a partir deste `size`, a versão em negrito
#define SPACING     0.03f    // espaço extra entre letras, em fração do tamanho
#define MIN_SIZE    12       // abaixo disto a fonte suave fica ilegível (a padrão, pixelada, não)

typedef struct {
    Font  font;
    float cap;   // altura do "H", em fração do tamanho da fonte
    float top;   // distância do topo da linha ao topo do "H", idem
} Face;

// Tamanhos carregados (em pixels). Cada texto usa o menor que não precise ser
// ampliado: reduzir uma letra mantém a nitidez, ampliar a borra.
static const int BODY_SIZES[]  = { 18, 30, 46 };
static const int TITLE_SIZES[] = { 60, 120 };
#define BODY_COUNT  (int)(sizeof BODY_SIZES / sizeof BODY_SIZES[0])
#define TITLE_COUNT (int)(sizeof TITLE_SIZES / sizeof TITLE_SIZES[0])

static Face body[BODY_COUNT];
static Face title[TITLE_COUNT];
static bool loaded = false;

static bool load_face(Face *f, const char *path, int size)
{
    int codepoints[CHAR_COUNT];
    for (int i = 0; i < CHAR_COUNT; i++) codepoints[i] = FIRST_CHAR + i;
    f->font = LoadFontEx(path, size, codepoints, CHAR_COUNT);
    if (f->font.texture.id == 0 || f->font.glyphCount == 0) return false;
    SetTextureFilter(f->font.texture, TEXTURE_FILTER_BILINEAR);

    GlyphInfo h = GetGlyphInfo(f->font, 'H');
    float base = (float)f->font.baseSize;
    f->cap = (h.image.height > 0) ? (float)h.image.height / base : DEFAULT_CAP;
    f->top = (float)h.offsetY / base;
    return true;
}

bool ui_font_load(void)
{
    ui_font_unload();
    if (!FileExists(BODY_PATH) || !FileExists(TITLE_PATH)) {
        TraceLog(LOG_WARNING, "fonte nao encontrada em assets/fonts: usando a padrao");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < BODY_COUNT; i++) ok = ok && load_face(&body[i], BODY_PATH, BODY_SIZES[i]);
    for (int i = 0; i < TITLE_COUNT; i++) ok = ok && load_face(&title[i], TITLE_PATH, TITLE_SIZES[i]);
    loaded = ok;
    if (!ok) {
        TraceLog(LOG_WARNING, "fonte com problema: usando a padrao");
        ui_font_unload();
    }
    return ok;
}

void ui_font_unload(void)
{
    for (int i = 0; i < BODY_COUNT; i++) {
        if (body[i].font.texture.id != 0) UnloadFont(body[i].font);
        body[i] = (Face){ 0 };
    }
    for (int i = 0; i < TITLE_COUNT; i++) {
        if (title[i].font.texture.id != 0) UnloadFont(title[i].font);
        title[i] = (Face){ 0 };
    }
    loaded = false;
}

// A face e o tamanho em pixels para um `size` no sentido da fonte padrão.
static const Face *pick(int size, float *px)
{
    if (size < MIN_SIZE) size = MIN_SIZE;
    const Face *set = (size >= TITLE_FROM) ? title : body;
    int count = (size >= TITLE_FROM) ? TITLE_COUNT : BODY_COUNT;
    *px = (float)size * DEFAULT_CAP / set[0].cap;
    for (int i = 0; i < count; i++) {
        if ((float)set[i].font.baseSize >= *px * 0.95f) return &set[i];
    }
    return &set[count - 1];
}

void ui_draw_text(const char *text, int x, int y, int size, Color color)
{
    if (!loaded) {
        DrawText(text, x, y, size, color);
        return;
    }
    float px;
    const Face *f = pick(size, &px);
    float top = (float)y + (float)size * DEFAULT_TOP - f->top * px;
    DrawTextEx(f->font, text, (Vector2){ (float)x, roundf(top) }, px, px * SPACING, color);
}

int ui_measure_text(const char *text, int size)
{
    if (!loaded) return MeasureText(text, size);
    float px;
    const Face *f = pick(size, &px);
    return (int)ceilf(MeasureTextEx(f->font, text, px, px * SPACING).x);
}

void ui_draw_text_shadow(const char *text, int x, int y, int size, Color color, int shadow)
{
    if (shadow != 0) {
        Color dark = { 0, 0, 0, (unsigned char)(200.0f * (float)color.a / 255.0f) };
        ui_draw_text(text, x + shadow, y + shadow, size, dark);
    }
    ui_draw_text(text, x, y, size, color);
}
