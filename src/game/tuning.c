#include "game/tuning.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum { TUNE_INT, TUNE_FLOAT, TUNE_COLOR } TuneKind;

typedef struct {
    const char *key;
    TuneKind    kind;
    size_t      offset;   // posição do campo dentro da struct Tuning
    double      min;
    double      max;
} TuneField;

#define I(key, field, lo, hi) { key, TUNE_INT,   offsetof(Tuning, field), lo, hi }
#define F(key, field, lo, hi) { key, TUNE_FLOAT, offsetof(Tuning, field), lo, hi }
#define C(key, field)         { key, TUNE_COLOR, offsetof(Tuning, field), 0, 255 }

// Uma linha por constante: chave no arquivo, campo na struct e faixa válida.
// A faixa existe para que um erro de digitação no tuning.txt vire uma mensagem
// clara em vez de um travamento (por exemplo, campo de visão igual a zero).
// Cores são escritas como três números de 0 a 255: "r g b".
static const TuneField FIELDS[] = {
    I("window.width",           window_width,           320,    7680),
    I("window.height",          window_height,          240,    4320),
    I("window.vsync",           window_vsync,           0,      1),
    I("window.msaa",            window_msaa,            0,      1),
    I("window.fps_cap",         window_fps_cap,         0,      1000),

    I("sim.hz",                 sim_hz,                 10,     240),
    I("sim.max_steps",          sim_max_steps,          1,      20),
    I("sim.seed",               sim_seed,               0,      2000000000),

    F("camera.fov",             camera_fov,             20,     120),
    F("camera.near",            camera_near,            0.001,  10),
    F("camera.far",             camera_far,             100,    100000),
    F("orbit.radius",           orbit_radius,           1,      10000),
    F("orbit.height",           orbit_height,           -10000, 10000),
    F("orbit.speed",            orbit_speed,            -360,   360),
    F("freecam.speed",          freecam_speed,          0.1,    10000),
    F("freecam.boost",          freecam_boost,          1,      100),
    F("freecam.smooth",         freecam_smooth,         0.1,    100),
    F("freecam.mouse_sens",     freecam_mouse_sens,     0.001,  5),

    I("stars.count",            stars_count,            0,      16000),
    I("stars.seed",             stars_seed,             0,      2000000000),
    F("stars.size_min",         stars_size_min,         0.0001, 0.2),
    F("stars.size_max",         stars_size_max,         0.0001, 0.2),
    F("stars.size_power",       stars_size_power,       0.1,    50),
    F("stars.brightness_min",   stars_brightness_min,   0,      1),
    F("stars.band_fraction",    stars_band_fraction,    0,      1),
    F("stars.band_width",       stars_band_width,       0.001,  1),
    F("stars.band_tilt",        stars_band_tilt,        -90,    90),

    I("planet.grid",            planet_grid,            32,     256),
    I("planet.chunk",           planet_chunk,           8,      VOXEL_MAX_CHUNK),
    I("planet.seed",            planet_seed,            0,      2000000000),
    F("planet.radius_frac",     planet_radius_frac,     0.1,    0.49),
    F("planet.land_height",     planet_land_height,     0,      20),
    F("planet.ocean_depth",     planet_ocean_depth,     0,      20),
    F("planet.land_bias",       planet_land_bias,       -1,     1),
    F("planet.noise_freq",      planet_noise_freq,      0.1,    20),
    I("planet.noise_octaves",   planet_noise_octaves,   1,      8),
    F("planet.relief_span",     planet_relief_span,     0.05,   1),
    F("planet.moisture_freq",   planet_moisture_freq,   0.1,    20),
    F("planet.ice_latitude",    planet_ice_latitude,    0.1,    1.2),
    F("planet.surface_depth",   planet_surface_depth,   0.1,    20),
    F("planet.mantle_radius",   planet_mantle_radius,   0.05,   0.99),
    F("planet.core_radius",     planet_core_radius,     0.01,   0.95),
    F("biome.sand_height",      biome_sand_height,      0,      20),
    F("biome.rock_height",      biome_rock_height,      0,      2),
    F("biome.snow_height",      biome_snow_height,      0,      2),
    F("biome.desert_moisture",  biome_desert_moisture,  -1,     1),
    F("biome.forest_moisture",  biome_forest_moisture,  -1,     1),

    C("color.water_shallow",    color_water_shallow),
    C("color.water",            color_water),
    C("color.water_deep",       color_water_deep),
    C("color.ice",              color_ice),
    C("color.sand",             color_sand),
    C("color.grass",            color_grass),
    C("color.forest",           color_forest),
    C("color.desert",           color_desert),
    C("color.rock",             color_rock),
    C("color.snow",             color_snow),
    C("color.crust",            color_crust),
    C("color.mantle",           color_mantle),
    C("color.core",             color_core),
    F("color.jitter",           color_jitter,           0,      0.5),
    F("emission.mantle",        emission_mantle,        0,      1),
    F("emission.core",          emission_core,          0,      1),
    F("gloss.water",            gloss_water,            0,      1),
    F("gloss.ice",              gloss_ice,              0,      1),

    F("shader.detail_scale",    shader_detail_scale,    0.01,   50),
    F("shader.detail_strength", shader_detail_strength, 0,      2),
    F("shader.bump_strength",   shader_bump_strength,   0,      20),
    F("shader.spec_strength",   shader_spec_strength,   0,      5),
    F("shader.spec_power",      shader_spec_power,      1,      1000),
    F("shader.pulse_amount",    shader_pulse_amount,    0,      1),
    F("shader.pulse_speed",     shader_pulse_speed,     0,      50),
    F("cavity.darkness",        cavity_darkness,        0,      1),

    F("sun.azimuth",            sun_azimuth,            -360,   360),
    F("sun.elevation",          sun_elevation,          -89,    89),
    F("sun.wrap",               sun_wrap,               0,      1),
    F("sun.size",               sun_size,               0.001,  1),
    F("sun.glow_size",          sun_glow_size,          0.001,  3),
    F("sun.glow_alpha",         sun_glow_alpha,         0,      1),
    C("sun.color",              sun_color),
    C("light.sun",              light_sun),
    C("light.ambient",          light_ambient),

    I("city.count",             city_count,             0,      16000),
    I("city.seed",              city_seed,              0,      2000000000),
    F("city.size",              city_size,              0.01,   10),
    F("city.brightness",        city_brightness,        0,      1),
    F("city.region_freq",       city_region_freq,       0.1,    30),
    F("city.region_threshold",  city_region_threshold,  -1,     1),
    I("city.cluster_max",       city_cluster_max,       1,      200),
    F("city.cluster_spread",    city_cluster_spread,    0.001,  1),
    F("city.lift",              city_lift,              0,      5),
    C("city.color",             city_color),

    F("atmo.height",            atmo_height,            0.001,  0.5),
    F("atmo.falloff",           atmo_falloff,           0.1,    16),
    F("atmo.strength",          atmo_strength,          0,      1),
    F("atmo.haze",              atmo_haze,              0,      1),
    C("atmo.color",             atmo_color),

    F("crater.small_radius",    crater_radius[0],       0.2,    60),
    F("crater.small_depth",     crater_depth[0],        0.2,    60),
    F("crater.medium_radius",   crater_radius[1],       0.2,    60),
    F("crater.medium_depth",    crater_depth[1],        0.2,    60),
    F("crater.large_radius",    crater_radius[2],       0.2,    60),
    F("crater.large_depth",     crater_depth[2],        0.2,    60),
    F("crater.huge_radius",     crater_radius[3],       0.2,    60),
    F("crater.huge_depth",      crater_depth[3],        0.2,    60),

    I("remesh.max_chunks",      remesh_max_chunks,      1,      512),
    F("remesh.budget_ms",       remesh_budget_ms,       0.1,    100),

    F("defeat.planet_hp",       defeat_planet_hp,       0,      1),
    F("defeat.core_hp",         defeat_core_hp,         0,      1),

    F("particles.gravity",      particles_gravity,      0,      200),
    F("particles.rock_count",   particles_rock_count,   0,      50),
    F("particles.rock_speed",   particles_rock_speed,   0,      200),
    F("particles.rock_life",    particles_rock_life,    0.05,   30),
    F("particles.rock_size",    particles_rock_size,    0.01,   10),
    F("particles.dust_count",   particles_dust_count,   0,      50),
    F("particles.dust_life",    particles_dust_life,    0.05,   30),
    F("particles.dust_size",    particles_dust_size,    0.01,   10),
    F("particles.ember_count",  particles_ember_count,  0,      50),
    F("particles.flash_size",   particles_flash_size,   0,      50),
    F("particles.flash_life",   particles_flash_life,   0.01,   5),

    F("shake.strength",         shake_strength,         0,      5),
    F("shake.falloff",          shake_falloff,          1,      10000),
    F("shake.max_angle",        shake_max_angle,        0,      30),
    F("shake.decay",            shake_decay,            0.05,   50),
    F("shake.frequency",        shake_frequency,        0.1,    200),

    I("autoplay.warmup_frames", autoplay_warmup_frames, 0,      1000),
};

#undef I
#undef F
#undef C

#define FIELD_COUNT ((int)(sizeof FIELDS / sizeof FIELDS[0]))

static bool is_known_key(const char *key)
{
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (strcmp(FIELDS[i].key, key) == 0) return true;
    }
    return false;
}

// Lê "r g b" (três inteiros de 0 a 255, e nada mais).
static bool parse_color(const char *text, Rgb8 *out)
{
    int r, g, b;
    char extra;
    if (sscanf(text, "%d %d %d %c", &r, &g, &b, &extra) != 3) return false;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return false;
    out->r = (uint8_t)r;
    out->g = (uint8_t)g;
    out->b = (uint8_t)b;
    return true;
}

bool tuning_from_config(Tuning *out, const Config *c, char *err, size_t err_len)
{
    Tuning t = *out;   // trabalha em uma cópia: `out` só muda se tudo der certo

    for (int i = 0; i < FIELD_COUNT; i++) {
        const TuneField *f = &FIELDS[i];
        const ConfigEntry *e = config_find(c, f->key);
        if (e == NULL) {
            snprintf(err, err_len, "chave ausente: %s", f->key);
            return false;
        }

        char *dst = (char *)&t + f->offset;

        if (f->kind == TUNE_COLOR) {
            Rgb8 color;
            if (!parse_color(e->text, &color)) {
                snprintf(err, err_len, "%s = \"%s\": esperado \"r g b\" de 0 a 255", f->key, e->text);
                return false;
            }
            memcpy(dst, &color, sizeof color);
            continue;
        }

        if (!e->is_number) {
            snprintf(err, err_len, "%s = \"%s\" nao e um numero", f->key, e->text);
            return false;
        }
        if (!(e->number >= f->min && e->number <= f->max)) {   // também rejeita NaN
            snprintf(err, err_len, "%s = %s fora da faixa [%g, %g]", f->key, e->text, f->min, f->max);
            return false;
        }

        if (f->kind == TUNE_INT) {
            if (e->number != floor(e->number)) {
                snprintf(err, err_len, "%s = %s precisa ser inteiro", f->key, e->text);
                return false;
            }
            int v = (int)e->number;
            memcpy(dst, &v, sizeof v);
        } else {
            float v = (float)e->number;
            memcpy(dst, &v, sizeof v);
        }
    }

    for (int i = 0; i < c->count; i++) {
        if (!is_known_key(c->entries[i].key)) {
            snprintf(err, err_len, "chave desconhecida: %s", c->entries[i].key);
            return false;
        }
    }

    // Regras que envolvem mais de um campo.
    if (t.stars_size_max < t.stars_size_min) {
        snprintf(err, err_len, "stars.size_max precisa ser >= stars.size_min");
        return false;
    }
    if (t.camera_far <= t.camera_near) {
        snprintf(err, err_len, "camera.far precisa ser maior que camera.near");
        return false;
    }
    if (t.planet_grid % t.planet_chunk != 0) {
        snprintf(err, err_len, "planet.grid precisa ser multiplo de planet.chunk");
        return false;
    }
    if (t.planet_core_radius >= t.planet_mantle_radius) {
        snprintf(err, err_len, "planet.core_radius precisa ser menor que planet.mantle_radius");
        return false;
    }
    float reach = t.planet_radius_frac * (float)t.planet_grid + t.planet_land_height + VOXEL_EDGE_MARGIN;
    if (reach > (float)(t.planet_grid / 2 - 1)) {
        snprintf(err, err_len, "o planeta nao cabe na grade: diminua planet.radius_frac ou planet.land_height");
        return false;
    }

    *out = t;
    if (err_len > 0) err[0] = '\0';
    return true;
}

bool tuning_load(Tuning *out, const char *path, char *err, size_t err_len)
{
    // static: a struct Config é grande (dezenas de KB) e não precisa ficar na pilha.
    static Config config;
    if (!config_load_file(&config, path)) {
        snprintf(err, err_len, "%s", config.error);
        return false;
    }
    return tuning_from_config(out, &config, err, err_len);
}

PlanetParams tuning_planet_params(const Tuning *t)
{
    PlanetParams pp;
    memset(&pp, 0, sizeof pp);
    pp.grid = t->planet_grid;
    pp.chunk = t->planet_chunk;
    pp.seed = (uint32_t)t->planet_seed;
    pp.radius = t->planet_radius_frac * (float)t->planet_grid;
    pp.land_height = t->planet_land_height;
    pp.ocean_depth = t->planet_ocean_depth;
    pp.land_bias = t->planet_land_bias;
    pp.noise_freq = t->planet_noise_freq;
    pp.noise_octaves = t->planet_noise_octaves;
    pp.relief_span = t->planet_relief_span;
    pp.moisture_freq = t->planet_moisture_freq;
    pp.ice_latitude = t->planet_ice_latitude;
    pp.sand_height = t->biome_sand_height;
    pp.rock_height = t->biome_rock_height;
    pp.snow_height = t->biome_snow_height;
    pp.desert_moisture = t->biome_desert_moisture;
    pp.forest_moisture = t->biome_forest_moisture;
    pp.surface_depth = t->planet_surface_depth;
    pp.mantle_radius = t->planet_mantle_radius;
    pp.core_radius = t->planet_core_radius;
    return pp;
}

MeshStyle tuning_mesh_style(const Tuning *t)
{
    MeshStyle s;
    memset(&s, 0, sizeof s);
    s.palette[VMAT_WATER_SHALLOW] = t->color_water_shallow;
    s.palette[VMAT_WATER] = t->color_water;
    s.palette[VMAT_WATER_DEEP] = t->color_water_deep;
    s.palette[VMAT_ICE] = t->color_ice;
    s.palette[VMAT_SAND] = t->color_sand;
    s.palette[VMAT_GRASS] = t->color_grass;
    s.palette[VMAT_FOREST] = t->color_forest;
    s.palette[VMAT_DESERT] = t->color_desert;
    s.palette[VMAT_ROCK] = t->color_rock;
    s.palette[VMAT_SNOW] = t->color_snow;
    s.palette[VMAT_CRUST] = t->color_crust;
    s.palette[VMAT_MANTLE] = t->color_mantle;
    s.palette[VMAT_CORE] = t->color_core;
    s.emission[VMAT_MANTLE] = (uint8_t)(t->emission_mantle * 255.0f + 0.5f);
    s.emission[VMAT_CORE] = (uint8_t)(t->emission_core * 255.0f + 0.5f);
    uint8_t water = (uint8_t)(t->gloss_water * 255.0f + 0.5f);
    s.gloss[VMAT_WATER_SHALLOW] = water;
    s.gloss[VMAT_WATER] = water;
    s.gloss[VMAT_WATER_DEEP] = water;
    s.gloss[VMAT_ICE] = (uint8_t)(t->gloss_ice * 255.0f + 0.5f);
    s.jitter = t->color_jitter;
    return s;
}

Vector3 tuning_sun_direction(const Tuning *t)
{
    float azimuth = t->sun_azimuth * DEG2RAD;
    float elevation = t->sun_elevation * DEG2RAD;
    return (Vector3){ cosf(elevation) * sinf(azimuth), sinf(elevation), cosf(elevation) * cosf(azimuth) };
}
