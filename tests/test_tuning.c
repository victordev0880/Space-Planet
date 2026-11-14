#include "test.h"
#include <stddef.h>

#include "core/config.h"
#include "game/tuning.h"

#define TUNING_PATH  "data/tuning.txt"
#define ENEMIES_PATH "data/enemies.txt"

static Config cfg;           // data/tuning.txt
static Config enemies_cfg;   // data/enemies.txt
static char err[160];

static bool load_both(void)
{
    return config_load_file(&cfg, TUNING_PATH) && config_load_file(&enemies_cfg, ENEMIES_PATH);
}

static ConfigEntry *find_in(Config *c, const char *key)
{
    for (int i = 0; i < c->count; i++) {
        if (strcmp(c->entries[i].key, key) == 0) return &c->entries[i];
    }
    return NULL;
}

static ConfigEntry *entry(const char *key)
{
    return find_in(&cfg, key);
}

// Os arquivos reais do jogo precisam estar sempre válidos.
static void tuning_real_file_is_valid(void)
{
    Tuning t;
    memset(&t, 0, sizeof t);
    bool ok = tuning_load(&t, "data", err, sizeof err);
    if (!ok) test_fail(__FILE__, __LINE__, err);
    CHECK(ok);
    CHECK(t.sim_hz >= 10);
    CHECK(t.window_width >= 320);
    CHECK(t.camera_fov > 0.0f);
    for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
        CHECK(t.enemy[i].hp > 0.0f && t.enemy[i].radius > 0.0f && t.enemy[i].speed > 0.0f);
    }
    // A nave alcança qualquer inimigo e escapa de qualquer um.
    for (int i = 0; i < ENEMY_TYPE_COUNT; i++) CHECK(t.enemy[i].speed < t.ship_speed_max);
}

// Cada arquivo tem a sua tabela: uma chave no arquivo errado é rejeitada, e o
// erro diz em qual arquivo está o problema.
static void tuning_enemies_file_is_separate(void)
{
    CHECK(load_both());
    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK_NEAR(t.enemy[ENEMY_SWARM].hp, (float)find_in(&enemies_cfg, "swarm.hp")->number, 1e-6);
    CHECK_NEAR(t.bomber_armor, (float)find_in(&enemies_cfg, "bomber.armor")->number, 1e-6);

    ConfigEntry *e = find_in(&enemies_cfg, "bomber.hp");
    CHECK(e != NULL);
    if (e == NULL) return;
    strcpy(e->key, "bomber.hpp");
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, ENEMIES_FILE) != NULL && strstr(err, "bomber.hp") != NULL);

    // Trocar os dois arquivos de lugar quebra tudo.
    CHECK(load_both());
    CHECK(!tuning_from_config(&t, &enemies_cfg, &cfg, err, sizeof err));
}

static void tuning_missing_key_is_rejected(void)
{
    CHECK(load_both());
    ConfigEntry *e = entry("camera.fov");
    CHECK(e != NULL);
    if (e == NULL) return;
    strcpy(e->key, "camera.fovv");   // erro de digitação na chave

    Tuning t;
    memset(&t, 0, sizeof t);
    t.sim_hz = 12345;
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, "camera.fov") != NULL);
    CHECK(t.sim_hz == 12345);   // falhou: os valores antigos ficam intactos
}

static void tuning_unknown_key_is_rejected(void)
{
    CHECK(load_both());
    CHECK(cfg.count < CONFIG_MAX_ENTRIES);
    ConfigEntry *extra = &cfg.entries[cfg.count++];
    strcpy(extra->key, "chave.sobrando");
    strcpy(extra->text, "1");
    extra->number = 1.0;
    extra->is_number = true;

    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, "chave.sobrando") != NULL);
}

static void tuning_out_of_range_is_rejected(void)
{
    CHECK(load_both());
    ConfigEntry *e = entry("camera.fov");
    CHECK(e != NULL);
    if (e == NULL) return;

    Tuning t;
    memset(&t, 0, sizeof t);
    e->number = 0.0;
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, "fora da faixa") != NULL);

    e->is_number = false;
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
}

static void tuning_int_field_needs_integer(void)
{
    CHECK(load_both());
    ConfigEntry *e = entry("stars.count");
    CHECK(e != NULL);
    if (e == NULL) return;
    e->number = 10.5;

    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, "inteiro") != NULL);
}

static void tuning_values_are_copied(void)
{
    CHECK(load_both());
    ConfigEntry *count = entry("stars.count");
    ConfigEntry *fov = entry("camera.fov");
    CHECK(count != NULL && fov != NULL);
    if (count == NULL || fov == NULL) return;
    count->number = 123.0;
    fov->number = 75.5;

    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(t.stars_count == 123);
    CHECK_NEAR(t.camera_fov, 75.5f, 1e-6);
}

static void tuning_colors_are_three_numbers(void)
{
    CHECK(load_both());
    ConfigEntry *e = entry("color.grass");
    CHECK(e != NULL);
    if (e == NULL) return;

    Tuning t;
    memset(&t, 0, sizeof t);
    strcpy(e->text, "10 20 30");
    CHECK(tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(t.color_grass.r == 10 && t.color_grass.g == 20 && t.color_grass.b == 30);

    static const char *const BAD[] = { "10 20", "10 20 30 40", "10 20 300", "-1 0 0", "verde", "10, 20, 30" };
    for (int i = 0; i < (int)(sizeof BAD / sizeof BAD[0]); i++) {
        strcpy(e->text, BAD[i]);
        CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
        CHECK(strstr(err, "color.grass") != NULL);
    }
    CHECK(t.color_grass.g == 20);   // as tentativas com erro não mexeram no valor bom
}

static void tuning_planet_must_fit_the_grid(void)
{
    CHECK(load_both());
    ConfigEntry *e = entry("planet.land_height");
    CHECK(e != NULL);
    if (e == NULL) return;
    e->number = 12.0;   // montanhas que passariam da borda da grade

    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(!tuning_from_config(&t, &cfg, &enemies_cfg, err, sizeof err));
    CHECK(strstr(err, "nao cabe") != NULL);
}

static void tuning_translates_to_planet_and_style(void)
{
    Tuning t;
    memset(&t, 0, sizeof t);
    CHECK(tuning_load(&t, "data", err, sizeof err));

    PlanetParams pp = tuning_planet_params(&t);
    CHECK(pp.grid == t.planet_grid && pp.chunk == t.planet_chunk);
    CHECK_NEAR(pp.radius, t.planet_radius_frac * (float)t.planet_grid, 1e-4);
    CHECK(pp.seed == (uint32_t)t.planet_seed);
    CHECK(pp.core_radius < pp.mantle_radius);

    MeshStyle style = tuning_mesh_style(&t);
    CHECK(style.palette[VMAT_GRASS].g == t.color_grass.g);
    CHECK(style.palette[VMAT_CORE].r == t.color_core.r);
    CHECK(style.emission[VMAT_GRASS] == 0);
    CHECK(style.emission[VMAT_CORE] == (uint8_t)(t.emission_core * 255.0f + 0.5f));

    CHECK_NEAR(Vector3Length(tuning_sun_direction(&t)), 1.0f, 1e-5);
    t.sun_azimuth = 90.0f;
    t.sun_elevation = 0.0f;
    Vector3 sun = tuning_sun_direction(&t);
    CHECK_NEAR(sun.x, 1.0f, 1e-5);
    CHECK_NEAR(sun.y, 0.0f, 1e-5);
    CHECK_NEAR(sun.z, 0.0f, 1e-5);
}

void suite_tuning(void)
{
    RUN(tuning_colors_are_three_numbers);
    RUN(tuning_planet_must_fit_the_grid);
    RUN(tuning_translates_to_planet_and_style);
    RUN(tuning_real_file_is_valid);
    RUN(tuning_enemies_file_is_separate);
    RUN(tuning_missing_key_is_rejected);
    RUN(tuning_unknown_key_is_rejected);
    RUN(tuning_out_of_range_is_rejected);
    RUN(tuning_int_field_needs_integer);
    RUN(tuning_values_are_copied);
}
