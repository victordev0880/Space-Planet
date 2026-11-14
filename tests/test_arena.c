#include "test.h"
#include <stddef.h>

#include <stdint.h>

#include "core/arena.h"

static void arena_hands_out_zeroed_separate_blocks(void)
{
    Arena a;
    CHECK(arena_init(&a, 1024));

    unsigned char *first = arena_alloc(&a, 100, 1);
    unsigned char *second = arena_alloc(&a, 100, 1);
    CHECK(first != NULL && second != NULL);
    CHECK(second >= first + 100);   // não se sobrepõem

    bool zeroed = true;
    for (int i = 0; i < 100; i++) {
        if (first[i] != 0 || second[i] != 0) zeroed = false;
    }
    CHECK(zeroed);
    arena_free(&a);
}

static void arena_respects_alignment(void)
{
    Arena a;
    CHECK(arena_init(&a, 1024));
    arena_alloc(&a, 3, 1);   // desalinha de propósito
    void *p = arena_alloc(&a, 8, 8);
    double *d = ARENA_ARRAY(&a, double, 4);
    CHECK(p != NULL && ((uintptr_t)p % 8) == 0);
    CHECK(d != NULL && ((uintptr_t)d % _Alignof(double)) == 0);
    arena_free(&a);
}

static void arena_refuses_when_full(void)
{
    Arena a;
    CHECK(arena_init(&a, 256));
    CHECK(arena_alloc(&a, 200, 1) != NULL);
    CHECK(arena_alloc(&a, 100, 1) == NULL);    // não cabe
    CHECK(arena_alloc(&a, 56, 1) != NULL);     // o que sobrou ainda serve
    CHECK(arena_alloc(&a, 1, 1) == NULL);
    CHECK(arena_alloc(&a, (size_t)-1, 1) == NULL);   // tamanho absurdo não estoura a conta
    arena_free(&a);
}

static void arena_reset_reuses_and_rezeroes(void)
{
    Arena a;
    CHECK(arena_init(&a, 256));
    unsigned char *first = arena_alloc(&a, 200, 1);
    CHECK(first != NULL);
    if (first != NULL) memset(first, 0xAB, 200);

    arena_reset(&a);
    CHECK(a.used == 0);
    unsigned char *again = arena_alloc(&a, 200, 1);
    CHECK(again == first);
    CHECK(again != NULL && again[0] == 0 && again[199] == 0);
    arena_free(&a);
    CHECK(a.base == NULL && a.size == 0);
}

void suite_arena(void)
{
    RUN(arena_hands_out_zeroed_separate_blocks);
    RUN(arena_respects_alignment);
    RUN(arena_refuses_when_full);
    RUN(arena_reset_reuses_and_rezeroes);
}
