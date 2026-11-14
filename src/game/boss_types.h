// Os bosses. Nas hordas (data/waves.txt) um boss entra como um grupo de tipo
// especial: o valor do tipo é WAVE_BOSS_BASE + BossType.
#ifndef GAME_BOSS_TYPES_H
#define GAME_BOSS_TYPES_H

#include "game/enemy_types.h"

typedef enum {
    BOSS_BREAKER,     // Rompe-Mundos: canhão que corta uma fatia do planeta
    BOSS_HIVE,        // Colmeia Lunar: agarrada à Lua, solta drones e empurra a Lua
    BOSS_WORM,        // Verme do Núcleo: cava túneis dentro do planeta e salta para fora
    BOSS_TYPE_COUNT
} BossType;

#define WAVE_BOSS_BASE ENEMY_TYPE_COUNT

#endif
