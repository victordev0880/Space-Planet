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
    F("planet.population",      planet_population,      0.001,  1000),

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
    F("shake.jolt",             shake_jolt,             0,      5),
    F("shake.falloff",          shake_falloff,          1,      10000),
    F("shake.max_angle",        shake_max_angle,        0,      30),
    F("shake.decay",            shake_decay,            0.05,   50),
    F("shake.frequency",        shake_frequency,        0.1,    200),

    F("ship.thrust",            ship_thrust,            0,      1000),
    F("ship.strafe",            ship_strafe,            0,      1000),
    F("ship.speed_max",         ship_speed_max,         1,      1000),
    F("ship.brake",             ship_brake,             0,      50),
    F("ship.drag",              ship_drag,              0,      50),
    F("ship.grip",              ship_grip,              0,      50),
    F("ship.drift_drag",        ship_drift_drag,        0,      50),
    F("ship.boost_mult",        ship_boost_mult,        1,      10),
    F("ship.boost_cost",        ship_boost_cost,        0,      1000),
    F("ship.dash_speed",        ship_dash_speed,        0,      1000),
    F("ship.dash_time",         ship_dash_time,         0.01,   5),
    F("ship.dash_cost",         ship_dash_cost,         0,      1000),
    F("ship.dash_cooldown",     ship_dash_cooldown,     0,      30),
    F("ship.mouse_sens",        ship_mouse_sens,        0.001,  5),
    F("ship.turn_rate",         ship_turn_rate,         1,      2000),
    F("ship.turn_smooth",       ship_turn_smooth,       0.1,    200),
    F("ship.roll_rate",         ship_roll_rate,         0,      2000),
    F("ship.auto_level",        ship_auto_level,        0,      50),
    F("ship.gravity",           ship_gravity,           0,      200),
    F("ship.gravity_range",     ship_gravity_range,     1.01,   50),
    F("ship.fall_drag",         ship_fall_drag,         0.05,   50),
    F("ship.lift_speed",        ship_lift_speed,        0.1,    1000),
    F("ship.reverse_thrust",    ship_reverse_thrust,    0,      1000),
    F("ship.reverse_max",       ship_reverse_max,       0,      1000),
    F("ship.hover_speed",       ship_hover_speed,       0.1,    1000),
    F("ship.hover_drag",        ship_hover_drag,        0,      50),
    F("ship.hug",               ship_hug,               0,      1),
    F("ship.hug_fade",          ship_hug_fade,          0.1,    100),
    F("ship.spawn_pitch",       ship_spawn_pitch,       -80,    80),

    F("ship.radius",            ship_radius,            0.1,    20),
    F("ship.bounce",            ship_bounce,            0,      1),
    F("ship.crash_min",         ship_crash_min,         0,      1000),
    F("ship.crash_damage",      ship_crash_damage,      0,      100),
    F("ship.hull_max",          ship_hull_max,          1,      100000),
    F("ship.shield_max",        ship_shield_max,        0,      100000),
    F("ship.shield_regen",      ship_shield_regen,      0,      100000),
    F("ship.shield_delay",      ship_shield_delay,      0,      60),
    F("ship.energy_max",        ship_energy_max,        1,      100000),
    F("ship.energy_regen",      ship_energy_regen,      0,      100000),
    F("ship.respawn_time",      ship_respawn_time,      0.1,    60),
    F("ship.spawn_distance",    ship_spawn_distance,    1.1,    20),

    F("chase.distance",         chase_distance,         0.5,    200),
    F("chase.height",           chase_height,           -50,    50),
    F("chase.look_ahead",       chase_look_ahead,       0,      500),
    F("chase.pos_smooth",       chase_pos_smooth,       0.1,    200),
    F("chase.rot_smooth",       chase_rot_smooth,       0.1,    200),
    F("chase.fov_boost",        chase_fov_boost,        0,      60),
    F("chase.clearance",        chase_clearance,        0,      10),
    F("chase.ground_tilt",      chase_ground_tilt,      0,      60),

    F("moon.radius",            moon_radius,            0.02,   1),
    F("moon.orbit_radius",      moon_orbit_radius,      1.5,    50),
    F("moon.orbit_period",      moon_orbit_period,      5,      100000),
    F("moon.orbit_tilt",        moon_orbit_tilt,        -89,    89),
    F("moon.start_angle",       moon_start_angle,       -360,   360),
    I("moon.seed",              moon_seed,              0,      2000000000),
    I("moon.craters",           moon_craters,           0,      400),
    C("moon.color",             moon_color),
    F("moon.gravity",           moon_gravity,           0,      200),
    F("moon.sling_range",       moon_sling_range,       1.05,   20),
    F("moon.sling_boost",       moon_sling_boost,       1,      10),
    F("moon.sling_time",        moon_sling_time,        0,      120),
    F("moon.sling_speed",       moon_sling_speed,       0,      10),

    F("weapon.rate",            weapon_rate,            0.5,    60),
    F("weapon.speed",           weapon_speed,           10,     2000),
    F("weapon.range",           weapon_range,           10,     2000),
    F("weapon.damage",          weapon_damage,          0.1,    1000),
    F("weapon.graze_rate",      weapon_graze_rate,      1,      5),
    F("weapon.assist_angle",    weapon_assist_angle,    0,      45),
    F("weapon.assist_range",    weapon_assist_range,    0,      2000),
    F("weapon.hit_margin",      weapon_hit_margin,      0,      20),
    F("ship.graze_shield",      ship_graze_shield,      1,      10),

    F("combat.hitstop_big",     combat_hitstop_big,     0,      0.5),
    F("combat.hitstop_player",  combat_hitstop_player,  0,      0.5),
    F("combat.marker_distance", combat_marker_distance, 0,      5000),

    F("waves.hp_growth",        waves_hp_growth,        0,      1),
    F("score.combo_window",     score_combo_window,     0.1,    30),
    F("score.combo_step",       score_combo_step,       0,      5),
    F("score.combo_max",        score_combo_max,        1,      50),
    F("score.planet_bonus",     score_planet_bonus,     0,      1000000),
    F("score.planet_bonus_hp",  score_planet_bonus_hp,  0,      1),

    F("tractor.range",          tractor_range,          1,      500),
    F("tractor.cone",           tractor_cone,           1,      90),
    F("tractor.distance",       tractor_distance,       0,      100),
    F("tractor.drop",           tractor_drop,           0,      100),
    F("tractor.stiffness",      tractor_stiffness,      0.1,    500),
    F("tractor.damping",        tractor_damping,        0,      200),
    F("tractor.break",          tractor_break,          1,      1000),
    F("tractor.slow",           tractor_slow,           0,      0.9),
    F("rock.fall_accel",        rock_fall_accel,        0.1,    500),
    F("rock.fall_drag",         rock_fall_drag,         0,      50),
    F("rock.hit_damage",        rock_hit_damage,        0,      1000),
    F("rock.hit_speed",         rock_hit_speed,         0,      500),
    F("fragment.volume",        fragment_volume,        1,      1000000),
    F("fragment.min_radius",    fragment_min_radius,    0,      100),
    F("fragment.radius_min",    fragment_radius_min,    0.2,    20),
    F("fragment.radius_max",    fragment_radius_max,    0.2,    20),
    F("fragment.fill",          fragment_fill,          0.5,    60),
    F("fragment.alt_min",       fragment_alt_min,       1,      500),
    F("fragment.alt_max",       fragment_alt_max,       1,      500),
    F("fragment.speed",         fragment_speed,         0,      200),
    F("fragment.eject_time",    fragment_eject_time,    0.1,    20),
    I("fragment.max",           fragment_max,           0,      32),   // ROCKS_MAX (game/rocks.h)
    I("fragment.per_impact",    fragment_per_impact,    0,      16),
    F("meteor.interval",        meteor_interval,        1,      3600),
    F("meteor.speed",           meteor_speed,           0.5,    500),
    F("meteor.pass_min",        meteor_pass_min,        1,      500),
    F("meteor.pass_max",        meteor_pass_max,        1,      500),
    F("meteor.radius_min",      meteor_radius_min,      0.2,    20),
    F("meteor.radius_max",      meteor_radius_max,      0.2,    20),
    F("meteor.fill",            meteor_fill,            0.5,    60),
    F("meteor.distance",        meteor_distance,        0.5,    20),
    F("meteor.calm_gap",        meteor_calm_gap,        0,      120),
    F("meteor.aim",             meteor_aim,             0,      180),
    I("meteor.calm_count",      meteor_calm_count,      0,      8),
    F("repair.time",            repair_time,            0.05,   20),
    F("repair.depth",           repair_depth,           0,      1),
    F("repair.points",          repair_points,          0,      1000000),
    F("repair.xp",              repair_xp,              0,      1000),
    F("fragment.hp",            fragment_hp,            0.1,    1000),
    F("meteor.hp",              meteor_hp,              0.1,    1000),
    I("ore.fragment",           ore_fragment,           0,      40),
    I("ore.meteor",             ore_meteor,             0,      40),
    F("ore.magnet",             ore_magnet,             1,      500),
    F("ore.pull",               ore_pull,               1,      1000),
    F("ore.speed",              ore_speed,              1,      1000),
    F("ore.life",               ore_life,               1,      600),
    F("ore.collect",            ore_collect,            0.1,    50),
    F("ore.drag",               ore_drag,               0,      50),
    I("turret.cost",            turret_cost,            0,      10000),
    I("turret.max",             turret_max,             0,      4),
    F("turret.altitude",        turret_altitude,        1,      500),
    F("turret.range",           turret_range,           1,      2000),
    F("turret.rate",            turret_rate,            0.1,    60),
    F("turret.damage",          turret_damage,          0,      1000),
    I("shield.cost",            shield_cost,            0,      10000),
    I("shield.max",             shield_max,             0,      4),
    F("shield.radius",          shield_radius,          1,      200),
    F("shield.hp",              shield_hp,              1,      100000),
    F("shield.life",            shield_life,            1,      3600),
    I("satellite.cost",         satellite_cost,         0,      10000),
    I("satellite.max",          satellite_max,          0,      4),
    F("satellite.altitude",     satellite_altitude,     1,      500),
    F("satellite.speed",        satellite_speed,        0,      500),
    F("satellite.range",        satellite_range,        1,      2000),
    F("satellite.rate",         satellite_rate,         0.1,    60),

    F("audio.volume",          audio_volume,           0,      1),
    F("audio.falloff",          audio_falloff,          1,      10000),
    F("audio.danger",           audio_danger,           0,      1),
    F("gamepad.deadzone",       gamepad_deadzone,       0,      0.9),
    F("gamepad.curve",          gamepad_curve,          0.5,    4),
    F("gamepad.look_speed",     gamepad_look_speed,     10,     20000),
    F("gamepad.move",           gamepad_move,           0.05,   0.95),
    F("gamepad.trigger",        gamepad_trigger,        0.05,   0.95),

    I("autoplay.warmup_frames", autoplay_warmup_frames, 0,      1000),
};

// data/enemies.txt
static const TuneField ENEMY_FIELDS[] = {
    F("enemies.spawn_distance", enemies_spawn_distance, 1.5,    20),
    F("enemies.spawn_spread",   enemies_spawn_spread,   0,      200),
    F("enemies.shot_life",      enemies_shot_life,      0.1,    30),

    F("swarm.hp",               enemy[ENEMY_SWARM].hp,          0.1,  10000),
    F("swarm.radius",           enemy[ENEMY_SWARM].radius,      0.1,  50),
    F("swarm.scale",            enemy[ENEMY_SWARM].scale,       0.1,  50),
    F("swarm.speed",            enemy[ENEMY_SWARM].speed,       0.1,  500),
    F("swarm.accel",            enemy[ENEMY_SWARM].accel,       0.1,  1000),
    F("swarm.points",           enemy[ENEMY_SWARM].points,      0,    1000000),
    F("swarm.ram_damage",       enemy[ENEMY_SWARM].ram_damage,  0,    10000),
    F("enemies.dodge_speed",    dodge_speed,            0,      500),
    F("enemies.dodge_time",     dodge_time,             0,      10),
    F("enemies.dodge_cooldown", dodge_cooldown,         0,      60),
    F("enemies.dodge_cone",     dodge_cone,             0,      90),
    F("enemies.dodge_range",    dodge_range,            0,      2000),
    F("enemies.dodge_chance",   dodge_chance,           0,      1),
    F("swarm.cruise_altitude",  swarm_cruise_altitude,  1,      500),
    F("swarm.attack_altitude",  swarm_attack_altitude,  0.5,    500),
    F("swarm.fire_rate",        swarm_fire_rate,        0,      60),
    F("swarm.shot_speed",       swarm_shot_speed,       1,      2000),
    F("swarm.shot_damage",      swarm_shot_damage,      0,      10000),
    F("swarm.crater_radius",    swarm_crater_radius,    0.2,    60),
    F("swarm.crater_depth",     swarm_crater_depth,     0.2,    60),
    F("swarm.aggro_range",      swarm_aggro_range,      0,      2000),
    F("swarm.aggro_rate",       swarm_aggro_rate,       0,      60),
    F("swarm.hold_time",        swarm_hold_time,        0,      60),
    F("swarm.engage_distance",  swarm_engage_distance,  2,      500),
    F("swarm.engage_time",      swarm_engage_time,      0.5,    120),
    F("swarm.engage_share",     swarm_engage_share,     0,      1),
    F("swarm.spacing",          swarm_spacing,          0,      100),
    F("swarm.dive_stagger",     swarm_dive_stagger,     0,      30),
    F("swarm.drift",            swarm_drift,            0,      180),

    F("bomber.hp",              enemy[ENEMY_BOMBER].hp,         0.1,  10000),
    F("bomber.radius",          enemy[ENEMY_BOMBER].radius,     0.1,  50),
    F("bomber.scale",           enemy[ENEMY_BOMBER].scale,      0.1,  50),
    F("bomber.speed",           enemy[ENEMY_BOMBER].speed,      0.1,  500),
    F("bomber.accel",           enemy[ENEMY_BOMBER].accel,      0.1,  1000),
    F("bomber.points",          enemy[ENEMY_BOMBER].points,     0,    1000000),
    F("bomber.ram_damage",      enemy[ENEMY_BOMBER].ram_damage, 0,    10000),
    F("bomber.altitude",        bomber_altitude,        1,      500),
    F("bomber.bomb_interval",   bomber_bomb_interval,   0.5,    600),
    F("bomber.bomb_warning",    bomber_bomb_warning,    0,      60),
    F("bomber.bomb_speed",      bomber_bomb_speed,      0.1,    500),
    F("bomber.bomb_accel",      bomber_bomb_accel,      0,      500),
    F("bomber.bomb_hp",         bomber_bomb_hp,         0.1,    1000),
    F("bomber.bomb_radius",     bomber_bomb_radius,     0.1,    50),
    F("bomber.bomb_points",     bomber_bomb_points,     0,      1000000),
    F("bomber.crater_radius",   bomber_crater_radius,   0.2,    60),
    F("bomber.crater_depth",    bomber_crater_depth,    0.2,    60),
    F("bomber.armor",           bomber_armor,           0,      1),
    F("bomber.weak_mult",       bomber_weak_mult,       0,      100),
    F("bomber.weak_radius",     bomber_weak_radius,     0,      50),
    F("bomber.turret_range",    bomber_turret_range,    0,      2000),
    F("bomber.turret_interval", bomber_turret_interval, 0.1,    600),
    I("bomber.turret_burst",    bomber_turret_burst,    0,      50),
    F("bomber.turret_damage",   bomber_turret_damage,   0,      10000),

#define STATS(name, type)                                                       \
    F(name ".hp",         enemy[type].hp,         0.1, 10000),                  \
    F(name ".radius",     enemy[type].radius,     0.1, 50),                     \
    F(name ".scale",      enemy[type].scale,      0.1, 50),                     \
    F(name ".speed",      enemy[type].speed,      0.1, 500),                    \
    F(name ".accel",      enemy[type].accel,      0.1, 1000),                   \
    F(name ".points",     enemy[type].points,     0,   1000000),                \
    F(name ".ram_damage", enemy[type].ram_damage, 0,   10000)

    STATS("kamikaze", ENEMY_KAMIKAZE),
    F("kamikaze.aggro_range",   kamikaze_aggro_range,   0,      2000),
    F("kamikaze.blast_radius",  kamikaze_blast_radius,  0.1,    100),
    F("kamikaze.crater_radius", kamikaze_crater_radius, 0.2,    60),
    F("kamikaze.crater_depth",  kamikaze_crater_depth,  0.2,    60),

    STATS("digger", ENEMY_DIGGER),
    F("digger.drill_speed",     digger_drill_speed,     0.01,   100),
    F("digger.drill_radius",    digger_drill_radius,    0.5,    30),
    F("digger.drill_interval",  digger_drill_interval,  0.02,   10),

    STATS("siphon", ENEMY_SIPHON),
    F("siphon.altitude",        siphon_altitude,        1,      500),
    F("siphon.drain_radius",    siphon_drain_radius,    0.2,    30),
    F("siphon.drain_interval",  siphon_drain_interval,  0.02,   10),
    F("siphon.wander",          siphon_wander,          0,      100),

    STATS("sniper", ENEMY_SNIPER),
    F("sniper.range",           sniper_range,           5,      2000),
    F("sniper.charge_time",     sniper_charge_time,     0.1,    30),
    F("sniper.lock_time",       sniper_lock_time,       0,      30),
    F("sniper.fire_interval",   sniper_fire_interval,   0.2,    120),
    F("sniper.shot_speed",      sniper_shot_speed,      1,      5000),
    F("sniper.shot_damage",     sniper_shot_damage,     0,      10000),

    STATS("shielder", ENEMY_SHIELDER),
    F("shielder.shield_radius", shielder_radius,        1,      500),
    F("shielder.follow",        shielder_follow,        0,      500),

    STATS("carrier", ENEMY_CARRIER),
    F("carrier.altitude",        carrier_altitude,        1,    500),
    F("carrier.launch_interval", carrier_launch_interval, 0.5,  600),
    F("carrier.launch_warning",  carrier_launch_warning,  0,    60),
    I("carrier.launch_count",    carrier_launch_count,    1,    24),
    I("carrier.max_swarm",       carrier_max_swarm,       0,    200),
#undef STATS
};

// data/evolutions.txt
static const TuneField EVO_FIELDS[] = {
    F("xp.base",                 xp_base,                 1,    10000),
    F("xp.growth",               xp_growth,               1,    10),
    F("xp.swarm",                xp_kill[ENEMY_SWARM],    0,    1000),
    F("xp.bomber",               xp_kill[ENEMY_BOMBER],   0,    1000),
    F("xp.kamikaze",             xp_kill[ENEMY_KAMIKAZE], 0,    1000),
    F("xp.digger",               xp_kill[ENEMY_DIGGER],   0,    1000),
    F("xp.siphon",               xp_kill[ENEMY_SIPHON],   0,    1000),
    F("xp.sniper",               xp_kill[ENEMY_SNIPER],   0,    1000),
    F("xp.shielder",             xp_kill[ENEMY_SHIELDER], 0,    1000),
    F("xp.carrier",              xp_kill[ENEMY_CARRIER],  0,    1000),
    F("xp.bomb",                 xp_bomb,                 0,    1000),
    F("cards.base_weight",       cards_base_weight,       0.01, 1000),
    F("cards.essence_weight",    cards_essence_weight,    0,    1000),
    I("escort.max",              evo_max[EVO_ESCORT],         1, 9),
    I("spread.max",              evo_max[EVO_SPREAD],         1, 9),
    I("blast_dash.max",          evo_max[EVO_BLAST_DASH],     1, 9),
    I("chain.max",               evo_max[EVO_CHAIN],          1, 9),
    I("heavy_hull.max",          evo_max[EVO_HEAVY_HULL],     1, 9),
    I("warheads.max",            evo_max[EVO_WARHEADS],       1, 9),
    I("pierce.max",              evo_max[EVO_PIERCE],         1, 9),
    I("armor_break.max",         evo_max[EVO_ARMOR_BREAK],    1, 9),
    I("lifesteal.max",           evo_max[EVO_LIFESTEAL],      1, 9),
    I("energy_siphon.max",       evo_max[EVO_ENERGY_SIPHON],  1, 9),
    I("crit.max",                evo_max[EVO_CRIT],           1, 9),
    I("long_range.max",          evo_max[EVO_LONG_RANGE],     1, 9),
    I("big_shield.max",          evo_max[EVO_BIG_SHIELD],     1, 9),
    I("reflect.max",             evo_max[EVO_REFLECT],        1, 9),
    I("fighters.max",            evo_max[EVO_FIGHTERS],       1, 9),
    I("hangar.max",              evo_max[EVO_HANGAR],         1, 9),
    F("escort.damage",           escort_damage,           0,    1000),
    F("escort.rate",             escort_rate,             0.1,  60),
    F("escort.range",            escort_range,            1,    2000),
    F("escort.orbit",            escort_orbit,            1,    100),
    F("spread.angle",            spread_angle,            0.5,  45),
    F("spread.damage",           spread_damage,           0,    10),
    F("blast_dash.damage",       blast_dash_damage,       0,    1000),
    F("blast_dash.radius",       blast_dash_radius,       0.5,  200),
    F("chain.damage",            chain_damage,            0,    1000),
    F("chain.radius",            chain_radius,            0.5,  200),
    F("heavy_hull.hull",         heavy_hull_hull,         0,    10000),
    F("warheads.fraction",       warheads_fraction,       0,    10),
    F("warheads.radius",         warheads_radius,         0.1,  100),
    F("armor_break.bonus",       armor_break_bonus,       0,    10),
    F("lifesteal.hull",          lifesteal_hull,          0,    1000),
    F("energy_siphon.energy",    energy_siphon_energy,    0,    1000),
    F("crit.chance",             crit_chance,             0,    1),
    F("crit.mult",               crit_mult,               1,    100),
    F("crit.far",                crit_far,                0,    5000),
    F("long_range.speed",        long_range_speed,        0,    10),
    F("long_range.assist",       long_range_assist,       0,    45),
    F("big_shield.shield",       big_shield_shield,       0,    10000),
    F("big_shield.regen",        big_shield_regen,        0,    10),
    F("reflect.chance",          reflect_chance,          0,    1),
    F("fighters.damage",         fighters_damage,         0,    1000),
    F("fighters.rate",           fighters_rate,           0.1,  60),
    F("fighters.speed",          fighters_speed,          1,    1000),
    F("fighters.range",          fighters_range,          1,    5000),
    F("hangar.rate",             hangar_rate,             0,    10),
    F("cards.synergy_weight",    cards_synergy_weight,    0,    1000),
    I("suicide.max",             evo_max[EVO_SYN_SUICIDE],    1, 9),
    F("suicide.interval",        suicide_interval,        0.2,  120),
    F("suicide.range",           suicide_range,           1,    5000),
    F("suicide.damage",          suicide_damage,          0,    1000),
    F("suicide.radius",          suicide_radius,          0,    200),
    F("suicide.speed",           suicide_speed,           1,    1000),
    I("restore.max",             evo_max[EVO_SYN_RESTORE],    1, 9),
    F("restore.fraction",        restore_fraction,        0,    4),
    I("salvage.max",             evo_max[EVO_SYN_SALVAGE],    1, 9),
    I("salvage.small",           salvage_small,           0,    20),
    I("salvage.big",             salvage_big,             0,    40),
    I("poise.max",               evo_max[EVO_SYN_POISE],      1, 9),
    F("poise.mult",              poise_mult,              1,    10),
};

// data/bosses.txt
static const TuneField BOSS_FIELDS[] = {
    F("breaker.hp",               breaker_hp,               1,    100000),
    F("breaker.scale",            breaker_scale,            0.5,  100),
    F("breaker.altitude",         breaker_altitude,         5,    1000),
    F("breaker.speed",            breaker_speed,            0,    500),
    F("breaker.points",           breaker_points,           0,    10000000),
    F("breaker.xp",               breaker_xp,               0,    10000),
    F("breaker.enter_time",       breaker_enter_time,       0.1,  60),
    F("breaker.spawn_distance",   breaker_spawn_distance,   1.5,  20),
    F("breaker.rest_1",           breaker_rest[0],          0,    120),
    F("breaker.rest_2",           breaker_rest[1],          0,    120),
    F("breaker.rest_3",           breaker_rest[2],          0,    120),
    F("breaker.charge_1",         breaker_charge[0],        0.5,  120),
    F("breaker.charge_2",         breaker_charge[1],        0.5,  120),
    F("breaker.charge_3",         breaker_charge[2],        0.5,  120),
    F("breaker.cut_length_1",     breaker_cut_length[0],    1,    180),
    F("breaker.cut_length_2",     breaker_cut_length[1],    1,    180),
    F("breaker.cut_length_3",     breaker_cut_length[2],    1,    180),
    F("breaker.turret_interval_1", breaker_turret_interval[0], 0.2, 60),
    F("breaker.turret_interval_2", breaker_turret_interval[1], 0.2, 60),
    F("breaker.turret_interval_3", breaker_turret_interval[2], 0.2, 60),
    F("breaker.fire_time",        breaker_fire_time,        0.2,  30),
    F("breaker.cut_radius",       breaker_cut_radius,       0.5,  40),
    F("breaker.cut_sink",         breaker_cut_sink,         0,    40),
    F("breaker.cut_spacing",      breaker_cut_spacing,      0.5,  40),
    F("breaker.cut_turn",         breaker_cut_turn,         0,    180),
    F("breaker.beam_damage",      breaker_beam_damage,      0,    10000),
    F("breaker.beam_width",       breaker_beam_width,       0.1,  50),
    F("breaker.capacitor_hp",     breaker_capacitor_hp,     0.1,  10000),
    F("breaker.stagger_time",     breaker_stagger_time,     0.5,  60),
    F("breaker.core_armor",       breaker_core_armor,       0,    1),
    F("breaker.core_open",        breaker_core_open,        0,    1),
    F("breaker.phase_2",          breaker_phase_2,          0,    1),
    F("breaker.phase_3",          breaker_phase_3,          0,    1),
    I("breaker.turret_burst",     breaker_turret_burst,     0,    50),
    F("breaker.turret_damage",    breaker_turret_damage,    0,    10000),
    F("breaker.turret_range",     breaker_turret_range,     0,    5000),
    F("breaker.turret_speed",     breaker_turret_speed,     1,    2000),
    F("breaker.launch_interval",  breaker_launch_interval,  0.5,  600),
    I("breaker.launch_count",     breaker_launch_count,     0,    24),
    F("breaker.ram_damage",       breaker_ram_damage,       0,    10000),
    F("ability.damage",           ability_damage,           0,    10000),
    F("ability.range",            ability_range,            1,    5000),
    F("ability.width",            ability_width,            0.1,  100),
    F("ability.cooldown",         ability_cooldown,         0,    600),
    F("ability.charge",           ability_charge,           0,    10),
    F("hive.hp",                  hive_hp,                  1,    100000),
    F("hive.points",              hive_points,              0,    10000000),
    F("hive.xp",                  hive_xp,                  0,    10000),
    F("hive.enter_time",          hive_enter_time,          0.1,  60),
    F("hive.spawn_distance",      hive_spawn_distance,      1,    100),
    F("hive.sac_hp",              hive_sac_hp,              0.1,  10000),
    F("hive.sac_regrow",          hive_sac_regrow,          0.1,  60),
    F("hive.inflate_time",        hive_inflate_time,        0.1,  30),
    F("hive.inflate_1",           hive_inflate[0],          0.2,  120),
    F("hive.inflate_2",           hive_inflate[1],          0.2,  120),
    F("hive.inflate_3",           hive_inflate[2],          0.2,  120),
    I("hive.brood",               hive_brood,               0,    16),
    I("hive.brood_cap",           hive_brood_cap,           0,    64),
    F("hive.open_time",           hive_open_time,           0.5,  60),
    F("hive.core_armor",          hive_core_armor,          0,    1),
    F("hive.core_open",           hive_core_open,           0,    1),
    F("hive.phase_2",             hive_phase_2,             0,    1),
    F("hive.phase_3",             hive_phase_3,             0,    1),
    F("hive.thruster_hp",         hive_thruster_hp,         0.1,  10000),
    F("hive.thruster_regrow",     hive_thruster_regrow,     0.5,  300),
    F("hive.push_rate",           hive_push_rate,           0,    1),
    F("hive.recede_rate",         hive_recede_rate,         0,    1),
    F("hive.moon_min",            hive_moon_min,            0.3,  1),
    F("hive.tide_start",          hive_tide_start,          0.3,  1),
    F("hive.tide_radius",         hive_tide_radius,         0.5,  40),
    F("hive.tide_sink",           hive_tide_sink,           0,    40),
    F("hive.tide_spread",         hive_tide_spread,         0,    200),
    F("hive.tide_slow",           hive_tide_slow,           0.05, 60),
    F("hive.tide_fast",           hive_tide_fast,           0.05, 60),
    F("hive.spit_interval",       hive_spit_interval,       0.2,  60),
    F("hive.spit_warn",           hive_spit_warn,           0,    10),
    F("hive.spit_damage",         hive_spit_damage,         0,    10000),
    F("hive.spit_speed",          hive_spit_speed,          1,    1000),
    F("hive.spore_size",          hive_spore_size,          0.2,  20),
    F("hive.spore_life",          hive_spore_life,          1,    600),
    F("hive.spore_throw",         hive_spore_throw,         0,    1000),
    F("hive.spore_damage",        hive_spore_damage,        0,    10000),
    F("hive.spore_blast",         hive_spore_blast,         0.5,  100),
    I("brood.count",              brood_count,              1,    32),
    F("brood.damage",             brood_damage,             0,    10000),
    F("brood.speed",              brood_speed,              1,    1000),
    F("brood.turn",               brood_turn,               0,    50),
    F("brood.life",               brood_life,               0.2,  30),
    F("brood.cooldown",           brood_cooldown,           0,    600),
    F("worm.hp",                  worm_hp,                  1,    100000),
    F("worm.points",              worm_points,              0,    10000000),
    F("worm.xp",                  worm_xp,                  0,    10000),
    F("worm.enter_time",          worm_enter_time,          0.1,  60),
    F("worm.spawn_distance",      worm_spawn_distance,      1.5,  20),
    F("worm.scale",               worm_scale,               0.5,  50),
    F("worm.spacing",             worm_spacing,             0.5,  50),
    F("worm.speed",               worm_speed,               1,    500),
    F("worm.swim_speed",          worm_swim_speed,          1,    500),
    F("worm.turn",                worm_turn,                0.1,  20),
    F("worm.depth",               worm_depth,               1,    200),
    F("worm.tunnel_radius",       worm_tunnel_radius,       1,    40),
    F("worm.tunnel_spacing",      worm_tunnel_spacing,      0.5,  40),
    F("worm.tunnel_life",         worm_tunnel_life,         0.5,  600),
    F("worm.tunnel_guard",        worm_tunnel_guard,        0,    200),
    F("worm.rumble_time",         worm_rumble_time,         0.1,  20),
    F("worm.breach_time",         worm_breach_time,         0.5,  30),
    F("worm.breach_height",       worm_breach_height,       1,    500),
    F("worm.breach_reach",        worm_breach_reach,        5,    170),
    F("worm.breach_radius",       worm_breach_radius,       0.5,  40),
    F("worm.breach_depth",        worm_breach_depth,        0.1,  40),
    F("worm.breach_blast",        worm_breach_blast,        0,    100),
    F("worm.breach_damage",       worm_breach_damage,       0,    10000),
    F("worm.mouth_armor",         worm_mouth_armor,         0,    1),
    F("worm.nodule_hp",           worm_nodule_hp,           0.1,  10000),
    F("worm.nodule_damage",       worm_nodule_damage,       0,    1),
    F("worm.tail_hp",             worm_tail_hp,             0.1,  10000),
    F("worm.panic_time",          worm_panic_time,          0.5,  30),
    F("worm.phase_2",             worm_phase_2,             0,    1),
    F("worm.phase_3",             worm_phase_3,             0,    1),
    F("worm.core_bite",           worm_core_bite,           0.5,  40),
    I("worm.larvae",              worm_larvae,              0,    16),
    F("worm.swim_depth",          worm_swim_depth,          0,    40),
    F("worm.swim_time",           worm_swim_time,           0.5,  60),
    F("worm.furrow_radius",       worm_furrow_radius,       0.5,  40),
    F("tunnel.cooldown",          tunnel_cooldown,          0,    600),
    F("tunnel.altitude",          tunnel_altitude,          1,    200),
    F("tunnel.time",              tunnel_time,              0,    10),
    F("dev.hp",                   dev_hp,                   1,    100000),
    F("dev.points",               dev_points,               0,    10000000),
    F("dev.enter_time",           dev_enter_time,           0.1,  60),
    F("dev.spawn_distance",       dev_spawn_distance,       1.5,  20),
    F("dev.scale",                dev_scale,                1,    100),
    F("dev.altitude",             dev_altitude,             5,    1000),
    F("dev.speed",                dev_speed,                0,    500),
    F("dev.vortex_range",         dev_vortex_range,         0,    2000),
    F("dev.vortex_pull",          dev_vortex_pull,          0,    1000),
    F("dev.heal_fragment",        dev_heal_fragment,        0,    1),
    F("dev.heal_meteor",          dev_heal_meteor,          0,    1),
    F("dev.heal_ore",             dev_heal_ore,             0,    1),
    F("dev.bite_interval_1",      dev_bite_interval[0],     0.5,  600),
    F("dev.bite_interval_2",      dev_bite_interval[1],     0.5,  600),
    F("dev.aim_time",             dev_aim_time,             0.2,  30),
    F("dev.aim_altitude",         dev_aim_altitude,         5,    500),
    F("dev.dive_time",            dev_dive_time,            0.1,  10),
    F("dev.chew_time",            dev_chew_time,            0.1,  30),
    F("dev.rise_time",            dev_rise_time,            0.1,  30),
    F("dev.bite_radius",          dev_bite_radius,          1,    60),
    F("dev.bite_depth",           dev_bite_depth,           0.5,  60),
    F("dev.bite_spread",          dev_bite_spread,          0,    180),
    F("dev.flinch",               dev_flinch,               0.1,  100000),
    F("dev.flinch_stun",          dev_flinch_stun,          0.1,  60),
    F("dev.broken_stun",          dev_broken_stun,          0.1,  60),
    F("dev.throat_armor",         dev_throat_armor,         0,    1),
    F("dev.frenzy_hp",            dev_frenzy_hp,            0,    1),
    F("dev.feast_hp",             dev_feast_hp,             0,    1),
    F("dev.teeth_interval",       dev_teeth_interval,       0.2,  60),
    F("dev.teeth_damage",         dev_teeth_damage,         0,    10000),
    F("dev.teeth_speed",          dev_teeth_speed,          1,    1000),
    I("dev.teeth_count",          dev_teeth_count,          0,    16),
    F("dev.latch_drain",          dev_latch_drain,          0.05, 60),
    F("dev.latch_radius",         dev_latch_radius,         0.5,  60),
    F("dev.latch_break",          dev_latch_break,          0,    1),
    F("dev.latch_cooldown",       dev_latch_cooldown,       0,    600),
    F("dev.torn_stun",            dev_torn_stun,            0.1,  60),
    I("dev.regurgitate",          dev_regurgitate,          0,    32),
};

#undef I
#undef F
#undef C

typedef struct {
    const char      *file;     // nome do arquivo, para as mensagens de erro
    const TuneField *fields;
    int              count;
} TuneTable;

static const TuneTable TABLES[4] = {
    { TUNING_FILE,     FIELDS,       (int)(sizeof FIELDS / sizeof FIELDS[0]) },
    { ENEMIES_FILE,    ENEMY_FIELDS, (int)(sizeof ENEMY_FIELDS / sizeof ENEMY_FIELDS[0]) },
    { EVOLUTIONS_FILE, EVO_FIELDS,   (int)(sizeof EVO_FIELDS / sizeof EVO_FIELDS[0]) },
    { BOSSES_FILE,     BOSS_FIELDS,  (int)(sizeof BOSS_FIELDS / sizeof BOSS_FIELDS[0]) },
};

static bool is_known_key(const TuneTable *table, const char *key)
{
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->fields[i].key, key) == 0) return true;
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

// Copia para `t` os campos de uma tabela, lidos do arquivo `c`, conferindo
// cada valor; e recusa chaves do arquivo que a tabela não conhece.
static bool apply_table(Tuning *t, const TuneTable *table, const Config *c, char *err, size_t err_len)
{
    for (int i = 0; i < table->count; i++) {
        const TuneField *f = &table->fields[i];
        const ConfigEntry *e = config_find(c, f->key);
        if (e == NULL) {
            snprintf(err, err_len, "%s: chave ausente: %s", table->file, f->key);
            return false;
        }

        char *dst = (char *)t + f->offset;

        if (f->kind == TUNE_COLOR) {
            Rgb8 color;
            if (!parse_color(e->text, &color)) {
                snprintf(err, err_len, "%s: %s = \"%s\": esperado \"r g b\" de 0 a 255", table->file, f->key,
                         e->text);
                return false;
            }
            memcpy(dst, &color, sizeof color);
            continue;
        }

        if (!e->is_number) {
            snprintf(err, err_len, "%s: %s = \"%s\" nao e um numero", table->file, f->key, e->text);
            return false;
        }
        if (!(e->number >= f->min && e->number <= f->max)) {   // também rejeita NaN
            snprintf(err, err_len, "%s: %s = %s fora da faixa [%g, %g]", table->file, f->key, e->text, f->min,
                     f->max);
            return false;
        }

        if (f->kind == TUNE_INT) {
            if (e->number != floor(e->number)) {
                snprintf(err, err_len, "%s: %s = %s precisa ser inteiro", table->file, f->key, e->text);
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
        if (!is_known_key(table, c->entries[i].key)) {
            snprintf(err, err_len, "%s: chave desconhecida: %s", table->file, c->entries[i].key);
            return false;
        }
    }
    return true;
}

bool tuning_from_config(Tuning *out, const Config *tuning_cfg, const Config *enemies_cfg, char *err,
                        size_t err_len)
{
    Tuning t = *out;   // trabalha em uma cópia: `out` só muda se tudo der certo
    if (!apply_table(&t, &TABLES[0], tuning_cfg, err, err_len)) return false;
    if (!apply_table(&t, &TABLES[1], enemies_cfg, err, err_len)) return false;

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
    if (t.swarm_attack_altitude >= t.swarm_cruise_altitude) {
        snprintf(err, err_len, "swarm.attack_altitude precisa ser menor que swarm.cruise_altitude");
        return false;
    }
    if (t.sniper_lock_time >= t.sniper_charge_time || t.sniper_charge_time >= t.sniper_fire_interval) {
        snprintf(err, err_len, "atirador: lock_time < charge_time < fire_interval");
        return false;
    }
    if (t.fragment_radius_min > t.fragment_radius_max || t.fragment_alt_min > t.fragment_alt_max
        || t.meteor_radius_min > t.meteor_radius_max || t.meteor_pass_min > t.meteor_pass_max) {
        snprintf(err, err_len, "fragment.* e meteor.*: cada _min precisa ser <= o _max");
        return false;
    }

    *out = t;
    if (err_len > 0) err[0] = '\0';
    return true;
}

bool tuning_evolutions_from_config(Tuning *out, const Config *evolutions_cfg, char *err, size_t err_len)
{
    Tuning t = *out;
    if (!apply_table(&t, &TABLES[2], evolutions_cfg, err, err_len)) return false;
    *out = t;
    if (err_len > 0) err[0] = '\0';
    return true;
}

bool tuning_bosses_from_config(Tuning *out, const Config *bosses_cfg, char *err, size_t err_len)
{
    Tuning t = *out;
    if (!apply_table(&t, &TABLES[3], bosses_cfg, err, err_len)) return false;
    if (t.breaker_phase_3 >= t.breaker_phase_2) {
        snprintf(err, err_len, "%s: breaker.phase_3 precisa ser menor que breaker.phase_2", BOSSES_FILE);
        return false;
    }
    if (t.hive_phase_3 >= t.hive_phase_2) {
        snprintf(err, err_len, "%s: hive.phase_3 precisa ser menor que hive.phase_2", BOSSES_FILE);
        return false;
    }
    if (t.dev_feast_hp >= t.dev_frenzy_hp) {
        snprintf(err, err_len, "%s: dev.feast_hp precisa ser menor que dev.frenzy_hp", BOSSES_FILE);
        return false;
    }
    if (t.worm_phase_3 >= t.worm_phase_2) {
        snprintf(err, err_len, "%s: worm.phase_3 precisa ser menor que worm.phase_2", BOSSES_FILE);
        return false;
    }
    if (t.hive_moon_min >= t.hive_tide_start) {
        snprintf(err, err_len, "%s: hive.moon_min precisa ser menor que hive.tide_start", BOSSES_FILE);
        return false;
    }
    *out = t;
    if (err_len > 0) err[0] = '\0';
    return true;
}

bool tuning_load(Tuning *out, const char *data_dir, char *err, size_t err_len)
{
    // static: a struct Config é grande (dezenas de KB) e não precisa ficar na pilha.
    static Config tuning_cfg, enemies_cfg, evolutions_cfg, bosses_cfg;
    char path[256];

    snprintf(path, sizeof path, "%s/%s", data_dir, TUNING_FILE);
    if (!config_load_file(&tuning_cfg, path)) {
        snprintf(err, err_len, "%s: %s", TUNING_FILE, tuning_cfg.error);
        return false;
    }
    snprintf(path, sizeof path, "%s/%s", data_dir, ENEMIES_FILE);
    if (!config_load_file(&enemies_cfg, path)) {
        snprintf(err, err_len, "%s: %s", ENEMIES_FILE, enemies_cfg.error);
        return false;
    }
    snprintf(path, sizeof path, "%s/%s", data_dir, EVOLUTIONS_FILE);
    if (!config_load_file(&evolutions_cfg, path)) {
        snprintf(err, err_len, "%s: %s", EVOLUTIONS_FILE, evolutions_cfg.error);
        return false;
    }
    snprintf(path, sizeof path, "%s/%s", data_dir, BOSSES_FILE);
    if (!config_load_file(&bosses_cfg, path)) {
        snprintf(err, err_len, "%s: %s", BOSSES_FILE, bosses_cfg.error);
        return false;
    }
    // Tudo ou nada: `out` só muda se os quatro arquivos estiverem certos.
    Tuning t = *out;
    if (!tuning_from_config(&t, &tuning_cfg, &enemies_cfg, err, err_len)) return false;
    if (!tuning_evolutions_from_config(&t, &evolutions_cfg, err, err_len)) return false;
    if (!tuning_bosses_from_config(&t, &bosses_cfg, err, err_len)) return false;
    *out = t;
    return true;
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
