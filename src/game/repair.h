// Conserto do planeta: as cicatrizes (onde ele foi ferido) e os consertos em
// andamento (onde uma rocha caiu).
//
// Cicatriz: cada impacto marca uma região do planeta (direção e raio, na
// superfície). Impactos próximos se juntam numa cicatriz só. De tempos em
// tempos cada uma é conferida: se o chão no lugar voltou, ela some. O HUD as
// mostra para quem está rebocando uma rocha, e o piloto automático as usa
// como destino.
//
// Conserto: uma esfera que cresce do ponto onde a rocha bateu até o raio do
// conserto, devolvendo o terreno original (voxel/terraform.h). Crescer aos
// poucos faz o buraco se encher à vista, em vez de sumir num quadro.
#ifndef GAME_REPAIR_H
#define GAME_REPAIR_H

#include <stdbool.h>
#include <stdint.h>

#include "core/vmath.h"
#include "game/tuning.h"
#include "voxel/planet.h"

#define REPAIR_JOBS   6
#define REPAIR_SCARS  24
#define REPAIR_STEPS  8     // vezes que a esfera cresce até o tamanho final

typedef struct {
    bool    active;
    Vector3 center;     // centro da esfera (abaixo de onde era a superfície)
    Vector3 ground;     // onde a rocha bateu
    float   radius;     // tamanho final
    float   age;
    int     step;       // passos de crescimento já feitos
    int64_t restored;   // densidade já devolvida
} RepairJob;

typedef struct {
    bool    used;
    Vector3 dir;        // direção do centro da cicatriz (comprimento 1)
    float   radius;     // tamanho na superfície
} Scar;

typedef struct {
    RepairJob jobs[REPAIR_JOBS];
    Scar      scars[REPAIR_SCARS];
    int       cursor;          // próxima cicatriz a conferir
    int64_t   restored_total;  // densidade devolvida na partida (estatística)

    // Consertos que terminaram neste passo (o mundo dá pontos e avisa).
    RepairJob finished[REPAIR_JOBS];
    int       finished_count;
} Repair;

void repair_reset(Repair *r);

// Um impacto em `ground` (ponto do solo) com boca de raio `radius`.
void repair_note_damage(Repair *r, Vector3 ground, float radius);

// Uma rocha bateu em `at`: começa um conserto de raio `fill`. Retorna false se
// já há consertos demais andando.
bool repair_start(Repair *r, const Planet *p, const Tuning *t, Vector3 at, float fill);

// Faz os consertos crescerem e confere uma cicatriz.
void repair_tick(Repair *r, Planet *p, const Tuning *t, float dt);

// O ponto da superfície original no centro da cicatriz `i` (que precisa
// estar em uso).
Vector3 repair_scar_point(const Planet *p, const Scar *s);

// Altura da superfície original (distância ao centro) na direção `dir`.
float repair_original_top(const Planet *p, Vector3 dir);

int repair_active_jobs(const Repair *r);

#endif
