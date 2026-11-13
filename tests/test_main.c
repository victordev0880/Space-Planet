#include "test.h"

#include <stdio.h>

int test_checks = 0;
int test_failures = 0;

static int test_count = 0;
static const char *current_test = "";
static int failures_at_start = 0;

static void finish_current(void)
{
    if (test_count == 0) return;
    printf("  %s  %s\n", (test_failures == failures_at_start) ? "ok   " : "FALHA", current_test);
}

void test_begin(const char *name)
{
    finish_current();
    test_count++;
    current_test = name;
    failures_at_start = test_failures;
}

void test_fail(const char *file, int line, const char *expr)
{
    test_failures++;
    printf("    %s:%d: falhou: %s\n", file, line, expr);
}

int main(void)
{
    suite_rng();
    suite_arena();
    suite_noise();
    suite_planet();
    suite_mesher();
    suite_damage();
    suite_ship();
    suite_moon();
    suite_config();
    suite_loop();
    suite_input();
    suite_tuning();
    suite_world();
    suite_pool();
    suite_waves();
    suite_combat();
    suite_enemies();
    suite_evolution();
    suite_boss();
    suite_autoplay();
    finish_current();

    printf("\n%d testes, %d verificacoes, %d falhas\n", test_count, test_checks, test_failures);
    return (test_failures == 0) ? 0 : 1;
}
