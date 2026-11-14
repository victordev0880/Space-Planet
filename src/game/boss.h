// Bosses: um a cada cinco hordas, cada tipo no próprio arquivo.
//   Rompe-Mundos  (boss_breaker.c)  canhão em órbita que corta uma fatia do
//                                   planeta; destrua os capacitores na carga.
//   Colmeia Lunar (boss_hive.c)     agarrada à Lua: bolsas que soltam drones;
//                                   da fase 2 em diante, empurra a Lua contra a
//                                   Terra e arranca a crosta com um tentáculo.
//   Verme do Núcleo (boss_worm.c)   nada dentro do planeta cavando túneis e
//                                   salta para fora; persiga-o pelos túneis.
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

// --- Verme do Núcleo --------------------------------------------------------------
// Um verme de segmentos que nada dentro do planeta. A cabeça cava um túnel
// (que desmorona depois de um tempo, se a nave não estiver nele); o corpo
// segue o rastro da cabeça. Ciclo: BURROW (debaixo da terra, rumo ao ponto
// de saída; da fase 2 em diante passa antes pelo núcleo e o morde) ->
// RUMBLE (o chão racha onde ele vai sair: o aviso) -> BREACH (salta num arco
// pelo espaço, boca aberta e nódulos à mostra, e mergulha de novo). Na fase 3
// nada rente à superfície (SWIM), com o dorso para fora, abrindo um sulco.
// Acertar o ferrão da cauda dentro do túnel o faz sair antes, atordoado.
#define WORM_SEGMENTS 14
#define WORM_NODULES  4
#define WORM_TRAIL    96     // pontos do rastro da cabeça (um a cada WORM_TRAIL_STEP)
#define WORM_TUNNEL   128    // trechos do túnel esperando para desmoronar
#define WORM_RESTORES 4      // trechos que desmoronam por passo

typedef enum { WORM_BURROW, WORM_RUMBLE, WORM_BREACH, WORM_SWIM } WormAction;

extern const int WORM_NODULE_SEGMENT[WORM_NODULES];   // em qual segmento fica cada nódulo

typedef struct {
    Vector3 trail[WORM_TRAIL];           // rastro da cabeça (o mais novo em trail_head)
    int     trail_head, trail_count;
    Vector3 seg[WORM_SEGMENTS];          // centros dos segmentos (0 = cabeça)
    Vector3 seg_prev[WORM_SEGMENTS];
    Vector3 heading;                     // para onde a cabeça vai (comprimento 1)
    float   speed;
    Vector3 goal;                        // ponto que a cabeça persegue agora
    int     stage;                       // etapa do mergulho (0: fundo ou núcleo; 1: sob a saída)
    Vector3 breach_dir;                  // direção (do centro) de onde ele vai sair
    Vector3 land_dir;                    // ... e onde vai mergulhar de volta
    float   arc_time, arc_total;         // salto: segundos decorridos e duração
    bool    to_core;                     // este mergulho passa pelo núcleo
    bool    bit_core;                    // ... e já o mordeu
    bool    panic;                       // saiu atordoado (o ferrão foi acertado)
    float   nodule_hp[WORM_NODULES];
    float   nodule_flash[WORM_NODULES];
    float   tail_hp, tail_flash;
    bool    nodule_open[WORM_NODULES];   // o nódulo está fora da rocha (dá para acertar)
    bool    tail_open;                   // o ferrão está no túnel, com a cabeça debaixo da terra
    bool    panic_now;                   // o ferrão acabou de ser acertado: sair já
    float   carve_left;                  // distância até a próxima mordida do túnel
    float   dust_left;                   // distância até o próximo tremor no chão (o mundo levanta poeira)
    bool    dust_now;
    Vector3 tunnel[WORM_TUNNEL];         // trechos do túnel, do mais velho ao mais novo
    float   tunnel_age[WORM_TUNNEL];
    int     tunnel_first, tunnel_count;
    Vector3 restores[WORM_RESTORES];     // pedidos: trechos que desmoronam neste passo
    int     restore_count;
    Vector3 crater_at[2];                // pedidos: crateras de saída e de mergulho
    int     crater_count;
    float   ship_damage;                 // pedido: dano na nave (a erupção pegou ela)
} WormState;

// --- Devorador (o chefe final) ---------------------------------------------------
// Uma bocarra colossal. Um redemoinho em volta dela suga rochas e minério
// (cada rocha engolida cura); o raio trator disputa as rochas com ele. De
// tempos em tempos escolhe um ponto do planeta e o marca (AIM: um círculo
// vermelho no chão e os olhos acesos), mergulha (DIVE) e morde (CHEW, com a
// garganta exposta). Acertar os olhos a tempo o faz errar; um escudo local no
// ponto da mordida quebra os dentes dele (STUN longo, garganta aberta). O
// comportamento muda com o HP do PLANETA: faminto, frenético (mordidas mais
// rápidas, rajadas de dentes na nave) e, abaixo da metade, o banquete (LATCH:
// agarrado ao planeta, devorando sem parar, garganta sempre aberta; um tanto
// de dano o arranca). Morto, devolve tudo o que comeu, chovendo sobre as crateras.
typedef enum {
    DEV_PROWL,    // ronda o planeta, sugando rochas
    DEV_AIM,      // marcou o ponto da mordida (o aviso)
    DEV_DIVE,     // mergulhando
    DEV_CHEW,     // mordeu: mastiga, garganta exposta
    DEV_RISE,     // volta à altura de ronda
    DEV_STUN,     // atordoado (errou, quebrou os dentes ou foi arrancado)
    DEV_LATCH,    // banquete: agarrado ao planeta
} DevourerAction;

typedef enum { DEV_HUNGRY = 1, DEV_FRENZY = 2, DEV_FEAST = 3 } DevourerMood;   // o "phase" dele

extern const Vector3 DEV_EYE[2];
extern const float   DEV_EYE_RADIUS;
extern const Vector3 DEV_THROAT;
extern const float   DEV_THROAT_RADIUS;
extern const Vector3 DEV_HUMP;           // o papo nas costas: o ponto fraco no banquete
extern const float   DEV_HUMP_RADIUS;

typedef struct {
    float   jaw;                // 0 = fechada, 1 = escancarada (desenho e peças)
    float   jaw_target;
    Vector3 bite_dir;           // direção (do centro) do ponto da mordida
    Vector3 hover;              // de onde ele mergulha
    float   bite_wait;          // segundos até marcar a próxima mordida
    float   flinch;             // dano nos olhos durante a mira (a partir de dev.flinch, ele erra)
    float   eye_flash[2];
    float   teeth_wait;         // frenético: até a próxima rajada de dentes
    float   latch_damage;       // dano tomado agarrado (a partir de dev.latch_break, é arrancado)
    float   latch_cooldown;     // depois de arrancado, segundos até poder agarrar de novo
    float   drain_wait;         // banquete: até a próxima mordida contínua
    int     eaten;              // rochas engolidas (devolvidas na morte)
    float   orbit_angle;        // ronda: posição na órbita
    Vector3 orbit_axis;
    // Pedidos ao mundo neste passo.
    bool    bite_now;           // morder agora em bite_dir (o mundo confere o escudo)
    float   heal_pending;       // vida a recuperar (o mundo soma as rochas engolidas)
} DevourerState;

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
    WormState    worm;
    DevourerState dev;
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
// troca de fase que isso causar (zero mata). Só com o boss lutando.
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

// --- Verme do Núcleo ----------------------------------------------------------------
float   worm_segment_radius(const Tuning *t, int k);
// "Cima" e frente de um segmento (cima = para longe do planeta).
Vector3 worm_segment_up(const Boss *b, int k);
Vector3 worm_segment_forward(const Boss *b, int k);
Vector3 worm_nodule_point(const Boss *b, const Tuning *t, int n);
int     worm_nodules_left(const Boss *b);
bool    worm_underground(const Boss *b, const Planet *planet);   // a cabeça está dentro do planeta

// --- Devorador ------------------------------------------------------------------------
// A boca dele (para onde o redemoinho puxa) no mundo.
Vector3 devourer_mouth(const Boss *b, const Tuning *t);
// O mundo conta o resultado da mordida: `blocked` = um escudo local segurou.
void    devourer_bite_result(Boss *b, const Tuning *t, bool blocked, Events *events, uint32_t tick);
// Uma rocha (ou cristal) foi engolida: cura `fraction` da vida máxima.
void    devourer_swallow(Boss *b, float fraction, bool rock);
const char *devourer_mood_name(int mood);

#endif
