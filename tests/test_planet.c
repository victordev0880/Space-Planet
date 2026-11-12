#include "test.h"
#include "test_util.h"

#include "core/rng.h"
#include "voxel/planet.h"

#define GRID TEST_GRID

static Arena arena;

static bool make_planet(Planet *p)
{
    arena_reset(&arena);
    PlanetParams pp = test_planet_params(GRID);
    return planet_init(p, &pp, &arena);
}

static void planet_rejects_bad_dimensions(void)
{
    Planet p;
    PlanetParams pp = test_planet_params(GRID);

    arena_reset(&arena);
    pp.grid = 50;   // não é múltiplo do chunk
    CHECK(!planet_init(&p, &pp, &arena));

    pp = test_planet_params(GRID);
    pp.chunk = 32;  // maior do que o mesher aceita
    CHECK(!planet_init(&p, &pp, &arena));

    pp = test_planet_params(GRID);
    pp.radius = 0.45f * GRID;   // com este relevo, encostaria na borda da grade
    CHECK(!planet_init(&p, &pp, &arena));

    Arena tiny;
    CHECK(arena_init(&tiny, 1000));
    pp = test_planet_params(GRID);
    CHECK(!planet_init(&p, &pp, &tiny));   // não cabe
    arena_free(&tiny);
}

static void planet_has_layers_from_core_to_space(void)
{
    Planet p;
    CHECK(make_planet(&p));
    int c = GRID / 2;

    CHECK(planet_density(&p, c, c, c) == 255);
    CHECK(planet_material(&p, c, c, c) == VMAT_CORE);

    // Andando do centro para fora, as camadas aparecem em ordem e a
    // densidade nunca aumenta.
    bool saw_mantle = false, saw_crust = false, saw_surface = false;
    int stage = 0;   // 0 núcleo, 1 manto, 2 crosta, 3 superfície, 4 vazio
    int previous_density = 255;
    for (int x = c; x < GRID; x++) {
        int density = planet_density(&p, x, c, c);
        int material = planet_material(&p, x, c, c);
        int now = (material == VMAT_CORE) ? 0 : (material == VMAT_MANTLE) ? 1 : (material == VMAT_CRUST) ? 2
                : (material == VMAT_NONE) ? 4 : 3;
        CHECK(now >= stage);
        CHECK(density <= previous_density);
        stage = now;
        previous_density = density;
        if (material == VMAT_MANTLE) saw_mantle = true;
        if (material == VMAT_CRUST) saw_crust = true;
        if (now == 3) saw_surface = true;
        if (material == VMAT_NONE) CHECK(density == 0);
    }
    CHECK(saw_mantle && saw_crust && saw_surface);
    // A superfície fica dentro da grade: no último ponto já não há matéria.
    CHECK(planet_density(&p, GRID - 1, c, c) < VOXEL_SOLID);

    // Os cantos da grade ficam fora do planeta.
    CHECK(planet_density(&p, 0, 0, 0) == 0);
    CHECK(planet_density(&p, GRID - 1, GRID - 1, GRID - 1) == 0);
}

static void planet_volume_matches_a_sphere(void)
{
    Planet p;
    CHECK(make_planet(&p));
    double radius = p.params.radius;
    double sphere = 4.0 / 3.0 * 3.14159265358979 * radius * radius * radius;
    double volume = (double)p.volume / 255.0;   // em voxels cheios

    // Um pouco maior que a esfera do nível do mar, por causa do relevo.
    CHECK(volume > sphere * 0.97);
    CHECK(volume < sphere * 1.12);

    // O contador bate com a soma real da grade.
    int64_t sum = 0;
    for (int i = 0; i < GRID * GRID * GRID; i++) sum += p.density[i];
    CHECK(sum == p.volume);
    CHECK(p.initial_volume == p.volume);
    CHECK_NEAR(planet_hp(&p), 1.0f, 1e-6);
}

static void planet_same_params_same_planet(void)
{
    static uint8_t first[GRID * GRID * GRID];
    Planet p;
    CHECK(make_planet(&p));
    memcpy(first, p.density, sizeof first);

    PlanetParams pp = test_planet_params(GRID);
    planet_generate(&p, &pp);
    CHECK(memcmp(first, p.density, sizeof first) == 0);

    pp.seed = 43u;
    planet_generate(&p, &pp);
    CHECK(memcmp(first, p.density, sizeof first) != 0);
}

static void planet_terrain_has_land_sea_and_ice(void)
{
    PlanetParams pp = test_planet_params(GRID);
    Rng r;
    rng_seed(&r, 1u, 1u);

    int land = 0, total = 20000;
    bool seen[VMAT_COUNT] = { false };
    for (int i = 0; i < total; i++) {
        Terrain t = planet_terrain(&pp, rng_unit_vec3(&r));
        CHECK(t.elevation >= -pp.ocean_depth - 1e-4f && t.elevation <= pp.land_height + 1e-4f);
        CHECK(t.land == (t.elevation > 0.0f));
        CHECK(t.surface > VMAT_NONE && t.surface < VMAT_CRUST);   // só materiais de superfície
        if (!t.land) {
            CHECK(t.surface == VMAT_WATER_SHALLOW || t.surface == VMAT_WATER || t.surface == VMAT_WATER_DEEP
                  || t.surface == VMAT_ICE);
        }
        if (t.land) land++;
        seen[t.surface] = true;
    }

    float land_fraction = (float)land / (float)total;
    CHECK(land_fraction > 0.20f && land_fraction < 0.75f);
    CHECK(seen[VMAT_WATER_SHALLOW] && seen[VMAT_WATER] && seen[VMAT_WATER_DEEP] && seen[VMAT_ICE]);
    CHECK(seen[VMAT_GRASS] && seen[VMAT_FOREST] && seen[VMAT_SAND]);

    // Os dois polos são gelo.
    CHECK(planet_terrain(&pp, (Vector3){ 0.0f, 1.0f, 0.0f }).surface == VMAT_ICE);
    CHECK(planet_terrain(&pp, (Vector3){ 0.0f, -1.0f, 0.0f }).surface == VMAT_ICE);
}

// Raio em que a densidade cruza a superfície, descendo ao longo de uma direção.
static float find_surface(const Planet *p, Vector3 dir)
{
    float previous = 0.0f;
    for (float r = p->params.radius + 8.0f; r > 0.0f; r -= 0.05f) {
        float d = planet_sample(p, Vector3Scale(dir, r));
        if (d >= VOXEL_ISO) {
            // Refina entre este passo e o anterior.
            float t = (d - VOXEL_ISO) / (d - previous + 1e-6f);
            return r + 0.05f * t;
        }
        previous = d;
    }
    return 0.0f;
}

static void planet_surface_follows_the_terrain(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Rng r;
    rng_seed(&r, 2u, 1u);

    for (int i = 0; i < 300; i++) {
        Vector3 dir = rng_unit_vec3(&r);
        Terrain t = planet_terrain(&p.params, dir);
        float expected = p.params.radius + (t.land ? t.elevation : 0.0f);
        CHECK_NEAR(find_surface(&p, dir), expected, 0.6f);
    }
}

static void planet_sample_interpolates_the_grid(void)
{
    Planet p;
    CHECK(make_planet(&p));
    float c = p.center;

    // Em cima de um ponto da grade devolve a densidade dele.
    for (int x = 0; x < GRID; x += 5) {
        Vector3 world = { (float)x - c, 3.0f, -2.0f };
        CHECK_NEAR(planet_sample(&p, world), planet_density(&p, x, GRID / 2 + 3, GRID / 2 - 2), 1e-3);
    }

    // No meio do caminho entre dois pontos, a média deles.
    Rng r;
    rng_seed(&r, 6u, 1u);
    for (int i = 0; i < 200; i++) {
        int x = rng_int(&r, 1, GRID - 3), y = rng_int(&r, 1, GRID - 2), z = rng_int(&r, 1, GRID - 2);
        float a = planet_density(&p, x, y, z), b = planet_density(&p, x + 1, y, z);
        Vector3 world = { (float)x + 0.5f - c, (float)y - c, (float)z - c };
        CHECK_NEAR(planet_sample(&p, world), 0.5f * (a + b), 1e-3);
    }

    // Fora da grade é vazio, mesmo muito longe.
    CHECK(planet_sample(&p, (Vector3){ 1000.0f, 0.0f, 0.0f }) == 0.0f);
    CHECK(planet_sample(&p, (Vector3){ 0.0f, -1e30f, 0.0f }) == 0.0f);
    CHECK(planet_sample(&p, (Vector3){ 0.0f, 0.0f, 0.0f }) == 255.0f);
}

static void planet_gradient_points_into_the_ground(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Rng r;
    rng_seed(&r, 7u, 1u);

    for (int i = 0; i < 300; i++) {
        Vector3 dir = rng_unit_vec3(&r);
        Vector3 surface = Vector3Scale(dir, find_surface(&p, dir));
        Vector3 g = planet_gradient(&p, surface);
        CHECK(Vector3Length(g) > 1.0f);
        // Para dentro = contra a direção que sai do centro.
        CHECK(Vector3DotProduct(Vector3Normalize(g), dir) < -0.7f);
    }
}

void suite_planet(void)
{
    CHECK(arena_init(&arena, 4u * 1024u * 1024u));
    RUN(planet_rejects_bad_dimensions);
    RUN(planet_has_layers_from_core_to_space);
    RUN(planet_volume_matches_a_sphere);
    RUN(planet_same_params_same_planet);
    RUN(planet_terrain_has_land_sea_and_ice);
    RUN(planet_surface_follows_the_terrain);
    RUN(planet_sample_interpolates_the_grid);
    RUN(planet_gradient_points_into_the_ground);
    arena_free(&arena);
}
