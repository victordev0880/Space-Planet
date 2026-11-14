// Defesas planetárias, compradas com minério e instaladas onde a nave está.
//
//   TORRE     parada no alto, acima do ponto do solo sob a nave; atira nos
//             inimigos que chegam perto (tiros de aliado: não quebram rochas).
//   ESCUDO    cúpula sobre o solo sob a nave; segura tudo o que cavaria o
//             planeta ali dentro, gastando vida pelo tamanho de cada cratera,
//             até a vida ou o tempo acabarem.
//   SATÉLITE  orbita o planeta e derruba bombas e tiros inimigos ao alcance.
//
// Como os inimigos, a lógica não conhece quem desenha nem quem toca os sons:
// anota eventos.
#ifndef GAME_DEFENSES_H
#define GAME_DEFENSES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "core/vmath.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/projectiles.h"
#include "game/tuning.h"

#define DEFENSES_MAX 12

typedef enum { DEF_TURRET, DEF_SHIELD, DEF_SATELLITE, DEF_TYPE_COUNT } DefenseType;

typedef struct {
    bool    alive;
    uint8_t type;        // DefenseType
    Vector3 pos, pos_prev;
    Vector3 dir;         // escudo: direção do centro (do centro do planeta); torre: para onde mira
    Vector3 axis;        // satélite: eixo da órbita
    float   rate;        // satélite: velocidade angular (rad/s)
    float   hp;          // escudo: vida que resta
    float   life;        // escudo: segundos que restam
    float   wait;        // segundos até poder atirar (ou interceptar) de novo
    float   flash;       // clarão de um tiro ou de um golpe segurado (só desenho)
    Vector3 zap;         // satélite: onde foi a última interceptação (só desenho)
    float   zap_time;
    float   age;
} Defense;

typedef struct {
    Defense items[DEFENSES_MAX];
} Defenses;

typedef struct {
    float         planet_radius;
    Enemies      *enemies;
    Projectiles  *shots;
    Events       *events;
    const Tuning *t;
    uint32_t      tick;
    float         dt;
} DefenseContext;

void defenses_reset(Defenses *d);
int  defenses_count(const Defenses *d, DefenseType type);
int  defense_cost(const Tuning *t, DefenseType type);
int  defense_max(const Tuning *t, DefenseType type);
const char *defense_name(DefenseType type);

// Instala uma defesa a partir da posição e do nariz da nave. A torre fica um
// pouco à frente (se nascesse onde a nave está, taparia a câmera); o escudo e
// o satélite, sob ela. NULL se já há o máximo.
Defense *defenses_build(Defenses *d, const Tuning *t, DefenseType type, Vector3 ship_pos, Vector3 ship_forward,
                        float planet_radius, Rng *rng);

// A mesma coisa, sobre o ponto do planeta na direção `dir` (o modo de
// construção, que mostra a sombra da defesa onde a mira aponta).
Defense *defenses_build_at(Defenses *d, const Tuning *t, DefenseType type, Vector3 dir, float planet_radius, Rng *rng);

void defenses_tick(Defenses *d, const DefenseContext *ctx);

// Um golpe vai cavar o planeta em `ground` (cratera de raio `radius`). Se um
// escudo cobre o lugar, ele segura (perde vida) e devolve true: não há cratera.
bool defenses_absorb(Defenses *d, const Tuning *t, float planet_radius, Vector3 ground, float radius, Events *events,
                     uint32_t tick);

#endif
