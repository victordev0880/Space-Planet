// Conserto do planeta: devolver voxels ao estado do começo da partida.
//
// É o contrário da cratera (damage.h): dentro de uma esfera, cada voxel volta
// a ter a densidade e o material originais (os de planet.original_*). Nunca
// passa do original: consertar onde nada foi destruído não muda nada, e o
// planeta não cresce além do que era. O relevo, a grama, a areia e o gelo
// voltam como eram.
//
// Na borda da esfera a mistura é gradual (uma faixa de alguns voxels), para a
// emenda entre o consertado e o que continua destruído sair lisa.
//
// O volume (o HP) e o volume do núcleo são atualizados na hora, somando só a
// diferença, e os chunks afetados são marcados para remalhar.
#ifndef VOXEL_TERRAFORM_H
#define VOXEL_TERRAFORM_H

#include <stdint.h>

#include "core/vmath.h"
#include "voxel/planet.h"

#define TERRAFORM_EDGE 2.0f   // largura da faixa de mistura na borda, em voxels

typedef struct {
    int64_t restored;        // densidade total devolvida
    int64_t restored_core;   // quanto disso era núcleo
    int     chunks_touched;
} RestoreResult;

// Devolve ao original a matéria dentro de uma esfera (posição e raio em
// unidades do mundo).
RestoreResult planet_restore(Planet *p, Vector3 center, float radius);

// Quanto falta (em densidade) para a esfera voltar ao original. Não muda nada.
int64_t planet_missing(const Planet *p, Vector3 center, float radius);

#endif
