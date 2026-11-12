// Arena: um bloco grande de memória pedido ao sistema uma única vez, do qual
// os sistemas do jogo retiram pedaços em sequência.
//
// Regra do projeto: nada de malloc dentro do loop de jogo. Os dados de uma
// partida (grade de voxels, buffers de trabalho) saem de uma arena criada na
// inicialização. Não existe "liberar um pedaço": zera-se a arena inteira de
// uma vez (arena_reset), o que elimina vazamentos e fragmentação.
#ifndef CORE_ARENA_H
#define CORE_ARENA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t         size;
    size_t         used;
} Arena;

bool arena_init(Arena *a, size_t size);   // faz o único malloc
void arena_free(Arena *a);
void arena_reset(Arena *a);               // esvazia, mantendo o bloco

// Devolve `size` bytes zerados, alinhados a `align` (potência de 2), ou NULL
// se não couber.
void *arena_alloc(Arena *a, size_t size, size_t align);

// Aloca um array de `count` elementos do tipo `type`.
#define ARENA_ARRAY(arena, type, count) \
    ((type *)arena_alloc((arena), sizeof(type) * (size_t)(count), _Alignof(type)))

#endif
