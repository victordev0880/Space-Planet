#include "raylib.h"
#include "rlgl.h"

#include "render/ship_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelo -----------------------------------------------------------------
// Tudo em eixos da nave: frente é -Z, direita é +X, topo é +Y. Só a metade
// direita é descrita; a esquerda sai espelhada. Cerca de 3,7 de comprimento
// e 3,7 de envergadura.

enum {
    PART_HULL,     // fuselagem e asas, em branco
    PART_SIDE,     // laterais de baixo, cinza claro
    PART_PANEL,    // painéis escuros: barriga, lombada, lado de baixo das asas
    PART_GLASS,    // cabine
    PART_ACCENT,   // vermelho-laranja: derivas, pontas das asas, canards
    PART_STRIPE,   // faixa azul do nariz e das asas
    PART_POD,      // carenagem dos motores
    PART_NOZZLE,   // bocais e entradas de ar
    PART_RING,     // anel do motor, que brilha
};

// Uma "fatia" da fuselagem, vista de frente: cinco pontos do topo à barriga.
typedef struct {
    float z;        // posição ao longo da nave
    float width;    // meia largura, no ponto mais largo (o "lado")
    float top;      // altura do topo
    float upper;    // altura do ombro
    float side;     // altura do lado
    float lower;    // altura do canto de baixo
    float bottom;   // altura da barriga
} Section;

static const Section SECTIONS[] = {
    { -1.55f, 0.14f, 0.10f, 0.06f,  0.00f, -0.07f, -0.10f },
    { -1.00f, 0.24f, 0.20f, 0.12f,  0.00f, -0.12f, -0.16f },
    { -0.45f, 0.34f, 0.38f, 0.16f,  0.02f, -0.15f, -0.21f },   // ponto mais alto da cabine
    {  0.15f, 0.42f, 0.30f, 0.17f,  0.03f, -0.16f, -0.23f },   // raiz das asas, o ponto mais largo
    {  0.75f, 0.40f, 0.24f, 0.15f,  0.02f, -0.14f, -0.20f },
    {  1.25f, 0.30f, 0.18f, 0.11f,  0.00f, -0.10f, -0.14f },   // cauda
};
#define SECTION_COUNT ((int)(sizeof SECTIONS / sizeof SECTIONS[0]))

// O que cobre cada trecho entre duas fatias, por faixa (topo, ombro, lado).
static const int TOP_PARTS[SECTION_COUNT - 1]   = { PART_HULL, PART_GLASS, PART_GLASS, PART_PANEL, PART_HULL };
static const int UPPER_PARTS[SECTION_COUNT - 1] = { PART_STRIPE, PART_HULL, PART_HULL, PART_HULL, PART_HULL };

static const Vector3 NOSE_TIP = { 0.0f, -0.01f, -2.10f };
#define SHOULDER_INSET 0.82f   // o ombro fica um pouco para dentro do lado
#define LOWER_INSET    0.72f   // ... e o canto de baixo, mais ainda

// Os canhões (mesma posição de game/combat.c), para o clarão dos tiros.
static const Vector3 GUNS[2] = { { 0.55f, -0.05f, -0.45f }, { -0.55f, -0.05f, -0.45f } };

// Luzes de navegação, nas pontas das asas e no alto da cauda.
static const Vector3 WINGTIP   = { 1.82f, 0.22f, 1.08f };
static const Vector3 TAIL_LAMP = { 0.0f, 0.20f, 1.30f };
// Propulsores de ré, na frente das entradas de ar.
static const Vector3 RETRO = { 0.36f, -0.03f, -0.62f };

static LowPoly model;
static bool    model_built;

static Vector3 lerp3(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, t); }

static void build_fuselage(LowPoly *m)
{
    Vector3 top[SECTION_COUNT], upper[SECTION_COUNT], side[SECTION_COUNT], lower[SECTION_COUNT];
    Vector3 bottom[SECTION_COUNT];
    for (int i = 0; i < SECTION_COUNT; i++) {
        const Section *s = &SECTIONS[i];
        top[i] = (Vector3){ 0.0f, s->top, s->z };
        upper[i] = (Vector3){ s->width * SHOULDER_INSET, s->upper, s->z };
        side[i] = (Vector3){ s->width, s->side, s->z };
        lower[i] = (Vector3){ s->width * LOWER_INSET, s->lower, s->z };
        bottom[i] = (Vector3){ 0.0f, s->bottom, s->z };
    }

    // Nariz: da ponta até a primeira fatia.
    lowpoly_tri(m, NOSE_TIP, upper[0], top[0], PART_HULL, true);
    lowpoly_tri(m, NOSE_TIP, side[0], upper[0], PART_STRIPE, true);
    lowpoly_tri(m, NOSE_TIP, lower[0], side[0], PART_SIDE, true);
    lowpoly_tri(m, NOSE_TIP, bottom[0], lower[0], PART_PANEL, true);

    for (int i = 0; i + 1 < SECTION_COUNT; i++) {
        lowpoly_quad(m, top[i], upper[i], upper[i + 1], top[i + 1], TOP_PARTS[i], true);
        lowpoly_quad(m, upper[i], side[i], side[i + 1], upper[i + 1], UPPER_PARTS[i], true);
        lowpoly_quad(m, side[i], lower[i], lower[i + 1], side[i + 1], PART_SIDE, true);
        lowpoly_quad(m, lower[i], bottom[i], bottom[i + 1], lower[i + 1], PART_PANEL, true);
    }

    // Tampa traseira.
    int last = SECTION_COUNT - 1;
    lowpoly_tri(m, top[last], upper[last], side[last], PART_NOZZLE, true);
    lowpoly_tri(m, top[last], side[last], bottom[last], PART_NOZZLE, true);
    lowpoly_tri(m, side[last], lower[last], bottom[last], PART_NOZZLE, true);

    // Entradas de ar, escuras, logo atrás da cabine.
    lowpoly_tri(m, (Vector3){ 0.35f, 0.04f, -0.42f }, (Vector3){ 0.43f, 0.06f, 0.12f },
                (Vector3){ 0.37f, -0.12f, 0.12f }, PART_NOZZLE, true);
}

static void build_wing(LowPoly *m)
{
    // Extensão da asa junto à fuselagem (LERX), saindo de perto da cabine.
    Vector3 lerx_front = { 0.24f, 0.02f, -1.00f };
    Vector3 root_lead  = { 0.40f, 0.03f, -0.30f };
    Vector3 root_ridge = { 0.42f, 0.13f, 0.35f };
    Vector3 root_trail = { 0.40f, 0.02f, 1.00f };
    lowpoly_tri(m, lerx_front, root_lead, (Vector3){ 0.30f, 0.13f, -0.45f }, PART_HULL, true);
    lowpoly_tri(m, lerx_front, (Vector3){ 0.28f, -0.10f, -0.45f }, root_lead, PART_PANEL, true);

    // Asa em flecha, com espessura: duas rampas em cima (que se encontram em
    // uma aresta ao longo da asa) e um plano embaixo. Perto da ponta, a faixa.
    Vector3 tip_lead  = { 1.78f, -0.08f, 0.72f };
    Vector3 tip_ridge = { 1.78f, -0.04f, 0.90f };
    Vector3 tip_trail = { 1.78f, -0.08f, 1.08f };
    const float band = 0.72f;   // onde começa a faixa, entre a raiz (0) e a ponta (1)
    Vector3 mid_lead = lerp3(root_lead, tip_lead, band);
    Vector3 mid_ridge = lerp3(root_ridge, tip_ridge, band);
    Vector3 mid_trail = lerp3(root_trail, tip_trail, band);

    lowpoly_quad(m, root_lead, mid_lead, mid_ridge, root_ridge, PART_HULL, true);
    lowpoly_quad(m, root_ridge, mid_ridge, mid_trail, root_trail, PART_SIDE, true);
    lowpoly_quad(m, mid_lead, tip_lead, tip_ridge, mid_ridge, PART_STRIPE, true);
    lowpoly_quad(m, mid_ridge, tip_ridge, tip_trail, mid_trail, PART_STRIPE, true);
    lowpoly_quad(m, root_lead, root_trail, tip_trail, tip_lead, PART_PANEL, true);
    lowpoly_tri(m, tip_lead, tip_ridge, tip_trail, PART_ACCENT, true);

    // Ponta virada para cima.
    lowpoly_tri(m, tip_lead, tip_trail, (Vector3){ 1.88f, 0.34f, 1.18f }, PART_ACCENT, true);

    // Canards, as pequenas asas perto do nariz.
    lowpoly_tri(m, (Vector3){ 0.18f, 0.02f, -1.38f }, (Vector3){ 0.58f, -0.01f, -1.02f },
                (Vector3){ 0.22f, 0.02f, -0.96f }, PART_ACCENT, true);
}

static void build_engine(LowPoly *m)
{
    // Carenagem do motor, sob a asa: prisma hexagonal, com a frente em ponta
    // e, atrás, o anel que brilha em volta do bocal escuro.
    const float x = SHIP_ENGINE_X, y = SHIP_ENGINE_Y;
    const float radius = 0.17f, front = 0.12f, back = SHIP_ENGINE_Z;
    Vector3 tip = { x, y, front - 0.42f };

    Vector3 ring_front[6], ring_back[6], ring_inner[6];
    for (int i = 0; i < 6; i++) {
        float a = (float)i * PI / 3.0f + PI / 6.0f;
        ring_front[i] = (Vector3){ x + cosf(a) * radius, y + sinf(a) * radius, front };
        ring_back[i] = (Vector3){ x + cosf(a) * radius * 1.06f, y + sinf(a) * radius * 1.06f, back };
        ring_inner[i] = (Vector3){ x + cosf(a) * radius * 0.62f, y + sinf(a) * radius * 0.62f, back + 0.01f };
    }
    for (int i = 0; i < 6; i++) {
        int next = (i + 1) % 6;
        lowpoly_tri(m, tip, ring_front[i], ring_front[next], PART_POD, true);
        lowpoly_quad(m, ring_front[i], ring_back[i], ring_back[next], ring_front[next], (i % 2 == 0) ? PART_POD : PART_SIDE,
                     true);
        lowpoly_quad(m, ring_back[i], ring_inner[i], ring_inner[next], ring_back[next], PART_RING, true);
    }
    Vector3 center = { x, y, back + 0.02f };
    for (int i = 0; i < 6; i++) lowpoly_tri(m, center, ring_inner[i], ring_inner[(i + 1) % 6], PART_NOZZLE, true);
}

static void build_fins(LowPoly *m)
{
    // Derivas inclinadas para fora, sobre a cauda.
    Vector3 front = { 0.18f, 0.20f, 0.52f };
    Vector3 back  = { 0.16f, 0.14f, 1.18f };
    Vector3 tip   = { 0.56f, 0.80f, 1.36f };
    lowpoly_tri(m, front, back, tip, PART_ACCENT, true);
    lowpoly_tri(m, front, (Vector3){ 0.40f, 0.56f, 1.06f }, tip, PART_HULL, true);   // bordo de ataque claro
}

static void build_model(void)
{
    LowPoly *m = &model;
    lowpoly_clear(m);
    lowpoly_part(m, PART_HULL,   (Color){ 230, 234, 242, 255 }, false);
    lowpoly_part(m, PART_SIDE,   (Color){ 170, 178, 194, 255 }, false);
    lowpoly_part(m, PART_PANEL,  (Color){ 62, 68, 82, 255 }, false);
    lowpoly_part(m, PART_GLASS,  (Color){ 50, 130, 210, 255 }, false);
    lowpoly_part(m, PART_ACCENT, (Color){ 232, 82, 48, 255 }, false);
    lowpoly_part(m, PART_STRIPE, (Color){ 44, 104, 200, 255 }, false);
    lowpoly_part(m, PART_POD,    (Color){ 140, 148, 166, 255 }, false);
    lowpoly_part(m, PART_NOZZLE, (Color){ 24, 26, 32, 255 }, false);
    lowpoly_part_glow(m, PART_RING, (Color){ 120, 200, 255, 255 });   // acompanha a cor do motor (boost)
    build_fuselage(m);
    build_wing(m);
    build_engine(m);
    build_fins(m);
    model_built = true;
}

// --- aparência --------------------------------------------------------------
#define SHIP_AMBIENT      0.38f   // luz mínima: a nave precisa ser legível mesmo à noite
#define SHIP_SHADOW       0.18f   // quanto da luz do Sol sobra na sombra do planeta
#define SHIP_WRAP         0.45f   // quanto a luz do Sol "dobra" para as faces de lado para ele
#define SHIP_SPECULAR     0.55f
#define SHIP_SHININESS    28.0f
#define SHIP_RIM          0.32f
#define SHIP_BOUNCE       0.30f
#define ENGINE_IDLE       0.20f   // tamanho do brilho com o motor em marcha lenta
#define ENGINE_THRUST     0.22f   // ... a mais com o motor aceso
#define ENGINE_BOOST      0.22f   // ... e a mais ainda no boost
#define ENGINE_HALO       2.4f    // o halo colorido, em tamanhos do brilho
#define ENGINE_HALO_ALPHA 150     // ... e a força dele (0 a 255): fraco, para não esconder a nave
#define DASH_GLOW         2.4f    // tamanho do halo durante o dash
#define DASH_WHITEN       0.55f   // quanto a nave clareia durante o dash
#define MUZZLE_SIZE       1.3f    // clarão no cano a cada tiro
#define MUZZLE_TIME       0.05f   // (mesma duração de game/combat.c)
#define SHIELD_SIZE       5.6f    // bolha do escudo ao segurar um golpe
#define HIT_TIME          0.35f   // (mesma duração do aviso de dano em game/combat.c)
#define NAV_SIZE          0.55f   // luzes de navegação
#define STROBE_PERIOD     1.2f    // segundos entre dois piscos da luz da cauda
#define STROBE_ON         0.08f
#define RETRO_SIZE        0.7f    // brilho dos propulsores de ré

static const Color ENGINE_COLOR = { 120, 196, 255, 255 };
static const Color BOOST_COLOR  = { 255, 200, 130, 255 };
static const Color DASH_COLOR   = { 150, 215, 255, 255 };
static const Color MUZZLE_COLOR = { 170, 230, 255, 255 };
static const Color SHIELD_COLOR = {  90, 190, 255, 255 };
static const Color HURT_COLOR   = { 255, 120,  80, 255 };
static const Color RIM_COLOR    = { 120, 180, 255, 255 };
static const Color BOUNCE_COLOR = { 110, 160, 230, 255 };
static const Color NAV_RED      = { 255,  60,  50, 255 };
static const Color NAV_GREEN    = {  70, 255, 120, 255 };

static Color faded(Color c, float alpha)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * (float)c.a) };
}

// --- peças da evolução --------------------------------------------------------
// Cada evolução acrescenta uma peça à nave, com as cores da nave e um detalhe
// aceso na cor da linha (o tipo de inimigo de onde ela vem). Assim a nave
// "vira" aquilo que mais derrotou.

static LowPoly kits[EVO_COUNT];

// Cores das linhas (as mesmas do HUD); as sinergias acendem em ouro.
static const Color SYNERGY_GLOW = { 255, 196, 64, 255 };
static const Color LINE_COLORS[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = { 196, 130, 255, 255 },
    [ENEMY_BOMBER]   = { 255, 150, 70, 255 },
    [ENEMY_KAMIKAZE] = { 255, 96, 72, 255 },
    [ENEMY_DIGGER]   = { 255, 186, 80, 255 },
    [ENEMY_SIPHON]   = { 120, 255, 190, 255 },
    [ENEMY_SNIPER]   = { 255, 110, 140, 255 },
    [ENEMY_SHIELDER] = { 110, 230, 255, 255 },
    [ENEMY_CARRIER]  = { 214, 150, 255, 255 },
};

static Vector3 kv(float x, float y, float z) { return (Vector3){ x, y, z }; }

static void kit_palette(LowPoly *m)
{
    lowpoly_part(m, PART_HULL,   (Color){ 230, 234, 242, 255 }, false);
    lowpoly_part(m, PART_SIDE,   (Color){ 170, 178, 194, 255 }, false);
    lowpoly_part(m, PART_PANEL,  (Color){ 62, 68, 82, 255 }, false);
    lowpoly_part(m, PART_GLASS,  (Color){ 50, 130, 210, 255 }, false);
    lowpoly_part(m, PART_ACCENT, (Color){ 232, 82, 48, 255 }, false);
    lowpoly_part(m, PART_STRIPE, (Color){ 44, 104, 200, 255 }, false);
    lowpoly_part(m, PART_POD,    (Color){ 140, 148, 166, 255 }, false);
    lowpoly_part(m, PART_NOZZLE, (Color){ 24, 26, 32, 255 }, false);
    lowpoly_part_glow(m, PART_RING, (Color){ 255, 255, 255, 255 });   // assume a cor da linha
}

static void build_kits(void)
{
    const Vector3 Y = { 0.0f, 1.0f, 0.0f }, X = { 1.0f, 0.0f, 0.0f }, Z = { 0.0f, 0.0f, 1.0f };
    for (int i = 0; i < EVO_COUNT; i++) {
        lowpoly_clear(&kits[i]);
        kit_palette(&kits[i]);
    }
    LowPoly *m;

    // Enxame: lâminas no bordo de ataque das asas; canhões extras nas asas.
    m = &kits[EVO_ESCORT];
    lp_plate(m, kv(0.55f, 0.06f, -0.05f), kv(1.65f, 0.20f, 0.62f), kv(1.62f, 0.20f, 0.70f), kv(0.55f, 0.06f, 0.08f), Y, 0.04f,
             0.02f, PART_PANEL, PART_PANEL, PART_RING, true);
    m = &kits[EVO_SPREAD];
    lp_tube(m, kv(0.95f, 0.02f, 0.10f), kv(0.95f, 0.02f, -0.75f), 0.05f, 0.04f, 6, PART_POD, PART_NOZZLE, true);
    lp_ring(m, kv(0.95f, 0.02f, -0.55f), Z, 0.05f, 0.08f, 0.05f, 6, PART_RING, PART_PANEL, true);

    // Kamikaze: espinhos no nariz; respiros acesos nas costas.
    m = &kits[EVO_BLAST_DASH];
    for (int k = 0; k < 3; k++) {
        float z = -1.35f + 0.3f * (float)k;
        lp_spike(m, kv(0.2f - 0.02f * (float)k, 0.0f, z), kv(0.48f, -0.02f, z + 0.12f), 0.05f, 4, PART_ACCENT, true);
    }
    lp_box(m, kv(0.0f, -0.12f, -1.7f), kv(0.05f, 0.03f, 0.06f), PART_RING, false);
    m = &kits[EVO_CHAIN];
    lp_box(m, kv(0.18f, 0.29f, 0.55f), kv(0.05f, 0.02f, 0.22f), PART_RING, true);
    lp_box(m, kv(0.18f, 0.27f, 0.55f), kv(0.08f, 0.02f, 0.26f), PART_PANEL, true);

    // Bombardeiro: placas de blindagem nas laterais; casulos de mísseis sob as asas.
    m = &kits[EVO_HEAVY_HULL];
    lp_plate(m, kv(0.40f, 0.10f, -0.70f), kv(0.48f, 0.08f, 0.10f), kv(0.46f, -0.12f, 0.10f), kv(0.36f, -0.10f, -0.70f), X,
             0.06f, 0.06f, PART_ACCENT, PART_ACCENT, PART_PANEL, true);
    lp_box(m, kv(0.0f, 0.36f, 0.45f), kv(0.16f, 0.03f, 0.25f), PART_POD, false);
    m = &kits[EVO_WARHEADS];
    {
        const LpRing pod[] = { { -0.55f, 0.0f, 0.0f, -0.16f }, { -0.35f, 0.08f, 0.08f, -0.16f }, { 0.35f, 0.09f, 0.09f, -0.16f },
                               { 0.45f, 0.06f, 0.06f, -0.16f } };
        lp_loft(m, pod, 4, 6, 1.15f, PART_SIDE, PART_PANEL, PART_NOZZLE, 0.0f, true);
        lp_box(m, kv(1.15f, -0.16f, -0.50f), kv(0.04f, 0.04f, 0.03f), PART_RING, true);
    }

    // Escavador: broca no nariz; lâminas de aríete dos lados do nariz.
    m = &kits[EVO_PIERCE];
    {
        const LpRing drill[] = { { -2.75f, 0.0f, 0.0f, -0.01f }, { -2.40f, 0.07f, 0.07f, -0.01f }, { -2.05f, 0.12f, 0.12f, -0.01f } };
        lp_loft(m, drill, 3, 5, 0.0f, PART_POD, PART_SIDE, -1, 0.7f, false);
        lp_ring(m, kv(0.0f, -0.01f, -2.0f), Z, 0.08f, 0.15f, 0.05f, 6, PART_RING, PART_PANEL, false);
    }
    m = &kits[EVO_ARMOR_BREAK];
    lp_plate(m, kv(0.14f, 0.0f, -1.55f), kv(0.40f, -0.02f, -1.95f), kv(0.44f, -0.02f, -1.75f), kv(0.26f, 0.0f, -1.10f), Y, 0.05f,
             0.02f, PART_ACCENT, PART_PANEL, PART_RING, true);

    // Sifão: tubos verdes nas costas; emissor em cima.
    m = &kits[EVO_LIFESTEAL];
    lp_tube(m, kv(0.12f, 0.31f, -0.1f), kv(0.12f, 0.24f, 1.1f), 0.04f, 0.04f, 5, PART_RING, -1, true);
    m = &kits[EVO_ENERGY_SIPHON];
    lp_tube(m, kv(0.0f, 0.30f, 0.15f), kv(0.0f, 0.46f, 0.15f), 0.08f, 0.05f, 6, PART_POD, PART_RING, false);

    // Atirador: cano longo sob a fuselagem; canos dos canhões mais compridos.
    m = &kits[EVO_CRIT];
    lp_tube(m, kv(0.0f, -0.24f, -0.3f), kv(0.0f, -0.24f, -2.55f), 0.05f, 0.04f, 6, PART_PANEL, PART_NOZZLE, false);
    lp_box(m, kv(0.0f, -0.24f, -2.4f), kv(0.06f, 0.06f, 0.05f), PART_RING, false);
    m = &kits[EVO_LONG_RANGE];
    lp_tube(m, kv(0.55f, -0.05f, -0.45f), kv(0.55f, -0.05f, -1.35f), 0.035f, 0.03f, 5, PART_PANEL, PART_RING, true);

    // Escudeiro: emissores de escudo nas pontas das asas; defletor no nariz.
    m = &kits[EVO_BIG_SHIELD];
    lp_ring(m, kv(1.70f, 0.20f, 0.85f), X, 0.10f, 0.20f, 0.05f, 8, PART_RING, PART_POD, true);
    m = &kits[EVO_REFLECT];
    lp_plate(m, kv(0.0f, 0.12f, -1.25f), kv(0.22f, 0.06f, -1.05f), kv(0.22f, 0.10f, -0.85f), kv(0.0f, 0.18f, -0.80f), Y, 0.03f,
             0.03f, PART_RING, PART_RING, PART_PANEL, true);

    // Porta-drones: casulos de hangar atrás; mastro com antena.
    m = &kits[EVO_FIGHTERS];
    lp_box(m, kv(0.62f, 0.12f, 1.15f), kv(0.12f, 0.08f, 0.22f), PART_POD, true);
    lp_box(m, kv(0.62f, 0.12f, 1.38f), kv(0.08f, 0.04f, 0.01f), PART_RING, true);
    m = &kits[EVO_HANGAR];
    lp_tube(m, kv(0.0f, 0.25f, 0.95f), kv(0.0f, 0.75f, 1.15f), 0.03f, 0.02f, 4, PART_PANEL, -1, false);
    lp_ring(m, kv(0.0f, 0.78f, 1.17f), Y, 0.0f, 0.12f, 0.02f, 8, PART_RING, PART_RING, false);

    // Sinergias (detalhe aceso em ouro): drones suicidas pendurados sob as
    // asas; emissor do feixe sob o nariz; garras de desmanche atrás; um halo
    // sobre a cabine.
    m = &kits[EVO_SYN_SUICIDE];
    lp_box(m, kv(0.80f, -0.14f, 0.15f), kv(0.07f, 0.05f, 0.16f), PART_ACCENT, true);
    lp_spike(m, kv(0.80f, -0.14f, -0.01f), kv(0.80f, -0.14f, -0.22f), 0.05f, 4, PART_RING, true);
    m = &kits[EVO_SYN_RESTORE];
    lp_tube(m, kv(0.0f, -0.22f, -0.9f), kv(0.0f, -0.36f, -0.9f), 0.05f, 0.12f, 8, PART_POD, PART_RING, false);
    m = &kits[EVO_SYN_SALVAGE];
    lp_plate(m, kv(0.30f, -0.10f, 1.05f), kv(0.42f, -0.30f, 1.35f), kv(0.36f, -0.30f, 1.45f), kv(0.24f, -0.10f, 1.20f), X, 0.04f,
             0.02f, PART_PANEL, PART_PANEL, PART_RING, true);
    m = &kits[EVO_SYN_POISE];
    lp_ring(m, kv(0.0f, 0.52f, -0.35f), Y, 0.22f, 0.28f, 0.02f, 10, PART_RING, PART_RING, false);
}

// --- aliados ------------------------------------------------------------------

static LowPoly escort_model, fighter_model;
static bool    allies_built;

static void build_allies(void)
{
    const Vector3 Y = { 0.0f, 1.0f, 0.0f };
    LowPoly *m = &escort_model;
    lowpoly_clear(m);
    kit_palette(m);
    const LpRing dart[] = { { -0.55f, 0.0f, 0.0f, 0.0f }, { -0.2f, 0.12f, 0.08f, 0.0f }, { 0.3f, 0.10f, 0.07f, 0.0f },
                            { 0.45f, 0.06f, 0.05f, 0.0f } };
    lp_loft(m, dart, 4, 5, 0.0f, PART_HULL, PART_PANEL, PART_RING, 0.0f, false);
    lp_plate(m, kv(0.08f, 0.0f, -0.05f), kv(0.5f, 0.02f, 0.3f), kv(0.45f, 0.02f, 0.42f), kv(0.08f, 0.0f, 0.3f), Y, 0.03f, 0.01f,
             PART_STRIPE, PART_PANEL, PART_RING, true);

    m = &fighter_model;
    lowpoly_clear(m);
    kit_palette(m);
    const LpRing body[] = { { -0.9f, 0.0f, 0.0f, 0.0f }, { -0.5f, 0.13f, 0.10f, 0.02f }, { 0.2f, 0.17f, 0.12f, 0.02f },
                            { 0.6f, 0.12f, 0.09f, 0.0f } };
    lp_loft(m, body, 4, 6, 0.0f, PART_HULL, PART_PANEL, PART_NOZZLE, 0.0f, false);
    lp_plate(m, kv(0.12f, 0.0f, -0.2f), kv(0.85f, -0.02f, 0.35f), kv(0.8f, -0.02f, 0.5f), kv(0.12f, 0.0f, 0.4f), Y, 0.04f,
             0.015f, PART_HULL, PART_PANEL, PART_ACCENT, true);
    lp_box(m, kv(0.0f, 0.12f, -0.3f), kv(0.06f, 0.04f, 0.15f), PART_GLASS, false);
    lp_ring(m, kv(0.0f, 0.0f, 0.62f), (Vector3){ 0.0f, 0.0f, 1.0f }, 0.0f, 0.1f, 0.02f, 6, PART_RING, PART_RING, false);
    allies_built = true;
}

// Cor do brilho da nave: a do motor, puxada para a cor da linha dominante.
static Color ship_glow(const Evolution *evo, float boost)
{
    Color base = ENGINE_COLOR;
    int line = (evo != NULL) ? evolution_dominant_line(evo) : -1;
    if (line >= 0) {
        float pull = Clamp(0.35f + 0.08f * (float)evolution_total_ranks(evo), 0.0f, 0.8f);
        Color lc = LINE_COLORS[line];
        base = (Color){ (unsigned char)Lerp((float)base.r, (float)lc.r, pull),
                        (unsigned char)Lerp((float)base.g, (float)lc.g, pull),
                        (unsigned char)Lerp((float)base.b, (float)lc.b, pull), 255 };
    }
    return (Color){ (unsigned char)Lerp((float)base.r, (float)BOOST_COLOR.r, boost),
                    (unsigned char)Lerp((float)base.g, (float)BOOST_COLOR.g, boost),
                    (unsigned char)Lerp((float)base.b, (float)BOOST_COLOR.b, boost), 255 };
}

void allies_draw(const Allies *a, const Planet *planet, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    if (!allies_built) build_allies();
    Vector3 sun = tuning_sun_direction(t);
    for (int i = 0; i < ALLIES_MAX; i++) {
        const Ally *al = &a->items[i];
        if (!al->alive) continue;
        Vector3 pos = Vector3Lerp(al->pos_prev, al->pos, alpha);
        Quaternion rot = QuaternionSlerp(al->rot_prev, al->rot, alpha);
        Color lc = LINE_COLORS[al->kind == ALLY_ESCORT ? ENEMY_SWARM : ENEMY_CARRIER];
        LowPolyPose pose = lowpoly_pose(pos, rot, 1.0f, sun, camera.position);
        pose.sunlight = lowpoly_sunlight(pos, sun, planet->params.radius, SHIP_SHADOW);
        pose.ambient = SHIP_AMBIENT;
        pose.wrap = SHIP_WRAP;
        pose.specular = SHIP_SPECULAR;
        pose.shininess = SHIP_SHININESS;
        pose.rim = SHIP_RIM;
        pose.rim_color = lc;
        pose.glow = lc;
        lowpoly_draw(al->kind == ALLY_ESCORT ? &escort_model : &fighter_model, &pose);
    }
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < ALLIES_MAX; i++) {
        const Ally *al = &a->items[i];
        if (!al->alive) continue;
        Vector3 pos = Vector3Lerp(al->pos_prev, al->pos, alpha);
        Quaternion rot = QuaternionSlerp(al->rot_prev, al->rot, alpha);
        Color lc = LINE_COLORS[al->kind == ALLY_ESCORT ? ENEMY_SWARM : ENEMY_CARRIER];
        Vector3 tail = { 0.0f, 0.0f, al->kind == ALLY_ESCORT ? 0.5f : 0.66f };
        DrawBillboard(camera, glow, Vector3Add(pos, Vector3RotateByQuaternion(tail, rot)), 0.5f, faded(lc, 0.6f));
    }
    EndBlendMode();
    rlEnableDepthMask();
}

void ship_draw(const Ship *s, const Evolution *evo, const Planet *planet, const Tuning *t, float alpha, float time,
               Texture2D glow, Camera3D camera)
{
    if (!s->alive) return;
    if (!model_built) {
        build_model();
        build_kits();
    }

    // Pose interpolada entre o passo anterior e o atual.
    Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
    Quaternion rot = QuaternionSlerp(s->rot_prev, s->rot, alpha);
    Vector3 sun = tuning_sun_direction(t);

    // Clarão: branco-azulado no dash (invulnerável); avermelhado logo depois
    // de levar dano no casco.
    float hurt = (!s->hit_shield) ? Clamp(s->hit_flash / HIT_TIME, 0.0f, 1.0f) : 0.0f;
    LowPolyPose pose = lowpoly_pose(pos, rot, 1.0f, sun, camera.position);
    pose.sunlight = lowpoly_sunlight(pos, sun, planet->params.radius, SHIP_SHADOW);
    pose.ambient = SHIP_AMBIENT;
    pose.wrap = SHIP_WRAP;
    pose.specular = SHIP_SPECULAR;
    pose.shininess = SHIP_SHININESS;
    pose.rim = SHIP_RIM;
    pose.rim_color = RIM_COLOR;
    pose.bounce = SHIP_BOUNCE;
    pose.bounce_color = BOUNCE_COLOR;
    pose.whiten = ship_invulnerable(s) ? DASH_WHITEN : hurt * 0.5f;
    pose.flash = ship_invulnerable(s) ? WHITE : HURT_COLOR;
    pose.glow = ship_glow(evo, s->boost_blend);
    lowpoly_draw(&model, &pose);

    // Peças das evoluções, com o detalhe aceso na cor de cada linha.
    if (evo != NULL) {
        for (int i = 0; i < EVO_COUNT; i++) {
            if (evo->rank[i] == 0) continue;
            LowPolyPose kit = pose;
            const EvolutionInfo *info = evolution_info((EvolutionId)i);
            kit.glow = info->synergy ? SYNERGY_GLOW : LINE_COLORS[info->line];
            lowpoly_draw(&kits[i], &kit);
        }
    }

    // Brilhos: somam luz à imagem.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    float size = ENGINE_IDLE + ENGINE_THRUST * s->thrust_blend + ENGINE_BOOST * s->boost_blend;
    Color engine = faded(pose.glow, ENGINE_HALO_ALPHA / 255.0f);
    for (int side = -1; side <= 1; side += 2) {
        Vector3 nozzle = { SHIP_ENGINE_X * (float)side, SHIP_ENGINE_Y, SHIP_ENGINE_Z + 0.05f };
        Vector3 at = Vector3Add(pos, Vector3RotateByQuaternion(nozzle, rot));
        DrawBillboard(camera, glow, at, size * ENGINE_HALO, engine);
        DrawBillboard(camera, glow, at, size, WHITE);

        // Luzes de navegação: vermelha à esquerda, verde à direita.
        Vector3 tip = { WINGTIP.x * (float)side, WINGTIP.y, WINGTIP.z };
        DrawBillboard(camera, glow, Vector3Add(pos, Vector3RotateByQuaternion(tip, rot)), NAV_SIZE,
                      side < 0 ? NAV_RED : NAV_GREEN);

        // Ré: os propulsores da frente acendem.
        if (s->reversing) {
            Vector3 retro = { RETRO.x * (float)side, RETRO.y, RETRO.z };
            DrawBillboard(camera, glow, Vector3Add(pos, Vector3RotateByQuaternion(retro, rot)), RETRO_SIZE,
                          faded(ENGINE_COLOR, 0.8f));
        }
    }
    // Pisca branco na cauda.
    if (fmodf(time, STROBE_PERIOD) < STROBE_ON) {
        DrawBillboard(camera, glow, Vector3Add(pos, Vector3RotateByQuaternion(TAIL_LAMP, rot)), NAV_SIZE * 1.6f, WHITE);
    }

    // Clarão no cano que acabou de atirar (o canhão já trocou: é o outro).
    if (s->muzzle > 0.0f) {
        float fade = s->muzzle / MUZZLE_TIME;
        Vector3 gun = Vector3Add(pos, Vector3RotateByQuaternion(GUNS[s->gun ^ 1], rot));
        DrawBillboard(camera, glow, gun, MUZZLE_SIZE, faded(MUZZLE_COLOR, fade));
    }

    // Escudo segurando um golpe: uma bolha azul que acende e apaga.
    if (s->hit_flash > 0.0f && s->hit_shield) {
        float fade = Clamp(s->hit_flash / HIT_TIME, 0.0f, 1.0f);
        DrawBillboard(camera, glow, pos, SHIELD_SIZE, faded(SHIELD_COLOR, 0.47f * fade));
    }
    if (ship_invulnerable(s)) DrawBillboard(camera, glow, pos, DASH_GLOW * 2.0f, DASH_COLOR);

    EndBlendMode();
    rlEnableDepthMask();
}
