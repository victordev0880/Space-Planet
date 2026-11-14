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
#define BOSSES_FILE     "bosses.txt"

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
    float ship_pitch_limit;    // o nariz sobe ou desce até este tanto (graus) do horizonte
    float ship_bank;           // inclinação de desenho máxima nas curvas (graus)
    float ship_bank_strafe;    // ... e a do deslocamento lateral
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
    float weapon_knockback;    // empurrão de cada tiro num inimigo (velocidade, dividida pelo tamanho dele)
    // Mísseis (botão direito) e esquiva perfeita.
    int   missile_count;
    float missile_recharge, missile_lock_time, missile_cone, missile_range, missile_damage, missile_radius;
    float missile_speed, missile_turn, missile_life;
    float dodge_focus, dodge_mult, dodge_hitstop, dodge_energy;
    float ship_graze_shield;

    // combate
    float combat_hitstop_big;
    float combat_hitstop_player;
    float combat_marker_distance;

    // pontuação
    float waves_hp_growth;   // vida extra dos inimigos a cada horda (fração da vida do tipo)
    float score_combo_window;
    float score_combo_step;
    float score_combo_max;
    float score_planet_bonus;
    float score_planet_bonus_hp;

    // raio trator, rochas e conserto do planeta (marco 9)
    float tractor_range, tractor_cone, tractor_distance, tractor_drop, tractor_stiffness, tractor_damping;
    float tractor_break;
    float tractor_slow;
    float rock_fall_accel, rock_fall_drag, rock_hit_damage, rock_hit_speed;
    float fragment_volume, fragment_min_radius, fragment_radius_min, fragment_radius_max, fragment_fill;
    float fragment_alt_min, fragment_alt_max, fragment_speed, fragment_eject_time;
    int   fragment_max, fragment_per_impact;
    float meteor_interval, meteor_speed, meteor_pass_min, meteor_pass_max, meteor_radius_min, meteor_radius_max;
    float meteor_fill, meteor_distance, meteor_calm_gap, meteor_aim;
    int   meteor_calm_count;
    float repair_time, repair_depth, repair_points, repair_xp;

    // mineração e defesas planetárias (marco 9, parte 2)
    float fragment_hp, meteor_hp;
    int   ore_fragment, ore_meteor;
    float ore_magnet, ore_pull, ore_speed, ore_life, ore_collect, ore_drag;
    int   turret_cost, turret_max, shield_cost, shield_max, satellite_cost, satellite_max;
    float turret_altitude, turret_range, turret_rate, turret_damage;
    float shield_radius, shield_hp, shield_life;
    float satellite_altitude, satellite_speed, satellite_range, satellite_rate;

    // áudio
    float audio_volume;
    float audio_falloff;
    float audio_danger;

    float gamepad_deadzone;
    float gamepad_curve;
    float gamepad_look_speed;
    float gamepad_move;
    float gamepad_trigger;

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
    float swarm_siege;     // o bando circula a região onde chegou, num círculo deste raio (graus)

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
    // Sinergias.
    float cards_synergy_weight;          // peso de uma sinergia já liberada no sorteio
    float suicide_interval, suicide_range, suicide_damage, suicide_radius, suicide_speed;
    float restore_fraction;
    int   salvage_small, salvage_big;
    float poise_mult;

    // --- data/bosses.txt ----------------------------------------------------
    // Rompe-Mundos. Os vetores [3] valem por fase (1, 2 e 3).
    float breaker_hp, breaker_scale, breaker_altitude, breaker_speed, breaker_points, breaker_xp;
    float breaker_enter_time, breaker_spawn_distance;
    float breaker_rest[3];              // segundos entre o fim de um corte e a próxima carga
    float breaker_charge[3];            // segundos de carga (com o traço do corte no chão)
    float breaker_cut_length[3];        // comprimento do corte, em graus de arco
    float breaker_turret_interval[3];
    float breaker_fire_time, breaker_cut_radius, breaker_cut_sink, breaker_cut_spacing;
    float breaker_cut_turn;   // graus que a órbita gira, no máximo, antes de cada carga
    float breaker_beam_damage, breaker_beam_width;
    float breaker_capacitor_hp, breaker_stagger_time, breaker_core_armor, breaker_core_open;
    float breaker_phase_2, breaker_phase_3;
    int   breaker_turret_burst;
    float breaker_turret_damage, breaker_turret_range, breaker_turret_speed;
    float breaker_launch_interval;
    int   breaker_launch_count;
    float breaker_ram_damage;
    // Núcleo Rompe-Mundos (a recompensa): o raio no botão direito.
    float ability_damage, ability_range, ability_width, ability_cooldown, ability_charge;

    // Colmeia Lunar (data/bosses.txt).
    float hive_hp, hive_points, hive_xp, hive_enter_time, hive_spawn_distance;
    float hive_sac_hp, hive_sac_regrow, hive_inflate_time;
    float hive_inflate[3];           // segundos entre uma bolsa e a próxima inchar, por fase
    int   hive_brood, hive_brood_cap;
    float hive_open_time, hive_core_armor, hive_core_open, hive_phase_2, hive_phase_3;
    float hive_thruster_hp, hive_thruster_regrow, hive_push_rate, hive_recede_rate, hive_moon_min;
    float hive_tide_start, hive_tide_radius, hive_tide_sink, hive_tide_spread, hive_tide_slow, hive_tide_fast;
    float hive_spit_interval, hive_spit_warn, hive_spit_damage, hive_spit_speed;
    float hive_spore_size, hive_spore_life, hive_spore_throw, hive_spore_damage, hive_spore_blast;
    // Núcleo Colmeia (a recompensa): a Ninhada, na tecla G.
    int   brood_count;
    float brood_damage, brood_speed, brood_turn, brood_life, brood_cooldown;

    // Verme do Núcleo (data/bosses.txt).
    float worm_hp, worm_points, worm_xp, worm_enter_time, worm_spawn_distance;
    float worm_scale, worm_spacing, worm_speed, worm_swim_speed, worm_turn, worm_depth;
    float worm_tunnel_radius, worm_tunnel_spacing, worm_tunnel_life, worm_tunnel_guard;
    float worm_rumble_time, worm_breach_time, worm_breach_height, worm_breach_reach;
    float worm_breach_radius, worm_breach_depth, worm_breach_blast, worm_breach_damage;
    float worm_mouth_armor, worm_nodule_hp, worm_nodule_damage, worm_tail_hp, worm_panic_time;
    float worm_phase_2, worm_phase_3, worm_core_bite;
    int   worm_larvae;
    float worm_swim_depth, worm_swim_time, worm_furrow_radius;
    // Núcleo do Verme (a recompensa): o Túnel, na tecla X.
    float tunnel_cooldown, tunnel_altitude, tunnel_time;

    // Devorador, o chefe final (data/bosses.txt).
    float dev_hp, dev_points, dev_enter_time, dev_spawn_distance, dev_scale, dev_altitude, dev_speed;
    float dev_vortex_range, dev_vortex_pull, dev_heal_fragment, dev_heal_meteor, dev_heal_ore;
    float dev_bite_interval[2];      // segundos entre mordidas: faminto, frenético
    float dev_aim_time, dev_aim_altitude, dev_dive_time, dev_chew_time, dev_rise_time;
    float dev_bite_radius, dev_bite_depth, dev_bite_spread;
    float dev_flinch, dev_flinch_stun, dev_broken_stun, dev_throat_armor;
    float dev_frenzy_hp, dev_feast_hp;
    float dev_teeth_interval, dev_teeth_damage, dev_teeth_speed;
    int   dev_teeth_count;
    float dev_latch_drain, dev_latch_radius, dev_latch_break, dev_latch_cooldown, dev_torn_stun;
    int   dev_regurgitate;
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

// ... e só os de data/bosses.txt.
bool tuning_bosses_from_config(Tuning *out, const Config *bosses_cfg, char *err, size_t err_len);

// Lê data_dir/tuning.txt, enemies.txt e evolutions.txt e preenche `out`.
// Mesmas garantias de tuning_from_config.
bool tuning_load(Tuning *out, const char *data_dir, char *err, size_t err_len);

// Traduções do tuning para as estruturas que os sistemas usam.
PlanetParams tuning_planet_params(const Tuning *t);
MeshStyle    tuning_mesh_style(const Tuning *t);
Vector3      tuning_sun_direction(const Tuning *t);   // do planeta para o Sol, comprimento 1

#endif
