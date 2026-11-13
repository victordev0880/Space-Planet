#include "core/settings.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *key;     // nome no arquivo
    const char *label;   // nome na tela
    bool        toggle;  // sim/não
    float       min, max, step;
    bool        percent; // mostra como porcentagem (senão, como multiplicador)
    float       fallback;
} SettingDef;

static const SettingDef DEFS[SET_COUNT] = {
    [SET_VOLUME]     = { "volume",     "Volume geral",               false, 0.0f,  1.0f, 0.10f, true,  0.8f },
    [SET_SFX]        = { "sfx",        "Efeitos sonoros",            false, 0.0f,  1.0f, 0.10f, true,  1.0f },
    [SET_MUSIC]      = { "music",      "Música",                     false, 0.0f,  1.0f, 0.10f, true,  0.7f },
    [SET_MOUSE]      = { "mouse",      "Sensibilidade do mouse",     false, 0.25f, 3.0f, 0.25f, false, 1.0f },
    [SET_STICK]      = { "stick",      "Sensibilidade do analógico", false, 0.25f, 3.0f, 0.25f, false, 1.0f },
    [SET_INVERT_Y]   = { "invert_y",   "Inverter o eixo vertical",   true,  0.0f,  1.0f, 1.0f,  false, 0.0f },
    [SET_SHAKE]      = { "shake",      "Tremor da câmera",           false, 0.0f,  1.0f, 0.10f, true,  1.0f },
    [SET_FULLSCREEN] = { "fullscreen", "Tela cheia",                 true,  0.0f,  1.0f, 1.0f,  false, 0.0f },
    [SET_VIBRATION]  = { "vibration",  "Vibração do controle",       true,  0.0f,  1.0f, 1.0f,  false, 1.0f },
};

static float *number_of(Settings *s, SettingId id)
{
    switch (id) {
    case SET_VOLUME: return &s->volume;
    case SET_SFX:    return &s->sfx;
    case SET_MUSIC:  return &s->music;
    case SET_MOUSE:  return &s->mouse;
    case SET_STICK:  return &s->stick;
    case SET_SHAKE:  return &s->shake;
    default:         return NULL;
    }
}

static bool *flag_of(Settings *s, SettingId id)
{
    switch (id) {
    case SET_INVERT_Y:   return &s->invert_y;
    case SET_FULLSCREEN: return &s->fullscreen;
    case SET_VIBRATION:  return &s->vibration;
    default:             return NULL;
    }
}

// Mesma coisa para leitura, sem tirar o const de quem chama.
static float get_value(const Settings *s, SettingId id)
{
    Settings copy = *s;
    if (DEFS[id].toggle) return *flag_of(&copy, id) ? 1.0f : 0.0f;
    return *number_of(&copy, id);
}

static void set_value(Settings *s, SettingId id, float value)
{
    const SettingDef *d = &DEFS[id];
    if (d->toggle) {
        *flag_of(s, id) = value >= 0.5f;
        return;
    }
    if (!(value == value)) value = d->fallback;   // NaN
    if (value < d->min) value = d->min;
    if (value > d->max) value = d->max;
    *number_of(s, id) = value;
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < SET_COUNT; i++) set_value(s, (SettingId)i, DEFS[i].fallback);
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

int settings_parse(Settings *s, const char *text)
{
    int applied = 0;
    char line[128];
    while (*text != '\0') {
        // Uma linha (cortada se for longa demais).
        size_t length = strcspn(text, "\r\n");
        size_t copy = (length < sizeof line - 1) ? length : sizeof line - 1;
        memcpy(line, text, copy);
        line[copy] = '\0';
        text += length;
        while (*text == '\r' || *text == '\n') text++;

        char *hash = strchr(line, '#');
        if (hash != NULL) *hash = '\0';
        char *equals = strchr(line, '=');
        if (equals == NULL) continue;
        *equals = '\0';
        char *key = trim(line);
        char *value = trim(equals + 1);

        char *end = NULL;
        float number = strtof(value, &end);
        if (end == value || *trim(end) != '\0') continue;   // não é um número
        for (int i = 0; i < SET_COUNT; i++) {
            if (strcmp(key, DEFS[i].key) == 0) {
                set_value(s, (SettingId)i, number);
                applied++;
                break;
            }
        }
    }
    return applied;
}

int settings_format(const Settings *s, char *out, size_t size)
{
    int used = snprintf(out, size, "# Preferências do Terra Fraturada (a tela de opções grava este arquivo)\n");
    if (used < 0 || (size_t)used >= size) return -1;
    for (int i = 0; i < SET_COUNT; i++) {
        float v = get_value(s, (SettingId)i);
        int n = DEFS[i].toggle ? snprintf(out + used, size - (size_t)used, "%s = %d\n", DEFS[i].key, v >= 0.5f ? 1 : 0)
                               : snprintf(out + used, size - (size_t)used, "%s = %.2f\n", DEFS[i].key, (double)v);
        if (n < 0 || (size_t)(used + n) >= size) return -1;
        used += n;
    }
    return used;
}

bool settings_load(Settings *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    char text[2048];
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    settings_parse(s, text);
    return true;
}

bool settings_save(const Settings *s, const char *path)
{
    char text[2048];
    int n = settings_format(s, text, sizeof text);
    if (n < 0) return false;
    FILE *f = fopen(path, "wb");
    if (f == NULL) return false;
    bool ok = fwrite(text, 1, (size_t)n, f) == (size_t)n;
    return (fclose(f) == 0) && ok;
}

const char *settings_label(SettingId id)
{
    return ((int)id >= 0 && id < SET_COUNT) ? DEFS[id].label : "";
}

bool settings_is_toggle(SettingId id)
{
    return ((int)id >= 0 && id < SET_COUNT) && DEFS[id].toggle;
}

float settings_fraction(const Settings *s, SettingId id)
{
    const SettingDef *d = &DEFS[id];
    return (get_value(s, id) - d->min) / (d->max - d->min);
}

void settings_set_fraction(Settings *s, SettingId id, float f)
{
    const SettingDef *d = &DEFS[id];
    float value = d->min + f * (d->max - d->min);
    // Arredonda para o passo: o slider anda em degraus, como as setas.
    value = d->min + roundf((value - d->min) / d->step) * d->step;
    set_value(s, id, value);
}

void settings_step(Settings *s, SettingId id, int direction)
{
    const SettingDef *d = &DEFS[id];
    if (d->toggle) {
        bool *flag = flag_of(s, id);
        *flag = !*flag;
        return;
    }
    float value = get_value(s, id) + (float)direction * d->step;
    value = d->min + roundf((value - d->min) / d->step) * d->step;   // sem erro acumulado
    set_value(s, id, value);
}

const char *settings_value_text(const Settings *s, SettingId id)
{
    static char text[16];
    const SettingDef *d = &DEFS[id];
    float v = get_value(s, id);
    if (d->toggle)       snprintf(text, sizeof text, "%s", v >= 0.5f ? "sim" : "não");
    else if (d->percent) snprintf(text, sizeof text, "%d%%", (int)lroundf(v * 100.0f));
    else                 snprintf(text, sizeof text, "%.2fx", (double)v);
    return text;
}
