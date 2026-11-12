#include "test.h"

#include "core/loop.h"

static void loop_one_step_per_frame_at_matching_rate(void)
{
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    for (int i = 0; i < 100; i++) CHECK(fixed_loop_advance(&l, 1.0 / 60.0) == 1);
    CHECK_NEAR(fixed_loop_alpha(&l), 0.0, 1e-9);
}

static void loop_slow_frames_run_several_steps(void)
{
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    CHECK(fixed_loop_advance(&l, 1.0 / 30.0) == 2);
    CHECK(fixed_loop_advance(&l, 3.5 / 60.0) == 3);
    CHECK_NEAR(fixed_loop_alpha(&l), 0.5, 1e-9);
}

static void loop_fast_frames_skip_steps(void)
{
    // Monitor de 120 Hz: um passo de lógica a cada dois quadros.
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    int total = 0;
    for (int i = 0; i < 120; i++) {
        int steps = fixed_loop_advance(&l, 1.0 / 120.0);
        CHECK(steps == 0 || steps == 1);
        total += steps;
    }
    CHECK(total == 60);
}

static void loop_alpha_is_the_leftover_fraction(void)
{
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    CHECK(fixed_loop_advance(&l, 1.5 / 60.0) == 1);
    CHECK_NEAR(fixed_loop_alpha(&l), 0.5, 1e-9);
    CHECK(fixed_loop_advance(&l, 0.25 / 60.0) == 0);
    CHECK_NEAR(fixed_loop_alpha(&l), 0.75, 1e-9);
}

static void loop_hitch_is_capped(void)
{
    // Travada de 2 segundos: roda só o teto e descarta o resto.
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    CHECK(fixed_loop_advance(&l, 2.0) == 5);
    double alpha = fixed_loop_alpha(&l);
    CHECK(alpha >= 0.0 && alpha < 1.0);
    // Depois da travada o ritmo volta ao normal.
    int steps = fixed_loop_advance(&l, 1.0 / 60.0);
    CHECK(steps == 1 || steps == 2);
}

static void loop_ignores_negative_time(void)
{
    FixedLoop l;
    fixed_loop_init(&l, 60, 5);
    CHECK(fixed_loop_advance(&l, -1.0) == 0);
    CHECK_NEAR(fixed_loop_alpha(&l), 0.0, 1e-12);
}

void suite_loop(void)
{
    RUN(loop_one_step_per_frame_at_matching_rate);
    RUN(loop_slow_frames_run_several_steps);
    RUN(loop_fast_frames_skip_steps);
    RUN(loop_alpha_is_the_leftover_fraction);
    RUN(loop_hitch_is_capped);
    RUN(loop_ignores_negative_time);
}
