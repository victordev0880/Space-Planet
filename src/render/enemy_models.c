#include "raylib.h"

#include "render/enemy_models.h"

#include <math.h>

#include "core/vmath.h"

// Eixos do modelo: frente -Z, direita +X, topo +Y; escala 1.

static EnemyModel models[ENEMY_TYPE_COUNT][ENEMY_VARIANTS];
static bool       built;

static Vector3 V(float x, float y, float z) { return (Vector3){ x, y, z }; }

static const Vector3 AX = { 1.0f, 0.0f, 0.0f };
static const Vector3 AY = { 0.0f, 1.0f, 0.0f };
static const Vector3 AZ = { 0.0f, 0.0f, 1.0f };

static void light(EnemyModel *em, Vector3 pos, float size)
{
    if (em->light_count < MODEL_LIGHTS_MAX) em->lights[em->light_count++] = (ModelLight){ pos, size };
}

// Faz `copies` cópias giradas em volta de `axis` dos triângulos montados
// desde `first` (pernas, pétalas, aletas em volta de um eixo).
static void radial_copies(LowPoly *m, int first, Vector3 axis, int copies)
{
    int last = m->count;
    for (int k = 1; k < copies; k++) {
        Quaternion q = QuaternionFromAxisAngle(axis, 2.0f * PI * (float)k / (float)copies);
        for (int i = first; i < last && m->count < LOWPOLY_MAX_TRIS; i++) {
            LowPolyTri t = m->tris[i];
            t.a = Vector3RotateByQuaternion(t.a, q);
            t.b = Vector3RotateByQuaternion(t.b, q);
            t.c = Vector3RotateByQuaternion(t.c, q);
            m->tris[m->count++] = t;
        }
    }
}

// Casco montado ao longo de z e depois posto de pé (z vira y): para o Sifão.
static void upright(LowPoly *m, int first)
{
    lp_transform(m, first, QuaternionFromAxisAngle(AX, -PI / 2.0f), V(0.0f, 0.0f, 0.0f));
}

// ---------------------------------------------------------------------------
//  Paletas (peças P_*) de cada tipo
// ---------------------------------------------------------------------------

typedef struct { Color hull, hull2, dark, accent, metal, glass, glow, core; } Palette;

static const Palette PALETTES[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = { { 104, 64, 150, 255 }, { 156, 106, 210, 255 }, { 46, 30, 66, 255 }, { 196, 140, 240, 255 },
                         { 118, 114, 132, 255 }, { 26, 18, 38, 255 }, { 150, 255, 150, 255 }, { 150, 255, 150, 255 } },
    [ENEMY_BOMBER]   = { { 110, 60, 62, 255 }, { 162, 116, 84, 255 }, { 40, 30, 34, 255 }, { 196, 82, 56, 255 },
                         { 120, 114, 120, 255 }, { 34, 26, 28, 255 }, { 255, 140, 60, 255 }, { 255, 96, 180, 255 } },
    [ENEMY_KAMIKAZE] = { { 124, 42, 38, 255 }, { 160, 64, 52, 255 }, { 40, 20, 20, 255 }, { 196, 186, 176, 255 },
                         { 130, 120, 118, 255 }, { 30, 14, 14, 255 }, { 255, 90, 40, 255 }, { 255, 200, 120, 255 } },
    [ENEMY_DIGGER]   = { { 150, 100, 58, 255 }, { 192, 142, 86, 255 }, { 48, 40, 34, 255 }, { 206, 190, 160, 255 },
                         { 118, 108, 96, 255 }, { 30, 26, 22, 255 }, { 255, 150, 50, 255 }, { 255, 150, 50, 255 } },
    [ENEMY_SIPHON]   = { { 36, 132, 100, 255 }, { 70, 196, 150, 255 }, { 26, 48, 42, 255 }, { 140, 220, 186, 255 },
                         { 100, 120, 116, 255 }, { 16, 34, 30, 255 }, { 120, 255, 190, 255 }, { 120, 255, 190, 255 } },
    [ENEMY_SNIPER]   = { { 88, 90, 112, 255 }, { 150, 150, 174, 255 }, { 40, 40, 52, 255 }, { 190, 56, 70, 255 },
                         { 112, 112, 124, 255 }, { 22, 22, 30, 255 }, { 255, 60, 90, 255 }, { 255, 60, 90, 255 } },
    [ENEMY_SHIELDER] = { { 176, 196, 212, 255 }, { 120, 142, 164, 255 }, { 56, 68, 84, 255 }, { 212, 190, 124, 255 },
                         { 128, 136, 148, 255 }, { 24, 34, 44, 255 }, { 90, 230, 255, 255 }, { 90, 230, 255, 255 } },
    [ENEMY_CARRIER]  = { { 112, 98, 140, 255 }, { 168, 156, 196, 255 }, { 36, 32, 48, 255 }, { 206, 160, 92, 255 },
                         { 118, 112, 130, 255 }, { 22, 20, 34, 255 }, { 200, 120, 255, 255 }, { 200, 120, 255, 255 } },
};

static void apply_palette(LowPoly *m, const Palette *p)
{
    lowpoly_part(m, P_HULL, p->hull, false);
    lowpoly_part(m, P_HULL2, p->hull2, false);
    lowpoly_part(m, P_DARK, p->dark, false);
    lowpoly_part(m, P_ACCENT, p->accent, false);
    lowpoly_part(m, P_METAL, p->metal, false);
    lowpoly_part(m, P_GLASS, p->glass, false);
    lowpoly_part_glow(m, P_GLOW, p->glow);
    lowpoly_part(m, P_HOT, (Color){ 255, 244, 226, 255 }, true);
    lowpoly_part(m, P_CORE, p->core, true);
}

// ---------------------------------------------------------------------------
//  Enxame (uns 2,6 de envergadura)
// ---------------------------------------------------------------------------

// Vespa: corpo em segmentos, mandíbulas, dois pares de asas-lâmina e ferrão.
static void swarm_wasp(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = {
        { -1.15f, 0.0f, 0.0f, 0.0f },  { -0.88f, 0.15f, 0.12f, 0.02f }, { -0.50f, 0.26f, 0.21f, 0.04f },
        { -0.10f, 0.22f, 0.19f, 0.04f }, { 0.18f, 0.11f, 0.10f, 0.03f }, { 0.48f, 0.27f, 0.24f, 0.02f },
        { 0.90f, 0.22f, 0.19f, 0.0f },  { 1.22f, 0.0f, 0.0f, -0.02f },
    };
    lp_loft(m, body, 8, 6, 0.0f, P_HULL2, P_DARK, -1, 0.0f, false);
    lp_ring(m, V(0.0f, 0.02f, 0.70f), AZ, 0.20f, 0.27f, 0.09f, 6, P_GLOW, P_GLOW, false);
    lp_plate(m, V(0.12f, -0.03f, -0.72f), V(0.36f, -0.07f, -1.30f), V(0.24f, -0.07f, -1.05f), V(0.10f, -0.04f, -0.55f), AY,
             0.06f, 0.02f, P_ACCENT, P_ACCENT, P_METAL, true);
    lp_plate(m, V(0.16f, 0.14f, -0.30f), V(1.30f, 0.34f, 0.02f), V(1.12f, 0.30f, 0.36f), V(0.16f, 0.12f, 0.04f), AY,
             0.06f, 0.015f, P_ACCENT, P_HULL, P_HULL2, true);
    lowpoly_tri(m, V(0.20f, 0.18f, -0.24f), V(1.26f, 0.37f, 0.04f), V(0.95f, 0.32f, 0.0f), P_GLOW, true);
    lp_plate(m, V(0.14f, -0.04f, 0.0f), V(0.92f, -0.16f, 0.42f), V(0.78f, -0.15f, 0.62f), V(0.13f, -0.04f, 0.26f), AY,
             0.05f, 0.015f, P_HULL, P_DARK, P_HULL2, true);
    lp_box(m, V(0.11f, 0.09f, -0.82f), V(0.06f, 0.04f, 0.07f), P_GLOW, true);
    lp_spike(m, V(0.0f, -0.01f, 1.18f), V(0.0f, -0.12f, 1.62f), 0.07f, 5, P_METAL, false);
    lowpoly_detail(m, true);
    for (int i = 0; i < 3; i++) {
        float z = 0.35f + 0.22f * (float)i;
        lp_spike(m, V(0.0f, 0.2f, z), V(0.0f, 0.36f - 0.04f * (float)i, z + 0.1f), 0.05f, 4, P_ACCENT, false);
    }
    lp_box(m, V(0.0f, 0.22f, -0.45f), V(0.12f, 0.03f, 0.16f), P_HULL, false);
    lp_spike(m, V(0.08f, 0.2f, -0.85f), V(0.22f, 0.45f, -1.15f), 0.02f, 3, P_METAL, true);   // antenas
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.09f, -0.85f), 1.2f);
    light(em, V(0.0f, 0.02f, 0.72f), 1.0f);
}

// Lâmina: asa em crescente com garras para a frente, aletas duplas.
static void swarm_blade(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing pod[] = {
        { -0.95f, 0.0f, 0.0f, 0.02f }, { -0.62f, 0.18f, 0.11f, 0.03f }, { -0.10f, 0.30f, 0.17f, 0.04f },
        { 0.45f, 0.24f, 0.14f, 0.03f }, { 0.82f, 0.12f, 0.08f, 0.02f },
    };
    lp_loft(m, pod, 5, 6, 0.0f, P_HULL2, P_DARK, P_METAL, 0.0f, false);
    lp_plate(m, V(0.24f, 0.03f, -0.35f), V(1.22f, 0.08f, -0.62f), V(1.10f, 0.07f, -0.30f), V(0.26f, 0.03f, 0.48f), AY,
             0.10f, 0.03f, P_HULL, P_DARK, P_ACCENT, true);
    lp_plate(m, V(1.12f, 0.08f, -0.58f), V(1.30f, 0.06f, -1.12f), V(1.36f, 0.06f, -0.95f), V(1.24f, 0.07f, -0.40f), AY,
             0.04f, 0.01f, P_ACCENT, P_ACCENT, P_HULL2, true);
    lp_plate(m, V(0.30f, 0.06f, 0.42f), V(1.12f, 0.10f, -0.26f), V(1.10f, 0.10f, -0.20f), V(0.30f, 0.06f, 0.49f), AY,
             0.02f, 0.01f, P_GLOW, P_GLOW, P_GLOW, true);
    lp_plate(m, V(0.20f, 0.10f, 0.25f), V(0.34f, 0.56f, 0.70f), V(0.34f, 0.52f, 0.92f), V(0.20f, 0.10f, 0.72f), AX,
             0.04f, 0.015f, P_HULL2, P_HULL2, P_ACCENT, true);
    lp_box(m, V(0.0f, 0.17f, -0.30f), V(0.10f, 0.05f, 0.18f), P_GLASS, false);
    lp_box(m, V(0.0f, 0.12f, -0.64f), V(0.07f, 0.03f, 0.06f), P_GLOW, false);
    lp_tube(m, V(0.14f, 0.0f, 0.55f), V(0.14f, 0.0f, 0.92f), 0.08f, 0.10f, 6, P_METAL, -1, true);
    lp_ring(m, V(0.14f, 0.0f, 0.93f), AZ, 0.0f, 0.09f, 0.02f, 6, P_GLOW, P_GLOW, true);
    lowpoly_detail(m, true);
    lp_box(m, V(0.0f, 0.2f, 0.25f), V(0.04f, 0.04f, 0.3f), P_ACCENT, false);
    lp_spike(m, V(1.33f, 0.06f, -1.05f), V(1.42f, 0.07f, -1.38f), 0.02f, 3, P_HOT, true);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.12f, -0.66f), 1.0f);
    light(em, V(0.14f, 0.0f, 0.96f), 0.8f);
    light(em, V(-0.14f, 0.0f, 0.96f), 0.8f);
}

// Tridente: espinha central e dois casulos com pontas, anel aceso atrás.
static void swarm_trident(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing spine[] = {
        { -1.35f, 0.0f, 0.0f, 0.0f }, { -1.0f, 0.07f, 0.07f, 0.0f }, { -0.45f, 0.17f, 0.17f, 0.0f },
        { 0.35f, 0.20f, 0.20f, 0.0f }, { 0.78f, 0.15f, 0.15f, 0.0f }, { 0.96f, 0.10f, 0.10f, 0.0f },
    };
    lp_loft(m, spine, 6, 6, 0.0f, P_HULL2, P_DARK, P_METAL, 0.0f, false);
    lp_tube(m, V(0.72f, 0.0f, -0.85f), V(0.72f, 0.0f, 0.55f), 0.10f, 0.15f, 6, P_HULL, P_METAL, true);
    lp_spike(m, V(0.72f, 0.0f, -0.85f), V(0.72f, 0.0f, -1.35f), 0.08f, 6, P_ACCENT, true);
    lp_plate(m, V(0.12f, 0.0f, -0.25f), V(0.66f, 0.0f, -0.12f), V(0.66f, 0.0f, 0.22f), V(0.12f, 0.0f, 0.30f), AY,
             0.08f, 0.05f, P_HULL, P_DARK, P_ACCENT, true);
    lp_ring(m, V(0.0f, 0.0f, 0.98f), AZ, 0.08f, 0.26f, 0.06f, 8, P_GLOW, P_METAL, false);
    lp_ring(m, V(0.72f, 0.0f, 0.57f), AZ, 0.0f, 0.12f, 0.03f, 6, P_GLOW, P_GLOW, true);
    lp_box(m, V(0.0f, 0.14f, -0.55f), V(0.06f, 0.04f, 0.10f), P_GLOW, false);
    lowpoly_detail(m, true);
    lp_plate(m, V(0.72f, 0.12f, 0.0f), V(0.72f, 0.42f, 0.35f), V(0.72f, 0.40f, 0.52f), V(0.72f, 0.12f, 0.45f), AX,
             0.03f, 0.01f, P_HULL2, P_HULL2, P_ACCENT, true);
    lp_box(m, V(0.0f, 0.19f, 0.1f), V(0.05f, 0.03f, 0.35f), P_ACCENT, false);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 1.0f), 1.3f);
    light(em, V(0.72f, 0.0f, 0.6f), 0.7f);
    light(em, V(-0.72f, 0.0f, 0.6f), 0.7f);
    light(em, V(0.0f, 0.14f, -0.55f), 0.7f);
}

// ---------------------------------------------------------------------------
//  Bombardeiro (uns 3,2 de envergadura). O ponto fraco é o bloco aceso atrás,
//  no centro: (0, 0.08, 1.17) nos três desenhos.
// ---------------------------------------------------------------------------

static void bomber_core(LowPoly *m)
{
    lp_box(m, V(0.0f, 0.08f, 1.17f), V(0.20f, 0.14f, 0.03f), P_CORE, false);
}

static void bomber_turret(LowPoly *m, Vector3 at)
{
    lp_box(m, at, V(0.14f, 0.07f, 0.14f), P_METAL, false);
    lp_tube(m, V(0.05f, at.y + 0.03f, at.z - 0.1f), V(0.05f, at.y + 0.03f, at.z - 0.55f), 0.03f, 0.025f, 5, P_DARK, -1,
            true);
}

// Arraia: asa-delta larga e grossa, casulo de bombas, dois motores.
static void bomber_manta(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = {
        { -1.38f, 0.04f, 0.02f, 0.05f }, { -0.95f, 0.34f, 0.20f, 0.08f }, { -0.25f, 0.56f, 0.38f, 0.12f },
        { 0.55f, 0.52f, 0.36f, 0.10f },  { 1.0f, 0.40f, 0.30f, 0.08f },   { 1.15f, 0.34f, 0.26f, 0.08f },
    };
    lp_loft(m, body, 6, 8, 0.0f, P_HULL, P_DARK, P_DARK, 0.0f, false);
    bomber_core(m);
    lp_plate(m, V(0.45f, 0.06f, -0.55f), V(1.62f, -0.04f, 0.55f), V(1.48f, -0.04f, 0.98f), V(0.50f, 0.06f, 0.92f), AY,
             0.24f, 0.06f, P_HULL2, P_DARK, P_ACCENT, true);
    lp_plate(m, V(1.50f, -0.02f, 0.60f), V(1.58f, -0.42f, 0.92f), V(1.58f, -0.38f, 1.06f), V(1.50f, -0.02f, 0.98f), AX,
             0.05f, 0.02f, P_HULL, P_HULL, P_ACCENT, true);
    lp_plate(m, V(0.62f, 0.20f, 0.22f), V(1.10f, 0.11f, 0.46f), V(1.06f, 0.11f, 0.58f), V(0.62f, 0.20f, 0.42f), AY,
             0.02f, 0.02f, P_GLOW, P_GLOW, P_GLOW, true);
    bomber_turret(m, V(0.0f, 0.50f, -0.15f));
    const LpRing pod[] = { { -0.50f, 0.0f, 0.0f, -0.30f }, { -0.30f, 0.16f, 0.12f, -0.36f }, { 0.40f, 0.20f, 0.14f, -0.38f },
                           { 0.62f, 0.10f, 0.08f, -0.34f } };
    lp_loft(m, pod, 4, 6, 0.0f, P_METAL, P_DARK, P_DARK, 0.0f, false);
    lp_tube(m, V(0.32f, 0.06f, 0.85f), V(0.32f, 0.06f, 1.22f), 0.15f, 0.18f, 8, P_METAL, -1, true);
    lp_ring(m, V(0.32f, 0.06f, 1.23f), AZ, 0.0f, 0.15f, 0.02f, 8, P_GLOW, P_GLOW, true);
    lowpoly_detail(m, true);
    lp_box(m, V(0.28f, 0.45f, -0.35f), V(0.12f, 0.03f, 0.20f), P_ACCENT, true);
    lp_box(m, V(0.0f, 0.47f, 0.35f), V(0.10f, 0.03f, 0.25f), P_HULL2, false);
    lp_box(m, V(1.0f, 0.06f, 0.15f), V(0.18f, 0.03f, 0.08f), P_METAL, true);
    lp_spike(m, V(0.0f, 0.45f, 0.65f), V(0.0f, 0.85f, 0.85f), 0.02f, 3, P_METAL, false);
    lowpoly_detail(m, false);
    light(em, V(0.32f, 0.06f, 1.27f), 0.9f);
    light(em, V(-0.32f, 0.06f, 1.27f), 0.9f);
    light(em, V(0.86f, 0.18f, 0.42f), 0.5f);
    light(em, V(-0.86f, 0.18f, 0.42f), 0.5f);
}

// Martelo: fuselagem estreita, uma barra larga blindada na frente, naceles.
static void bomber_hammer(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = {
        { -0.75f, 0.22f, 0.20f, 0.05f }, { -0.30f, 0.30f, 0.27f, 0.06f }, { 0.50f, 0.30f, 0.27f, 0.07f },
        { 1.0f, 0.27f, 0.24f, 0.08f },   { 1.15f, 0.24f, 0.20f, 0.08f },
    };
    lp_loft(m, body, 5, 8, 0.0f, P_HULL, P_DARK, P_DARK, 0.0f, false);
    bomber_core(m);
    int first = m->count;
    const LpRing bar[] = { { -1.30f, 0.06f, 0.08f, 0.0f }, { -1.05f, 0.26f, 0.17f, 0.0f }, { 1.05f, 0.26f, 0.17f, 0.0f },
                           { 1.30f, 0.06f, 0.08f, 0.0f } };
    lp_loft(m, bar, 4, 6, 0.0f, P_HULL2, P_DARK, P_ACCENT, 0.0f, false);
    lp_transform(m, first, QuaternionFromAxisAngle(AY, PI / 2.0f), V(0.0f, 0.06f, -0.95f));
    lp_box(m, V(0.82f, 0.07f, -1.21f), V(0.18f, 0.05f, 0.03f), P_GLOW, true);
    lp_spike(m, V(1.18f, 0.06f, -1.05f), V(1.26f, 0.06f, -1.48f), 0.09f, 5, P_ACCENT, true);
    lp_tube(m, V(0.62f, 0.0f, -0.10f), V(0.62f, 0.0f, 0.95f), 0.20f, 0.24f, 8, P_HULL, P_METAL, true);
    lp_ring(m, V(0.62f, 0.0f, 0.97f), AZ, 0.0f, 0.2f, 0.03f, 8, P_GLOW, P_GLOW, true);
    lp_plate(m, V(0.26f, 0.05f, 0.10f), V(0.45f, 0.0f, 0.15f), V(0.45f, 0.0f, 0.70f), V(0.26f, 0.05f, 0.75f), AY,
             0.08f, 0.06f, P_HULL2, P_DARK, P_ACCENT, true);
    bomber_turret(m, V(0.0f, 0.36f, 0.10f));
    lp_tube(m, V(0.0f, -0.32f, -0.30f), V(0.0f, -0.32f, 0.50f), 0.14f, 0.16f, 6, P_METAL, P_DARK, false);
    lowpoly_detail(m, true);
    lp_ring(m, V(0.62f, 0.0f, 0.2f), AZ, 0.2f, 0.26f, 0.08f, 8, P_ACCENT, P_DARK, true);
    lp_ring(m, V(0.62f, 0.0f, 0.6f), AZ, 0.22f, 0.28f, 0.08f, 8, P_ACCENT, P_DARK, true);
    lp_box(m, V(0.5f, 0.24f, -0.95f), V(0.3f, 0.03f, 0.1f), P_METAL, true);
    lp_spike(m, V(0.62f, 0.2f, 0.4f), V(0.62f, 0.5f, 0.55f), 0.02f, 3, P_METAL, true);
    lowpoly_detail(m, false);
    light(em, V(0.62f, 0.0f, 1.0f), 1.0f);
    light(em, V(-0.62f, 0.0f, 1.0f), 1.0f);
    light(em, V(0.82f, 0.07f, -1.24f), 0.5f);
    light(em, V(-0.82f, 0.07f, -1.24f), 0.5f);
}

// Besouro: carapaça arredondada em gomos, chifres, saias laterais, patas.
static void bomber_beetle(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing shell[] = {
        { -1.20f, 0.25f, 0.12f, 0.12f }, { -0.85f, 0.80f, 0.36f, 0.12f }, { -0.15f, 1.10f, 0.55f, 0.12f },
        { 0.55f, 1.06f, 0.50f, 0.12f },  { 0.98f, 0.70f, 0.36f, 0.10f },  { 1.15f, 0.38f, 0.24f, 0.08f },
    };
    lp_loft(m, shell, 6, 10, 0.0f, P_HULL, P_DARK, P_DARK, 0.0f, false);
    bomber_core(m);
    lp_box(m, V(0.0f, 0.66f, -0.10f), V(0.50f, 0.03f, 0.05f), P_ACCENT, false);
    lp_box(m, V(0.0f, 0.63f, 0.45f), V(0.45f, 0.03f, 0.05f), P_ACCENT, false);
    lp_box(m, V(0.0f, 0.58f, -0.62f), V(0.36f, 0.03f, 0.05f), P_ACCENT, false);
    lp_spike(m, V(0.30f, 0.05f, -1.05f), V(0.62f, 0.0f, -1.55f), 0.10f, 5, P_ACCENT, true);
    lp_plate(m, V(1.05f, 0.0f, -0.40f), V(1.20f, -0.26f, -0.30f), V(1.18f, -0.26f, 0.60f), V(1.02f, 0.0f, 0.70f), AX,
             0.05f, 0.04f, P_HULL2, P_HULL2, P_ACCENT, true);
    lp_box(m, V(0.18f, 0.10f, -1.18f), V(0.07f, 0.04f, 0.03f), P_GLOW, true);
    bomber_turret(m, V(0.0f, 0.72f, 0.10f));
    lp_tube(m, V(0.45f, 0.05f, 0.95f), V(0.45f, 0.05f, 1.20f), 0.12f, 0.15f, 6, P_METAL, -1, true);
    lp_ring(m, V(0.45f, 0.05f, 1.21f), AZ, 0.0f, 0.12f, 0.02f, 6, P_GLOW, P_GLOW, true);
    lowpoly_detail(m, true);
    for (int i = 0; i < 3; i++) {
        float z = -0.4f + 0.4f * (float)i;
        lp_spike(m, V(0.6f, -0.15f, z), V(0.98f, -0.52f, z + 0.1f), 0.05f, 4, P_METAL, true);
    }
    lp_box(m, V(0.55f, 0.42f, 0.1f), V(0.12f, 0.03f, 0.3f), P_HULL2, true);
    lowpoly_detail(m, false);
    light(em, V(0.45f, 0.05f, 1.25f), 0.8f);
    light(em, V(-0.45f, 0.05f, 1.25f), 0.8f);
    light(em, V(0.18f, 0.10f, -1.22f), 0.45f);
    light(em, V(-0.18f, 0.10f, -1.22f), 0.45f);
}

// ---------------------------------------------------------------------------
//  Kamikaze (uns 2 de diâmetro)
// ---------------------------------------------------------------------------

// Ouriço: núcleo cravado de espinhos (os 12 vértices de um icosaedro).
static void kamikaze_urchin(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing core[] = { { -0.46f, 0.0f, 0.0f, 0.0f }, { -0.33f, 0.33f, 0.33f, 0.0f }, { 0.0f, 0.46f, 0.46f, 0.0f },
                            { 0.33f, 0.33f, 0.33f, 0.0f }, { 0.46f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, core, 5, 8, 0.0f, P_HULL, P_HULL2, -1, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AZ, 0.44f, 0.52f, 0.10f, 8, P_GLOW, P_GLOW, false);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AX, 0.44f, 0.50f, 0.08f, 8, P_GLOW, P_GLOW, false);
    const float phi = 1.618f;
    const Vector3 dirs[12] = {
        { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi }, { 1, phi, 0 }, { -1, phi, 0 },
        { 1, -phi, 0 }, { -1, -phi, 0 }, { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
    };
    for (int i = 0; i < 12; i++) {
        Vector3 d = Vector3Normalize(dirs[i]);
        lp_spike(m, Vector3Scale(d, 0.38f), Vector3Scale(d, 1.02f), 0.12f, 4, P_ACCENT, false);
    }
    lp_spike(m, V(0.0f, 0.0f, -0.40f), V(0.0f, 0.0f, -1.45f), 0.16f, 4, P_ACCENT, false);
    lowpoly_detail(m, true);
    for (int i = 0; i < 12; i++) {
        Vector3 d = Vector3Normalize(dirs[i]);
        lp_spike(m, Vector3Scale(d, 0.86f), Vector3Scale(d, 1.06f), 0.04f, 3, P_GLOW, false);   // pontas acesas
    }
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 0.0f), 1.2f);
}

// Míssil: ogiva com faixa acesa, quatro aletas e escape atrás.
static void kamikaze_missile(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = {
        { -1.45f, 0.0f, 0.0f, 0.0f }, { -1.10f, 0.12f, 0.12f, 0.0f }, { -0.65f, 0.30f, 0.30f, 0.0f },
        { 0.30f, 0.34f, 0.34f, 0.0f }, { 0.75f, 0.28f, 0.28f, 0.0f }, { 0.95f, 0.20f, 0.20f, 0.0f },
    };
    lp_loft(m, body, 6, 8, 0.0f, P_HULL, P_HULL2, P_METAL, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, -0.55f), AZ, 0.27f, 0.36f, 0.12f, 8, P_GLOW, P_METAL, false);
    lp_ring(m, V(0.0f, 0.0f, 0.05f), AZ, 0.33f, 0.37f, 0.08f, 8, P_ACCENT, P_DARK, false);
    lp_plate(m, V(0.30f, 0.0f, 0.35f), V(0.74f, 0.0f, 0.72f), V(0.72f, 0.0f, 0.95f), V(0.28f, 0.0f, 0.90f), AY, 0.05f,
             0.02f, P_ACCENT, P_ACCENT, P_DARK, true);
    lp_plate(m, V(0.0f, 0.30f, 0.35f), V(0.0f, 0.74f, 0.72f), V(0.0f, 0.72f, 0.95f), V(0.0f, 0.28f, 0.90f), AX, 0.05f,
             0.02f, P_ACCENT, P_ACCENT, P_DARK, false);
    lp_plate(m, V(0.0f, -0.30f, 0.35f), V(0.0f, -0.74f, 0.72f), V(0.0f, -0.72f, 0.95f), V(0.0f, -0.28f, 0.90f), AX, 0.05f,
             0.02f, P_ACCENT, P_ACCENT, P_DARK, false);
    lp_tube(m, V(0.0f, 0.0f, 0.95f), V(0.0f, 0.0f, 1.15f), 0.16f, 0.12f, 8, P_METAL, -1, false);
    lp_ring(m, V(0.0f, 0.0f, 1.16f), AZ, 0.0f, 0.12f, 0.02f, 8, P_HOT, P_HOT, false);
    lp_spike(m, V(0.0f, 0.0f, -1.30f), V(0.0f, 0.0f, -1.72f), 0.05f, 4, P_ACCENT, false);
    lowpoly_detail(m, true);
    lp_box(m, V(0.0f, 0.33f, -0.2f), V(0.05f, 0.03f, 0.25f), P_DARK, false);
    lp_box(m, V(0.0f, -0.33f, -0.2f), V(0.05f, 0.03f, 0.25f), P_DARK, false);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 1.2f), 1.2f);
    light(em, V(0.0f, 0.0f, -0.55f), 0.9f);
}

// Estrela: disco com seis lâminas, que gira como um shuriken.
static void kamikaze_star(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing disc[] = { { -0.26f, 0.0f, 0.0f, 0.0f }, { -0.16f, 0.42f, 0.42f, 0.0f }, { 0.16f, 0.42f, 0.42f, 0.0f },
                            { 0.26f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, disc, 4, 10, 0.0f, P_HULL, P_HULL2, -1, 0.0f, false);
    int first = m->count;
    lp_plate(m, V(-0.22f, 0.34f, 0.0f), V(-0.04f, 1.22f, 0.0f), V(0.12f, 1.05f, 0.0f), V(0.24f, 0.34f, 0.0f), AZ, 0.12f,
             0.02f, P_ACCENT, P_ACCENT, P_HULL, false);
    radial_copies(m, first, AZ, 6);
    lp_ring(m, V(0.0f, 0.0f, -0.22f), AZ, 0.10f, 0.26f, 0.04f, 10, P_GLOW, P_METAL, false);
    lp_box(m, V(0.0f, 0.0f, -0.24f), V(0.08f, 0.08f, 0.03f), P_HOT, false);
    lowpoly_detail(m, true);
    first = m->count;
    lp_box(m, V(0.0f, 0.62f, -0.07f), V(0.05f, 0.18f, 0.02f), P_GLOW, false);   // veio aceso em cada lâmina
    radial_copies(m, first, AZ, 6);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, -0.26f), 1.6f);
    em->spins = true;
}

// ---------------------------------------------------------------------------
//  Escavador (uns 3 de comprimento)
// ---------------------------------------------------------------------------

// Broca: broca em espiral, colar aceso, corpo hexagonal blindado, três pernas.
static void digger_drill(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing drill[] = { { -1.60f, 0.0f, 0.0f, 0.0f }, { -1.15f, 0.22f, 0.22f, 0.0f }, { -0.70f, 0.42f, 0.42f, 0.0f },
                             { -0.28f, 0.56f, 0.56f, 0.0f } };
    lp_loft(m, drill, 4, 6, 0.0f, P_ACCENT, P_METAL, -1, 0.5f, false);
    lp_ring(m, V(0.0f, 0.0f, -0.22f), AZ, 0.40f, 0.66f, 0.10f, 6, P_GLOW, P_METAL, false);
    const LpRing body[] = { { -0.16f, 0.60f, 0.60f, 0.0f }, { 0.50f, 0.62f, 0.62f, 0.0f }, { 0.95f, 0.50f, 0.50f, 0.0f },
                            { 1.05f, 0.42f, 0.42f, 0.0f } };
    lp_loft(m, body, 4, 6, 0.0f, P_HULL, P_HULL2, P_DARK, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, 0.20f), AZ, 0.58f, 0.68f, 0.12f, 6, P_ACCENT, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.70f), AZ, 0.54f, 0.64f, 0.10f, 6, P_ACCENT, P_DARK, false);
    int first = m->count;
    lp_plate(m, V(-0.08f, 0.50f, 0.45f), V(-0.06f, 1.12f, 1.25f), V(0.06f, 1.12f, 1.25f), V(0.08f, 0.50f, 0.90f), AX,
             0.06f, 0.04f, P_METAL, P_METAL, P_DARK, false);
    lp_spike(m, V(0.0f, 1.12f, 1.25f), V(0.0f, 1.25f, 1.45f), 0.06f, 4, P_ACCENT, false);
    radial_copies(m, first, AZ, 3);
    lp_ring(m, V(0.0f, 0.0f, 1.07f), AZ, 0.0f, 0.28f, 0.03f, 6, P_GLOW, P_GLOW, false);
    lowpoly_detail(m, true);
    lp_tube(m, V(0.64f, 0.0f, -0.05f), V(0.64f, 0.0f, 0.8f), 0.09f, 0.09f, 5, P_METAL, P_DARK, true);
    lp_box(m, V(0.0f, 0.62f, 0.45f), V(0.15f, 0.04f, 0.18f), P_HULL2, false);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, -0.20f), 1.4f);
    light(em, V(0.0f, 0.0f, 1.10f), 0.9f);
    em->spins = true;
}

// Toupeira: duas brocas lado a lado, corpo oval, garras.
static void digger_mole(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = { { -0.55f, 0.55f, 0.40f, 0.0f }, { -0.10f, 0.70f, 0.50f, 0.02f }, { 0.60f, 0.62f, 0.45f, 0.02f },
                            { 1.0f, 0.42f, 0.32f, 0.0f },  { 1.10f, 0.30f, 0.22f, 0.0f } };
    lp_loft(m, body, 5, 8, 0.0f, P_HULL, P_DARK, P_DARK, 0.0f, false);
    const LpRing drill[] = { { -1.60f, 0.0f, 0.0f, 0.0f }, { -1.20f, 0.12f, 0.12f, 0.0f }, { -0.75f, 0.22f, 0.22f, 0.0f },
                             { -0.45f, 0.26f, 0.26f, 0.0f } };
    lp_loft(m, drill, 4, 6, 0.33f, P_ACCENT, P_METAL, -1, 0.6f, true);
    lp_ring(m, V(0.33f, 0.0f, -0.44f), AZ, 0.18f, 0.32f, 0.08f, 6, P_GLOW, P_METAL, true);
    lp_plate(m, V(0.60f, -0.10f, -0.30f), V(1.05f, -0.35f, -0.75f), V(1.0f, -0.40f, -0.50f), V(0.62f, -0.15f, 0.10f), AY,
             0.08f, 0.03f, P_ACCENT, P_DARK, P_METAL, true);
    lp_box(m, V(0.2f, 0.42f, -0.40f), V(0.07f, 0.04f, 0.05f), P_GLOW, true);
    lp_tube(m, V(0.30f, 0.25f, 0.85f), V(0.30f, 0.45f, 1.15f), 0.07f, 0.06f, 5, P_METAL, P_GLOW, true);
    lowpoly_detail(m, true);
    lp_box(m, V(0.0f, 0.5f, 0.15f), V(0.25f, 0.04f, 0.3f), P_HULL2, false);
    lp_box(m, V(0.45f, 0.38f, 0.2f), V(0.1f, 0.03f, 0.25f), P_ACCENT, true);
    lp_spike(m, V(0.0f, 0.52f, -0.2f), V(0.0f, 0.75f, -0.05f), 0.04f, 4, P_ACCENT, false);
    lowpoly_detail(m, false);
    light(em, V(0.33f, 0.0f, -0.42f), 0.8f);
    light(em, V(-0.33f, 0.0f, -0.42f), 0.8f);
    light(em, V(0.30f, 0.46f, 1.17f), 0.5f);
    light(em, V(-0.30f, 0.46f, 1.17f), 0.5f);
}

// Parafuso: um saca-rolhas comprido, corpo fino, anéis estabilizadores.
static void digger_screw(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing screw[] = { { -1.75f, 0.0f, 0.0f, 0.0f }, { -1.40f, 0.14f, 0.14f, 0.0f }, { -1.0f, 0.26f, 0.26f, 0.0f },
                             { -0.60f, 0.36f, 0.36f, 0.0f }, { -0.20f, 0.42f, 0.42f, 0.0f } };
    lp_loft(m, screw, 5, 4, 0.0f, P_ACCENT, P_METAL, -1, 0.9f, false);
    lp_ring(m, V(0.0f, 0.0f, -0.15f), AZ, 0.30f, 0.46f, 0.08f, 8, P_GLOW, P_METAL, false);
    const LpRing body[] = { { -0.20f, 0.38f, 0.38f, 0.0f }, { 0.40f, 0.32f, 0.32f, 0.0f }, { 1.0f, 0.28f, 0.28f, 0.0f },
                            { 1.20f, 0.18f, 0.18f, 0.0f } };
    lp_loft(m, body, 4, 8, 0.0f, P_HULL, P_HULL2, P_DARK, 0.0f, false);
    lp_ring(m, V(0.0f, 0.0f, 0.25f), AZ, 0.34f, 0.78f, 0.06f, 8, P_HULL2, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.85f), AZ, 0.28f, 0.62f, 0.05f, 8, P_HULL2, P_DARK, false);
    int first = m->count;
    lp_plate(m, V(0.0f, 0.3f, 0.25f), V(0.0f, 0.62f, 0.3f), V(0.0f, 0.55f, 0.85f), V(0.0f, 0.28f, 0.85f), AX, 0.03f, 0.03f,
             P_HULL, P_HULL, P_ACCENT, false);
    radial_copies(m, first, AZ, 4);
    lp_ring(m, V(0.0f, 0.0f, 1.21f), AZ, 0.0f, 0.16f, 0.02f, 8, P_GLOW, P_GLOW, false);
    lowpoly_detail(m, true);
    first = m->count;
    lp_box(m, V(0.0f, 0.78f, 0.25f), V(0.06f, 0.06f, 0.06f), P_GLOW, false);
    radial_copies(m, first, AZ, 4);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, -0.15f), 1.2f);
    light(em, V(0.0f, 0.0f, 1.25f), 0.8f);
    em->spins = true;
}

// ---------------------------------------------------------------------------
//  Sifão (uns 2 de diâmetro, de pé: o feixe sai para baixo)
// ---------------------------------------------------------------------------

// Medusa: cúpula com coroa acesa, emissor embaixo e tentáculos.
static void siphon_jelly(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    int first = m->count;
    const LpRing dome[] = { { -0.12f, 0.50f, 0.50f, 0.0f }, { 0.0f, 0.98f, 0.98f, 0.0f }, { 0.30f, 0.86f, 0.86f, 0.0f },
                            { 0.55f, 0.56f, 0.56f, 0.0f }, { 0.72f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, dome, 5, 10, 0.0f, P_HULL2, P_HULL2, P_DARK, 0.0f, false);
    upright(m, first);
    lp_ring(m, V(0.0f, 0.30f, 0.0f), AY, 0.84f, 0.90f, 0.04f, 10, P_GLOW, P_GLOW, false);
    lp_ring(m, V(0.0f, 0.02f, 0.0f), AY, 0.96f, 1.04f, 0.06f, 10, P_HULL, P_DARK, false);
    lp_tube(m, V(0.0f, -0.10f, 0.0f), V(0.0f, -0.38f, 0.0f), 0.35f, 0.15f, 8, P_DARK, P_GLOW, false);
    lp_box(m, V(0.0f, 0.70f, 0.0f), V(0.08f, 0.05f, 0.08f), P_GLOW, false);
    first = m->count;
    lp_tube(m, V(0.70f, -0.05f, 0.0f), V(0.86f, -0.80f, 0.0f), 0.07f, 0.045f, 4, P_ACCENT, -1, false);
    lp_tube(m, V(0.86f, -0.80f, 0.0f), V(0.74f, -1.40f, 0.0f), 0.045f, 0.01f, 4, P_ACCENT, -1, false);
    radial_copies(m, first, AY, 8);
    lowpoly_detail(m, true);
    first = m->count;
    lp_spike(m, V(0.80f, -1.2f, 0.0f), V(0.74f, -1.42f, 0.0f), 0.03f, 3, P_GLOW, false);
    lp_box(m, V(0.0f, 0.62f, 0.38f), V(0.05f, 0.04f, 0.12f), P_ACCENT, false);
    radial_copies(m, first, AY, 8);
    lowpoly_detail(m, false);
    light(em, V(0.0f, -0.42f, 0.0f), 1.4f);
    light(em, V(0.0f, 0.74f, 0.0f), 0.6f);
    em->beam = V(0.0f, -0.40f, 0.0f);
}

// Funil: boca larga para cima, borda acesa, braços e emissor embaixo.
static void siphon_funnel(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    int first = m->count;
    const LpRing funnel[] = { { 0.55f, 1.0f, 1.0f, 0.0f }, { 0.35f, 0.95f, 0.95f, 0.0f }, { 0.0f, 0.45f, 0.45f, 0.0f },
                              { -0.30f, 0.20f, 0.20f, 0.0f }, { -0.42f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, funnel, 5, 12, 0.0f, P_HULL, P_HULL, -1, 0.0f, false);
    upright(m, first);
    lp_tube(m, V(0.0f, 0.5f, 0.0f), V(0.0f, 0.06f, 0.0f), 0.9f, 0.15f, 12, P_DARK, -1, false);
    lp_ring(m, V(0.0f, 0.56f, 0.0f), AY, 0.92f, 1.06f, 0.05f, 12, P_GLOW, P_METAL, false);
    first = m->count;
    lp_plate(m, V(0.95f, 0.45f, 0.0f), V(0.30f, -0.28f, 0.0f), V(0.24f, -0.24f, 0.0f), V(0.85f, 0.50f, 0.0f), AZ, 0.06f,
             0.04f, P_HULL2, P_HULL2, P_ACCENT, false);
    radial_copies(m, first, AY, 3);
    lp_tube(m, V(0.0f, -0.30f, 0.0f), V(0.0f, -0.48f, 0.0f), 0.15f, 0.10f, 6, P_METAL, P_GLOW, false);
    lowpoly_detail(m, true);
    lp_ring(m, V(0.0f, 0.10f, 0.0f), AY, 0.50f, 0.56f, 0.04f, 12, P_ACCENT, P_DARK, false);
    first = m->count;
    lp_box(m, V(0.0f, 0.62f, 1.0f), V(0.05f, 0.05f, 0.05f), P_HOT, false);
    radial_copies(m, first, AY, 6);
    lowpoly_detail(m, false);
    light(em, V(0.0f, -0.50f, 0.0f), 1.2f);
    light(em, V(0.0f, 0.45f, 0.0f), 1.5f);
    em->beam = V(0.0f, -0.48f, 0.0f);
}

// Flor: núcleo esférico, cinco pétalas de bordas acesas, caule com emissor.
static void siphon_flower(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    int first = m->count;
    const LpRing orb[] = { { 0.45f, 0.0f, 0.0f, 0.0f }, { 0.32f, 0.32f, 0.32f, 0.0f }, { 0.0f, 0.45f, 0.45f, 0.0f },
                           { -0.32f, 0.32f, 0.32f, 0.0f }, { -0.45f, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, orb, 5, 8, 0.0f, P_HULL2, P_HULL2, -1, 0.0f, false);
    upright(m, first);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AY, 0.43f, 0.50f, 0.07f, 8, P_GLOW, P_GLOW, false);
    first = m->count;
    Vector3 petal_normal = Vector3Normalize(V(-0.45f, 0.9f, 0.0f));
    lp_plate(m, V(0.32f, 0.12f, -0.20f), V(1.0f, 0.48f, -0.28f), V(1.0f, 0.48f, 0.28f), V(0.32f, 0.12f, 0.20f),
             petal_normal, 0.06f, 0.02f, P_HULL, P_ACCENT, P_GLOW, false);
    lowpoly_tri(m, V(1.0f, 0.48f, -0.28f), V(1.28f, 0.64f, 0.0f), V(1.0f, 0.48f, 0.28f), P_HULL, false);
    radial_copies(m, first, AY, 5);
    lp_tube(m, V(0.0f, -0.40f, 0.0f), V(0.0f, -0.95f, 0.0f), 0.12f, 0.07f, 6, P_METAL, P_GLOW, false);
    lowpoly_detail(m, true);
    first = m->count;
    lp_plate(m, V(0.05f, -0.6f, 0.0f), V(0.35f, -0.48f, 0.0f), V(0.3f, -0.62f, 0.0f), V(0.05f, -0.72f, 0.0f), AZ, 0.02f,
             0.01f, P_ACCENT, P_ACCENT, P_HULL, false);
    radial_copies(m, first, AY, 3);
    lowpoly_detail(m, false);
    light(em, V(0.0f, -0.98f, 0.0f), 1.0f);
    light(em, V(0.0f, 0.0f, 0.0f), 1.2f);
    em->beam = V(0.0f, -0.96f, 0.0f);
}

// ---------------------------------------------------------------------------
//  Atirador (uns 3,4 de comprimento; boca do cano em z = -1.7 nos três)
// ---------------------------------------------------------------------------

// Agulha: corpo fino, cano longo com mira, asas em flecha.
static void sniper_needle(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = {
        { -0.70f, 0.06f, 0.06f, 0.0f }, { -0.45f, 0.16f, 0.14f, 0.02f }, { 0.0f, 0.24f, 0.20f, 0.03f },
        { 0.55f, 0.20f, 0.17f, 0.02f }, { 0.95f, 0.12f, 0.10f, 0.02f },  { 1.05f, 0.06f, 0.05f, 0.02f },
    };
    lp_loft(m, body, 6, 6, 0.0f, P_HULL2, P_DARK, P_METAL, 0.0f, false);
    lp_tube(m, V(0.0f, 0.0f, -0.6f), V(0.0f, 0.0f, -1.7f), 0.07f, 0.05f, 6, P_DARK, P_METAL, false);
    lp_box(m, V(0.0f, 0.0f, -1.6f), V(0.08f, 0.08f, 0.06f), P_METAL, false);
    lp_tube(m, V(0.0f, 0.20f, -0.50f), V(0.0f, 0.20f, -0.05f), 0.05f, 0.05f, 6, P_METAL, P_GLOW, false);
    lp_plate(m, V(0.18f, 0.0f, 0.10f), V(0.95f, -0.06f, 0.72f), V(0.88f, -0.06f, 0.90f), V(0.16f, 0.0f, 0.62f), AY, 0.06f,
             0.02f, P_HULL, P_DARK, P_ACCENT, true);
    lp_box(m, V(0.0f, 0.17f, -0.35f), V(0.06f, 0.04f, 0.06f), P_GLOW, false);
    lp_ring(m, V(0.0f, 0.02f, 1.07f), AZ, 0.0f, 0.07f, 0.02f, 6, P_GLOW, P_GLOW, false);
    lowpoly_detail(m, true);
    lp_box(m, V(0.0f, 0.21f, 0.2f), V(0.03f, 0.02f, 0.3f), P_ACCENT, false);
    lp_ring(m, V(0.0f, 0.0f, -1.0f), AZ, 0.06f, 0.1f, 0.05f, 6, P_ACCENT, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, -1.3f), AZ, 0.055f, 0.09f, 0.05f, 6, P_ACCENT, P_DARK, false);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.17f, -0.35f), 0.8f);
    light(em, V(0.0f, 0.02f, 1.1f), 0.8f);
}

// Arpão: dois trilhos de canhão com bobinas, cabine e aletas em cruz.
static void sniper_harpoon(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing pod[] = { { -0.50f, 0.12f, 0.12f, 0.0f }, { -0.20f, 0.28f, 0.22f, 0.0f }, { 0.50f, 0.30f, 0.24f, 0.0f },
                           { 0.95f, 0.18f, 0.14f, 0.0f } };
    lp_loft(m, pod, 4, 8, 0.0f, P_HULL, P_DARK, P_METAL, 0.0f, false);
    lp_box(m, V(0.10f, 0.0f, -1.06f), V(0.035f, 0.05f, 0.62f), P_DARK, true);
    lp_box(m, V(0.06f, 0.0f, -1.06f), V(0.01f, 0.02f, 0.60f), P_GLOW, true);
    lp_plate(m, V(0.25f, 0.0f, 0.40f), V(0.80f, 0.0f, 0.75f), V(0.80f, 0.0f, 0.95f), V(0.25f, 0.0f, 0.85f), AY, 0.05f,
             0.02f, P_HULL2, P_HULL2, P_ACCENT, true);
    lp_plate(m, V(0.0f, 0.22f, 0.40f), V(0.0f, 0.70f, 0.80f), V(0.0f, 0.68f, 0.98f), V(0.0f, 0.20f, 0.85f), AX, 0.05f,
             0.02f, P_HULL2, P_HULL2, P_ACCENT, false);
    lp_plate(m, V(0.0f, -0.22f, 0.40f), V(0.0f, -0.62f, 0.80f), V(0.0f, -0.60f, 0.98f), V(0.0f, -0.20f, 0.85f), AX, 0.05f,
             0.02f, P_HULL2, P_HULL2, P_ACCENT, false);
    lp_box(m, V(0.0f, 0.22f, -0.15f), V(0.10f, 0.06f, 0.20f), P_GLASS, false);
    lp_ring(m, V(0.0f, 0.0f, 0.96f), AZ, 0.0f, 0.12f, 0.02f, 8, P_GLOW, P_GLOW, false);
    lowpoly_detail(m, true);
    for (int i = 0; i < 3; i++) {
        lp_ring(m, V(0.0f, 0.0f, -0.6f - 0.3f * (float)i), AZ, 0.16f, 0.22f, 0.06f, 8, P_METAL, P_DARK, false);
    }
    lp_spike(m, V(0.0f, 0.28f, 0.2f), V(0.0f, 0.55f, 0.45f), 0.02f, 3, P_METAL, false);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, -1.68f), 0.6f);
    light(em, V(0.0f, 0.0f, 1.0f), 0.9f);
}

// Lança tripla: corpo triangular, três aletas longas com trilhos acesos.
static void sniper_trilance(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing body[] = { { -0.80f, 0.0f, 0.0f, 0.0f }, { -0.50f, 0.30f, 0.30f, 0.0f }, { 0.60f, 0.28f, 0.28f, 0.0f },
                            { 1.0f, 0.12f, 0.12f, 0.0f } };
    lp_loft(m, body, 4, 3, 0.0f, P_HULL, P_HULL2, P_METAL, 0.0f, false);
    int first = m->count;
    lp_plate(m, V(0.0f, 0.18f, -0.40f), V(0.0f, 0.62f, 0.20f), V(0.0f, 0.60f, 0.85f), V(0.0f, 0.15f, 0.90f), AX, 0.05f,
             0.02f, P_HULL2, P_HULL2, P_ACCENT, false);
    lp_plate(m, V(0.0f, 0.20f, -0.38f), V(0.0f, 0.60f, 0.18f), V(0.0f, 0.64f, 0.23f), V(0.0f, 0.24f, -0.33f), AX, 0.07f,
             0.03f, P_GLOW, P_GLOW, P_GLOW, false);
    radial_copies(m, first, AZ, 3);
    lp_tube(m, V(0.0f, 0.0f, -0.75f), V(0.0f, 0.0f, -1.7f), 0.06f, 0.045f, 5, P_DARK, P_METAL, false);
    lp_ring(m, V(0.0f, 0.0f, -0.78f), AZ, 0.08f, 0.18f, 0.03f, 6, P_GLOW, P_METAL, false);
    lp_ring(m, V(0.0f, 0.0f, 1.01f), AZ, 0.0f, 0.12f, 0.02f, 6, P_GLOW, P_GLOW, false);
    lowpoly_detail(m, true);
    first = m->count;
    lp_spike(m, V(0.0f, 0.62f, 0.5f), V(0.0f, 0.8f, 0.75f), 0.02f, 3, P_METAL, false);
    radial_copies(m, first, AZ, 3);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, -0.80f), 0.8f);
    light(em, V(0.0f, 0.0f, 1.04f), 0.8f);
}

// ---------------------------------------------------------------------------
//  Escudeiro (uns 2 de diâmetro)
// ---------------------------------------------------------------------------

static void shielder_core(LowPoly *m, float r)
{
    const LpRing core[] = { { -r, 0.0f, 0.0f, 0.0f }, { -r * 0.6f, r * 0.75f, r * 0.75f, 0.0f },
                            { r * 0.6f, r * 0.75f, r * 0.75f, 0.0f }, { r, 0.0f, 0.0f, 0.0f } };
    lp_loft(m, core, 4, 6, 0.0f, P_GLOW, P_GLOW, -1, 0.0f, false);
}

// Anel: aro octogonal grosso, quatro nós emissores, raios até o núcleo.
static void shielder_ring(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AZ, 0.72f, 1.0f, 0.24f, 8, P_HULL, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AZ, 0.64f, 0.72f, 0.10f, 8, P_GLOW, P_GLOW, false);
    int first = m->count;
    lp_box(m, V(1.0f, 0.0f, 0.0f), V(0.10f, 0.12f, 0.17f), P_METAL, false);
    lp_box(m, V(1.0f, 0.0f, -0.17f), V(0.06f, 0.07f, 0.02f), P_HOT, false);
    lp_tube(m, V(0.25f, 0.0f, 0.0f), V(0.66f, 0.0f, 0.0f), 0.04f, 0.04f, 4, P_METAL, -1, false);
    radial_copies(m, first, AZ, 4);
    shielder_core(m, 0.28f);
    lowpoly_detail(m, true);
    first = m->count;
    lp_box(m, V(0.0f, 0.86f, 0.13f), V(0.12f, 0.04f, 0.02f), P_ACCENT, false);
    radial_copies(m, first, AZ, 8);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 0.0f), 1.6f);
}

// Tríade: três pilones com emissores, ligados por uma moldura triangular.
static void shielder_triad(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    int first = m->count;
    lp_tube(m, V(0.0f, 0.2f, 0.0f), V(0.0f, 0.88f, 0.0f), 0.08f, 0.05f, 5, P_METAL, -1, false);
    const LpRing pod[] = { { -0.22f, 0.0f, 0.0f, 1.0f }, { -0.12f, 0.16f, 0.12f, 1.0f }, { 0.14f, 0.16f, 0.12f, 1.0f },
                           { 0.22f, 0.08f, 0.06f, 1.0f } };
    lp_loft(m, pod, 4, 6, 0.0f, P_HULL, P_HULL2, P_METAL, 0.0f, false);
    lp_box(m, V(0.0f, 1.0f, -0.20f), V(0.06f, 0.06f, 0.03f), P_HOT, false);
    // Lado da moldura até o próximo pilone (a 120 graus).
    Vector3 here = V(0.0f, 0.95f, 0.0f);
    Vector3 next = V(-sinf(2.0f * PI / 3.0f) * 0.95f, cosf(2.0f * PI / 3.0f) * 0.95f, 0.0f);
    Vector3 in_here = Vector3Scale(here, 0.88f), in_next = Vector3Scale(next, 0.88f);
    lp_plate(m, here, next, in_next, in_here, AZ, 0.08f, 0.08f, P_HULL2, P_HULL2, P_DARK, false);
    radial_copies(m, first, AZ, 3);
    shielder_core(m, 0.30f);
    lowpoly_detail(m, true);
    first = m->count;
    lp_spike(m, V(0.0f, 1.18f, 0.0f), V(0.0f, 1.42f, 0.0f), 0.04f, 4, P_ACCENT, false);
    radial_copies(m, first, AZ, 3);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 0.0f), 1.5f);
    light(em, V(0.0f, 1.0f, -0.2f), 0.6f);
    light(em, V(-0.87f, -0.5f, -0.2f), 0.6f);
    light(em, V(0.87f, -0.5f, -0.2f), 0.6f);
}

// Esfera: dois aros cruzados e um aro aceso, núcleo no meio.
static void shielder_sphere(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AZ, 0.80f, 0.95f, 0.12f, 10, P_HULL, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AX, 0.80f, 0.95f, 0.12f, 10, P_HULL2, P_DARK, false);
    lp_ring(m, V(0.0f, 0.0f, 0.0f), AY, 0.88f, 0.93f, 0.04f, 10, P_GLOW, P_GLOW, false);
    shielder_core(m, 0.32f);
    int first = m->count;
    lp_spike(m, V(0.0f, 0.93f, 0.0f), V(0.0f, 1.22f, 0.0f), 0.08f, 4, P_ACCENT, false);
    radial_copies(m, first, AZ, 4);
    lowpoly_detail(m, true);
    first = m->count;
    lp_box(m, V(0.0f, 0.0f, 0.88f), V(0.06f, 0.06f, 0.06f), P_HOT, false);
    radial_copies(m, first, AY, 4);
    lowpoly_detail(m, false);
    light(em, V(0.0f, 0.0f, 0.0f), 1.6f);
}

// ---------------------------------------------------------------------------
//  Porta-drones (uns 2,4 de comprimento; hangar embaixo, perto de (0, -0.3, 0.2))
// ---------------------------------------------------------------------------

static void carrier_engines(EnemyModel *em, float z, float spread, int count, float y)
{
    LowPoly *m = &em->mesh;
    for (int i = 0; i < count; i++) {
        float x = (count == 1) ? 0.0f : -spread + 2.0f * spread * (float)i / (float)(count - 1);
        lp_tube(m, V(x, y, z), V(x, y, z + 0.22f), 0.10f, 0.12f, 6, P_METAL, -1, false);
        lp_ring(m, V(x, y, z + 0.23f), AZ, 0.0f, 0.10f, 0.02f, 6, P_GLOW, P_GLOW, false);
        light(em, V(x, y, z + 0.26f), 0.6f);
    }
}

// Nave-mãe: casco longo de proa em cunha, ponte de comando, asas curtas.
static void carrier_mothership(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing hull[] = { { -1.25f, 0.04f, 0.02f, 0.0f }, { -0.70f, 0.42f, 0.22f, 0.0f }, { 0.60f, 0.46f, 0.24f, 0.0f },
                            { 1.10f, 0.44f, 0.22f, 0.0f } };
    lp_loft(m, hull, 4, 6, 0.0f, P_HULL, P_DARK, P_DARK, 0.0f, false);
    lp_box(m, V(0.0f, -0.24f, 0.15f), V(0.20f, 0.02f, 0.42f), P_GLOW, false);
    lp_box(m, V(0.0f, 0.36f, 0.55f), V(0.12f, 0.14f, 0.20f), P_HULL2, false);
    lp_box(m, V(0.0f, 0.44f, 0.36f), V(0.10f, 0.04f, 0.02f), P_GLASS, false);
    lp_plate(m, V(0.42f, 0.0f, 0.20f), V(0.95f, -0.05f, 0.75f), V(0.92f, -0.05f, 0.95f), V(0.44f, 0.0f, 0.95f), AY, 0.08f,
             0.03f, P_HULL2, P_DARK, P_ACCENT, true);
    carrier_engines(em, 1.10f, 0.26f, 3, 0.0f);
    lowpoly_detail(m, true);
    lp_box(m, V(0.25f, 0.2f, -0.2f), V(0.04f, 0.03f, 0.55f), P_ACCENT, true);
    lp_box(m, V(0.30f, -0.24f, 0.15f), V(0.06f, 0.02f, 0.40f), P_METAL, true);
    lp_spike(m, V(0.0f, 0.50f, 0.6f), V(0.0f, 0.95f, 0.75f), 0.02f, 3, P_METAL, false);
    lp_spike(m, V(0.08f, 0.50f, 0.65f), V(0.25f, 0.8f, 0.85f), 0.015f, 3, P_METAL, false);
    lowpoly_detail(m, false);
}

// Catamarã: dois cascos ligados por um convés, hangar entre eles.
static void carrier_catamaran(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing hull[] = { { -1.20f, 0.0f, 0.0f, 0.0f }, { -0.80f, 0.18f, 0.18f, 0.0f }, { 0.60f, 0.22f, 0.22f, 0.0f },
                            { 1.10f, 0.20f, 0.20f, 0.0f } };
    lp_loft(m, hull, 4, 6, 0.50f, P_HULL, P_DARK, P_DARK, 0.0f, true);
    lp_plate(m, V(0.0f, 0.08f, -0.60f), V(0.48f, 0.08f, -0.50f), V(0.48f, 0.08f, 0.90f), V(0.0f, 0.08f, 0.95f), AY, 0.10f,
             0.10f, P_HULL2, P_DARK, P_ACCENT, true);
    lp_box(m, V(0.0f, 0.0f, 0.20f), V(0.30f, 0.02f, 0.45f), P_GLOW, false);
    lp_box(m, V(0.0f, 0.28f, 0.45f), V(0.16f, 0.18f, 0.18f), P_HULL, false);
    lp_box(m, V(0.0f, 0.38f, 0.26f), V(0.13f, 0.05f, 0.02f), P_GLASS, false);
    carrier_engines(em, 1.10f, 0.50f, 2, 0.0f);
    lowpoly_detail(m, true);
    lp_spike(m, V(0.5f, 0.0f, -1.15f), V(0.5f, 0.0f, -1.45f), 0.04f, 4, P_ACCENT, true);
    lp_box(m, V(0.5f, 0.2f, 0.1f), V(0.05f, 0.03f, 0.6f), P_ACCENT, true);
    lp_spike(m, V(0.0f, 0.46f, 0.5f), V(0.0f, 0.85f, 0.6f), 0.02f, 3, P_METAL, false);
    lowpoly_detail(m, false);
}

// Colmeia: casco hexagonal com favos acesos e braços de atracação.
static void carrier_hive(EnemyModel *em)
{
    LowPoly *m = &em->mesh;
    const LpRing hull[] = { { -1.10f, 0.20f, 0.20f, 0.0f }, { -0.80f, 0.50f, 0.50f, 0.0f }, { 0.80f, 0.50f, 0.50f, 0.0f },
                            { 1.10f, 0.32f, 0.32f, 0.0f } };
    lp_loft(m, hull, 4, 6, 0.0f, P_HULL, P_HULL2, P_DARK, 0.0f, false);
    lp_box(m, V(0.0f, -0.42f, 0.10f), V(0.12f, 0.04f, 0.35f), P_GLOW, false);
    lp_plate(m, V(0.25f, 0.0f, -1.0f), V(0.45f, 0.05f, -1.45f), V(0.35f, 0.05f, -1.50f), V(0.15f, 0.0f, -1.05f), AY, 0.06f,
             0.03f, P_ACCENT, P_DARK, P_METAL, true);
    lp_ring(m, V(0.0f, 0.0f, -0.82f), AZ, 0.38f, 0.52f, 0.06f, 6, P_ACCENT, P_DARK, false);
    carrier_engines(em, 1.10f, 0.22f, 3, 0.0f);
    lowpoly_detail(m, true);
    for (int iy = 0; iy < 2; iy++) {
        for (int iz = 0; iz < 4; iz++) {
            lp_ring(m, V(0.44f, -0.12f + 0.24f * (float)iy, -0.5f + 0.35f * (float)iz), AX, 0.0f, 0.10f, 0.02f, 6, P_GLOW,
                    P_GLOW, true);
        }
    }
    lowpoly_detail(m, false);
}

// ---------------------------------------------------------------------------

typedef void (*BuildFn)(EnemyModel *em);

static const BuildFn BUILDERS[ENEMY_TYPE_COUNT][ENEMY_VARIANTS] = {
    [ENEMY_SWARM]    = { swarm_wasp, swarm_blade, swarm_trident },
    [ENEMY_BOMBER]   = { bomber_manta, bomber_hammer, bomber_beetle },
    [ENEMY_KAMIKAZE] = { kamikaze_urchin, kamikaze_missile, kamikaze_star },
    [ENEMY_DIGGER]   = { digger_drill, digger_mole, digger_screw },
    [ENEMY_SIPHON]   = { siphon_jelly, siphon_funnel, siphon_flower },
    [ENEMY_SNIPER]   = { sniper_needle, sniper_harpoon, sniper_trilance },
    [ENEMY_SHIELDER] = { shielder_ring, shielder_triad, shielder_sphere },
    [ENEMY_CARRIER]  = { carrier_mothership, carrier_catamaran, carrier_hive },
};

static void build_all(void)
{
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
        for (int v = 0; v < ENEMY_VARIANTS; v++) {
            EnemyModel *em = &models[type][v];
            lowpoly_clear(&em->mesh);
            em->light_count = 0;
            em->beam = V(0.0f, -0.4f, 0.0f);
            em->spins = false;
            apply_palette(&em->mesh, &PALETTES[type]);
            BUILDERS[type][v](em);
        }
    }
    built = true;
}

const EnemyModel *enemy_model(EnemyType type, int variant)
{
    if (!built) build_all();
    if ((int)type < 0 || type >= ENEMY_TYPE_COUNT) type = ENEMY_SWARM;
    return &models[type][((variant % ENEMY_VARIANTS) + ENEMY_VARIANTS) % ENEMY_VARIANTS];
}

int enemy_models_triangles(int *largest, bool *overflow)
{
    if (!built) build_all();
    int total = 0, most = 0;
    bool over = false;
    for (int type = 0; type < ENEMY_TYPE_COUNT; type++) {
        for (int v = 0; v < ENEMY_VARIANTS; v++) {
            const LowPoly *m = &models[type][v].mesh;
            total += m->count;
            if (m->count > most) most = m->count;
            over = over || m->overflow;
        }
    }
    if (largest != NULL) *largest = most;
    if (overflow != NULL) *overflow = over;
    return total;
}
