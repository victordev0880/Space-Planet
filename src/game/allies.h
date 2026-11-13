// Aliados da nave, ganhos com a evolução:
//   - drones de escolta (linha do Enxame): orbitam a nave e atiram em quem
//     chega perto dela;
//   - caças (linha do Porta-drones): saem para caçar inimigos mais longe e
//     voltam para perto da nave quando não há o que caçar.
// Não levam dano (são parte da nave). Somem enquanto a nave está destruída e
// voltam com ela. Os tiros deles são tiros da nave (contam abates e pontos).
#ifndef GAME_ALLIES_H
#define GAME_ALLIES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "core/vmath.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/projectiles.h"
#include "game/ship.h"
#include "game/tuning.h"

#define ALLIES_MAX 8

typedef enum { ALLY_ESCORT, ALLY_FIGHTER } AllyKind;

typedef struct {
    Vector3    pos, vel;
    Quaternion rot;
    Vector3    pos_prev;
    Quaternion rot_prev;
    float      fire_wait;
    int        target;    // inimigo que está atacando (-1 = nenhum)
    uint8_t    kind;      // AllyKind
    bool       alive;
} Ally;

typedef struct {
    Ally items[ALLIES_MAX];
    int  escorts;    // quantos de cada tipo deveriam existir (pela evolução)
    int  fighters;
} Allies;

typedef struct {
    const Ship    *ship;
    const Planet  *planet;
    const Enemies *enemies;
    Projectiles   *shots;
    Events        *events;
    Rng           *rng;
    const Tuning  *t;
    uint32_t       tick;
    float          dt;
    float          rate_mult;   // cadência extra (Hangar rápido)
} AllyContext;

void allies_reset(Allies *a);

// Quantos aliados de cada tipo deve haver. Os novos nascem junto da nave.
void allies_set_count(Allies *a, const Ship *ship, int escorts, int fighters);

void allies_tick(Allies *a, const AllyContext *ctx);

int allies_alive(const Allies *a);

#endif
