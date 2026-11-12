// Partículas de impacto: pedras, poeira, brasas e o clarão de uma explosão.
//
// São só efeito visual (não afetam o jogo), mas são simuladas em passos fixos
// junto com o resto do mundo, para o autoplay produzir sempre as mesmas imagens.
//
// Sem malloc durante o jogo: todas moram em um array de tamanho fixo,
// reservado uma vez. As vivas ficam juntas no começo do array; quando uma
// morre, a última viva é copiada para o lugar dela. Se o array encher, as
// novas simplesmente não nascem.
#ifndef GAME_PARTICLES_H
#define GAME_PARTICLES_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/rng.h"
#include "core/vmath.h"
#include "game/tuning.h"
#include "voxel/damage.h"
#include "voxel/planet.h"

#define PARTICLES_CAPACITY 8000

typedef enum {
    PARTICLE_ROCK,    // pedaço de rocha: cai de volta no planeta
    PARTICLE_DUST,    // nuvem de poeira: freia, cresce e some
    PARTICLE_EMBER,   // brasa: brilha por conta própria
    PARTICLE_FLASH,   // clarão da explosão: grande, parado, dura um instante
} ParticleKind;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float   age;      // segundos desde que nasceu
    float   life;     // idade em que morre
    float   size;
    uint8_t r, g, b;
    uint8_t kind;     // ParticleKind
} Particle;

typedef struct {
    Particle *items;
    int       capacity;
    int       count;   // vivas agora
    int       peak;    // máximo de vivas ao mesmo tempo
    int       dust;    // quantas das vivas são nuvens de poeira
} Particles;

bool particles_init(Particles *ps, Arena *arena);
void particles_clear(Particles *ps);

// Devolve uma partícula zerada para preencher, ou NULL se não há espaço.
Particle *particles_spawn(Particles *ps);

void particles_tick(Particles *ps, const Planet *planet, const Tuning *t, float dt);

// Solta os destroços de um impacto. `ground` é o ponto atingido, `normal`
// aponta para fora do solo, `rim_radius` é o raio da boca da cratera e
// `carve` diz quanto de cada material foi arrancado (define as cores).
void particles_impact(Particles *ps, Rng *rng, const Tuning *t, Vector3 ground, Vector3 normal,
                      float rim_radius, const CarveResult *carve);

#endif
