#include "core/pool.h"
#include <stddef.h>

bool pool_init(Pool *p, int capacity, Arena *arena)
{
    *p = (Pool){ 0 };
    if (capacity <= 0 || capacity > POOL_MAX_CAPACITY) return false;
    p->generation = ARENA_ARRAY(arena, uint16_t, capacity);
    p->free_list = ARENA_ARRAY(arena, uint16_t, capacity);
    p->alive = ARENA_ARRAY(arena, uint8_t, capacity);
    if (p->generation == NULL || p->free_list == NULL || p->alive == NULL) return false;
    p->capacity = capacity;
    for (int i = 0; i < capacity; i++) p->generation[i] = 1;
    pool_clear(p);
    return true;
}

static void bump_generation(Pool *p, int index)
{
    p->generation[index]++;
    if (p->generation[index] == 0) p->generation[index] = 1;   // a geração 0 nunca é usada
}

void pool_clear(Pool *p)
{
    // A pilha fica com a vaga 0 no topo: as primeiras a sair são 0, 1, 2...
    for (int i = 0; i < p->capacity; i++) {
        if (p->alive[i]) bump_generation(p, i);
        p->alive[i] = 0;
        p->free_list[i] = (uint16_t)(p->capacity - 1 - i);
    }
    p->free_count = p->capacity;
    p->live = 0;
}

int pool_acquire(Pool *p)
{
    if (p->free_count == 0) return -1;
    int index = p->free_list[--p->free_count];
    p->alive[index] = 1;
    p->live++;
    return index;
}

void pool_release(Pool *p, int index)
{
    if (index < 0 || index >= p->capacity || !p->alive[index]) return;
    p->alive[index] = 0;
    bump_generation(p, index);
    p->free_list[p->free_count++] = (uint16_t)index;
    p->live--;
}

Handle pool_handle(const Pool *p, int index)
{
    if (index < 0 || index >= p->capacity || !p->alive[index]) return (Handle){ 0u };
    return (Handle){ ((uint32_t)p->generation[index] << 16) | (uint32_t)(index + 1) };
}

int pool_resolve(const Pool *p, Handle h)
{
    int index = (int)(h.value & 0xffffu) - 1;
    uint16_t generation = (uint16_t)(h.value >> 16);
    if (index < 0 || index >= p->capacity) return -1;
    if (!p->alive[index] || p->generation[index] != generation) return -1;
    return index;
}
