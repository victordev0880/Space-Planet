// Inimigos: quem são, onde estão e como pensam.
//
// Cada inimigo escolhe a cada passo uma "velocidade desejada" (para onde quer
// ir e com que pressa) e acelera em direção a ela, com um limite de
// aceleração (`accel` em data/enemies.txt). É o que dá curvas suaves e
// previsíveis, em vez de mudanças de direção instantâneas.
//
// Enxame. Chega em bando e ataca o planeta em passadas:
//   APPROACH  voa até o ponto acima do alvo do bando, na altura de cruzeiro;
//   DIVE      espera a sua vez e mergulha atirando no solo (cada tiro abre uma
//             cratera pequena); sai do mergulho na altura de ataque;
//   CLIMB     sobe de volta, e o alvo do bando anda alguns graus.
// Dentro do bando, cada drone mantém distância dos vizinhos ("boids"). Se a
// nave do jogador fica na frente dele, atira nela.
//
// Bombardeiro. Desce até a altura da órbita e dá voltas no planeta. Antes de
// cada bomba, marca no solo onde ela vai cair (`bomb_warning` segundos); a
// bomba cai exatamente ali e pode ser derrubada no ar. Uma torreta atira na
// nave quando ela chega perto. A frente é blindada; o motor, atrás, é o
// ponto fraco.
//
// Morte: quem aplica dano só marca o inimigo como morto (hp <= 0 e a causa).
// Os efeitos (explosão, pontos) e a remoção ficam com game/combat.c.
#ifndef GAME_ENEMIES_H
#define GAME_ENEMIES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/vmath.h"
#include "game/enemy_types.h"
#include "game/events.h"
#include "game/moon.h"
#include "game/projectiles.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

#define ENEMIES_CAPACITY 64
#define FLOCKS_CAPACITY  16

typedef enum {
    SWARM_APPROACH,
    SWARM_DIVE,
    SWARM_CLIMB,
    BOMBER_APPROACH,
    BOMBER_ORBIT,
} EnemyState;

typedef enum {
    DEATH_NONE,
    DEATH_PLAYER,   // abatido pela nave: dá pontos
    DEATH_CRASH,    // bateu no planeta, na Lua ou na nave
} DeathCause;

typedef struct {
    Vector3    pos;
    Vector3    vel;
    Quaternion rot;           // orientação do modelo (frente = direção do voo)
    Vector3    pos_prev;      // um passo antes, para interpolar o desenho
    Quaternion rot_prev;
    Vector3    target;        // Enxame: ponto do solo da passada; Bombardeiro: alvo da bomba
    Vector3    axis;          // Bombardeiro: eixo da órbita
    float      hp;
    float      age;           // segundos em cena
    float      flash;         // segundos restantes do clarão de dano (só desenho)
    float      timer;         // Enxame: espera antes do mergulho; Bombardeiro: até a próxima bomba
    float      arming;        // Bombardeiro: segundos até soltar a bomba marcada (0 = não está armando)
    float      fire_wait;     // até o próximo tiro
    float      turret_wait;   // até a próxima rajada (ou tiro na nave, no Enxame)
    int        burst;         // tiros restantes da rajada
    uint8_t    type;          // EnemyType
    uint8_t    state;         // EnemyState
    uint8_t    flock;         // Enxame: bando a que pertence
    uint8_t    slot;          // Enxame: posição no bando
    uint8_t    pass;          // Enxame: passada em que está
    uint8_t    death;         // DeathCause
} Enemy;

typedef struct {
    Vector3 target;       // direção (comprimento 1) do ponto do planeta que o bando ataca
    Vector3 drift_axis;   // o alvo gira em torno deste eixo a cada passada
    int     members;      // drones vivos; 0 = bando livre
    uint8_t pass;         // passadas já feitas
} Flock;

typedef struct {
    Pool   pool;
    Enemy *items;         // array paralelo ao pool
    Flock  flocks[FLOCKS_CAPACITY];
    int    alive_by_type[ENEMY_TYPE_COUNT];
} Enemies;

// Tudo de que os inimigos precisam para pensar em um passo.
typedef struct {
    const Planet *planet;
    const Moon   *moon;
    const Ship   *ship;     // NULL = ignoram a nave
    Projectiles  *shots;    // onde os tiros deles nascem
    Events       *events;
    Rng          *rng;
    const Tuning *t;
    uint32_t      tick;
    float         dt;
} EnemyContext;

bool enemies_init(Enemies *e, Arena *arena);
void enemies_clear(Enemies *e);

// Põe em cena um grupo de `count` inimigos espalhados em volta de `center`,
// voltados para o planeta. Um grupo de Enxame forma um bando, que ataca o
// lado do planeta de onde veio. Devolve quantos couberam.
int enemies_spawn_group(Enemies *e, const EnemyContext *ctx, EnemyType type, int count, Vector3 center);

void enemies_tick(Enemies *e, const EnemyContext *ctx);

// Tira `amount` de vida. Devolve true se foi este golpe que destruiu o inimigo.
bool enemies_damage(Enemies *e, int index, float amount, DeathCause cause);

// Tira o inimigo do jogo (chamado por quem trata a morte).
void enemies_remove(Enemies *e, int index);

int enemies_alive(const Enemies *e);

// Ponto fraco do Bombardeiro (o motor), no mundo.
Vector3 enemy_weak_spot(const Enemy *en, const Tuning *t);

static inline Vector3 enemy_forward(const Enemy *en) { return quat_forward(en->rot); }
static inline Vector3 enemy_up(const Enemy *en)      { return quat_up(en->rot); }

#endif
