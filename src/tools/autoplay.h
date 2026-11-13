// Autoplay: joga sozinho a partir de um roteiro de texto e mede o resultado.
//
// É a forma de verificar o jogo sem uma pessoa olhando: o roteiro dita os
// inputs quadro a quadro, pede screenshots e, no fim, sai um relatório.
//
// Há dois modos:
//   determinístico (padrão)  cada quadro desenhado roda exatamente um passo de
//                            lógica, sem vsync. O mesmo roteiro produz sempre a
//                            mesma partida, e os tempos mostram o custo real.
//   tempo real (--realtime)  relógio de verdade e vsync, como no jogo normal.
//                            Serve para medir o ritmo que o jogador vai sentir.
//
// Formato do roteiro (uma instrução por linha, # inicia comentário):
//
//   frames 360              duração total, em quadros (obrigatório)
//   seed 1234               seed da partida (opcional; padrão: sim.seed)
//   at 0   hold forward     aperta e segura uma ação
//   at 90  release forward  solta
//   at 30  press overlay    aperta por um único quadro
//   at 40  look 3 -1        mouse anda 3 px/quadro em x e -1 em y, até mudar
//   at 120 shot nome        salva captures/nome.png
//   at 50  cam 0 40 190     põe a câmera livre em (x y z), olhando para o
//                           centro do planeta
//   at 50  cam 0 40 190 10 0 0    ... ou olhando para outro ponto (x y z)
//   at 60  crater 0 1 0 5 3.5     impacto vindo da direção (x y z) a partir do
//                                 centro do planeta: abre uma cratera com boca
//                                 de raio 5 e profundidade 3.5. Repetir na
//                                 mesma direção cava cada vez mais fundo.
//   at 70  barrage 20 3 2         20 crateras (boca 3, profundidade 2) em
//                                 direções sorteadas, todas no mesmo quadro
//   at 0   view orbit             câmera de apresentação, com a nave fora de
//                                 cena (para fotografar só o planeta)
//   at 0   view ship              câmera de jogo, atrás da nave (o padrão)
//   at 80  ship 0 70 0 0 0 0      põe a nave em (x y z), de nariz para o ponto
//                                 (x y z) seguinte, e passa à câmera de jogo
//   at 80  ship 0 70 0 0 0 0 30   ... já voando a 30 unidades por segundo
//   at 0   waves off              desliga as hordas (on recomeça a sequência)
//   at 90  spawn swarm 6 0 90 40  põe um grupo de inimigos em (x y z)
//   at 100 autopilot on           um piloto automático simples assume a nave:
//                                 persegue o inimigo mais urgente e atira
//                                 (off devolve o controle ao roteiro)
//   at 110 clear                  tira de cena todos os inimigos e tiros (para
//                                 separar as cenas de um roteiro)
//
// Em um mesmo quadro a ordem é: view, ship, cam, waves, clear, spawn, crateras. Um
// "cam" depois de um "ship" fotografa a nave de fora (na câmera livre a
// partida fica congelada).
//
// Os nomes das ações são os de core/input.c (forward, boost, freecam...).
#ifndef TOOLS_AUTOPLAY_H
#define TOOLS_AUTOPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "core/input.h"
#include "core/vmath.h"
#include "game/enemy_types.h"
#include "game/tuning.h"
#include "game/world.h"

#define AUTOPLAY_MAX_EVENTS  512
#define AUTOPLAY_NAME_LEN    48
#define AUTOPLAY_ERROR_LEN   160
#define AUTOPLAY_MAX_CRATERS 16     // comandos "crater" em um mesmo quadro
#define AUTOPLAY_MAX_BARRAGE 500    // crateras de um único "barrage"
#define AUTOPLAY_MAX_SPAWNS  8      // comandos "spawn" em um mesmo quadro

typedef enum {
    AP_HOLD, AP_RELEASE, AP_PRESS, AP_LOOK, AP_SHOT, AP_CAM, AP_CRATER, AP_BARRAGE, AP_VIEW, AP_SHIP,
    AP_WAVES, AP_SPAWN, AP_AUTOPILOT, AP_CLEAR
} AutoplayCmd;

typedef struct {
    int         frame;
    AutoplayCmd cmd;
    int         action;                    // HOLD, RELEASE, PRESS; em SPAWN, o tipo de inimigo
    int         count;                     // BARRAGE, SPAWN; em VIEW, o modo de câmera; em WAVES e AUTOPILOT, 1 = on
    float       dx, dy;                    // LOOK
    float       rim_radius, depth;         // CRATER, BARRAGE
    float       speed;                     // SHIP
    Vector3     pos, target;               // CAM, SHIP; em CRATER, pos é a direção
    char        name[AUTOPLAY_NAME_LEN];   // SHOT
} AutoplayEvent;

typedef struct {
    Vector3 dir;          // direção a partir do centro do planeta
    float   rim_radius;
    float   depth;
} AutoplayCrater;

typedef struct {
    EnemyType type;
    int       count;
    Vector3   center;
} AutoplaySpawn;

// O que o roteiro manda fazer em um quadro.
typedef struct {
    InputSample input;
    const char *shot;         // nome do screenshot pedido, ou NULL
    bool        has_cam;      // true = reposicionar a câmera livre
    Vector3     cam_pos;
    Vector3     cam_target;
    bool        has_view;     // true = trocar de câmera
    CamMode     view;
    bool        has_ship;     // true = reposicionar a nave
    Vector3     ship_pos;
    Vector3     ship_target;
    float       ship_speed;
    bool        has_waves;    // true = ligar ou desligar as hordas
    bool        waves_on;
    bool        clear;        // tirar de cena todos os inimigos e tiros
    bool        autopilot;    // o piloto automático está no comando neste quadro

    AutoplaySpawn spawns[AUTOPLAY_MAX_SPAWNS];
    int           spawn_count;

    AutoplayCrater craters[AUTOPLAY_MAX_CRATERS];
    int            crater_count;
    int            barrage_count;   // crateras em direções sorteadas
    float          barrage_rim_radius;
    float          barrage_depth;
} AutoplayFrame;

typedef struct {
    // roteiro
    AutoplayEvent events[AUTOPLAY_MAX_EVENTS];   // ordenados por quadro
    int      event_count;
    int      frames;
    bool     has_seed;
    uint64_t seed;
    char     error[AUTOPLAY_ERROR_LEN];

    // estado da reprodução
    int      cursor;
    uint32_t held;
    float    look_dx;
    float    look_dy;
    bool     autopilot;
} Autoplay;

// Substituem o conteúdo de `ap`. Em caso de erro retornam false e ap->error
// descreve o problema (com o número da linha).
bool autoplay_parse(Autoplay *ap, const char *text);
bool autoplay_load_file(Autoplay *ap, const char *path);

// Avança a reprodução até o quadro `frame` (chamar em ordem: 0, 1, 2...) e
// preenche o que o roteiro pede para ele.
void autoplay_step(Autoplay *ap, int frame, AutoplayFrame *out);

// Executa no mundo os comandos do quadro que não são input: trocar de câmera,
// posicionar a nave e a câmera e abrir crateras. Chamar antes dos passos de
// lógica do quadro.
void autoplay_apply(const AutoplayFrame *frame, World *w, const Tuning *t);

// --- medições ---------------------------------------------------------------

#define FRAME_BUDGET_MS    (1000.0 / 60.0)   // meta do projeto: 60 FPS
#define FRAME_HIST_BUCKETS 200
#define FRAME_HIST_STEP_MS 0.25              // o histograma cobre até 50 ms
#define FRAME_SLOW_LIST    12                // quantos quadros estourados são listados

typedef struct {
    int    frames;          // quadros medidos
    double sum_ms;          // tempo total de quadro (inclui apresentar na tela)
    double worst_ms;
    int    worst_frame;
    double sum_cpu_ms;      // só atualizar + enviar o desenho
    double worst_cpu_ms;
    double sum_sim_ms;      // só a simulação (input, passos de lógica, comandos do roteiro)
    double worst_sim_ms;
    double budget_ms;       // acima disso o quadro conta como estourado
    int    over_budget;
    // Os primeiros quadros estourados. Como a simulação é determinística, se
    // forem os mesmos em toda execução a causa é o jogo; se mudarem, é ruído
    // da máquina (outros programas, o sistema, o driver).
    int    slow_frames[FRAME_SLOW_LIST];
    int    histogram[FRAME_HIST_BUCKETS];   // contagem de quadros por faixa de tempo
} FrameStats;

// budget_ms: FRAME_BUDGET_MS quando se mede o custo puro do quadro. Com vsync
// os quadros oscilam em torno de 16,7 ms, então lá o limite precisa de folga.
void frame_stats_init(FrameStats *s, double budget_ms);
void frame_stats_add(FrameStats *s, int frame, double frame_ms, double cpu_ms);

// Tempo gasto só na simulação em um quadro (opcional; chamar junto com
// frame_stats_add). É CPU pura: não inclui esperas pela placa de vídeo.
void frame_stats_add_sim(FrameStats *s, double sim_ms);

// Tempo de quadro abaixo do qual fica a fração pedida dos quadros (0.99 = p99).
// Um único quadro ruim, causado por outro programa, estraga o "pior quadro"
// mas quase não mexe no p99; por isso ele é a medida mais confiável.
double frame_stats_percentile(const FrameStats *s, double fraction);

typedef struct {
    const char *script;
    bool        realtime;         // false = um passo por quadro, sem vsync (determinístico)
    int         frames_planned;
    int         frames_run;
    uint64_t    ticks;
    uint64_t    seed;
    uint64_t    state_hash;
    int         screenshots;
    int         warmup_frames;
    FrameStats  stats;
    double      world_init_ms;     // tempo para criar o mundo (gerar o planeta)
    double      render_init_ms;    // tempo para criar tudo que é desenhado
    double      planet_mesh_ms;    // parte de render_init gasta nas malhas do planeta
    int         planet_meshes;     // quantos chunks têm malha
    int         planet_triangles;
    bool        shaded;            // desenhou com o shader do planeta

    // Estado do planeta ao fim do roteiro e custo de mantê-lo atualizado.
    float       planet_hp;         // massa restante, de 0 a 1
    float       core_hp;
    bool        collapsed;
    int         impacts;
    int         particles_peak;
    int         remeshed_total;        // chunks remalhados ao longo do roteiro
    double      remesh_ms_total;       // tempo somado desses remalhamentos
    int         remeshed_max_frame;    // máximo em um único quadro
    int         remesh_backlog_max;    // maior fila de chunks à espera
    double      remesh_ms_max;         // quadro que mais gastou remalhando

    // A partida ao fim do roteiro.
    bool        waves_on;          // as hordas estavam ligadas
    int64_t     score;
    int         kills;
    int         wave;              // horda alcançada (1 = a primeira)
    int         waves_total;
    const char *game_state;        // "jogando", "vitoria" ou "derrota"
    int         enemies_alive;

    int         warnings;         // avisos e erros emitidos durante a execução
    int         errors;
    const char *first_problem;    // texto do primeiro aviso/erro, ou NULL
} AutoplayReport;

bool autoplay_write_report(const char *path, const AutoplayReport *r);

#endif
