#include "core/arena.h"

#include <stdlib.h>
#include <string.h>

bool arena_init(Arena *a, size_t size)
{
    a->base = malloc(size);
    a->size = (a->base != NULL) ? size : 0;
    a->used = 0;
    return a->base != NULL;
}

void arena_free(Arena *a)
{
    free(a->base);
    a->base = NULL;
    a->size = 0;
    a->used = 0;
}

void arena_reset(Arena *a)
{
    a->used = 0;
}

void *arena_alloc(Arena *a, size_t size, size_t align)
{
    if (align == 0) align = 1;

    // Arredonda a posição atual para cima até o próximo múltiplo de `align`.
    size_t start = (a->used + (align - 1)) & ~(align - 1);
    if (start > a->size || size > a->size - start) return NULL;

    a->used = start + size;
    void *p = a->base + start;
    memset(p, 0, size);
    return p;
}
