#include "test_util.h"

#include "test.h"

PlanetParams test_planet_params(int grid)
{
    PlanetParams pp;
    memset(&pp, 0, sizeof pp);
    pp.grid = grid;
    pp.chunk = 16;
    pp.seed = 42u;
    pp.radius = 0.36f * (float)grid;
    pp.land_height = 2.5f;
    pp.ocean_depth = 2.0f;
    pp.land_bias = 0.0f;
    pp.noise_freq = 1.7f;
    pp.noise_octaves = 5;
    pp.relief_span = 0.42f;
    pp.moisture_freq = 2.3f;
    pp.ice_latitude = 0.88f;
    pp.sand_height = 0.35f;
    pp.rock_height = 0.55f;
    pp.snow_height = 0.80f;
    pp.desert_moisture = -0.15f;
    pp.forest_moisture = 0.10f;
    pp.surface_depth = 1.5f;
    pp.mantle_radius = 0.72f;
    pp.core_radius = 0.35f;
    return pp;
}

Tuning test_tuning(void)
{
    Tuning t;
    char err[160];
    memset(&t, 0, sizeof t);
    if (!tuning_load(&t, "data/tuning.txt", err, sizeof err)) test_fail(__FILE__, __LINE__, err);

    t.planet_grid = TEST_GRID;
    t.planet_chunk = 16;
    t.planet_radius_frac = 0.36f;
    t.planet_land_height = 2.5f;
    t.planet_ocean_depth = 2.0f;
    return t;
}
