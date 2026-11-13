#include "raylib.h"
#include "rlgl.h"

#include "render/ship_render.h"

#include <math.h>

#include "core/vmath.h"
#include "render/lowpoly.h"

// --- modelo -----------------------------------------------------------------
// Tudo em eixos da nave: frente é -Z, direita é +X, topo é +Y. Só a metade
// direita é descrita; a esquerda sai espelhada.

enum {
    PART_HULL,     // fuselagem
    PART_GLASS,    // cabine
    PART_PANEL,    // painel escuro sobre a traseira
    PART_BELLY,    // barriga e o lado de baixo das asas
    PART_WING,     // lado de cima das asas
    PART_POD,      // carenagem dos motores
    PART_NOZZLE,   // bocal dos motores e tampa traseira
    PART_ACCENT,   // derivas e pontas das asas
};

// Uma "fatia" da fuselagem, vista de frente: meio hexágono, do topo à barriga.
typedef struct {
    float z;        // posição ao longo da nave
    float width;    // meia largura
    float top;      // altura do topo
    float upper;    // altura do canto de cima
    float lower;    // altura do canto de baixo
    float bottom;   // altura da barriga
} Section;

static const Section SECTIONS[] = {
    { -1.10f, 0.20f, 0.13f, 0.02f, -0.09f, -0.13f },   // atrás do nariz
    { -0.35f, 0.34f, 0.36f, 0.06f, -0.13f, -0.20f },   // cabine, o ponto mais alto
    {  0.45f, 0.40f, 0.27f, 0.07f, -0.14f, -0.22f },   // raiz das asas, o ponto mais largo
    {  1.10f, 0.30f, 0.17f, 0.05f, -0.10f, -0.14f },   // cauda
};
#define SECTION_COUNT ((int)(sizeof SECTIONS / sizeof SECTIONS[0]))

// O que cobre o topo de cada trecho entre duas fatias.
static const int TOP_PARTS[SECTION_COUNT - 1] = { PART_GLASS, PART_HULL, PART_PANEL };

static const Vector3 NOSE_TIP = { 0.0f, -0.02f, -1.95f };
#define LOWER_INSET 0.7f   // o canto de baixo fica mais para dentro que o de cima

// Os canhões (mesma posição de game/combat.c), para o clarão dos tiros.
static const Vector3 GUNS[2] = { { 0.55f, -0.05f, -0.45f }, { -0.55f, -0.05f, -0.45f } };

static LowPoly model;
static bool    model_built;

static void build_fuselage(LowPoly *m)
{
    Vector3 top[SECTION_COUNT], upper[SECTION_COUNT], lower[SECTION_COUNT], bottom[SECTION_COUNT];
    for (int i = 0; i < SECTION_COUNT; i++) {
        const Section *s = &SECTIONS[i];
        top[i] = (Vector3){ 0.0f, s->top, s->z };
        upper[i] = (Vector3){ s->width, s->upper, s->z };
        lower[i] = (Vector3){ s->width * LOWER_INSET, s->lower, s->z };
        bottom[i] = (Vector3){ 0.0f, s->bottom, s->z };
    }

    // Nariz: da ponta até a primeira fatia.
    lowpoly_tri(m, NOSE_TIP, upper[0], top[0], PART_HULL, true);
    lowpoly_tri(m, NOSE_TIP, lower[0], upper[0], PART_HULL, true);
    lowpoly_tri(m, NOSE_TIP, bottom[0], lower[0], PART_BELLY, true);

    for (int i = 0; i + 1 < SECTION_COUNT; i++) {
        lowpoly_quad(m, top[i], upper[i], upper[i + 1], top[i + 1], TOP_PARTS[i], true);
        lowpoly_quad(m, upper[i], lower[i], lower[i + 1], upper[i + 1], PART_HULL, true);
        lowpoly_quad(m, lower[i], bottom[i], bottom[i + 1], lower[i + 1], PART_BELLY, true);
    }

    // Tampa traseira.
    int last = SECTION_COUNT - 1;
    lowpoly_tri(m, top[last], upper[last], lower[last], PART_NOZZLE, true);
    lowpoly_tri(m, top[last], lower[last], bottom[last], PART_NOZZLE, true);
}

static void build_wing(LowPoly *m)
{
    // Asa em flecha, com espessura: duas rampas em cima, que se encontram em
    // uma aresta ao longo da asa, e um plano embaixo. A raiz fica escondida
    // dentro da fuselagem.
    Vector3 root_lead  = { 0.30f,  0.00f, -0.30f };
    Vector3 root_ridge = { 0.30f,  0.11f,  0.30f };
    Vector3 root_trail = { 0.30f,  0.00f,  0.92f };
    Vector3 tip_lead   = { 1.55f, -0.10f,  0.62f };
    Vector3 tip_ridge  = { 1.55f, -0.06f,  0.82f };
    Vector3 tip_trail  = { 1.55f, -0.10f,  1.02f };

    lowpoly_quad(m, root_lead, tip_lead, tip_ridge, root_ridge, PART_WING, true);
    lowpoly_quad(m, root_ridge, tip_ridge, tip_trail, root_trail, PART_PANEL, true);
    lowpoly_quad(m, root_lead, root_trail, tip_trail, tip_lead, PART_BELLY, true);
    lowpoly_tri(m, tip_lead, tip_ridge, tip_trail, PART_ACCENT, true);

    // Ponta virada para cima.
    Vector3 winglet = { 1.66f, 0.30f, 1.12f };
    lowpoly_tri(m, tip_lead, tip_trail, winglet, PART_ACCENT, true);
}

static void build_engine(LowPoly *m)
{
    // Carenagem do motor, sob a asa: um prisma de seção em losango, com a
    // frente em ponta e o bocal escuro atrás.
    const float x = SHIP_ENGINE_X, y = SHIP_ENGINE_Y;
    const float half = 0.15f, front = 0.30f, back = SHIP_ENGINE_Z;
    Vector3 tip = { x, y, front - 0.38f };

    Vector3 ring_front[4] = {
        { x, y + half, front }, { x + half, y, front }, { x, y - half, front }, { x - half, y, front },
    };
    Vector3 ring_back[4];
    for (int i = 0; i < 4; i++) {
        ring_back[i] = ring_front[i];
        ring_back[i].z = back;
    }

    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        lowpoly_tri(m, tip, ring_front[i], ring_front[next], PART_POD, true);
        lowpoly_quad(m, ring_front[i], ring_back[i], ring_back[next], ring_front[next], PART_POD, true);
    }
    lowpoly_quad(m, ring_back[0], ring_back[1], ring_back[2], ring_back[3], PART_NOZZLE, true);
}

static void build_fin(LowPoly *m)
{
    // Deriva inclinada para fora, sobre a cauda.
    Vector3 front = { 0.17f, 0.19f, 0.48f };
    Vector3 back  = { 0.15f, 0.13f, 1.12f };
    Vector3 tip   = { 0.52f, 0.68f, 1.28f };
    lowpoly_tri(m, front, back, tip, PART_ACCENT, true);
}

static void build_model(void)
{
    LowPoly *m = &model;
    lowpoly_clear(m);
    lowpoly_part(m, PART_HULL,   (Color){ 222, 227, 236, 255 }, false);
    lowpoly_part(m, PART_GLASS,  (Color){  40, 120, 196, 255 }, false);
    lowpoly_part(m, PART_PANEL,  (Color){ 104, 114, 134, 255 }, false);
    lowpoly_part(m, PART_BELLY,  (Color){  98, 104, 120, 255 }, false);
    lowpoly_part(m, PART_WING,   (Color){ 168, 178, 198, 255 }, false);
    lowpoly_part(m, PART_POD,    (Color){ 132, 142, 162, 255 }, false);
    lowpoly_part(m, PART_NOZZLE, (Color){  34,  36,  44, 255 }, false);
    lowpoly_part(m, PART_ACCENT, (Color){ 226,  84,  52, 255 }, false);
    build_fuselage(m);
    build_wing(m);
    build_engine(m);
    build_fin(m);
    model_built = true;
}

// --- aparência --------------------------------------------------------------
#define SHIP_AMBIENT      0.42f   // luz mínima: a nave precisa ser legível mesmo à noite
#define SHIP_SHADOW       0.18f   // quanto da luz do Sol sobra na sombra do planeta
#define SHIP_WRAP         0.45f   // quanto a luz do Sol "dobra" para as faces de lado para ele
#define ENGINE_IDLE       0.20f   // tamanho do brilho com o motor em marcha lenta
#define ENGINE_THRUST     0.20f   // ... a mais com o motor aceso
#define ENGINE_BOOST      0.20f   // ... e a mais ainda no boost
#define ENGINE_HALO       2.4f    // o halo colorido, em tamanhos do brilho
#define ENGINE_HALO_ALPHA 150     // ... e a força dele (0 a 255): fraco, para não esconder a nave
#define DASH_GLOW         2.4f    // tamanho do halo durante o dash
#define DASH_WHITEN       0.55f   // quanto a nave clareia durante o dash
#define MUZZLE_SIZE       1.3f    // clarão no cano a cada tiro
#define MUZZLE_TIME       0.05f   // (mesma duração de game/combat.c)
#define SHIELD_SIZE       5.2f    // bolha do escudo ao segurar um golpe
#define HIT_TIME          0.35f   // (mesma duração do aviso de dano em game/combat.c)

static const Color ENGINE_COLOR = { 120, 196, 255, 255 };
static const Color BOOST_COLOR  = { 255, 200, 130, 255 };
static const Color DASH_COLOR   = { 150, 215, 255, 255 };
static const Color MUZZLE_COLOR = { 170, 230, 255, 255 };
static const Color SHIELD_COLOR = {  90, 190, 255, 255 };
static const Color HURT_COLOR   = { 255, 120,  80, 255 };

void ship_draw(const Ship *s, const Planet *planet, const Tuning *t, float alpha, Texture2D glow, Camera3D camera)
{
    if (!s->alive) return;
    if (!model_built) build_model();

    // Pose interpolada entre o passo anterior e o atual.
    Vector3 pos = Vector3Lerp(s->pos_prev, s->pos, alpha);
    Quaternion rot = QuaternionSlerp(s->rot_prev, s->rot, alpha);
    Vector3 sun = tuning_sun_direction(t);

    // Clarão: branco-azulado no dash (invulnerável); avermelhado logo depois
    // de levar dano no casco.
    float hurt = (!s->hit_shield) ? Clamp(s->hit_flash / HIT_TIME, 0.0f, 1.0f) : 0.0f;
    LowPolyPose pose = {
        .pos = pos, .rot = rot, .scale = 1.0f, .sun = sun,
        .sunlight = lowpoly_sunlight(pos, sun, planet->params.radius, SHIP_SHADOW),
        .ambient = SHIP_AMBIENT, .wrap = SHIP_WRAP,
        .whiten = ship_invulnerable(s) ? DASH_WHITEN : hurt * 0.5f,
        .flash = ship_invulnerable(s) ? WHITE : HURT_COLOR,
        .eye = camera.position,
    };
    lowpoly_draw(&model, &pose);

    // Brilhos: somam luz à imagem.
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);

    float size = ENGINE_IDLE + ENGINE_THRUST * s->thrust_blend + ENGINE_BOOST * s->boost_blend;
    Color engine = {
        (unsigned char)Lerp((float)ENGINE_COLOR.r, (float)BOOST_COLOR.r, s->boost_blend),
        (unsigned char)Lerp((float)ENGINE_COLOR.g, (float)BOOST_COLOR.g, s->boost_blend),
        (unsigned char)Lerp((float)ENGINE_COLOR.b, (float)BOOST_COLOR.b, s->boost_blend),
        ENGINE_HALO_ALPHA,
    };
    for (int side = -1; side <= 1; side += 2) {
        Vector3 nozzle = { SHIP_ENGINE_X * (float)side, SHIP_ENGINE_Y, SHIP_ENGINE_Z };
        Vector3 at = Vector3Add(pos, Vector3RotateByQuaternion(nozzle, rot));
        DrawBillboard(camera, glow, at, size * ENGINE_HALO, engine);
        DrawBillboard(camera, glow, at, size, WHITE);
    }

    // Clarão no cano que acabou de atirar (o canhão já trocou: é o outro).
    if (s->muzzle > 0.0f) {
        float fade = s->muzzle / MUZZLE_TIME;
        Vector3 gun = Vector3Add(pos, Vector3RotateByQuaternion(GUNS[s->gun ^ 1], rot));
        Color c = { MUZZLE_COLOR.r, MUZZLE_COLOR.g, MUZZLE_COLOR.b, (unsigned char)(255.0f * fade) };
        DrawBillboard(camera, glow, gun, MUZZLE_SIZE, c);
    }

    // Escudo segurando um golpe: uma bolha azul que acende e apaga.
    if (s->hit_flash > 0.0f && s->hit_shield) {
        float fade = Clamp(s->hit_flash / HIT_TIME, 0.0f, 1.0f);
        Color c = { SHIELD_COLOR.r, SHIELD_COLOR.g, SHIELD_COLOR.b, (unsigned char)(120.0f * fade) };
        DrawBillboard(camera, glow, pos, SHIELD_SIZE, c);
    }
    if (ship_invulnerable(s)) DrawBillboard(camera, glow, pos, DASH_GLOW * 2.0f, DASH_COLOR);

    EndBlendMode();
    rlEnableDepthMask();
}
