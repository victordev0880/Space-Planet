#include "test.h"

#include "core/input.h"

static InputSample sample(uint32_t held, float dx, float dy)
{
    InputSample s = { held, dx, dy };
    return s;
}

static void input_reports_edges_once(void)
{
    InputTracker t;
    input_init(&t);
    uint32_t fire = ACT_BIT(ACT_FIRE);

    InputSample s = sample(fire, 0.0f, 0.0f);
    input_feed(&t, &s);
    InputState st = input_tick(&t);
    CHECK(st.held == fire && st.pressed == fire && st.released == 0u);

    input_feed(&t, &s);
    st = input_tick(&t);
    CHECK(st.held == fire && st.pressed == 0u && st.released == 0u);

    s = sample(0u, 0.0f, 0.0f);
    input_feed(&t, &s);
    st = input_tick(&t);
    CHECK(st.held == 0u && st.pressed == 0u && st.released == fire);
}

static void input_quick_tap_between_ticks_is_not_lost(void)
{
    // Dois quadros sem passo de lógica: a tecla desce em um e sobe no outro.
    InputTracker t;
    input_init(&t);
    uint32_t dash = ACT_BIT(ACT_DASH);

    InputSample down = sample(dash, 0.0f, 0.0f);
    InputSample up = sample(0u, 0.0f, 0.0f);
    input_feed(&t, &down);
    input_feed(&t, &up);

    InputState st = input_tick(&t);
    CHECK(st.pressed == dash && st.held == dash);
    st = input_tick(&t);
    CHECK(st.released == dash && st.held == 0u);
}

static void input_mouse_delta_accumulates_and_is_used_once(void)
{
    InputTracker t;
    input_init(&t);

    InputSample a = sample(0u, 3.0f, -1.0f);
    InputSample b = sample(0u, 2.0f, 4.0f);
    input_feed(&t, &a);
    input_feed(&t, &b);

    InputState st = input_tick(&t);
    CHECK_NEAR(st.look_dx, 5.0f, 1e-6);
    CHECK_NEAR(st.look_dy, 3.0f, 1e-6);

    // Segundo passo no mesmo quadro: o movimento já foi consumido.
    st = input_tick(&t);
    CHECK(st.look_dx == 0.0f && st.look_dy == 0.0f);
}

static void input_held_persists_across_ticks_in_one_frame(void)
{
    InputTracker t;
    input_init(&t);
    uint32_t fwd = ACT_BIT(ACT_FORWARD);

    InputSample s = sample(fwd, 0.0f, 0.0f);
    input_feed(&t, &s);
    InputState st1 = input_tick(&t);
    InputState st2 = input_tick(&t);
    CHECK(st1.pressed == fwd);
    CHECK(st2.held == fwd && st2.pressed == 0u && st2.released == 0u);
}

static void input_action_names_round_trip(void)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        const char *name = action_name((Action)i);
        CHECK(name != NULL && name[0] != '\0');
        CHECK(action_from_name(name) == i);
    }
    CHECK(action_from_name("nao_existe") == -1);
    CHECK(ACT_COUNT <= 32);   // as ações cabem em uma máscara de 32 bits
}

static void input_resync_swallows_the_closing_press(void)
{
    // O jogador fecha a pausa com Enter (que também é "chamar a horda") e
    // segura o clique: nada disso pode chegar ao jogo como "apertou".
    InputTracker t;
    input_init(&t);
    uint32_t held = ACT_BIT(ACT_READY) | ACT_BIT(ACT_FIRE);
    InputSample s = sample(held, 40.0f, 0.0f);
    input_feed(&t, &s);
    input_resync(&t, held);

    s = sample(held, 0.0f, 0.0f);
    input_feed(&t, &s);
    InputState st = input_tick(&t);
    CHECK(st.held == held && st.pressed == 0u && st.released == 0u);
    CHECK(st.look_dx == 0.0f);

    s = sample(0u, 0.0f, 0.0f);
    input_feed(&t, &s);
    st = input_tick(&t);
    CHECK(st.released == held);
}

void suite_input(void)
{
    RUN(input_resync_swallows_the_closing_press);
    RUN(input_reports_edges_once);
    RUN(input_quick_tap_between_ticks_is_not_lost);
    RUN(input_mouse_delta_accumulates_and_is_used_once);
    RUN(input_held_persists_across_ticks_in_one_frame);
    RUN(input_action_names_round_trip);
}
