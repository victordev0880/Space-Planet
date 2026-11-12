#include "core/config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 512

static void set_error(Config *c, int line, const char *fmt, ...)
{
    c->error_count++;
    if (c->error_count > 1) return;   // guarda só a primeira mensagem

    char msg[CONFIG_ERROR_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    if (line > 0) snprintf(c->error, sizeof c->error, "linha %d: %.120s", line, msg);
    else          snprintf(c->error, sizeof c->error, "%.150s", msg);
}

// Remove espaços das pontas, modificando a string no lugar.
static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static bool key_char_ok(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '-';
}

static void parse_line(Config *c, char *line, int line_no)
{
    // Alguns editores do Windows gravam uma marca invisível (BOM) no começo
    // do arquivo; sem pular, ela viraria parte da primeira linha.
    if (line_no == 1 && strncmp(line, "\xEF\xBB\xBF", 3) == 0) line += 3;

    char *hash = strchr(line, '#');
    if (hash != NULL) *hash = '\0';

    char *text = trim(line);
    if (*text == '\0') return;

    char *eq = strchr(text, '=');
    if (eq == NULL) {
        set_error(c, line_no, "falta '=' em \"%s\"", text);
        return;
    }
    *eq = '\0';
    char *key = trim(text);
    char *value = trim(eq + 1);

    if (*key == '\0') { set_error(c, line_no, "chave vazia"); return; }
    if (*value == '\0') { set_error(c, line_no, "valor vazio para \"%s\"", key); return; }
    if (strlen(key) >= CONFIG_KEY_LEN) { set_error(c, line_no, "chave longa demais: \"%s\"", key); return; }
    if (strlen(value) >= CONFIG_VALUE_LEN) { set_error(c, line_no, "valor longo demais em \"%s\"", key); return; }
    for (const char *p = key; *p != '\0'; p++) {
        if (!key_char_ok(*p)) { set_error(c, line_no, "caractere invalido na chave \"%s\"", key); return; }
    }
    if (config_find(c, key) != NULL) { set_error(c, line_no, "chave repetida: \"%s\"", key); return; }
    if (c->count >= CONFIG_MAX_ENTRIES) { set_error(c, line_no, "chaves demais (maximo %d)", CONFIG_MAX_ENTRIES); return; }

    ConfigEntry *e = &c->entries[c->count++];
    strcpy(e->key, key);
    strcpy(e->text, value);

    // É número se o strtod consumir o valor inteiro.
    char *end = NULL;
    e->number = strtod(value, &end);
    e->is_number = (end != value && *end == '\0');
    if (!e->is_number) e->number = 0.0;
}

void config_clear(Config *c)
{
    c->count = 0;
    c->error_count = 0;
    c->error[0] = '\0';
}

bool config_parse(Config *c, const char *text)
{
    config_clear(c);

    char line[LINE_MAX_LEN];
    int line_no = 0;
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        line_no++;
        if (len >= sizeof line) {
            set_error(c, line_no, "linha longa demais");
        } else {
            memcpy(line, p, len);
            line[len] = '\0';
            parse_line(c, line, line_no);
        }
        p += len;
        if (*p == '\n') p++;
    }
    return c->error_count == 0;
}

bool config_load_file(Config *c, const char *path)
{
    config_clear(c);

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        set_error(c, 0, "nao consegui abrir \"%s\"", path);
        return false;
    }

    char line[LINE_MAX_LEN];
    int line_no = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        line_no++;
        size_t len = strlen(line);
        bool complete = (len > 0 && line[len - 1] == '\n') || feof(f);
        if (!complete) {
            set_error(c, line_no, "linha longa demais");
            // Descarta o resto da linha para não interpretá-lo como outra.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') { }
            continue;
        }
        parse_line(c, line, line_no);
    }
    fclose(f);
    return c->error_count == 0;
}

const ConfigEntry *config_find(const Config *c, const char *key)
{
    for (int i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].key, key) == 0) return &c->entries[i];
    }
    return NULL;
}
