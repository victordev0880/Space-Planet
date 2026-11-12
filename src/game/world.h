// Estado da simulação. Tudo que precisa ser determinístico mora aqui e só é
// alterado por world_tick, um passo fixo de cada vez.
//
// Por enquanto o mundo é o planeta e a câmera: uma câmera de apresentação que
// gira sozinha em volta do planeta, e a câmera livre de debug (F2).
#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/input.h"
#include "core/rng.h"
#include "core/vmath.h"
#include "game/particles.h"
#include "game/tuning.h"
#include "voxel/planet.h"

// Posição e orientação de uma câmera.
// yaw = 0 olha para -Z e cresce girando para a direita; pitch positivo olha
// para cima. Os ângulos ficam em radianos e o yaw não é "enrolado" em 360
// graus, para a interpolação entre dois passos nunca dar a volta errada.
typedef struct {
    Vector3 pos;
    float   yaw;
    float   pitch;
} CamPose;

typedef enum { CAM_ORBIT, CAM_FREE } CamMode;

// Um impacto no planeta. Quem desenha consulta os mais recentes para reagir
// (tremer a câmera, apagar luzes de cidades).
typedef struct {
    Vector3  pos;      // ponto atingido
    float    radius;   // raio da boca da cratera
    uint32_t id;       // 1 para o primeiro impacto da partida, 2 para o segundo...
} Impact;

#define WORLD_IMPACT_HISTORY 32

typedef struct {
    uint64_t  tick;         // quantos passos de lógica já rodaram
    Rng       rng;
    Planet    planet;
    Particles particles;
    Impact    impacts[WORLD_IMPACT_HISTORY];   // os últimos impactos, em fila circular
    uint32_t  impact_count;                    // total de impactos desde o início
    bool      collapsed;    // o planeta passou do ponto de colapso (derrota)
    CamMode   cam_mode;
    float     orbit_angle;  // radianos
    Vector3   free_vel;     // velocidade da câmera livre
    CamPose   view;         // pose depois do último passo
    CamPose   view_prev;    // pose um passo antes (para interpolar)
} World;

// Cria o mundo e gera o planeta, reservando a grade na arena. Retorna false
// se a arena não tiver espaço.
bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena);
void world_tick(World *w, const InputState *in, const Tuning *t);

// Depois de recarregar o tuning: se a receita do planeta mudou, gera o
// planeta de novo. Retorna true quando isso aconteceu.
bool world_apply_tuning(World *w, const Tuning *t);

// Ferramenta de debug e do autoplay: põe a câmera livre em `pos`, olhando
// para `target`.
void world_place_camera(World *w, Vector3 pos, Vector3 target);

// Um impacto vindo de `origin` na direção `dir`: acha onde o raio toca o solo
// e abre ali uma cratera com a boca de raio `rim_radius` e a profundidade
// `depth`, soltando destroços. Retorna false se o raio não acerta o planeta.
bool world_impact(World *w, const Tuning *t, Vector3 origin, Vector3 dir, float rim_radius, float depth);

// Ferramenta de debug: devolve o planeta ao estado inicial.
void world_reset_planet(World *w, const Tuning *t);

// Impacto de número `id` (1 = o primeiro), ou NULL se ele já saiu da fila.
const Impact *world_find_impact(const World *w, uint32_t id);

// Pose para desenhar: mistura a anterior com a atual. alpha em [0, 1].
CamPose world_view(const World *w, float alpha);

// Resumo numérico do estado. Duas execuções com a mesma seed e os mesmos
// inputs precisam chegar ao mesmo valor; é assim que se confere o determinismo.
uint64_t world_hash(const World *w);

Vector3 cam_forward(CamPose p);
Vector3 cam_right(CamPose p);

#endif
