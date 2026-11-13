// Bosses. Por enquanto um: o Rompe-Mundos.
//
// O Rompe-Mundos é um canhão colossal em órbita. Ciclo de ataque:
//   REST     orbita e atira na nave com duas torretas;
//   CHARGE   desacelera e carrega o canhão. O traço do corte aparece no chão
//            (um arco à frente dele) e quatro capacitores acendem no casco:
//            só nessa hora eles levam dano. Destruídos os quatro antes do fim
//            da carga, o tiro falha e ele fica atordoado;
//   FIRE     o laser varre o traço e abre uma vala no planeta;
//   STAGGER  atordoado, com o núcleo aberto: o núcleo leva o dano inteiro.
// Fora do atordoamento o núcleo é blindado (só passa uma fração do dano; na
// fase 3, uma fração maior). A vida dele é a do núcleo. Três fases, pela vida:
// cargas mais curtas, cortes mais longos e, da fase 2 em diante, drones.
//
// Como os inimigos, o boss não mexe no planeta nem em outras entidades: deixa
// pedidos (escavações, drones a soltar) que o mundo executa.
#ifndef GAME_BOSS_H
#define GAME_BOSS_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "core/vmath.h"
#include "game/boss_types.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/projectiles.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

#define BOSS_CAPACITORS   4
#define BOSS_HULL_SPHERES 7
#define BOSS_CARVES       8
#define BOSS_TURRETS      2
#define BOSS_CUT_SAMPLES  65   // medidas do relevo ao longo do traço do corte

typedef enum { BOSS_OFF, BOSS_ENTER, BOSS_FIGHT, BOSS_DYING } BossState;
typedef enum { BREAKER_REST, BREAKER_CHARGE, BREAKER_FIRE, BREAKER_STAGGER } BreakerAction;

// Pontos do Rompe-Mundos em eixos do modelo (frente -Z, topo +Y), vezes a
// escala. A lógica (tiros, colisão) e o desenho usam os mesmos.
typedef struct { Vector3 center; float radius; } BossSphere;
extern const Vector3    BREAKER_CAPACITOR[BOSS_CAPACITORS];
extern const float      BREAKER_CAPACITOR_RADIUS;
extern const Vector3    BREAKER_CORE;
extern const float      BREAKER_CORE_RADIUS;
extern const BossSphere BREAKER_HULL[BOSS_HULL_SPHERES];
extern const Vector3    BREAKER_MUZZLE;              // boca do canhão: de onde sai o laser
extern const Vector3    BREAKER_TURRET[BOSS_TURRETS];
extern const Vector3    BREAKER_BAY;                 // de onde saem os drones

typedef struct {
    uint8_t      type;          // BossType
    uint8_t      state;         // BossState
    uint8_t      action;        // BreakerAction
    uint8_t      phase;         // 1 a 3
    Vector3      pos, vel, pos_prev;
    Quaternion   rot, rot_prev;
    Vector3      axis;          // eixo da órbita
    Vector3      enter_from;    // onde a entrada começou
    Vector3      enter_to;      // ... e onde termina (na órbita)
    float        hp, hp_max;    // vida do núcleo
    float        timer;         // o que falta da ação atual (ou da entrada, ou da morte)
    float        age;
    float        cap_hp[BOSS_CAPACITORS];
    float        flash;         // clarão de dano no casco (só desenho)
    float        core_flash;
    float        cap_flash[BOSS_CAPACITORS];
    Vector3      cut_from;      // começo do corte (direção do centro do planeta, comprimento 1)
    float        cut_angle;     // comprimento do corte, em radianos (o corte gira em volta de `axis`)
    float        cut_done;      // fração já cavada
    // Distância do centro à superfície original ao longo do traço, medida no
    // começo da carga. As mordidas são fundas em relação a ela: se medissem o
    // chão já cavado (pela mordida anterior, ou por um corte antigo que este
    // cruza), o corte desceria em escada até o núcleo.
    float        cut_ground[BOSS_CUT_SAMPLES];
    Vector3      beam_point;    // onde o laser toca o solo agora
    float        turret_wait, fire_wait;
    int          burst, turret;
    float        launch_wait;
    float        blast_wait;    // explosões da morte
    int          launch_request;              // drones a soltar neste passo (o mundo solta)
    bool         blast_now;                   // morrendo: uma explosão no casco neste passo...
    Vector3      blast_at;                    // ... aqui (o mundo faz as partículas)
    CarveRequest carves[BOSS_CARVES];         // escavações deste passo (o mundo executa)
    int          carve_count;
    bool         defeated;      // acabou de ser destruído (o mundo dá pontos e recompensa)
} Boss;

typedef struct {
    const Planet *planet;
    const Ship   *ship;     // NULL = ignora a nave
    Projectiles  *shots;
    Events       *events;
    Rng          *rng;
    const Tuning *t;
    uint32_t      tick;
    float         dt;
} BossContext;

void boss_reset(Boss *b);

// Põe o boss em cena: ele chega de longe, na direção `dir`.
void boss_spawn(Boss *b, BossType type, const BossContext *ctx, Vector3 dir);

void boss_tick(Boss *b, const BossContext *ctx);

// Em cena (entrando, lutando ou explodindo): a horda não acaba enquanto isso.
bool boss_active(const Boss *b);

// Pode levar dano agora (já entrou e ainda não está morrendo)?
bool boss_vulnerable(const Boss *b);

// Um ponto do modelo (eixos do modelo) no mundo.
Vector3 boss_point(const Boss *b, const Tuning *t, Vector3 local);

// Fração do dano que chega ao núcleo agora (1 = aberto).
float boss_core_factor(const Boss *b, const Tuning *t);

// Os capacitores estão carregados (levando dano)?
bool boss_capacitors_armed(const Boss *b);
int  boss_capacitors_left(const Boss *b);

// Dano. Devolvem o dano que de fato entrou.
float boss_damage_core(Boss *b, const Tuning *t, float amount, Events *events, uint32_t tick);
float boss_damage_capacitor(Boss *b, const Tuning *t, int index, float amount, Events *events, uint32_t tick);

// Ferramentas (roteiros do autoplay): põe a vida numa fração da máxima, com a
// troca de fase que isso causar. Só com o boss lutando.
void boss_set_health(Boss *b, const Tuning *t, float fraction, Events *events, uint32_t tick);

// Direção (do centro do planeta) do ponto do corte na fração `u` (0 a 1) dele.
Vector3 boss_cut_direction(const Boss *b, float u);

// O ponto do solo na fração `u` do corte, como era no começo da carga.
Vector3 boss_cut_point(const Boss *b, float u);

const char *boss_name(BossType type);

#endif
