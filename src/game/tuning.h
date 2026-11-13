// Constantes de ajuste do jogo, lidas de dois arquivos:
//   data/tuning.txt    o jogo em geral (planeta, nave, câmera, arma, pontos...)
//   data/enemies.txt   os inimigos
//
// O código nunca usa números soltos: lê os campos desta struct. Para criar
// uma constante nova são três passos:
//   1. um campo aqui;
//   2. uma linha na tabela do arquivo certo em tuning.c (FIELDS ou
//      ENEMY_FIELDS), com a faixa de valores válida;
//   3. uma linha no arquivo de dados.
#ifndef GAME_TUNING_H
#define GAME_TUNING_H

#include <stdbool.h>
#include <stddef.h>

#include "core/color.h"
#include "core/config.h"
#include "core/vmath.h"
#include "game/enemy_types.h"
#include "game/evolution_types.h"
#include "voxel/mesher.h"
#include "voxel/planet.h"

#define TUNING_FILE     "tuning.txt"
#define ENEMIES_FILE    "enemies.txt"
#define EVOLUTIONS_FILE "evolutions.txt"

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
    float planet_population;   // bilhões de habitantes com o planeta inteiro (só para mostrar)

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
    float shake_jolt;
    float shake_falloff;
    float shake_max_angle;
    float shake_decay;
    float shake_frequency;

    // nave: movimento
    float ship_thrust;
    float ship_strafe;
    float ship_speed_max;
    float ship_brake;
    float ship_drag;
    float ship_grip;
    float ship_drift_drag;
    float ship_boost_mult;
    float ship_boost_cost;
    float ship_dash_speed;
    float ship_dash_time;
    float ship_dash_cost;
    float ship_dash_cooldown;
    float ship_mouse_sens;
    float ship_turn_rate;
    float ship_turn_smooth;
    float ship_roll_rate;
    float ship_auto_level;
    float ship_gravity;
    float ship_gravity_range;
    float ship_fall_drag;
    float ship_lift_speed;
    float ship_reverse_thrust;
    float ship_reverse_max;
    float ship_hover_speed;
    float ship_hover_drag;
    float ship_hug;
    float ship_hug_fade;
    float ship_spawn_pitch;

    // nave: colisão e recursos
    float ship_radius;
    float ship_bounce;
    float ship_crash_min;
    float ship_crash_damage;
    float ship_hull_max;
    float ship_shield_max;
    float ship_shield_regen;
    float ship_shield_delay;
    float ship_energy_max;
    float ship_energy_regen;
    float ship_respawn_time;
    float ship_spawn_distance;

    // câmera em terceira pessoa
    float chase_distance;
    float chase_height;
    float chase_look_ahead;
    float chase_pos_smooth;
    float chase_rot_smooth;
    float chase_fov_boost;
    float chase_clearance;
    float chase_ground_tilt;

    // Lua
    float moon_radius;
    float moon_orbit_radius;
    float moon_orbit_period;
    float moon_orbit_tilt;
    float moon_start_angle;
    int   moon_seed;
    int   moon_craters;
    Rgb8  moon_color;
    float moon_gravity;
    float moon_sling_range;
    float moon_sling_boost;
    float moon_sling_time;
    float moon_sling_speed;

    // arma principal do jogador
    float weapon_rate;
    float weapon_speed;
    float weapon_range;
    float weapon_damage;
    float weapon_graze_rate;
    float weapon_assist_angle;
    float weapon_assist_range;
    float weapon_hit_margin;
    float ship_graze_shield;

    // combate
    float combat_hitstop_big;
    float combat_hitstop_player;
    float combat_marker_distance;

    // pontuação
    float score_combo_window;
    float score_combo_step;
    float score_combo_max;
    float score_planet_bonus;
    float score_planet_bonus_hp;

    // áudio
    float audio_volume;
    float audio_falloff;

    // autoplay
    int   autoplay_warmup_frames;

    // --- data/enemies.txt ----------------------------------------------------
    EnemyStats enemy[ENEMY_TYPE_COUNT];   // o que todos os tipos têm

    float enemies_spawn_distance;
    float enemies_spawn_spread;
    float enemies_shot_life;

    // Enxame
    float dodge_speed;      // esquiva dos inimigos leves quando a nave atira neles
    float dodge_time;
    float dodge_cooldown;
    float dodge_cone;       // graus: a nave precisa estar mirando até este tanto deles
    float dodge_range;
    float dodge_chance;     // chance de esquivar a cada vez que isso acontece
    float swarm_cruise_altitude;
    float swarm_attack_altitude;
    float swarm_fire_rate;
    float swarm_shot_speed;
    float swarm_shot_damage;
    float swarm_crater_radius;
    float swarm_crater_depth;
    float swarm_aggro_range;
    float swarm_aggro_rate;
    float swarm_hold_time;
    float swarm_engage_distance;
    float swarm_engage_time;
    float swarm_engage_share;
    float swarm_spacing;
    float swarm_dive_stagger;
    float swarm_drift;

    // Bombardeiro
    float bomber_altitude;
    float bomber_bomb_interval;
    float bomber_bomb_warning;
    float bomber_bomb_speed;
    float bomber_bomb_accel;
    float bomber_bomb_hp;
    float bomber_bomb_radius;
    float bomber_bomb_points;
    float bomber_crater_radius;
    float bomber_crater_depth;
    float bomber_armor;
    float bomber_weak_mult;
    float bomber_weak_radius;
    float bomber_turret_range;
    float bomber_turret_interval;
    int   bomber_turret_burst;
    float bomber_turret_damage;

    // Kamikaze
    float kamikaze_aggro_range;
    float kamikaze_blast_radius;
    float kamikaze_crater_radius;
    float kamikaze_crater_depth;

    // Escavador
    float digger_drill_speed;
    float digger_drill_radius;
    float digger_drill_interval;

    // Sifão
    float siphon_altitude;
    float siphon_drain_radius;
    float siphon_drain_interval;
    float siphon_wander;

    // Atirador
    float sniper_range;
    float sniper_charge_time;
    float sniper_lock_time;
    float sniper_fire_interval;
    float sniper_shot_speed;
    float sniper_shot_damage;

    // Escudeiro
    float shielder_radius;
    float shielder_follow;

    // Porta-drones
    float carrier_altitude;
    float carrier_launch_interval;
    float carrier_launch_warning;
    int   carrier_launch_count;
    int   carrier_max_swarm;

    // --- data/evolutions.txt ------------------------------------------------
    float xp_base;                       // XP para chegar ao nível 2
    float xp_growth;                     // cada nível pede este tanto a mais
    float xp_kill[ENEMY_TYPE_COUNT];     // XP (e essência) de cada abate
    float xp_bomb;                       // bomba derrubada no ar
    float cards_base_weight;             // peso de cada evolução no sorteio...
    float cards_essence_weight;          // ... mais este tanto por essência da linha
    int   evo_max[EVO_COUNT];            // nível máximo de cada evolução
    float escort_damage, escort_rate, escort_range, escort_orbit;
    float spread_angle, spread_damage;
    float blast_dash_damage, blast_dash_radius;
    float chain_damage, chain_radius;
    float heavy_hull_hull;
    float warheads_fraction, warheads_radius;
    float armor_break_bonus;
    float lifesteal_hull;
    float energy_siphon_energy;
    float crit_chance, crit_mult, crit_far;
    float long_range_speed, long_range_assist;
    float big_shield_shield, big_shield_regen;
    float reflect_chance;
    float fighters_damage, fighters_rate, fighters_speed, fighters_range;
    float hangar_rate;
} Tuning;

#define CRATER_SIZE_COUNT 4

// Preenche `out` a partir dos dois arquivos já lidos. Se algo estiver errado
// (chave ausente, desconhecida, valor inválido ou fora da faixa), retorna
// false, escreve o motivo em `err` (começando pelo nome do arquivo) e NÃO
// altera `out`.
bool tuning_from_config(Tuning *out, const Config *tuning_cfg, const Config *enemies_cfg, char *err,
                        size_t err_len);

// Preenche em `out` só os campos de data/evolutions.txt (mesmas garantias).
bool tuning_evolutions_from_config(Tuning *out, const Config *evolutions_cfg, char *err, size_t err_len);

// Lê data_dir/tuning.txt, enemies.txt e evolutions.txt e preenche `out`.
// Mesmas garantias de tuning_from_config.
bool tuning_load(Tuning *out, const char *data_dir, char *err, size_t err_len);

// Traduções do tuning para as estruturas que os sistemas usam.
PlanetParams tuning_planet_params(const Tuning *t);
MeshStyle    tuning_mesh_style(const Tuning *t);
Vector3      tuning_sun_direction(const Tuning *t);   // do planeta para o Sol, comprimento 1

#endif
