// Preferências do jogador (tela de opções), gravadas em save/settings.txt no
// mesmo formato "chave = valor" dos arquivos de dados.
//
// Não fazem parte da simulação: volume, sensibilidade do mouse e tremor de
// câmera mudam o que o jogador sente e vê, nunca o resultado de uma partida
// (o autoplay ignora o arquivo e usa os valores padrão).
#ifndef CORE_SETTINGS_H
#define CORE_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    float volume;       // volume geral, 0 a 1
    float sfx;          // efeitos, 0 a 1
    float music;        // música, 0 a 1
    float mouse;        // multiplicador da sensibilidade do mouse, 0.25 a 3
    float stick;        // multiplicador da velocidade do analógico direito, 0.25 a 3
    bool  invert_y;     // mouse e analógico: para cima aponta o nariz para baixo
    float shake;        // intensidade do tremor de câmera, 0 a 1
    bool  fullscreen;   // tela cheia sem bordas
    bool  vibration;    // vibração do controle
} Settings;

// Uma opção da tela, na ordem em que aparece.
typedef enum {
    SET_VOLUME, SET_SFX, SET_MUSIC, SET_MOUSE, SET_STICK, SET_INVERT_Y, SET_SHAKE, SET_FULLSCREEN, SET_VIBRATION,
    SET_COUNT
} SettingId;

void settings_defaults(Settings *s);

// Lê o texto de um arquivo de preferências por cima de `s`. Chaves
// desconhecidas e valores inválidos são ignorados (o arquivo é do jogador:
// um erro nele não pode impedir o jogo de abrir). Retorna quantas chaves valeram.
int settings_parse(Settings *s, const char *text);

// Escreve as preferências em `out`. Retorna o tamanho, ou -1 se não coube.
int settings_format(const Settings *s, char *out, size_t size);

bool settings_load(Settings *s, const char *path);   // false se o arquivo não existe
bool settings_save(const Settings *s, const char *path);

// --- para a tela de opções ---------------------------------------------------
const char *settings_label(SettingId id);
bool        settings_is_toggle(SettingId id);
float       settings_fraction(const Settings *s, SettingId id);    // posição no slider, 0 a 1
void        settings_set_fraction(Settings *s, SettingId id, float f);
void        settings_step(Settings *s, SettingId id, int direction);   // -1 ou +1 (inverte os sim/não)
const char *settings_value_text(const Settings *s, SettingId id);     // "70%", "1.5x", "sim"...

#endif
