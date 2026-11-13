#include "raylib.h"
#include "rlgl.h"

#include "render/enemies_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"
#include "render/particles_render.h"

// ---------------------------------------------------------------------------
//  Modelos (eixos do modelo: frente -Z, direita +X, topo +Y; escala 1)
// ---------------------------------------------------------------------------

static LowPoly models[ENEMY_TYPE_COUNT];
static bool    models_built;

static Vector3 v3(float x, float y, float z) { return (Vector3){ x, y, z }; }

// Enxame: um dardo com duas pinças à frente, asas em lâmina e um espinho nas
// costas. Cerca de 2,6 de envergadura.
enum { SW_SHELL, SW_PLATE, SW_UNDER, SW_BLADE, SW_EYE, SW_VEIN };
static void build_swarm(LowPoly *m)
{
    lowpoly_part(m, SW_SHELL, (Color){ 104, 64, 150, 255 }, false);
    lowpoly_part(m, SW_PLATE, (Color){ 156, 106, 210, 255 }, false);
    lowpoly_part(m, SW_UNDER, (Color){ 56, 36, 80, 255 }, false);
    lowpoly_part(m, SW_BLADE, (Color){ 186, 128, 236, 255 }, false);
    lowpoly_part_glow(m, SW_EYE, (Color){ 150, 255, 150, 255 });
    lowpoly_part_glow(m, SW_VEIN, (Color){ 90, 220, 130, 255 });

    Vector3 nose = v3(0.0f, 0.0f, -1.1f), top = v3(0.0f, 0.28f, -0.1f), side = v3(0.32f, 0.0f, -0.05f);
    Vector3 bottom = v3(0.0f, -0.22f, 0.0f), tail = v3(0.0f, 0.05f, 0.95f);
    lowpoly_tri(m, nose, top, side, SW_PLATE, true);
    lowpoly_tri(m, nose, side, bottom, SW_UNDER, true);
    lowpoly_tri(m, top, side, tail, SW_SHELL, true);
    lowpoly_tri(m, side, bottom, tail, SW_UNDER, true);

    Vector3 pincer_root = v3(0.24f, 0.0f, -0.15f), pincer_back = v3(0.30f, 0.0f, 0.30f);
    Vector3 pincer_tip = v3(0.62f, -0.06f, -1.25f), pincer_elbow = v3(0.78f, -0.02f, -0.35f);
    lowpoly_tri(m, pincer_root, pincer_elbow, pincer_back, SW_BLADE, true);
    lowpoly_tri(m, pincer_root, pincer_tip, pincer_elbow, SW_SHELL, true);
    lowpoly_tri(m, v3(0.60f, -0.05f, -1.05f), pincer_tip, v3(0.70f, -0.04f, -0.75f), SW_VEIN, true);

    Vector3 wing_root = v3(0.28f, 0.04f, 0.10f), wing_tip = v3(1.30f, 0.16f, 0.62f), wing_trail = v3(0.22f, 0.02f, 0.72f);
    lowpoly_tri(m, wing_root, wing_tip, wing_trail, SW_BLADE, true);
    lowpoly_tri(m, wing_root, v3(1.10f, 0.15f, 0.48f), wing_tip, SW_VEIN, true);

    lowpoly_tri(m, top, v3(0.0f, 0.62f, 0.50f), v3(0.0f, 0.15f, 0.60f), SW_PLATE, false);
    lowpoly_tri(m, v3(0.0f, 0.10f, -0.80f), v3(0.13f, 0.06f, -0.55f), v3(0.03f, 0.19f, -0.45f), SW_EYE, true);
}

// Bombardeiro: asa-delta larga e grossa, com lombada, casulo de bombas
// embaixo, torreta em cima e o bloco do motor atrás. Cerca de 3,2 de envergadura.
enum { BM_HULL, BM_PLATE, BM_DARK, BM_VENT, BM_CORE, BM_TURRET };
static void build_bomber(LowPoly *m)
{
    lowpoly_part(m, BM_HULL, (Color){ 104, 58, 62, 255 }, false);
    lowpoly_part(m, BM_PLATE, (Color){ 156, 112, 82, 255 }, false);
    lowpoly_part(m, BM_DARK, (Color){ 42, 30, 34, 255 }, false);
    lowpoly_part_glow(m, BM_VENT, (Color){ 255, 140, 60, 255 });
    lowpoly_part(m, BM_CORE, (Color){ 255, 96, 180, 255 }, true);   // o ponto fraco: sempre magenta
    lowpoly_part(m, BM_TURRET, (Color){ 118, 112, 118, 255 }, false);

    Vector3 nose = v3(0.0f, 0.05f, -1.35f), spine_front = v3(0.0f, 0.42f, -0.20f), spine_rear = v3(0.0f, 0.38f, 0.70f);
    Vector3 mid_edge = v3(0.75f, 0.14f, 0.10f), wing_tip = v3(1.60f, -0.05f, 0.75f), root_rear = v3(0.45f, 0.15f, 1.00f);
    Vector3 center_rear = v3(0.0f, 0.20f, 1.00f), nose_low = v3(0.0f, -0.15f, -1.20f);
    Vector3 root_low = v3(0.45f, -0.20f, 1.00f), center_low = v3(0.0f, -0.25f, 1.00f);

    lowpoly_tri(m, nose, spine_front, mid_edge, BM_PLATE, true);
    lowpoly_tri(m, spine_front, spine_rear, mid_edge, BM_HULL, true);
    lowpoly_tri(m, mid_edge, spine_rear, root_rear, BM_HULL, true);
    lowpoly_tri(m, nose, mid_edge, wing_tip, BM_HULL, true);
    lowpoly_tri(m, mid_edge, root_rear, wing_tip, BM_PLATE, true);
    lowpoly_tri(m, spine_rear, center_rear, root_rear, BM_HULL, true);
    lowpoly_tri(m, nose_low, wing_tip, root_low, BM_DARK, true);
    lowpoly_tri(m, nose_low, root_low, center_low, BM_DARK, true);
    lowpoly_tri(m, nose, nose_low, wing_tip, BM_DARK, true);
    lowpoly_tri(m, root_rear, wing_tip, root_low, BM_DARK, true);
    lowpoly_quad(m, center_rear, root_rear, root_low, center_low, BM_DARK, true);

    lowpoly_tri(m, v3(0.55f, 0.20f, 0.30f), v3(1.05f, 0.08f, 0.52f), v3(0.62f, 0.19f, 0.52f), BM_VENT, true);
    lowpoly_tri(m, wing_tip, v3(1.55f, -0.38f, 0.98f), v3(1.40f, -0.06f, 0.98f), BM_PLATE, true);

    Vector3 pod_front = v3(0.0f, -0.22f, -0.50f), pod_side = v3(0.22f, -0.36f, 0.10f);
    Vector3 pod_low = v3(0.0f, -0.52f, 0.15f), pod_back = v3(0.0f, -0.28f, 0.62f);
    lowpoly_tri(m, pod_front, pod_side, pod_low, BM_PLATE, true);
    lowpoly_tri(m, pod_side, pod_back, pod_low, BM_HULL, true);

    lowpoly_tri(m, v3(0.13f, 0.40f, -0.24f), v3(0.0f, 0.60f, -0.10f), v3(0.13f, 0.40f, 0.04f), BM_TURRET, true);
    lowpoly_tri(m, v3(0.0f, 0.55f, -0.12f), v3(0.03f, 0.53f, -0.55f), v3(0.0f, 0.50f, -0.12f), BM_DARK, true);

    Vector3 eng_a = v3(0.34f, 0.28f, 0.88f), eng_b = v3(0.34f, -0.10f, 0.88f);
    Vector3 eng_c = v3(0.28f, -0.06f, 1.16f), eng_d = v3(0.28f, 0.24f, 1.16f);
    lowpoly_quad(m, eng_a, eng_b, eng_c, eng_d, BM_DARK, true);
    lowpoly_quad(m, v3(0.0f, 0.30f, 0.88f), eng_a, eng_d, v3(0.0f, 0.26f, 1.16f), BM_HULL, true);
    lowpoly_quad(m, v3(0.0f, 0.24f, 1.17f), eng_d, eng_c, v3(0.0f, -0.06f, 1.17f), BM_CORE, true);
}

// Kamikaze: um núcleo de oito faces cravado de espinhos, com um espinho
// maior à frente. Cerca de 2 de diâmetro.
enum { KZ_SHELL, KZ_SPIKE, KZ_CORE };
static void build_kamikaze(LowPoly *m)
{
    lowpoly_part(m, KZ_SHELL, (Color){ 120, 44, 40, 255 }, false);
    lowpoly_part(m, KZ_SPIKE, (Color){ 186, 176, 172, 255 }, false);
    lowpoly_part_glow(m, KZ_CORE, (Color){ 255, 90, 40, 255 });

    const float r = 0.5f;
    Vector3 px = v3(r, 0, 0), nx = v3(-r, 0, 0), py = v3(0, r, 0), ny = v3(0, -r, 0), pz = v3(0, 0, r), nz = v3(0, 0, -r);
    Vector3 faces[8][3] = {
        { px, py, pz }, { py, nx, pz }, { nx, ny, pz }, { ny, px, pz },
        { py, px, nz }, { nx, py, nz }, { ny, nx, nz }, { px, ny, nz },
    };
    for (int i = 0; i < 8; i++) {
        Vector3 a = faces[i][0], b = faces[i][1], c = faces[i][2];
        Vector3 center = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
        Vector3 out = Vector3Normalize(center);
        Vector3 tip = Vector3Scale(out, 1.05f);
        // A face vira três triângulos que sobem até a ponta do espinho; o
        // meio de cada aresta fica no núcleo, em vermelho aceso.
        Vector3 ab = Vector3Lerp(a, b, 0.5f), bc = Vector3Lerp(b, c, 0.5f), ca = Vector3Lerp(c, a, 0.5f);
        lowpoly_tri(m, ab, bc, tip, KZ_SPIKE, false);
        lowpoly_tri(m, bc, ca, tip, KZ_SPIKE, false);
        lowpoly_tri(m, ca, ab, tip, KZ_SPIKE, false);
        lowpoly_tri(m, a, ab, ca, KZ_SHELL, false);
        lowpoly_tri(m, b, bc, ab, KZ_SHELL, false);
        lowpoly_tri(m, c, ca, bc, KZ_CORE, false);
    }
    // O espinho da frente, maior.
    Vector3 front_tip = v3(0.0f, 0.0f, -1.45f);
    lowpoly_tri(m, v3(0.18f, 0.0f, -0.40f), v3(0.0f, 0.18f, -0.40f), front_tip, KZ_SPIKE, true);
    lowpoly_tri(m, v3(0.18f, 0.0f, -0.40f), v3(0.0f, -0.18f, -0.40f), front_tip, KZ_SHELL, true);
}

// Escavador: uma broca cônica à frente e um corpo hexagonal blindado, com
// três pernas atrás. Cerca de 3 de comprimento.
enum { DG_DRILL, DG_DRILL2, DG_BODY, DG_PLATE, DG_DARK, DG_GLOW };
static void build_digger(LowPoly *m)
{
    lowpoly_part(m, DG_DRILL, (Color){ 196, 180, 150, 255 }, false);
    lowpoly_part(m, DG_DRILL2, (Color){ 120, 108, 92, 255 }, false);
    lowpoly_part(m, DG_BODY, (Color){ 150, 100, 60, 255 }, false);
    lowpoly_part(m, DG_PLATE, (Color){ 186, 138, 84, 255 }, false);
    lowpoly_part(m, DG_DARK, (Color){ 48, 40, 34, 255 }, false);
    lowpoly_part_glow(m, DG_GLOW, (Color){ 255, 150, 50, 255 });

    const int sides = 6;
    Vector3 drill_tip = v3(0.0f, 0.0f, -1.55f);
    Vector3 ring[6], ring_glow[6], body_back[6];
    for (int i = 0; i < sides; i++) {
        float a = (float)i * 2.0f * PI / (float)sides;
        ring[i] = v3(cosf(a) * 0.55f, sinf(a) * 0.55f, -0.25f);
        ring_glow[i] = v3(cosf(a) * 0.62f, sinf(a) * 0.62f, -0.15f);
        body_back[i] = v3(cosf(a) * 0.55f, sinf(a) * 0.55f, 0.95f);
    }
    for (int i = 0; i < sides; i++) {
        int n = (i + 1) % sides;
        lowpoly_tri(m, drill_tip, ring[i], ring[n], (i % 2) ? DG_DRILL : DG_DRILL2, false);   // espiral
        lowpoly_quad(m, ring[i], ring_glow[i], ring_glow[n], ring[n], DG_GLOW, false);
        lowpoly_quad(m, ring_glow[i], body_back[i], body_back[n], ring_glow[n], (i % 2) ? DG_BODY : DG_PLATE, false);
        lowpoly_tri(m, v3(0.0f, 0.0f, 1.0f), body_back[n], body_back[i], DG_DARK, false);
    }
    // Pernas atrás, para se firmar ao pousar.
    for (int i = 0; i < 3; i++) {
        float a = (float)i * 2.0f * PI / 3.0f + PI / 2.0f;
        Vector3 root = v3(cosf(a) * 0.5f, sinf(a) * 0.5f, 0.55f);
        Vector3 foot = v3(cosf(a) * 1.15f, sinf(a) * 1.15f, 1.35f);
        lowpoly_tri(m, root, foot, v3(cosf(a) * 0.5f, sinf(a) * 0.5f, 0.95f), DG_PLATE, false);
    }
}

// Sifão: uma "água-viva" de cúpula larga, com o emissor do feixe embaixo e
// tentáculos. Simétrica em volta do eixo vertical. Cerca de 2 de diâmetro.
enum { SP_DOME, SP_DOME2, SP_UNDER, SP_TENTACLE, SP_CORE };
static void build_siphon(LowPoly *m)
{
    lowpoly_part(m, SP_DOME, (Color){ 40, 140, 104, 255 }, false);
    lowpoly_part(m, SP_DOME2, (Color){ 70, 196, 150, 255 }, false);
    lowpoly_part(m, SP_UNDER, (Color){ 30, 52, 46, 255 }, false);
    lowpoly_part(m, SP_TENTACLE, (Color){ 130, 210, 176, 255 }, false);
    lowpoly_part_glow(m, SP_CORE, (Color){ 120, 255, 190, 255 });

    const int sides = 8;
    Vector3 top = v3(0.0f, 0.65f, 0.0f), core = v3(0.0f, -0.35f, 0.0f);
    Vector3 rim[8], mid[8], inner[8];
    for (int i = 0; i < sides; i++) {
        float a = (float)i * 2.0f * PI / (float)sides;
        rim[i] = v3(cosf(a) * 0.95f, 0.0f, sinf(a) * 0.95f);
        mid[i] = v3(cosf(a + 0.39f) * 0.62f, 0.45f, sinf(a + 0.39f) * 0.62f);
        inner[i] = v3(cosf(a) * 0.45f, -0.12f, sinf(a) * 0.45f);
    }
    for (int i = 0; i < sides; i++) {
        int n = (i + 1) % sides;
        // No alto da cúpula, uma estrela de veias acesas: é o que se vê de cima.
        lowpoly_tri(m, top, mid[i], mid[n], (i % 2) ? SP_DOME2 : SP_CORE, false);
        lowpoly_tri(m, mid[i], rim[n], mid[n], SP_DOME, false);
        lowpoly_tri(m, mid[i], rim[i], rim[n], SP_DOME2, false);
        lowpoly_quad(m, rim[i], inner[i], inner[n], rim[n], SP_UNDER, false);
        lowpoly_tri(m, inner[i], core, inner[n], SP_CORE, false);
        // Tentáculos.
        float a = (float)i * 2.0f * PI / (float)sides;
        Vector3 root = v3(cosf(a) * 0.7f, -0.05f, sinf(a) * 0.7f);
        Vector3 tip = v3(cosf(a) * 0.95f, -1.35f, sinf(a) * 0.95f);
        Vector3 side = v3(cosf(a + 0.3f) * 0.62f, -0.08f, sinf(a + 0.3f) * 0.62f);
        lowpoly_tri(m, root, tip, side, SP_TENTACLE, false);
    }
}

// Atirador: uma agulha longa com um cano comprido, asinhas e um olho
// vermelho. Cerca de 3,4 de comprimento.
enum { SN_BODY, SN_PLATE, SN_BARREL, SN_EYE };
static void build_sniper(LowPoly *m)
{
    lowpoly_part(m, SN_BODY, (Color){ 92, 92, 112, 255 }, false);
    lowpoly_part(m, SN_PLATE, (Color){ 148, 148, 170, 255 }, false);
    lowpoly_part(m, SN_BARREL, (Color){ 46, 46, 56, 255 }, false);
    lowpoly_part_glow(m, SN_EYE, (Color){ 255, 60, 90, 255 });

    Vector3 front = v3(0.0f, 0.0f, -0.65f), back = v3(0.0f, 0.04f, 0.95f);
    Vector3 top = v3(0.0f, 0.24f, 0.1f), bottom = v3(0.0f, -0.18f, 0.1f), side = v3(0.22f, 0.02f, 0.1f);
    lowpoly_tri(m, front, top, side, SN_PLATE, true);
    lowpoly_tri(m, front, side, bottom, SN_BODY, true);
    lowpoly_tri(m, top, back, side, SN_BODY, true);
    lowpoly_tri(m, side, back, bottom, SN_BARREL, true);
    // Cano comprido.
    lowpoly_tri(m, v3(0.06f, 0.0f, -0.55f), v3(0.0f, 0.06f, -0.55f), v3(0.0f, 0.0f, -1.70f), SN_BARREL, true);
    lowpoly_tri(m, v3(0.06f, 0.0f, -0.55f), v3(0.0f, -0.06f, -0.55f), v3(0.0f, 0.0f, -1.70f), SN_PLATE, true);
    // Asinhas em flecha e o olho.
    lowpoly_tri(m, v3(0.18f, 0.0f, 0.15f), v3(0.95f, -0.05f, 0.75f), v3(0.15f, 0.02f, 0.65f), SN_PLATE, true);
    lowpoly_tri(m, v3(0.0f, 0.14f, -0.42f), v3(0.08f, 0.10f, -0.25f), v3(0.0f, 0.22f, -0.15f), SN_EYE, true);
}

// Escudeiro: um anel octogonal com espessura e um emissor no centro. O anel
// fica de frente para onde ele voa. Cerca de 2 de diâmetro.
enum { SH_RING, SH_RING2, SH_EDGE, SH_CORE };
static void build_shielder(LowPoly *m)
{
    lowpoly_part(m, SH_RING, (Color){ 176, 196, 210, 255 }, false);
    lowpoly_part(m, SH_RING2, (Color){ 120, 140, 160, 255 }, false);
    lowpoly_part(m, SH_EDGE, (Color){ 60, 72, 86, 255 }, false);
    lowpoly_part_glow(m, SH_CORE, (Color){ 90, 230, 255, 255 });

    const int sides = 8;
    const float outer = 1.0f, inner = 0.7f, thick = 0.13f;
    for (int i = 0; i < sides; i++) {
        float a0 = (float)i * 2.0f * PI / (float)sides, a1 = (float)(i + 1) * 2.0f * PI / (float)sides;
        Vector3 o0 = v3(cosf(a0) * outer, sinf(a0) * outer, 0), o1 = v3(cosf(a1) * outer, sinf(a1) * outer, 0);
        Vector3 i0 = v3(cosf(a0) * inner, sinf(a0) * inner, 0), i1 = v3(cosf(a1) * inner, sinf(a1) * inner, 0);
        Vector3 f = v3(0, 0, -thick), b = v3(0, 0, thick);
        int face = (i % 2) ? SH_RING : SH_RING2;
        lowpoly_quad(m, Vector3Add(o0, f), Vector3Add(i0, f), Vector3Add(i1, f), Vector3Add(o1, f), face, false);
        lowpoly_quad(m, Vector3Add(o0, b), Vector3Add(o1, b), Vector3Add(i1, b), Vector3Add(i0, b), face, false);
        lowpoly_quad(m, Vector3Add(o0, f), Vector3Add(o1, f), Vector3Add(o1, b), Vector3Add(o0, b), SH_EDGE, false);
        // Raios do anel até o emissor.
        if (i % 2 == 0) {
            lowpoly_tri(m, Vector3Scale(i0, 0.98f), v3(0.0f, 0.0f, -0.05f), Vector3Scale(i1, 0.98f), SH_EDGE, false);
        }
    }
    // Emissor: um octaedro pequeno, aceso.
    const float r = 0.28f;
    Vector3 px = v3(r, 0, 0), nx = v3(-r, 0, 0), py = v3(0, r, 0), ny = v3(0, -r, 0), pz = v3(0, 0, r), nz = v3(0, 0, -r);
    Vector3 tris[8][3] = {
        { px, py, pz }, { py, nx, pz }, { nx, ny, pz }, { ny, px, pz },
        { py, px, nz }, { nx, py, nz }, { ny, nx, nz }, { px, ny, nz },
    };
    for (int i = 0; i < 8; i++) lowpoly_tri(m, tris[i][0], tris[i][1], tris[i][2], SH_CORE, false);
}

// Porta-drones: casco longo e chato com proa em cunha, ponte em cima,
// hangar embaixo e três motores atrás. Cerca de 2,4 de comprimento.
enum { CR_HULL, CR_PLATE, CR_DARK, CR_BAY, CR_ENGINE, CR_BRIDGE };
static void build_carrier(LowPoly *m)
{
    lowpoly_part(m, CR_HULL, (Color){ 112, 98, 140, 255 }, false);
    lowpoly_part(m, CR_PLATE, (Color){ 168, 156, 196, 255 }, false);
    lowpoly_part(m, CR_DARK, (Color){ 36, 32, 46, 255 }, false);
    lowpoly_part_glow(m, CR_BAY, (Color){ 200, 120, 255, 255 });
    lowpoly_part_glow(m, CR_ENGINE, (Color){ 200, 120, 255, 255 });
    lowpoly_part(m, CR_BRIDGE, (Color){ 170, 162, 186, 255 }, false);

    Vector3 prow = v3(0.0f, 0.0f, -1.25f);
    Vector3 deck_f = v3(0.42f, 0.16f, -0.65f), deck_b = v3(0.46f, 0.16f, 1.10f);
    Vector3 keel_f = v3(0.36f, -0.20f, -0.60f), keel_b = v3(0.40f, -0.20f, 1.10f);
    Vector3 top_f = v3(0.0f, 0.22f, -0.70f), top_b = v3(0.0f, 0.22f, 1.10f);
    Vector3 bot_f = v3(0.0f, -0.26f, -0.60f), bot_b = v3(0.0f, -0.26f, 1.10f);

    lowpoly_tri(m, prow, deck_f, top_f, CR_PLATE, true);
    lowpoly_tri(m, prow, keel_f, deck_f, CR_HULL, true);
    lowpoly_tri(m, prow, bot_f, keel_f, CR_DARK, true);
    lowpoly_quad(m, top_f, deck_f, deck_b, top_b, CR_HULL, true);
    lowpoly_quad(m, deck_f, keel_f, keel_b, deck_b, CR_PLATE, true);
    lowpoly_quad(m, keel_f, bot_f, bot_b, keel_b, CR_DARK, true);
    lowpoly_quad(m, top_b, deck_b, keel_b, bot_b, CR_DARK, true);

    // Hangar: uma faixa acesa embaixo, que fica mais forte antes de lançar.
    lowpoly_quad(m, v3(0.20f, -0.27f, -0.30f), v3(0.20f, -0.27f, 0.60f), v3(0.0f, -0.27f, 0.60f), v3(0.0f, -0.27f, -0.30f),
                 CR_BAY, true);
    // Ponte de comando.
    lowpoly_quad(m, v3(0.12f, 0.22f, 0.30f), v3(0.10f, 0.48f, 0.45f), v3(0.10f, 0.48f, 0.75f), v3(0.12f, 0.22f, 0.85f),
                 CR_BRIDGE, true);
    lowpoly_tri(m, v3(0.10f, 0.48f, 0.45f), v3(0.0f, 0.52f, 0.45f), v3(0.10f, 0.48f, 0.75f), CR_PLATE, true);
    lowpoly_tri(m, v3(0.0f, 0.52f, 0.45f), v3(0.0f, 0.52f, 0.75f), v3(0.10f, 0.48f, 0.75f), CR_PLATE, true);
    // Motores.
    for (int i = -1; i <= 1; i++) {
        float x = (float)i * 0.26f;
        lowpoly_quad(m, v3(x - 0.09f, 0.06f, 1.11f), v3(x + 0.09f, 0.06f, 1.11f), v3(x + 0.09f, -0.12f, 1.11f),
                     v3(x - 0.09f, -0.12f, 1.11f), CR_ENGINE, false);
    }
    // Asas curtas com antenas.
    lowpoly_tri(m, v3(0.44f, 0.0f, 0.2f), v3(0.95f, -0.05f, 0.75f), v3(0.45f, 0.0f, 0.95f), CR_PLATE, true);
}

static void build_models(void)
{
    for (int i = 0; i < ENEMY_TYPE_COUNT; i++) lowpoly_clear(&models[i]);
    build_swarm(&models[ENEMY_SWARM]);
    build_bomber(&models[ENEMY_BOMBER]);
    build_kamikaze(&models[ENEMY_KAMIKAZE]);
    build_digger(&models[ENEMY_DIGGER]);
    build_siphon(&models[ENEMY_SIPHON]);
    build_sniper(&models[ENEMY_SNIPER]);
    build_shielder(&models[ENEMY_SHIELDER]);
    build_carrier(&models[ENEMY_CARRIER]);
    models_built = true;
}

// ---------------------------------------------------------------------------
//  Variações de cor: cada grupo sai em uma das três
// ---------------------------------------------------------------------------

typedef struct { Color tint, glow; } Variant;
static const Variant VARIANTS[ENEMY_TYPE_COUNT][ENEMY_VARIANTS] = {
    [ENEMY_SWARM] = { { { 255, 255, 255, 255 }, { 150, 255, 150, 255 } },
                      { { 255, 175, 195, 255 }, { 255, 210, 90, 255 } },
                      { { 170, 225, 255, 255 }, { 120, 240, 255, 255 } } },
    [ENEMY_BOMBER] = { { { 255, 255, 255, 255 }, { 255, 140, 60, 255 } },
                       { { 170, 190, 225, 255 }, { 255, 90, 90, 255 } },
                       { { 205, 225, 160, 255 }, { 255, 200, 80, 255 } } },
    [ENEMY_KAMIKAZE] = { { { 255, 255, 255, 255 }, { 255, 90, 40, 255 } },
                         { { 255, 210, 170, 255 }, { 255, 170, 40, 255 } },
                         { { 200, 170, 255, 255 }, { 255, 60, 160, 255 } } },
    [ENEMY_DIGGER] = { { { 255, 255, 255, 255 }, { 255, 150, 50, 255 } },
                       { { 190, 200, 220, 255 }, { 255, 90, 40, 255 } },
                       { { 230, 200, 160, 255 }, { 255, 220, 90, 255 } } },
    [ENEMY_SIPHON] = { { { 255, 255, 255, 255 }, { 120, 255, 190, 255 } },
                       { { 200, 170, 255, 255 }, { 190, 140, 255, 255 } },
                       { { 255, 220, 170, 255 }, { 255, 230, 120, 255 } } },
    [ENEMY_SNIPER] = { { { 255, 255, 255, 255 }, { 255, 60, 90, 255 } },
                       { { 220, 200, 180, 255 }, { 255, 120, 60, 255 } },
                       { { 170, 200, 170, 255 }, { 255, 60, 200, 255 } } },
    [ENEMY_SHIELDER] = { { { 255, 255, 255, 255 }, { 90, 230, 255, 255 } },
                         { { 200, 230, 200, 255 }, { 120, 255, 170, 255 } },
                         { { 230, 210, 255, 255 }, { 170, 150, 255, 255 } } },
    [ENEMY_CARRIER] = { { { 255, 255, 255, 255 }, { 200, 120, 255, 255 } },
                        { { 200, 180, 160, 255 }, { 255, 120, 150, 255 } },
                        { { 160, 190, 210, 255 }, { 120, 200, 255, 255 } } },
};

// ---------------------------------------------------------------------------
//  Desenho dos inimigos
// ---------------------------------------------------------------------------

#define ENEMY_AMBIENT  0.38f
#define ENEMY_SHADOW   0.22f
#define ENEMY_WRAP     0.35f
#define ENEMY_SPECULAR 0.35f
#define ENEMY_SHINE    14.0f
#define ENEMY_RIM      0.35f
#define ENEMY_BOUNCE   0.2f
#define FLASH_TIME     0.12f   // (mesma duração de game/enemies.c)
#define DRILL_SPIN     7.0f    // radianos por segundo da broca girando
#define SHIELD_BUBBLE  2.6f    // bolha em volta de um aliado protegido, em raios dele
#define SHIELD_TETHER  0.18f   // espessura do cabo de energia do Escudeiro

static const Color BOUNCE_COLOR = { 110, 160, 230, 255 };
static const Color CORE_COLOR   = { 255, 90, 190, 255 };
static const Color ARMING_COLOR = { 255, 60, 40, 255 };
static const Color BEAM_COLOR   = { 120, 255, 190, 255 };
static const Color LASER_COLOR  = { 255, 40, 60, 255 };

static Vector3 at(Vector3 pos, Quaternion rot, float scale, Vector3 local)
{
    return Vector3Add(pos, Vector3RotateByQuaternion(Vector3Scale(local, scale), rot));
}

static Color faded(Color c, float alpha)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * 255.0f) };
}

// Um risco de `a` a `b`, sempre de frente para a câmera. A textura de brilho é
// lida só na linha do meio: borda suave na largura, cheio no comprimento.
static void streak(Vector3 a, Vector3 b, float width, Color color, Vector3 eye)
{
    Vector3 along = Vector3Subtract(b, a);
    Vector3 side = Vector3CrossProduct(along, Vector3Subtract(eye, a));
    if (Vector3LengthSqr(side) < 1e-8f) return;
    side = Vector3Scale(Vector3Normalize(side), width * 0.5f);

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0.0f, 0.5f); rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlTexCoord2f(1.0f, 0.5f); rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlTexCoord2f(1.0f, 0.5f); rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlTexCoord2f(0.0f, 0.5f); rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
}

typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      scale;
    Color      glow;
} Placed;

static Placed place(const Enemy *en, const Tuning *t, float alpha)
{
    Placed p;
    p.pos = Vector3Lerp(en->pos_prev, en->pos, alpha);
    p.rot = QuaternionSlerp(en->rot_prev, en->rot, alpha);
    p.scale = t->enemy[en->type].scale * en->size;
    p.glow = VARIANTS[en->type][en->variant % ENEMY_VARIANTS].glow;
    return p;
}

void enemies_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    if (!models_built) build_models();
    const Enemies *e = &w->enemies;
    Vector3 sun = tuning_sun_direction(t);
    float time = (float)w->tick / (float)t->sim_hz;

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        Placed p = place(en, t, alpha);
        const Variant *v = &VARIANTS[en->type][en->variant % ENEMY_VARIANTS];

        // A broca do Escavador gira enquanto ele cava.
        Quaternion rot = p.rot;
        if (en->type == ENEMY_DIGGER && en->state == DIGGER_DRILL) {
            rot = QuaternionMultiply(rot, QuaternionFromAxisAngle((Vector3){ 0.0f, 0.0f, 1.0f }, time * DRILL_SPIN));
        }

        LowPolyPose pose = lowpoly_pose(p.pos, rot, p.scale, sun, camera.position);
        pose.sunlight = lowpoly_sunlight(p.pos, sun, w->planet.params.radius, ENEMY_SHADOW);
        pose.ambient = ENEMY_AMBIENT;
        pose.wrap = ENEMY_WRAP;
        pose.specular = ENEMY_SPECULAR;
        pose.shininess = ENEMY_SHINE;
        pose.rim = ENEMY_RIM;
        pose.rim_color = v->glow;
        pose.bounce = ENEMY_BOUNCE;
        pose.bounce_color = BOUNCE_COLOR;
        pose.tint = v->tint;
        pose.glow = v->glow;
        pose.whiten = Clamp(en->flash / FLASH_TIME, 0.0f, 1.0f) * 0.8f;
        lowpoly_draw(&models[en->type], &pose);
    }

    // Brilhos, por cima.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        Placed p = place(en, t, alpha);
        float pulse = 0.75f + 0.25f * sinf(time * 5.0f + (float)i);

        switch ((EnemyType)en->type) {
        case ENEMY_SWARM:
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, 0.10f, -0.75f)), 1.5f * p.scale,
                          faded(p.glow, 0.9f));
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, 0.05f, 0.95f)), 1.2f * p.scale,
                          faded((Color){ 200, 110, 255, 255 }, 0.7f));
            break;
        case ENEMY_BOMBER: {
            // Ponto fraco: pulsa, para chamar o olho.
            Vector3 core = at(p.pos, p.rot, p.scale, v3(0.0f, 0.09f, 1.20f));
            DrawBillboard(camera, glow, core, p.scale * pulse * 1.6f, faded(CORE_COLOR, 0.55f));
            DrawBillboard(camera, glow, core, p.scale * 0.5f, faded(WHITE, 0.8f));
            for (int side = -1; side <= 1; side += 2) {
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.80f * (float)side, 0.16f, 0.42f)),
                              0.5f * p.scale, faded(p.glow, 0.5f));
            }
            if (en->arming > 0.0f) {
                float progress = 1.0f - en->arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
                float blink = 0.6f + 0.4f * sinf(time * (10.0f + 20.0f * progress));
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, -0.40f, 0.15f)),
                              1.4f * p.scale * (0.5f + progress), faded(ARMING_COLOR, blink));
            }
            break;
        }
        case ENEMY_KAMIKAZE: {
            // Travado na nave, o núcleo pisca depressa.
            float rate = (en->state == KAMIKAZE_HUNT) ? 22.0f : 6.0f;
            float beat = 0.6f + 0.4f * sinf(time * rate + (float)i);
            DrawBillboard(camera, glow, p.pos, 2.4f * p.scale * beat, faded(p.glow, 0.7f));
            DrawBillboard(camera, glow, p.pos, 0.8f * p.scale, faded(WHITE, 0.6f * beat));
            break;
        }
        case ENEMY_DIGGER:
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, 0.0f, -0.2f)), 1.6f * p.scale * pulse,
                          faded(p.glow, (en->state == DIGGER_DRILL) ? 0.8f : 0.4f));
            break;
        case ENEMY_SIPHON:
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, -0.35f, 0.0f)), 1.4f * p.scale * pulse,
                          faded(p.glow, 0.8f));
            break;
        case ENEMY_SNIPER:
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, 0.14f, -0.3f)), 0.9f * p.scale,
                          faded(p.glow, 0.9f));
            if (en->arming > 0.0f) {
                // Carregando: o cano acende cada vez mais.
                float charge = 1.0f - en->arming / fmaxf(t->sniper_charge_time, 1e-3f);
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, 0.0f, -1.7f)),
                              (0.6f + 1.8f * charge) * p.scale, faded(LASER_COLOR, 0.4f + 0.6f * charge));
            }
            break;
        case ENEMY_SHIELDER:
            DrawBillboard(camera, glow, p.pos, 2.2f * p.scale * pulse, faded(p.glow, 0.45f));
            break;
        case ENEMY_CARRIER: {
            float bay = (en->arming > 0.0f) ? 0.6f + 0.4f * sinf(time * 18.0f) : 0.25f;
            DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3(0.0f, -0.32f, 0.15f)),
                          (en->arming > 0.0f ? 2.2f : 1.2f) * p.scale, faded(p.glow, bay));
            for (int k = -1; k <= 1; k++) {
                DrawBillboard(camera, glow, at(p.pos, p.rot, p.scale, v3((float)k * 0.26f, -0.03f, 1.15f)),
                              0.45f * p.scale, faded(p.glow, 0.7f));
            }
            break;
        }
        default:
            break;
        }
    }
    EndBlendMode();
    rlEnableDepthMask();
}

// Feixes e cabos: o feixe do Sifão, a mira do Atirador e os cabos de energia
// do Escudeiro até cada aliado protegido (com uma bolha em volta dele).
static void draw_links(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera, float time)
{
    const Enemies *e = &w->enemies;
    Vector3 eye = camera.position;

    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        const Enemy *en = &e->items[i];
        if (en->hp <= 0.0f) continue;
        Placed p = place(en, t, alpha);

        if (en->type == ENEMY_SIPHON && en->state == SIPHON_DRAIN) {
            Vector3 from = at(p.pos, p.rot, p.scale, v3(0.0f, -0.35f, 0.0f));
            float flicker = 0.75f + 0.25f * sinf(time * 30.0f + (float)i);
            streak(from, en->aux, 1.6f, faded(p.glow, 0.25f * flicker), eye);
            streak(from, en->aux, 0.45f, faded(BEAM_COLOR, 0.8f * flicker), eye);
            // Material subindo pelo feixe: pontos que andam do solo para cima.
            for (int k = 0; k < 4; k++) {
                float u = fmodf(time * 0.9f + (float)k * 0.25f, 1.0f);
                Vector3 dot = Vector3Lerp(en->aux, from, u);
                Vector3 up = Vector3Scale(Vector3Normalize(Vector3Subtract(from, en->aux)), 0.8f);
                streak(dot, Vector3Add(dot, up), 0.5f, faded(WHITE, 0.6f * (1.0f - u)), eye);
            }
        } else if (en->type == ENEMY_SNIPER && en->arming > 0.0f) {
            // A linha da mira: fina e trêmula enquanto segue a nave; firme e
            // forte quando trava.
            Vector3 muzzle = at(p.pos, p.rot, p.scale, v3(0.0f, 0.0f, -1.7f));
            bool locked = en->arming <= t->sniper_lock_time;
            float charge = 1.0f - en->arming / fmaxf(t->sniper_charge_time, 1e-3f);
            float width = locked ? 0.35f : 0.12f + 0.1f * charge;
            float shimmer = locked ? 1.0f : 0.5f + 0.3f * sinf(time * 40.0f);
            streak(muzzle, en->aux, width, faded(LASER_COLOR, shimmer * (0.35f + 0.65f * charge)), eye);
        } else if (en->type == ENEMY_SHIELDER) {
            for (int k = 0; k < e->pool.capacity; k++) {
                if (k == i || !pool_alive(&e->pool, k) || e->items[k].hp <= 0.0f) continue;
                if (enemies_shielded_by(e, t, k) != i) continue;
                Vector3 ally = Vector3Lerp(e->items[k].pos_prev, e->items[k].pos, alpha);
                streak(p.pos, ally, SHIELD_TETHER, faded(p.glow, 0.35f), eye);
            }
        }
    }
    rlEnd();
    rlSetTexture(0);

    // Bolhas de escudo em volta de quem está protegido.
    rlDrawRenderBatchActive();
    Texture2D ring = particles_ring_texture();
    for (int k = 0; k < e->pool.capacity; k++) {
        if (!pool_alive(&e->pool, k) || e->items[k].hp <= 0.0f) continue;
        int shielder = enemies_shielded_by(e, t, k);
        if (shielder < 0) continue;
        const Enemy *ally = &e->items[k];
        Color c = VARIANTS[ENEMY_SHIELDER][e->items[shielder].variant % ENEMY_VARIANTS].glow;
        Vector3 pos = Vector3Lerp(ally->pos_prev, ally->pos, alpha);
        float size = t->enemy[ally->type].radius * SHIELD_BUBBLE;
        DrawBillboard(camera, ring, pos, size, faded(c, 0.55f));
        DrawBillboard(camera, glow, pos, size * 0.9f, faded(c, 0.12f));
    }
}

// ---------------------------------------------------------------------------
//  Projéteis e marcas de alvo
// ---------------------------------------------------------------------------

#define BOLT_LENGTH     0.028f   // comprimento do risco, em segundos de voo
#define BOLT_LENGTH_MAX 7.0f
#define BOLT_WIDTH      0.32f
#define BOLT_HALO       2.2f     // largura do halo, em larguras do risco
#define BOLT_WIDTH_AT   25.0f    // distância em que o risco tem a largura normal
#define BOLT_NEAR       6.0f     // mais perto da câmera que isto, o tiro quase some...
#define BOLT_CLEAR      18.0f    // ... e a partir daqui aparece inteiro (senão, colado na
                                 // câmera, vira uma faixa clara enorme sobre a nave)
#define PLASMA_SIZE     1.3f
#define PLASMA_LENGTH   0.05f
#define LANCE_LENGTH    0.06f    // o tiro do Atirador é um risco vermelho comprido
#define BOMB_SIZE       2.6f
#define MARK_SEGMENTS   40
#define MARK_LIFT       0.6f     // a marca fica um pouco acima do solo
#define MARK_WIDTH      0.16f    // espessura do anel, em frações do raio
#define MARK_START      2.2f     // o anel de aviso começa este tanto maior e se fecha sobre o alvo

static const Color BOLT_CORE       = { 235, 250, 255, 255 };
static const Color BOLT_HALO_COLOR = {  80, 190, 255, 255 };
static const Color PLASMA_CORE     = { 255, 210, 255, 255 };
static const Color PLASMA_HALO     = { 190,  80, 255, 255 };
static const Color BOMB_COLOR      = { 255,  70,  50, 255 };
static const Color MARK_COLOR      = { 255,  44,  32, 255 };
static const Color DIG_MARK_COLOR  = { 255, 150,  40, 255 };

// Anel deitado no solo em volta de `center` (normal = para fora do planeta).
static void ground_ring(Vector3 center, float radius, float width, Color color)
{
    Vector3 n = Vector3Normalize(center);
    Vector3 hint = (fabsf(n.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(n, hint));
    Vector3 v = Vector3CrossProduct(n, u);
    Vector3 c = Vector3Add(center, Vector3Scale(n, MARK_LIFT));
    float inner = radius * (1.0f - width);

    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < MARK_SEGMENTS; i++) {
        float a0 = 2.0f * PI * (float)i / MARK_SEGMENTS, a1 = 2.0f * PI * (float)(i + 1) / MARK_SEGMENTS;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(a0)), Vector3Scale(v, sinf(a0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(a1)), Vector3Scale(v, sinf(a1)));
        Vector3 p0 = Vector3Add(c, Vector3Scale(d0, inner)), p1 = Vector3Add(c, Vector3Scale(d0, radius));
        Vector3 p2 = Vector3Add(c, Vector3Scale(d1, radius)), p3 = Vector3Add(c, Vector3Scale(d1, inner));
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }
}

// Só do lado do planeta voltado para a câmera; some aos poucos perto da borda.
static float facing_fade(Vector3 target, Vector3 eye)
{
    Vector3 n = Vector3Normalize(target);
    float facing = Vector3DotProduct(n, Vector3Normalize(Vector3Subtract(eye, target)));
    return Clamp(facing * 4.0f, 0.0f, 1.0f) * 0.9f;
}

// Marca no solo onde uma bomba vai cair. `closing` vai de 0 (acabou de
// marcar) a 1 (a bomba está caindo): o anel de fora se fecha sobre o alvo.
static void target_mark(Vector3 target, float radius, float closing, float time, Vector3 eye)
{
    float fade = facing_fade(target, eye);
    if (fade <= 0.0f) return;
    float blink = 0.6f + 0.4f * sinf(time * (8.0f + 16.0f * closing));
    ground_ring(target, radius, MARK_WIDTH, faded(MARK_COLOR, fade * blink));
    ground_ring(target, radius * Lerp(MARK_START, 1.0f, closing), MARK_WIDTH * 0.6f, faded(MARK_COLOR, fade * 0.7f));
    ground_ring(target, radius * 0.25f, 0.6f, faded(MARK_COLOR, fade * blink));
}

void projectiles_draw(const World *w, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    const Projectiles *p = &w->shots;
    const Enemies *e = &w->enemies;
    float time = (float)w->tick / (float)t->sim_hz;
    Vector3 eye = camera.position;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // Marcas no solo: por cima do terreno (sem teste de profundidade), para o
    // relevo não esconder o aviso, e com transparência comum (somando luz, o
    // vermelho sobre o mar azul viraria rosa).
    rlDisableDepthTest();
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        if (en->type == ENEMY_BOMBER && en->arming > 0.0f) {
            float closing = 1.0f - en->arming / fmaxf(t->bomber_bomb_warning, 1e-3f);
            target_mark(en->target, t->bomber_crater_radius, closing * 0.85f, time, eye);
        } else if (en->type == ENEMY_DIGGER && en->state == DIGGER_DRILL) {
            // A boca do túnel do Escavador, em laranja: é por ali que se entra.
            // Anel firme (transparência alta: sobre o mar, laranja fraco vira cinza) e
            // um segundo anel que pulsa para fora.
            Vector3 mouth = Vector3Scale(en->target, w->planet.params.radius);
            float fade = facing_fade(mouth, eye);
            float pulse = fmodf(time * 0.8f, 1.0f);
            float r = t->digger_drill_radius * 1.3f;
            if (fade > 0.0f) {
                ground_ring(mouth, r, 0.12f, faded(DIG_MARK_COLOR, fade));
                ground_ring(mouth, r * (1.0f + pulse * 0.8f), 0.08f, faded(DIG_MARK_COLOR, fade * (1.0f - pulse)));
            }
        }
    }
    for (int i = 0; i < p->pool.capacity; i++) {
        if (pool_alive(&p->pool, i) && p->items[i].kind == SHOT_BOMB) {
            target_mark(p->items[i].target, p->items[i].crater_radius, 1.0f, time, eye);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableDepthTest();

    // Daqui em diante, tudo soma brilho.
    BeginBlendMode(BLEND_ADDITIVE);
    draw_links(w, t, alpha, glow, camera, time);

    // Tiros: riscos e bolas de luz.
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        float speed = Vector3Length(s->vel);
        if (speed < 1e-3f) continue;
        Vector3 dir = Vector3Scale(s->vel, 1.0f / speed);

        if (s->kind == SHOT_BOLT) {
            // O risco não passa de onde o tiro saiu. Perto da câmera ele fica
            // mais fino e mais fraco; longe, um pouco mais grosso, para
            // continuar visível.
            float length = fminf(fminf(speed * BOLT_LENGTH, BOLT_LENGTH_MAX), s->age * speed + 0.5f);
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, length));
            float distance = Vector3Distance(pos, eye);
            float near = Clamp((distance - BOLT_NEAR) / (BOLT_CLEAR - BOLT_NEAR), 0.15f, 1.0f);
            float width = BOLT_WIDTH * Clamp(distance / BOLT_WIDTH_AT, 0.25f, 1.5f);
            streak(tail, pos, width * BOLT_HALO, faded(BOLT_HALO_COLOR, 0.5f * near), eye);
            streak(tail, pos, width, faded(BOLT_CORE, near), eye);
        } else if (s->kind == SHOT_PLASMA && s->source == ENEMY_SNIPER) {
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, speed * LANCE_LENGTH));
            streak(tail, pos, 1.4f, faded(LASER_COLOR, 0.6f), eye);
            streak(tail, pos, 0.4f, faded(WHITE, 0.9f), eye);
        } else if (s->kind == SHOT_PLASMA) {
            Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, speed * PLASMA_LENGTH));
            streak(tail, pos, PLASMA_SIZE * 0.6f, faded(PLASMA_HALO, 0.6f), eye);
        }
    }
    rlEnd();
    rlSetTexture(0);

    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        const Shot *s = &p->items[i];
        Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
        if (s->kind == SHOT_PLASMA && s->source != ENEMY_SNIPER) {
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 1.6f, faded(PLASMA_HALO, 0.8f));
            DrawBillboard(camera, glow, pos, PLASMA_SIZE * 0.6f, PLASMA_CORE);
        } else if (s->kind == SHOT_BOMB) {
            // Pisca cada vez mais rápido conforme se aproxima do solo.
            float remaining = Vector3Distance(pos, s->target);
            float blink = 0.6f + 0.4f * sinf(time * (6.0f + 60.0f / (remaining + 4.0f)));
            DrawBillboard(camera, glow, pos, BOMB_SIZE * 2.0f, faded(BOMB_COLOR, 0.5f * blink));
            DrawBillboard(camera, glow, pos, BOMB_SIZE, faded(BOMB_COLOR, blink));
            DrawBillboard(camera, glow, pos, BOMB_SIZE * 0.35f, WHITE);
        }
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
