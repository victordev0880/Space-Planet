// Bosses: um a cada cinco hordas, cada tipo no próprio arquivo.
//   Rompe-Mundos  (boss_breaker.c)  canhão em órbita que corta uma fatia do
//                                   planeta; destrua os capacitores na carga.
//   Colmeia Lunar (boss_hive.c)     agarrada à Lua: bolsas que soltam drones;
//                                   da fase 2 em diante, empurra a Lua contra a
//                                   Terra e arranca a crosta com um tentáculo.
//
// Comum a todos: uma entrada (invulnerável), a luta em fases pela vida, pontos
// fracos que abrem e fecham, e a morte em explosões. O corpo é descrito como
// uma lista de esferas (BossPart): casco, que só bloqueia, alvos (capacitores,
// bolsas...) e o núcleo. Tiros, o raio do núcleo, batidas da nave e o piloto
// automático só enxergam essa lista; o dano numa peça volta ao tipo por
// boss_hit.
//
// Como os inimigos, o boss não mexe no planeta nem em outras entidades: deixa
// pedidos (escavações, drones, esporos, a distância da Lua) que o mundo executa.
#ifndef GAME_BOSS_H
#define GAME_BOSS_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "core/vmath.h"
#include "game/boss_types.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/moon.h"
#include "game/projectiles.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "voxel/planet.h"

#define BOSS_CARVES     8
#define BOSS_PARTS_MAX  32

typedef enum { BOSS_OFF, BOSS_ENTER, BOSS_FIGHT, BOSS_DYING } BossState;

// --- peças ----------------------------------------------------------------------
typedef enum {
    PART_HULL,     // casco: bloqueia tiros e a nave
    PART_TARGET,   // alvo destrutível (capacitor, bolsa, propulsor)
    PART_CORE,     // o núcleo: onde a vida do boss está
} BossPartRole;

typedef struct {
    Vector3 center;
    float   radius;
    uint8_t role;     // BossPartRole
    uint8_t id;       // número da peça para boss_hit (o tipo decide o que é)
    bool    open;     // ponto fraco aceso agora: o tiro entra (faísca e som de acerto bom)
    bool    solid;    // a nave bate nela
    float   aim;      // piloto automático: urgência (menor = antes; 0 = não mirar)
    Vector3 facing;   // de que lado ela se mostra (piloto automático)
} BossPart;

// --- Rompe-Mundos ---------------------------------------------------------------
#define BOSS_CAPACITORS   4
#define BOSS_HULL_SPHERES 7
#define BOSS_TURRETS      2
#define BOSS_CUT_SAMPLES  65   // medidas do relevo ao longo do traço do corte

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
    float   cap_hp[BOSS_CAPACITORS];
    float   cap_flash[BOSS_CAPACITORS];
    Vector3 cut_from;      // começo do corte (direção do centro do planeta, comprimento 1)
    float   cut_angle;     // comprimento do corte, em radianos (o corte gira em volta de `axis`)
    float   cut_done;      // fração já cavada
    // Distância do centro à superfície original ao longo do traço, medida no
    // começo da carga. As mordidas são fundas em relação a ela: se medissem o
    // chão já cavado (pela mordida anterior, ou por um corte antigo que este
    // cruza), o corte desceria em escada até o núcleo.
    float   cut_ground[BOSS_CUT_SAMPLES];
    Vector3 beam_point;    // onde o laser toca o solo agora
    float   turret_wait, fire_wait;
    int     burst, turret;
    float   launch_wait;
} BreakerState;

// --- Colmeia Lunar --------------------------------------------------------------
// Eixos da Colmeia: origem no centro da Lua, frente (-Z) apontando para o
// planeta, topo (+Y) no eixo da órbita da Lua; vezes o raio da Lua.
#define HIVE_SACS      6
#define HIVE_THRUSTERS 3

typedef enum {
    HIVE_NEST,     // as bolsas incham e soltam drones; o coração está fechado
    HIVE_OPEN,     // todas as bolsas estouradas: o coração abre
    HIVE_REGROW,   // as bolsas renascem (o coração fecha)
} HiveAction;

extern const Vector3 HIVE_SAC[HIVE_SACS];             // direções (comprimento 1) a partir do centro da Lua
extern const float   HIVE_SAC_RADIUS;
extern const Vector3 HIVE_THRUSTER[HIVE_THRUSTERS];
extern const float   HIVE_THRUSTER_RADIUS;
extern const Vector3 HIVE_CORE;                       // o coração, voltado para o planeta
extern const float   HIVE_CORE_RADIUS;

typedef struct {
    float   sac_hp[HIVE_SACS];
    float   sac_grow[HIVE_SACS];     // 0 a 1: renascendo (só leva dano inteira)
    float   sac_flash[HIVE_SACS];
    int     inflating;               // bolsa inchando (-1 = nenhuma)
    float   inflate;                 // segundos até ela soltar os drones
    float   inflate_wait;            // até a próxima bolsa começar a inchar
    float   thr_hp[HIVE_THRUSTERS];
    float   thr_grow[HIVE_THRUSTERS];
    float   thr_flash[HIVE_THRUSTERS];
    float   thr_regrow;              // segundos até os propulsores renascerem (todos mortos)
    bool    thr_born;                // os propulsores já nasceram (fase 2)
    bool    pushing;                 // os propulsores estão empurrando a Lua
    float   moon_scale;              // distância da Lua pedida, em frações da órbita normal
    float   tide;                    // força da maré, de 0 a 1 (Lua perto)
    float   tide_wait;               // até a próxima mordida do tentáculo
    Vector3 tide_point;              // onde o tentáculo toca o planeta
    float   spit_wait;               // fase 3: até o próximo cuspe no jogador
    float   spit_charge;             // > 0: o coração está preparando um cuspe
    int     popped;                  // bolsas estouradas pela nave (a cada duas, um esporo)
    int     spore_count;             // pedidos: esporos a soltar neste passo...
    Vector3 spore_at[2];             // ... aqui
} HiveState;

// --- o boss ---------------------------------------------------------------------
typedef struct {
    uint8_t      type;          // BossType
    uint8_t      state;         // BossState
    uint8_t      action;        // ação do tipo (BreakerAction, HiveAction...)
    uint8_t      phase;         // 1 a 3
    Vector3      pos, vel, pos_prev;
    Quaternion   rot, rot_prev;
    float        scale;         // tamanho: os pontos do modelo são multiplicados por isto
    Vector3      axis;          // eixo da órbita (Rompe-Mundos)
    Vector3      enter_from;    // onde a entrada começou
    Vector3      enter_to;      // ... e onde termina
    float        hp, hp_max;    // vida do núcleo
    float        timer;         // o que falta da ação atual (ou da entrada, ou da morte)
    float        age;
    float        flash;         // clarão de dano no casco (só desenho)
    float        core_flash;

    // Pedidos ao mundo, válidos por um passo.
    CarveRequest carves[BOSS_CARVES];
    int          carve_count;
    int          launch_count;  // drones a soltar...
    uint8_t      launch_type;   // ... deste tipo...
    Vector3      launch_from;   // ... daqui
    float        blast_wait;    // explosões da morte
    bool         blast_now;     // morrendo: uma explosão no casco neste passo...
    Vector3      blast_at;      // ... aqui (o mundo faz as partículas)
    bool         defeated;      // acabou de ser destruído (o mundo dá pontos e recompensa)

    BreakerState breaker;
    HiveState    hive;
} Boss;

typedef struct {
    const Planet *planet;
    const Moon   *moon;
    const Ship   *ship;     // NULL = ignora a nave
    Projectiles  *shots;
    Events       *events;
    Rng          *rng;
    const Tuning *t;
    uint32_t      tick;
    float         dt;
    int           drones;   // drones do Enxame em cena (a Colmeia respeita um teto)
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

// As peças do corpo agora, no mundo. Devolve quantas (até BOSS_PARTS_MAX).
int boss_parts(const Boss *b, const Tuning *t, BossPart *out);

// Dano na peça `id` (de boss_parts). Devolve o dano que de fato entrou.
float boss_hit(Boss *b, const Tuning *t, int id, float amount, Events *events, uint32_t tick);

// Dano direto no núcleo, ignorando a blindagem (esporo arremessado).
float boss_hit_core_raw(Boss *b, const Tuning *t, float amount, Events *events, uint32_t tick);

// Fração do dano que chega ao núcleo agora (1 = aberto).
float boss_core_factor(const Boss *b, const Tuning *t);

// Ferramentas (roteiros do autoplay): põe a vida numa fração da máxima, com a
// troca de fase que isso causar. Só com o boss lutando.
void boss_set_health(Boss *b, const Tuning *t, float fraction, Events *events, uint32_t tick);

// Números de cada tipo (data/bosses.txt).
float boss_enter_time(const Boss *b, const Tuning *t);
float boss_points(BossType type, const Tuning *t);
float boss_xp(BossType type, const Tuning *t);
void  boss_phase_marks(BossType type, const Tuning *t, float out[2]);   // frações da vida das fases 2 e 3
const char *boss_name(BossType type);

// --- Rompe-Mundos -----------------------------------------------------------------
bool boss_capacitors_armed(const Boss *b);
int  boss_capacitors_left(const Boss *b);
// Direção (do centro do planeta) do ponto do corte na fração `u` (0 a 1) dele.
Vector3 boss_cut_direction(const Boss *b, float u);
// O ponto do solo na fração `u` do corte, como era no começo da carga.
Vector3 boss_cut_point(const Boss *b, float u);

// --- Colmeia Lunar ----------------------------------------------------------------
int  hive_sacs_left(const Boss *b);
int  hive_thrusters_left(const Boss *b);
// Uma bolsa (ou propulsor) no mundo, com o tamanho atual (inchando, nascendo).
Vector3 hive_sac_point(const Boss *b, int k);
float   hive_sac_size(const Boss *b, const Tuning *t, int k);
Vector3 hive_thruster_point(const Boss *b, int k);

#endif
