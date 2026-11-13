// Estado da simulação. Tudo que precisa ser determinístico mora aqui e só é
// alterado por world_tick, um passo fixo de cada vez.
//
// O mundo tem o planeta, a Lua, a nave do jogador, os inimigos, os
// projéteis, as partículas, a pontuação e a câmera. Há três câmeras: a de
// jogo (em terceira pessoa, atrás da nave), a de apresentação (gira sozinha
// em volta do planeta, com a nave fora de cena) e a livre de debug (F2).
//
// A partida (nave, inimigos, hordas) só anda na câmera de jogo. Nas outras
// ela fica congelada: o F2 funciona como pausa com câmera livre.
#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/input.h"
#include "core/rng.h"
#include "core/vmath.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/moon.h"
#include "game/particles.h"
#include "game/projectiles.h"
#include "game/score.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "game/waves.h"
#include "voxel/planet.h"

// Posição, orientação e campo de visão de uma câmera (ver core/vmath.h para
// a convenção de orientação).
typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      fov;   // campo de visão vertical, em graus
} CamPose;

typedef enum {
    CAM_SHIP,    // jogo: terceira pessoa, atrás da nave
    CAM_ORBIT,   // apresentação: gira sozinha em volta do planeta
    CAM_FREE,    // debug: voa livre (F2)
} CamMode;

// Um impacto no planeta. Quem desenha consulta os mais recentes para reagir
// (tremer a câmera, apagar luzes de cidades).
typedef struct {
    Vector3  pos;      // ponto atingido
    float    radius;   // raio da boca da cratera
    uint32_t id;       // 1 para o primeiro impacto da partida, 2 para o segundo...
    bool     quiet;    // escavação contínua (broca, feixe): sem tremor nem som
} Impact;

#define WORLD_IMPACT_HISTORY 32

typedef enum {
    GAME_PLAYING,
    GAME_WON,      // todas as hordas vencidas
    GAME_LOST,     // colapso planetário
} GameState;

typedef struct {
    uint64_t  tick;         // quantos passos de lógica já rodaram
    Rng       rng;
    Planet    planet;
    Moon      moon;
    Particles particles;
    Impact    impacts[WORLD_IMPACT_HISTORY];   // os últimos impactos, em fila circular
    uint32_t  impact_count;                    // total de impactos desde o início
    bool      collapsed;    // o planeta passou do ponto de colapso (derrota)

    Ship      ship;
    bool      ship_active;  // false na câmera de apresentação: nave parada e fora de cena
    uint32_t  jolt_count;   // quantas pancadas a nave já levou (batidas, destruição)
    float     jolt_strength;   // força da mais recente, para o tremor de câmera

    // --- combate ---
    Enemies      enemies;
    Projectiles  shots;
    Events       events;
    Score        score;
    WavePlan     plan;          // a sequência de hordas desta partida
    WaveDirector director;
    bool         waves_enabled; // false = sem hordas (roteiros de teste)
    GameState    state;
    uint64_t     state_tick;    // passo em que a partida terminou
    int          hitstop;       // passos de congelamento restantes ("hit-stop")
    InputState   frozen_input;  // o que o jogador fez durante o congelamento (vale depois dele)
    float        hit_marker;    // segundos restantes do marcador de acerto na mira (HUD)
    float        kill_marker;   // ... e do de abate

    CamMode   cam_mode;
    CamMode   cam_return;   // câmera para a qual o F2 volta ao sair da livre
    float     orbit_angle;  // radianos
    float     free_yaw;     // orientação da câmera livre, em radianos
    float     free_pitch;
    Vector3   free_vel;     // velocidade da câmera livre
    CamPose   view;         // pose depois do último passo
    CamPose   view_prev;    // pose um passo antes (para interpolar)
} World;

// Cria o mundo e gera o planeta, reservando a grade na arena. Retorna false
// se a arena não tiver espaço. Começa na câmera de jogo, com a nave pronta.
bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena);
void world_tick(World *w, const InputState *in, const Tuning *t);

// Depois de recarregar o tuning: se a receita do planeta mudou, gera o
// planeta de novo. Retorna true quando isso aconteceu.
bool world_apply_tuning(World *w, const Tuning *t);

// Troca de câmera sem transição. CAM_SHIP põe a nave em jogo; CAM_ORBIT a
// tira de cena (usado em roteiros que só querem fotografar o planeta).
void world_set_view(World *w, const Tuning *t, CamMode mode);

// Ferramenta de debug e do autoplay: põe a câmera livre em `pos`, olhando
// para `target`.
void world_place_camera(World *w, Vector3 pos, Vector3 target);

// Ferramenta de debug e do autoplay: põe a nave em `pos`, inteira, com o nariz
// para `target` e o topo para longe do planeta, já voando a `speed` nessa
// direção. Passa para a câmera de jogo.
void world_place_ship(World *w, const Tuning *t, Vector3 pos, Vector3 target, float speed);

// Define a sequência de hordas e começa pela calmaria da primeira. Sem plano
// (o padrão), não há hordas.
void world_set_plan(World *w, const WavePlan *plan);

// Liga ou desliga as hordas (roteiros de teste que não querem inimigos).
// Ligar recomeça a sequência.
void world_set_waves(World *w, bool on);

// Ferramenta de debug e do autoplay: põe um grupo de inimigos em `center`.
int world_spawn(World *w, const Tuning *t, EnemyType type, int count, Vector3 center);

// Ferramenta do autoplay: tira de cena todos os inimigos e tiros.
void world_clear_enemies(World *w);

// Nova partida com o mesmo plano: planeta inteiro, sem inimigos, nave no
// ponto de partida, pontuação zerada.
void world_restart(World *w, const Tuning *t);

// A partida está andando (câmera de jogo, nave em cena)?
bool world_in_play(const World *w);

// Contexto para os inimigos pensarem (também usado nos testes).
EnemyContext world_enemy_context(World *w, const Tuning *t);

// Um impacto vindo de `origin` na direção `dir`: acha onde o raio toca o solo
// e abre ali uma cratera com a boca de raio `rim_radius` e a profundidade
// `depth`, soltando destroços. Retorna false se o raio não acerta o planeta.
bool world_impact(World *w, const Tuning *t, Vector3 origin, Vector3 dir, float rim_radius, float depth);

// Retira uma esfera do planeta sem o estardalhaço de um impacto (a broca do
// Escavador, o feixe do Sifão): poucos destroços, sem tremor nem som.
void world_carve(World *w, const Tuning *t, Vector3 center, float radius);

// Ferramenta de debug: devolve o planeta ao estado inicial.
void world_reset_planet(World *w, const Tuning *t);

// Uma pancada na nave: quem desenha usa isto para tremer a câmera.
void world_jolt(World *w, float strength);

// Impacto de número `id` (1 = o primeiro), ou NULL se ele já saiu da fila.
const Impact *world_find_impact(const World *w, uint32_t id);

// Pose para desenhar: mistura a anterior com a atual. alpha em [0, 1].
CamPose world_view(const World *w, float alpha);

// Resumo numérico do estado. Duas execuções com a mesma seed e os mesmos
// inputs precisam chegar ao mesmo valor; é assim que se confere o determinismo.
uint64_t world_hash(const World *w);

static inline Vector3 cam_forward(CamPose p) { return quat_forward(p.rot); }
static inline Vector3 cam_right(CamPose p)   { return quat_right(p.rot); }
static inline Vector3 cam_up(CamPose p)      { return quat_up(p.rot); }

#endif
