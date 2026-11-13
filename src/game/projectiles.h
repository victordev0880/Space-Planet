// Projéteis: os tiros da nave, os tiros de plasma dos inimigos e as bombas.
//
// Este módulo só guarda e move os projéteis. O que acontece quando eles
// acertam alguma coisa (dano, crateras, pontos) é decidido em game/combat.c,
// que confere o trecho percorrido em cada passo (de pos_prev a pos): assim
// um tiro rápido não atravessa um alvo entre uma conferência e outra.
#ifndef GAME_PROJECTILES_H
#define GAME_PROJECTILES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/pool.h"
#include "core/vmath.h"

#define PROJECTILES_CAPACITY 256

typedef enum {
    SHOT_BOLT,     // tiro da nave do jogador
    SHOT_PLASMA,   // tiro de um inimigo
    SHOT_BOMB,     // bomba de um bombardeiro: cai no alvo marcado e pode ser derrubada
} ShotKind;

// De quem é um SHOT_BOLT. Só os do jogador quebram rochas: um aliado (ou uma
// torre) não pode destruir o meteoro que o jogador queria rebocar.
enum { BOLT_PLAYER = 0, BOLT_ALLY = 1 };

typedef struct {
    Vector3 pos;
    Vector3 pos_prev;
    Vector3 vel;
    Vector3 target;          // bomba: ponto do solo onde vai cair
    float   age;             // segundos desde o disparo
    float   life;            // segundos até sumir sozinho
    float   damage;          // dano em quem for atingido
    float   hp;              // bomba: dano que ainda aguenta
    float   accel;           // bomba: aceleração em direção ao alvo
    float   crater_radius;   // cratera ao bater no planeta (0 = não cava)
    float   crater_depth;
    uint8_t kind;            // ShotKind
    uint8_t source;          // tipo do inimigo que disparou (EnemyType); nos tiros SHOT_BOLT, BOLT_PLAYER ou BOLT_ALLY
    uint8_t pierce;          // tiro da nave: inimigos que ainda pode atravessar (evolução)
    int16_t last_hit;        // ... e o último que atravessou (-1 = nenhum), para não acertá-lo de novo
} Shot;

typedef struct {
    Pool  pool;
    Shot *items;   // array paralelo ao pool
} Projectiles;

bool projectiles_init(Projectiles *p, Arena *arena);
void projectiles_clear(Projectiles *p);

// Devolve um projétil novo, já posicionado, para completar; ou NULL se não há vaga.
Shot *projectiles_spawn(Projectiles *p, ShotKind kind, Vector3 pos, Vector3 vel, float life);
void  projectiles_remove(Projectiles *p, int index);

// Move todos um passo e remove os que chegaram ao fim da vida.
void projectiles_move(Projectiles *p, float dt);

int projectiles_count(const Projectiles *p, ShotKind kind);

#endif
