// Rochas soltas no espaço: fragmentos arrancados do planeta e meteoros de
// passagem. São a matéria-prima do conserto: o raio trator da nave puxa uma
// rocha, ela vem a reboque, e quando toca o chão (solta, ou arrastada até
// ele) o planeta volta a ser o que era em volta do ponto onde ela bateu.
//
// Estados:
//   EJECT    fragmento saindo da cratera, subindo até a órbita;
//   ORBIT    fragmento em órbita (o cinturão de destroços);
//   FLYBY    meteoro de passagem, em linha reta (vai embora se ninguém pegar);
//   TOWED    presa ao raio trator, atrás da nave, por uma mola;
//   FALLING  solta: cai quase reto no planeta.
//
// Como os inimigos, as rochas não mexem no planeta: quando uma toca o chão,
// deixa um pedido de conserto que o mundo executa.
#ifndef GAME_ROCKS_H
#define GAME_ROCKS_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "core/vmath.h"
#include "game/events.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

#define ROCKS_MAX     32
#define ROCK_SHAPES   4     // desenhos diferentes
#define ROCK_DEPOSITS 4     // rochas que podem tocar o chão no mesmo passo

typedef enum { ROCK_FRAGMENT, ROCK_METEOR, ROCK_KIND_COUNT } RockKind;
typedef enum { ROCK_EJECT, ROCK_ORBIT, ROCK_FLYBY, ROCK_TOWED, ROCK_FALLING } RockState;

typedef struct {
    bool       alive;
    uint8_t    kind;        // RockKind
    uint8_t    state;       // RockState
    uint8_t    shape;       // desenho, de 0 a ROCK_SHAPES - 1
    uint8_t    material;    // fragmento: o material da superfície de onde saiu (a cor do "topo")
    Vector3    pos, pos_prev, vel;
    Quaternion rot, rot_prev;
    Vector3    spin;        // eixo de giro vezes a velocidade angular (rad/s)
    float      radius;      // tamanho
    float      fill;        // raio do conserto que ela faz ao tocar o chão
    float      age;
    Vector3    eject_from;  // fragmento: o ponto da cratera de onde saiu
    Vector3    orbit;       // fragmento: o ponto dele na órbita (gira em volta de axis)
    Vector3    axis;        // fragmento: eixo da órbita
    float      orbit_rate;  // fragmento: velocidade angular na órbita (rad/s)
} Rock;

// Uma rocha tocou o chão: o mundo conserta o planeta ali.
typedef struct {
    Vector3 pos;
    float   fill;
    uint8_t kind;
} RockDeposit;

typedef struct {
    Rock        items[ROCKS_MAX];
    int         towed;           // índice da rocha no raio trator; -1 = nenhuma
    RockDeposit deposits[ROCK_DEPOSITS];
    int         deposit_count;   // pedidos deste passo (o mundo esvazia)
    int         delivered;       // rochas que já tocaram o chão (estatística)
} Rocks;

typedef struct {
    const Planet *planet;
    const Ship   *ship;     // NULL = sem nave (a rocha a reboque se solta)
    Events       *events;
    const Tuning *t;
    uint32_t      tick;
    float         dt;
} RockContext;

void rocks_reset(Rocks *r);
int  rocks_alive(const Rocks *r);
int  rocks_count(const Rocks *r, RockKind kind);

// Um fragmento sai da cratera em `ground` e sobe até uma órbita sorteada.
// `material` é o da superfície arrancada (só cor). NULL se não há espaço.
Rock *rocks_spawn_fragment(Rocks *r, Rng *rng, const Tuning *t, float planet_radius, Vector3 ground, uint8_t material);

// Um meteoro em `pos`, voando com a velocidade `vel`. NULL se não há espaço.
Rock *rocks_spawn_meteor(Rocks *r, Rng *rng, const Tuning *t, Vector3 pos, Vector3 vel);

void rocks_tick(Rocks *r, const RockContext *ctx);

// Raio trator: a rocha que ele pegaria agora (à frente da nave, ao alcance),
// ou -1.
int  rocks_find_grab(const Rocks *r, const Ship *s, const Tuning *t);
void rocks_grab(Rocks *r, int index);
void rocks_release(Rocks *r);

// Peso da rocha a reboque, de 0 (nenhuma) a 1 (a maior possível).
float rocks_tow_load(const Rocks *r, const Tuning *t);

// A rocha a reboque, ou NULL.
const Rock *rocks_towed(const Rocks *r);

#endif
