#include "test.h"

#include <string.h>

#include "core/settings.h"

static void settings_round_trip(void)
{
    Settings a;
    settings_defaults(&a);
    a.volume = 0.3f;
    a.music = 0.0f;
    a.mouse = 2.25f;
    a.invert_y = true;
    a.fullscreen = true;
    a.vibration = false;

    char text[1024];
    CHECK(settings_format(&a, text, sizeof text) > 0);
    Settings b;
    settings_defaults(&b);
    CHECK(settings_parse(&b, text) == SET_COUNT);
    CHECK_NEAR(b.volume, 0.3f, 1e-4);
    CHECK_NEAR(b.music, 0.0f, 1e-4);
    CHECK_NEAR(b.mouse, 2.25f, 1e-4);
    CHECK(b.invert_y && b.fullscreen && !b.vibration);
}

static void settings_ignore_garbage_and_clamp(void)
{
    Settings s;
    settings_defaults(&s);
    float sfx = s.sfx;
    const char *text = "# comentario\n"
                       "volume = 7\n"            // acima do máximo: vira 1
                       "mouse = -2\n"            // abaixo do mínimo
                       "sfx = alto\n"            // não é número: ignorado
                       "nao_existe = 1\n"
                       "linha sem igual\n"
                       "shake = 0.5   # metade\r\n";
    CHECK(settings_parse(&s, text) == 3);
    CHECK_NEAR(s.volume, 1.0f, 1e-6);
    CHECK_NEAR(s.mouse, 0.25f, 1e-6);
    CHECK_NEAR(s.sfx, sfx, 1e-6);
    CHECK_NEAR(s.shake, 0.5f, 1e-6);
}

static void settings_steps_stay_on_the_grid(void)
{
    Settings s;
    settings_defaults(&s);
    s.volume = 0.0f;
    for (int i = 0; i < 7; i++) settings_step(&s, SET_VOLUME, +1);
    CHECK_NEAR(s.volume, 0.7f, 1e-5);
    for (int i = 0; i < 20; i++) settings_step(&s, SET_VOLUME, +1);
    CHECK_NEAR(s.volume, 1.0f, 1e-6);
    CHECK_STR(settings_value_text(&s, SET_VOLUME), "100%");

    bool before = s.invert_y;
    settings_step(&s, SET_INVERT_Y, -1);
    CHECK(s.invert_y != before);
    CHECK(settings_is_toggle(SET_INVERT_Y) && !settings_is_toggle(SET_MOUSE));

    settings_set_fraction(&s, SET_MOUSE, 0.0f);
    CHECK_NEAR(s.mouse, 0.25f, 1e-6);
    settings_set_fraction(&s, SET_MOUSE, 1.0f);
    CHECK_NEAR(s.mouse, 3.0f, 1e-6);
    CHECK_NEAR(settings_fraction(&s, SET_MOUSE), 1.0f, 1e-6);
    CHECK_STR(settings_value_text(&s, SET_MOUSE), "3.00x");
    for (int i = 0; i < SET_COUNT; i++) CHECK(strlen(settings_label((SettingId)i)) > 0);
}

void suite_settings(void)
{
    RUN(settings_round_trip);
    RUN(settings_ignore_garbage_and_clamp);
    RUN(settings_steps_stay_on_the_grid);
}
