#include "test.h"
#include "test_util.h"

#include "core/rng.h"
#include "voxel/damage.h"
#include "voxel/planet.h"

#define GRID TEST_GRID
#define PI_D 3.14159265358979

static Arena arena;

static bool make_planet(Planet *p)
{
    arena_reset(&arena);
    PlanetParams pp = test_planet_params(GRID);
    return planet_init(p, &pp, &arena);
}

static int64_t recount_volume(const Planet *p, int64_t *core)
{
    int64_t sum = 0;
    *core = 0;
    for (int i = 0; i < GRID * GRID * GRID; i++) {
        sum += p->density[i];
        if (p->material[i] == VMAT_CORE) *core += p->density[i];
    }
    return sum;
}

static void carve_removes_about_a_sphere_of_matter(void)
{
    Planet p;
    CHECK(make_planet(&p));
    int64_t before = p.volume;

    // Bem dentro do planeta, onde tudo em volta tem densidade máxima.
    float radius = 5.0f;
    CarveResult r = planet_carve(&p, (Vector3){ 4.0f, 0.0f, 0.0f }, radius);

    // A densidade retirada é a da esfera mais a da faixa em volta dela, onde
    // a densidade cai aos poucos (meia largura w). Somando as duas, dá o
    // volume da esfera vezes (1 + w² / r²). Em uma cratera pequena esse fator
    // é grande: ela custa mais HP do que o tamanho aparente sugere.
    double w = VOXEL_ISO / VOXEL_DENSITY_PER_UNIT;
    double sphere = 4.0 / 3.0 * PI_D * radius * radius * radius * 255.0;
    double expected = sphere * (1.0 + (w * w) / (radius * radius));
    CHECK(r.removed > 0);
    CHECK_NEAR((double)r.removed / expected, 1.0, 0.05);
    CHECK(p.volume == before - r.removed);
    CHECK(planet_hp(&p) < 1.0f);

    // No centro da esfera não sobra nada, nem material.
    int c = GRID / 2;
    CHECK(planet_density(&p, c + 4, c, c) == 0);
    CHECK(planet_material(&p, c + 4, c, c) == VMAT_NONE);
    // Na superfície da esfera a densidade está no limiar entre vazio e sólido.
    CHECK_NEAR(planet_sample(&p, (Vector3){ 9.0f, 0.0f, 0.0f }), VOXEL_ISO, 1.0f);
    // Longe dela, nada mudou.
    CHECK(planet_density(&p, c - 8, c, c) == 255);
}

static void carve_keeps_the_volume_counters_exact(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Rng rng;
    rng_seed(&rng, 9u, 1u);

    // Muitas crateras, de todos os tamanhos, em todo lugar (inclusive fora).
    for (int i = 0; i < 60; i++) {
        Vector3 center = Vector3Scale(rng_unit_vec3(&rng), rng_range(&rng, 0.0f, 30.0f));
        planet_carve(&p, center, rng_range(&rng, 0.5f, 7.0f));
    }

    int64_t core = 0;
    int64_t total = recount_volume(&p, &core);
    CHECK(total == p.volume);            // o contador incremental bate com a recontagem
    CHECK(core == p.core_volume);
    CHECK(p.volume < p.initial_volume);
    CHECK(p.volume >= 0);
}

static void carve_never_adds_matter(void)
{
    static uint8_t before[GRID * GRID * GRID];
    Planet p;
    CHECK(make_planet(&p));
    memcpy(before, p.density, sizeof before);

    planet_carve(&p, (Vector3){ 0.0f, p.params.radius, 0.0f }, 6.0f);   // na superfície
    bool increased = false;
    for (int i = 0; i < GRID * GRID * GRID; i++) {
        if (p.density[i] > before[i]) increased = true;
    }
    CHECK(!increased);
}

static void carve_twice_in_the_same_place_does_nothing_new(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Vector3 center = { 0.0f, p.params.radius - 2.0f, 0.0f };

    CarveResult first = planet_carve(&p, center, 4.0f);
    uint32_t version = p.chunk_version[planet_chunk_index(&p, 1, 2, 1)];
    CarveResult second = planet_carve(&p, center, 4.0f);

    CHECK(first.removed > 0 && first.chunks_touched > 0);
    CHECK(second.removed == 0 && second.chunks_touched == 0);
    CHECK(p.chunk_version[planet_chunk_index(&p, 1, 2, 1)] == version);   // nada a remalhar
}

static void carve_in_empty_space_changes_nothing(void)
{
    Planet p;
    CHECK(make_planet(&p));
    int64_t before = p.volume;

    CHECK(planet_carve(&p, (Vector3){ 500.0f, 0.0f, 0.0f }, 5.0f).removed == 0);   // fora da grade
    CHECK(planet_carve(&p, (Vector3){ 0.0f, 0.0f, 0.0f }, 0.0f).removed == 0);     // raio zero
    CHECK(planet_carve(&p, (Vector3){ 0.0f, 0.0f, 0.0f }, -3.0f).removed == 0);
    // Canto da grade, onde não há planeta.
    float corner = (float)GRID * 0.5f - 2.0f;
    CHECK(planet_carve(&p, (Vector3){ corner, corner, corner }, 1.5f).removed == 0);
    CHECK(p.volume == before);
}

static void carve_marks_only_nearby_chunks(void)
{
    Planet p;
    CHECK(make_planet(&p));
    int chunks = planet_chunk_count(&p);
    static uint32_t before[64];
    CHECK(chunks <= 64);
    memcpy(before, p.chunk_version, sizeof(uint32_t) * (size_t)chunks);

    // Furo minúsculo bem no meio do chunk (2, 1, 1), que cobre x de 32 a 47 e
    // y, z de 16 a 31. Mesmo um furo assim mexe em pontos a até 4 voxels de
    // distância (a faixa suave), então só cabe em um chunk se estiver centrado.
    float c = p.center;
    Vector3 center = { 39.5f - c, 23.5f - c, 23.5f - c };
    CarveResult r = planet_carve(&p, center, 0.3f);
    CHECK(r.removed > 0);

    int changed = 0;
    for (int i = 0; i < chunks; i++) {
        if (p.chunk_version[i] != before[i]) changed++;
    }
    CHECK(changed == r.chunks_touched);
    CHECK(p.chunk_version[planet_chunk_index(&p, 2, 1, 1)] != before[planet_chunk_index(&p, 2, 1, 1)]);
    CHECK(changed == 1);   // só o chunk dele

    // O mesmo furo perto da divisa entre dois chunks: os dois precisam ser refeitos.
    memcpy(before, p.chunk_version, sizeof(uint32_t) * (size_t)chunks);
    center = (Vector3){ 33.0f - c, 23.5f - c, 23.5f - c };
    planet_carve(&p, center, 0.3f);
    CHECK(p.chunk_version[planet_chunk_index(&p, 2, 1, 1)] != before[planet_chunk_index(&p, 2, 1, 1)]);
    CHECK(p.chunk_version[planet_chunk_index(&p, 1, 1, 1)] != before[planet_chunk_index(&p, 1, 1, 1)]);
    CHECK(p.chunk_version[planet_chunk_index(&p, 0, 0, 0)] == before[planet_chunk_index(&p, 0, 0, 0)]);
}

static void carve_tracks_core_damage(void)
{
    Planet p;
    CHECK(make_planet(&p));
    CHECK(p.initial_core_volume > 0);
    CHECK_NEAR(planet_core_hp(&p), 1.0f, 1e-6);

    // Uma cratera na superfície não toca o núcleo.
    CarveResult shallow = planet_carve(&p, (Vector3){ 0.0f, p.params.radius, 0.0f }, 4.0f);
    CHECK(shallow.removed > 0 && shallow.removed_core == 0);
    CHECK_NEAR(planet_core_hp(&p), 1.0f, 1e-6);
    CHECK(shallow.removed_by_material[VMAT_CORE] == 0);

    // No centro do planeta, com uma esfera que não chega ao manto, tudo que
    // sai é núcleo.
    CarveResult deep = planet_carve(&p, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.5f);
    CHECK(deep.removed > 0);
    CHECK(deep.removed_core == deep.removed);
    CHECK(deep.removed_by_material[VMAT_CORE] == deep.removed);
    CHECK(planet_core_hp(&p) < 1.0f && planet_core_hp(&p) > 0.5f);
}

static void crater_sphere_matches_rim_and_depth(void)
{
    Vector3 ground = { 0.0f, 20.0f, 0.0f };
    Vector3 normal = { 0.0f, 1.0f, 0.0f };

    // Raso e largo, hemisfério, e fundo e estreito.
    float cases[3][2] = { { 6.0f, 2.0f }, { 5.0f, 5.0f }, { 2.0f, 6.0f } };
    for (int i = 0; i < 3; i++) {
        float rim = cases[i][0], depth = cases[i][1], radius = 0.0f;
        Vector3 center = crater_sphere(ground, normal, rim, depth, &radius);

        // O fundo da esfera fica `depth` abaixo do solo...
        CHECK_NEAR(center.y - radius, ground.y - depth, 1e-4);
        // ...e a esfera corta o plano do solo em um círculo de raio `rim`.
        float height = ground.y - center.y;
        CHECK_NEAR(sqrtf(radius * radius - height * height), rim, 1e-3);
    }

    // Valores absurdos não geram números inválidos.
    float radius = 0.0f;
    Vector3 center = crater_sphere(ground, normal, 0.0f, 0.0f, &radius);
    CHECK(radius > 0.0f && center.y == center.y);
}

static void raycast_finds_the_ground(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Rng rng;
    rng_seed(&rng, 4u, 1u);

    for (int i = 0; i < 200; i++) {
        Vector3 dir = rng_unit_vec3(&rng);
        Vector3 origin = Vector3Scale(dir, 60.0f);
        Vector3 hit;
        bool found = planet_raycast(&p, origin, Vector3Negate(dir), 200.0f, &hit);
        CHECK(found);
        if (!found) continue;

        // O ponto fica na superfície: densidade perto do limiar...
        CHECK_NEAR(planet_sample(&p, hit), VOXEL_ISO, 6.0f);
        // ...e na altura que o relevo manda.
        Terrain t = planet_terrain(&p.params, dir);
        float expected = p.params.radius + (t.land ? t.elevation : 0.0f);
        CHECK_NEAR(Vector3Length(hit), expected, 0.6f);
        // A normal aponta para fora.
        CHECK(Vector3DotProduct(planet_normal(&p, hit), dir) > 0.7f);
    }
}

static void raycast_misses_and_limits(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Vector3 hit = { 0.0f, 0.0f, 0.0f };

    // Passa longe do planeta.
    CHECK(!planet_raycast(&p, (Vector3){ 100.0f, 0.0f, 60.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, 500.0f, &hit));
    // Aponta para o lado oposto.
    CHECK(!planet_raycast(&p, (Vector3){ 60.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, 500.0f, &hit));
    // Aponta para o planeta, mas o alcance não chega lá.
    CHECK(!planet_raycast(&p, (Vector3){ 60.0f, 0.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, 10.0f, &hit));
    // Direção nula.
    CHECK(!planet_raycast(&p, (Vector3){ 60.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 0.0f }, 500.0f, &hit));

    // A direção não precisa ter comprimento 1.
    CHECK(planet_raycast(&p, (Vector3){ 60.0f, 0.0f, 0.0f }, (Vector3){ -7.0f, 0.0f, 0.0f }, 500.0f, &hit));
    CHECK(hit.x > 0.0f && hit.x < 30.0f);

    // Partindo de dentro da matéria, o impacto é o próprio ponto de partida.
    Vector3 inside = { 5.0f, 0.0f, 0.0f };
    CHECK(planet_raycast(&p, inside, (Vector3){ 1.0f, 0.0f, 0.0f }, 500.0f, &hit));
    CHECK_NEAR(Vector3Distance(hit, inside), 0.0f, 1e-4);
}

static void raycast_goes_into_craters(void)
{
    Planet p;
    CHECK(make_planet(&p));
    Vector3 origin = { 0.0f, 60.0f, 0.0f };
    Vector3 down = { 0.0f, -1.0f, 0.0f };

    Vector3 before, after;
    CHECK(planet_raycast(&p, origin, down, 200.0f, &before));
    planet_carve(&p, before, 5.0f);
    CHECK(planet_raycast(&p, origin, down, 200.0f, &after));

    // O mesmo raio agora desce até o fundo da cratera.
    CHECK_NEAR(before.y - after.y, 5.0f, 0.5f);
}

void suite_damage(void)
{
    CHECK(arena_init(&arena, 4u * 1024u * 1024u));
    RUN(carve_removes_about_a_sphere_of_matter);
    RUN(carve_keeps_the_volume_counters_exact);
    RUN(carve_never_adds_matter);
    RUN(carve_twice_in_the_same_place_does_nothing_new);
    RUN(carve_in_empty_space_changes_nothing);
    RUN(carve_marks_only_nearby_chunks);
    RUN(carve_tracks_core_damage);
    RUN(crater_sphere_matches_rim_and_depth);
    RUN(raycast_finds_the_ground);
    RUN(raycast_misses_and_limits);
    RUN(raycast_goes_into_craters);
    arena_free(&arena);
}
