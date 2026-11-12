// Dano ao planeta: escavar crateras no campo de densidade.
//
// Escavar é "subtrair uma esfera": cada voxel perto do ponto de impacto fica
// limitado à densidade que teria se ali houvesse a superfície de uma esfera
// vazia. A densidade cai aos poucos na borda, do mesmo jeito que cai na
// superfície do planeta, então a cratera sai lisa e com normais corretas.
// Só se retira matéria: escavar nunca aumenta densidade.
//
// O volume do planeta (o HP) é atualizado na hora, somando só a diferença.
#ifndef VOXEL_DAMAGE_H
#define VOXEL_DAMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "core/vmath.h"
#include "voxel/planet.h"

typedef struct {
    int64_t removed;                        // densidade total retirada
    int64_t removed_core;                   // quanto disso era núcleo
    int64_t removed_by_material[VMAT_COUNT];
    int     chunks_touched;                 // chunks marcados para remalhar
} CarveResult;

// Retira a matéria dentro de uma esfera. Posição e raio em unidades do mundo.
CarveResult planet_carve(Planet *p, Vector3 center, float radius);

// Esfera que escava, a partir de um ponto do solo, uma cratera com a boca de
// raio `rim_radius` e a profundidade `depth`. `normal` aponta para fora do
// solo. Devolve o centro da esfera e, em *sphere_radius, o raio dela.
// Cratera rasa e larga = esfera grande com o centro acima do solo; buraco
// fundo e estreito = esfera com o centro abaixo do solo.
Vector3 crater_sphere(Vector3 ground, Vector3 normal, float rim_radius, float depth, float *sphere_radius);

// Segue um raio até encontrar matéria. `dir` não precisa ter comprimento 1.
// Retorna false se o raio não acerta o planeta dentro de max_distance.
bool planet_raycast(const Planet *p, Vector3 origin, Vector3 dir, float max_distance, Vector3 *hit);

// Normal do solo em um ponto da superfície (aponta para fora da matéria).
Vector3 planet_normal(const Planet *p, Vector3 surface_point);

#endif
