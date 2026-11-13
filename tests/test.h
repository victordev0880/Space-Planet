// Framework de testes mínimo: só asserts e contadores.
//
// Cada arquivo test_*.c define funções de teste e uma função suite_* que as
// executa com RUN(). A suite é registrada em test_main.c.
#ifndef TEST_H
#define TEST_H

#include <math.h>
#include <stdbool.h>
#include <string.h>

extern int test_checks;
extern int test_failures;

void test_begin(const char *name);
void test_fail(const char *file, int line, const char *expr);

#define CHECK(cond) \
    do { test_checks++; if (!(cond)) test_fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { test_checks++; \
         if (!(fabs((double)(a) - (double)(b)) <= (double)(eps))) \
             test_fail(__FILE__, __LINE__, #a " ~= " #b); } while (0)

#define CHECK_STR(a, b) \
    do { test_checks++; \
         if (strcmp((a), (b)) != 0) test_fail(__FILE__, __LINE__, #a " == " #b); } while (0)

#define RUN(fn) do { test_begin(#fn); fn(); } while (0)

void suite_rng(void);
void suite_arena(void);
void suite_noise(void);
void suite_planet(void);
void suite_mesher(void);
void suite_damage(void);
void suite_ship(void);
void suite_moon(void);
void suite_pool(void);
void suite_waves(void);
void suite_combat(void);
void suite_enemies(void);
void suite_evolution(void);
void suite_boss(void);
void suite_rocks(void);
void suite_config(void);
void suite_loop(void);
void suite_input(void);
void suite_tuning(void);
void suite_world(void);
void suite_autoplay(void);

#endif
