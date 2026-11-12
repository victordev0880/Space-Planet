// Leitor de arquivos de dados no formato "chave = valor".
//
//   # comentário
//   stars.count = 3000
//   nome = texto livre
//
// Tudo fica em arrays de tamanho fixo: não há alocação dinâmica, então pode
// ser recarregado durante o jogo (tecla F5) sem risco.
#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <stdbool.h>

#define CONFIG_MAX_ENTRIES 256
#define CONFIG_KEY_LEN     48
#define CONFIG_VALUE_LEN   64
#define CONFIG_ERROR_LEN   160

typedef struct {
    char   key[CONFIG_KEY_LEN];
    char   text[CONFIG_VALUE_LEN];
    double number;      // válido só quando is_number é true
    bool   is_number;
} ConfigEntry;

typedef struct {
    ConfigEntry entries[CONFIG_MAX_ENTRIES];
    int  count;
    int  error_count;
    char error[CONFIG_ERROR_LEN];   // primeira mensagem de erro, com o número da linha
} Config;

void config_clear(Config *c);

// Substituem o conteúdo de `c`. Retornam false se houve algum erro; nesse caso
// c->error descreve o primeiro problema encontrado.
bool config_parse(Config *c, const char *text);
bool config_load_file(Config *c, const char *path);

const ConfigEntry *config_find(const Config *c, const char *key);   // NULL se não existe

#endif
