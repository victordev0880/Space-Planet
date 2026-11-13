// Áudio: efeitos sonoros sintetizados em código.
//
// Não há arquivos de som: cada efeito é gerado na abertura do jogo a partir
// de ondas simples (seno, quadrada, dente de serra, ruído), envelopes de
// volume e filtros, e guardado como um Sound do raylib. Assim o jogo não
// depende de nenhum recurso externo, e um som é ajustado mexendo em poucos
// números.
//
// O áudio reage aos eventos do mundo, como o desenho: guarda até qual evento
// e qual impacto já tocou e, a cada quadro, toca os novos. Sons com posição
// ficam mais baixos com a distância e vão para o lado certo (pan). Avisos
// (bomba armando, horda chegando) têm volume mínimo: precisam ser ouvidos.
#ifndef AUDIO_AUDIO_H
#define AUDIO_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"

typedef enum {
    SND_SHOT, SND_ENEMY_SHOT, SND_HIT, SND_ARMOR, SND_WEAK,
    SND_KILL_SMALL, SND_KILL_BIG, SND_BOMB_ARM, SND_BOMB_DROP, SND_BOMB_DOWN,
    SND_IMPACT_SMALL, SND_IMPACT_BIG, SND_SHIELD, SND_HULL, SND_PLAYER_DOWN,
    SND_HORN, SND_CHIME, SND_WIN, SND_LOSE, SND_DASH, SND_BUMP, SND_WARP,
    SND_LOCK, SND_DETONATE, SND_DIG, SND_DRAIN, SND_CHARGE, SND_LANCE, SND_DEFLECT, SND_LAUNCH, SND_READY,
    SND_LEVEL_UP, SND_EVOLVE, SND_REFLECT, SND_BLAST, SND_CRIT, SND_ALLY_SHOT,
    SND_BOSS_ENTER, SND_BOSS_CHARGE, SND_BOSS_FIRE, SND_BOSS_CAPACITOR, SND_BOSS_STAGGER, SND_BOSS_PHASE,
    SND_BOSS_DOWN, SND_REWARD, SND_ABILITY,
    SND_METEOR, SND_FRAGMENT, SND_TRACTOR_GRAB, SND_TRACTOR_MISS, SND_TRACTOR_RELEASE, SND_TRACTOR_BREAK,
    SND_DEPOSIT, SND_REPAIR,
    SND_ROCK_SHOT, SND_ORE, SND_BUY, SND_DENY, SND_INTERCEPT, SND_SHIELD_BLOCK, SND_SHIELD_DOWN,
    SND_COUNT
} SoundId;

#define AUDIO_VOICES 6   // cópias de cada som, para ele poder tocar sobreposto

typedef struct {
    bool     ready;
    Sound    sounds[SND_COUNT];
    Sound    voices[SND_COUNT][AUDIO_VOICES];
    int      next_voice[SND_COUNT];
    uint32_t events_seen;
    uint32_t impacts_seen;
    uint32_t noise;     // gerador próprio (variações de tom), separado do da simulação
} Audio;

// Abre o dispositivo de som e gera os efeitos. Se não houver dispositivo, o
// jogo segue mudo.
void audio_init(Audio *a, const World *w, const Tuning *t, bool muted);
void audio_apply_tuning(Audio *a, const Tuning *t, bool muted);

// Toca os sons do que aconteceu desde o quadro anterior.
void audio_update(Audio *a, const World *w, const Tuning *t, Camera3D camera);

void audio_shutdown(Audio *a);

#endif
