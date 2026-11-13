// Inimigos: quem são, onde estão e como pensam.
//
// Cada inimigo escolhe a cada passo uma "velocidade desejada" (para onde quer
// ir e com que pressa) e acelera em direção a ela, com um limite de
// aceleração (`accel` em data/enemies.txt). É o que dá curvas suaves e
// previsíveis, em vez de mudanças de direção instantâneas.
//
// O comportamento de cada tipo mora no seu arquivo (game/ai_*.c); aqui fica
// o que é comum a todos. Resumo:
//   Enxame        chega em bando, espera parado sobre o alvo e faz passadas
//                 rasantes atirando no solo; parte do bando sai para encarar
//                 a nave quando ela chega perto.
//   Bombardeiro   orbita soltando bombas; antes de cada uma, o alvo é marcado
//                 no solo; blindado na frente, ponto fraco no motor.
//   Kamikaze      trava na nave se ela passa perto e a persegue para explodir;
//                 senão, mergulha no planeta (cratera média).
//   Escavador     pousa e cava um túnel rumo ao núcleo.
//   Sifão         paira baixo e drena o solo com um feixe.
//   Atirador      fica longe e carrega tiros fortes contra a nave, com aviso.
//   Escudeiro     acompanha os aliados e os protege dos tiros.
//   Porta-drones  orbita alto e solta bandos de Enxame.
//
// Morte: quem aplica dano só marca o inimigo como morto (hp <= 0 e a causa).
// Os efeitos (explosão, pontos) e a remoção ficam com game/combat.c.
//
// O planeta é só lido aqui. Quem cava (Escavador, Sifão) deixa um pedido de
// escavação em `carves`, que o mundo executa logo depois.
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

#define ENEMIES_CAPACITY 96
#define FLOCKS_CAPACITY  32
#define CARVES_CAPACITY  32
#define ENEMY_VARIANTS   3     // variações de cor dentro de cada tipo

typedef enum {
    SWARM_APPROACH,     // voa até o ponto de espera do bando
    SWARM_HOLD,         // espera ali, quase parado, de olho na nave
    SWARM_DIVE,         // passada: desce atirando no alvo
    SWARM_CLIMB,        // sobe, já rumo ao próximo ponto de espera
    SWARM_ENGAGE,       // saiu do bando para encarar a nave
    HEAVY_APPROACH,     // Bombardeiro e Porta-drones: descem até a órbita
    HEAVY_ORBIT,        // ... e dão voltas no planeta
    KAMIKAZE_DIVE,      // mergulha no planeta
    KAMIKAZE_HUNT,      // persegue a nave
    DIGGER_DESCEND,     // voa até o ponto de pouso
    DIGGER_DRILL,       // cava rumo ao núcleo
    SIPHON_APPROACH,    // voa até o ponto onde vai drenar
    SIPHON_DRAIN,       // paira e drena
    SNIPER_HUNT,        // mantém distância da nave e atira
    SHIELDER_ESCORT,    // acompanha os aliados
} EnemyState;

typedef enum {
    DEATH_NONE,
    DEATH_PLAYER,     // abatido pela nave: dá pontos
    DEATH_CRASH,      // bateu no planeta, na Lua ou na nave
    DEATH_DETONATE,   // Kamikaze que explodiu junto da nave
} DeathCause;

typedef struct {
    Vector3    pos;
    Vector3    vel;
    Quaternion rot;           // orientação do modelo (frente = direção do voo)
    Vector3    pos_prev;      // um passo antes, para interpolar o desenho
    Quaternion rot_prev;
    Vector3    target;        // ponto ou direção que está atacando (depende do tipo)
    Vector3    axis;          // eixo de órbita, ou de deriva do alvo
    Vector3    aux;           // Sifão: onde o feixe toca o solo; Atirador: onde a mira está
    Vector3    look;          // para onde o modelo olha quando está quase parado (zero = para onde voa)
    float      bank;          // inclinação atual nas curvas, em radianos (suavizada)
    float      hp;
    float      age;           // segundos em cena
    float      flash;         // segundos restantes do clarão de dano (só desenho)
    float      timer;         // uso de cada tipo (próxima bomba, próximo tiro, próxima mordida...)
    float      arming;        // segundos de aviso restantes (bomba, lançamento, tiro carregando); 0 = nada
    float      fire_wait;     // até o próximo tiro
    float      turret_wait;   // até a próxima rajada (ou tiro na nave, no Enxame)
    float      cooldown;      // Enxame: até poder sair do bando de novo para encarar a nave
    float      size;          // variação de tamanho do modelo (só desenho)
    int        burst;         // tiros restantes da rajada
    uint8_t    type;          // EnemyType
    uint8_t    state;         // EnemyState
    uint8_t    flock;         // Enxame: bando a que pertence
    uint8_t    slot;          // Enxame: posição no bando
    uint8_t    pass;          // Enxame: passada em que está
    uint8_t    death;         // DeathCause
    uint8_t    variant;       // variação de cor (só desenho)
} Enemy;

typedef struct {
    Vector3 target;       // direção (comprimento 1) do ponto do planeta que o bando ataca
    Vector3 drift_axis;   // o alvo gira em torno deste eixo a cada passada
    int     members;      // drones vivos; 0 = bando livre
    int     engaged;      // drones encarando a nave agora (recontado a cada passo)
    uint8_t pass;         // passadas já feitas
} Flock;

// Pedido de escavação: retirar uma esfera do planeta.
typedef struct {
    Vector3 center;
    float   radius;
    uint8_t type;      // quem pediu (EnemyType)
} CarveRequest;

typedef struct {
    Pool         pool;
    Enemy       *items;         // array paralelo ao pool
    Flock        flocks[FLOCKS_CAPACITY];
    int          alive_by_type[ENEMY_TYPE_COUNT];
    CarveRequest carves[CARVES_CAPACITY];   // pedidos do passo atual
    int          carve_count;
    int          forced_variant;            // ferramenta de roteiro: -1 = sorteia a variação de cada grupo
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

// O inimigo está coberto pelo escudo de um Escudeiro? (Escudeiros não
// protegem uns aos outros.) Devolve o índice do Escudeiro, ou -1.
int enemies_shielded_by(const Enemies *e, const Tuning *t, int index);

// Ponto fraco do Bombardeiro (o motor), no mundo.
Vector3 enemy_weak_spot(const Enemy *en, const Tuning *t);

// Cratera que um inimigo deste tipo abre ao cair no planeta.
void enemy_crater(const Tuning *t, EnemyType type, float *radius, float *depth);

// Quanto o Escavador já avançou rumo ao núcleo (0 = na superfície, 1 = chegou).
float digger_progress(const Enemy *en, const Planet *planet);

static inline Vector3 enemy_forward(const Enemy *en) { return quat_forward(en->rot); }
static inline Vector3 enemy_up(const Enemy *en)      { return quat_up(en->rot); }

#endif
