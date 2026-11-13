// Pool: vagas de tamanho fixo para objetos que nascem e morrem durante o jogo
// (inimigos, tiros), sem malloc.
//
// O pool só controla QUAIS vagas estão ocupadas; os dados ficam em um array
// paralelo, do mesmo tamanho, no módulo que usa o pool.
//
// Handle: para guardar uma referência a um objeto de um passo para o outro
// (o alvo de um tiro, quem soltou uma bomba), nunca se guarda o ponteiro nem
// só o índice: a vaga pode ter sido liberada e reocupada por outro objeto.
// O handle junta o índice com a "geração" da vaga, que muda cada vez que ela
// é liberada. Um handle velho deixa de ser reconhecido em vez de apontar para
// o objeto errado.
#ifndef CORE_POOL_H
#define CORE_POOL_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"

#define POOL_MAX_CAPACITY 65534

typedef struct {
    uint32_t value;   // 0 = nenhum objeto
} Handle;

typedef struct {
    uint16_t *generation;   // por vaga; muda a cada liberação
    uint16_t *free_list;    // pilha de vagas livres
    uint8_t  *alive;        // 1 = vaga ocupada
    int       capacity;
    int       free_count;
    int       live;         // vagas ocupadas agora
} Pool;

// Reserva os controles na arena. Retorna false se não couber.
bool pool_init(Pool *p, int capacity, Arena *arena);

// Libera todas as vagas. Os handles antigos deixam de valer.
void pool_clear(Pool *p);

// Ocupa uma vaga e devolve o índice dela, ou -1 se o pool está cheio. Com o
// pool recém-limpo, as vagas saem em ordem: 0, 1, 2...
int  pool_acquire(Pool *p);
void pool_release(Pool *p, int index);

Handle pool_handle(const Pool *p, int index);
int    pool_resolve(const Pool *p, Handle h);   // índice do objeto, ou -1 se ele não existe mais

static inline bool pool_alive(const Pool *p, int index) { return p->alive[index] != 0; }

#endif
