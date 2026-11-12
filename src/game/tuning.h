// Constantes de ajuste do jogo, lidas de data/tuning.txt.
//
// O código nunca usa números soltos: lê os campos desta struct. Para criar
// uma constante nova são três passos:
//   1. um campo aqui;
//   2. uma linha na tabela FIELDS de tuning.c (com a faixa de valores válida);
//   3. uma linha em data/tuning.txt.
#ifndef GAME_TUNING_H
#define GAME_TUNING_H

#include <stdbool.h>
#include <stddef.h>

#include "core/color.h"
#include "core/config.h"
#include "core/vmath.h"
#include "voxel/mesher.h"
#include "voxel/planet.h"

typedef struct {
    // janela
    int   window_width;
    int   window_height;
    int   window_vsync;
    int   window_msaa;
    int   window_fps_cap;

    // simulação
    int   sim_hz;
    int   sim_max_steps;
    int   sim_seed;

    // câmera
    float camera_fov;
    float camera_near;
    float camera_far;
    float orbit_radius;
    float orbit_height;
    float orbit_speed;
    float freecam_speed;
    float freecam_boost;
    float freecam_smooth;
    float freecam_mouse_sens;

    // estrelas
    int   stars_count;
    int   stars_seed;
    float stars_size_min;
    float stars_size_max;
    float stars_size_power;
    float stars_brightness_min;
    float stars_band_fraction;
    float stars_band_width;
    float stars_band_tilt;

    // planeta: forma
    int   planet_grid;
    int   planet_chunk;
    int   planet_seed;
    float planet_radius_frac;
    float planet_land_height;
    float planet_ocean_depth;
    float planet_land_bias;
    float planet_noise_freq;
    int   planet_noise_octaves;
    float planet_relief_span;
    float planet_moisture_freq;
    float planet_ice_latitude;
    float planet_surface_depth;
    float planet_mantle_radius;
    float planet_core_radius;
    float biome_sand_height;
    float biome_rock_height;
    float biome_snow_height;
    float biome_desert_moisture;
    float biome_forest_moisture;

    // planeta: cores
    Rgb8  color_water_shallow;
    Rgb8  color_water;
    Rgb8  color_water_deep;
    Rgb8  color_ice;
    Rgb8  color_sand;
    Rgb8  color_grass;
    Rgb8  color_forest;
    Rgb8  color_desert;
    Rgb8  color_rock;
    Rgb8  color_snow;
    Rgb8  color_crust;
    Rgb8  color_mantle;
    Rgb8  color_core;
    float color_jitter;
    float emission_mantle;
    float emission_core;
    float gloss_water;
    float gloss_ice;

    // shader do planeta (sem efeito com --no-shaders)
    float shader_detail_scale;
    float shader_detail_strength;
    float shader_bump_strength;
    float shader_spec_strength;
    float shader_spec_power;
    float shader_pulse_amount;
    float shader_pulse_speed;

    // crateras: quanto o fundo escurece (vale com e sem shader)
    float cavity_darkness;

    // sol e iluminação
    float sun_azimuth;
    float sun_elevation;
    float sun_wrap;
    float sun_size;
    float sun_glow_size;
    float sun_glow_alpha;
    Rgb8  sun_color;
    Rgb8  light_sun;
    Rgb8  light_ambient;

    // luzes de cidades
    int   city_count;
    int   city_seed;
    float city_size;
    float city_brightness;
    float city_region_freq;
    float city_region_threshold;
    int   city_cluster_max;
    float city_cluster_spread;
    float city_lift;
    Rgb8  city_color;

    // atmosfera
    float atmo_height;
    float atmo_falloff;
    float atmo_strength;
    float atmo_haze;
    Rgb8  atmo_color;

    // crateras da ferramenta de debug: quatro tamanhos (boca e profundidade)
    float crater_radius[4];
    float crater_depth[4];

    // remalhamento por quadro
    int   remesh_max_chunks;
    float remesh_budget_ms;

    // derrota
    float defeat_planet_hp;
    float defeat_core_hp;

    // partículas de impacto
    float particles_gravity;
    float particles_rock_count;
    float particles_rock_speed;
    float particles_rock_life;
    float particles_rock_size;
    float particles_dust_count;
    float particles_dust_life;
    float particles_dust_size;
    float particles_ember_count;
    float particles_flash_size;
    float particles_flash_life;

    // tremor de câmera
    float shake_strength;
    float shake_falloff;
    float shake_max_angle;
    float shake_decay;
    float shake_frequency;

    // autoplay
    int   autoplay_warmup_frames;
} Tuning;

#define CRATER_SIZE_COUNT 4

// Preenche `out` a partir de um Config já lido. Se algo estiver errado (chave
// ausente, desconhecida, valor inválido ou fora da faixa), retorna false,
// escreve o motivo em `err` e NÃO altera `out`.
bool tuning_from_config(Tuning *out, const Config *c, char *err, size_t err_len);

// Lê o arquivo e preenche `out`. Mesmas garantias de tuning_from_config.
bool tuning_load(Tuning *out, const char *path, char *err, size_t err_len);

// Traduções do tuning para as estruturas que os sistemas usam.
PlanetParams tuning_planet_params(const Tuning *t);
MeshStyle    tuning_mesh_style(const Tuning *t);
Vector3      tuning_sun_direction(const Tuning *t);   // do planeta para o Sol, comprimento 1

#endif
