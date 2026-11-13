// Música sintetizada e adaptativa.
//
// Uma única peça em ré menor, a 112 batidas por minuto, tocada em camadas:
// pad (acordes), arpejo (sininhos com eco), baixo, bateria, metais (no boss)
// e um tique-taque de tensão (planeta em perigo). O clima do jogo decide
// quais camadas soam, e elas entram e saem devagar, sempre no compasso: a
// música nunca "corta", ela se transforma.
//
// Este arquivo é C puro (sem raylib): o audio.c entrega as amostras a um
// AudioStream, e a ferramenta --music-wav grava a mesma síntese num arquivo.
//
// music_set é chamado pela thread do jogo; music_render, pela thread de áudio.
// Só inteiros atravessam de uma para a outra (atômicos).
#ifndef AUDIO_MUSIC_H
#define AUDIO_MUSIC_H

#include <stdbool.h>

typedef enum {
    MOOD_SILENT,    // nada
    MOOD_TITLE,     // tela de título: pad e arpejo, calmo
    MOOD_CALM,      // calmaria entre hordas
    MOOD_BATTLE,    // horda em andamento: baixo e bateria
    MOOD_BOSS,      // boss: bateria cheia e metais
    MOOD_VICTORY,   // todas as hordas vencidas
    MOOD_DEFEAT,    // colapso planetário: só o pad, em tom sombrio
    MOOD_COUNT
} MusicMood;

#define MUSIC_SAMPLE_RATE 44100

void music_reset(void);

// danger: o planeta está perto do colapso (a harmonia fica tensa e entra o
// tique-taque). muffled: menu de pausa (a música fica abafada e mais baixa).
void music_set(MusicMood mood, bool danger, bool muffled);

// Preenche `frames` quadros estéreo intercalados (esquerda, direita), em [-1, 1].
void music_render(float *stereo, int frames);

#endif
