// Modelos facetados ("low poly") montados em código: a nave e os inimigos.
//
// Um modelo é uma lista de triângulos em eixos próprios (frente = -Z,
// direita = +X, topo = +Y), cada um com uma "peça" que define a cor. Quem
// monta descreve só a metade direita; lowpoly_tri acrescenta o espelho.
//
// Desenho: a cada quadro os triângulos são girados para a pose do objeto e
// iluminados na CPU, uma cor por face (o aspecto facetado). Cada face é
// iluminada pelo lado que a câmera enxerga, então a ordem dos vértices não
// importa e peças finas (asas, lâminas) aparecem dos dois lados. Peças
// "emissivas" brilham sozinhas e ignoram a luz. Sem shader: igual com e sem
// --no-shaders. Com algumas centenas de triângulos por modelo e algumas
// dezenas de modelos, o custo é pequeno.
//
// A luz de cada face soma quatro partes:
//   difusa     o Sol, que "dobra" um pouco para as faces de lado (wrap);
//   especular  o reflexo do Sol: faz as faces faiscarem quando a nave vira;
//   borda      um brilho nas faces quase de perfil, que separa o modelo do
//              fundo escuro (rim light);
//   planeta    a luz que o planeta reflete de volta, vinda de baixo.
#ifndef RENDER_LOWPOLY_H
#define RENDER_LOWPOLY_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "core/vmath.h"

#define LOWPOLY_MAX_TRIS  900
#define LOWPOLY_MAX_PARTS 12

typedef struct {
    Vector3 a, b, c;
    uint8_t part;
    uint8_t detail;   // 1 = detalhe pequeno: só desenhado de perto (pose.far = false)
} LowPolyTri;

typedef struct {
    LowPolyTri tris[LOWPOLY_MAX_TRIS];
    int        count;
    bool       overflow;                       // algum triângulo não coube (erro de quem monta)
    uint8_t    detail_mode;                    // os próximos triângulos são detalhe?
    Color      colors[LOWPOLY_MAX_PARTS];
    bool       emissive[LOWPOLY_MAX_PARTS];
    bool       glow_slot[LOWPOLY_MAX_PARTS];   // emissiva que assume a cor `glow` da pose
} LowPoly;

void lowpoly_clear(LowPoly *m);
void lowpoly_part(LowPoly *m, int part, Color color, bool emissive);

// Entre lowpoly_detail(m, true) e lowpoly_detail(m, false), os triângulos
// montados são detalhes (rebites, placas, antenas): somem de longe.
void lowpoly_detail(LowPoly *m, bool on);

// Peça que brilha na cor `glow` da pose (a cor da variação do inimigo, a do
// motor da nave); `color` vale quando a pose não traz cor de brilho.
void lowpoly_part_glow(LowPoly *m, int part, Color color);

// Triângulo e o seu espelho (x negativo). Com `mirror` false, só ele.
void lowpoly_tri(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, int part, bool mirror);
void lowpoly_quad(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, int part, bool mirror);

typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      scale;
    Vector3    sun;        // direção do Sol (comprimento 1)
    float      sunlight;   // 1 = em pleno Sol; menos na sombra do planeta
    float      ambient;    // luz mínima
    float      wrap;       // quanto a luz "dobra" para as faces de lado para o Sol
    float      whiten;     // 0 a 1: clarão (dano, dash)
    Color      flash;      // cor do clarão
    Vector3    eye;        // posição da câmera

    float      specular;   // força do reflexo do Sol (0 = fosco)
    float      shininess;  // maior = reflexo menor e mais concentrado
    float      rim;        // força da luz de borda
    Color      rim_color;
    float      bounce;     // força da luz refletida pelo planeta
    Color      bounce_color;
    Color      tint;       // multiplica as cores das peças comuns (branco = sem efeito)
    Color      glow;       // se alpha > 0, cor das peças marcadas com lowpoly_part_glow
    bool       far;        // longe da câmera: pula os triângulos de detalhe
} LowPolyPose;

// Pose com os valores neutros (sem reflexo, sem borda, sem tinta).
LowPolyPose lowpoly_pose(Vector3 pos, Quaternion rot, float scale, Vector3 sun, Vector3 eye);

void lowpoly_draw(const LowPoly *m, const LowPolyPose *pose);

// Luz do Sol em um ponto: 1, ou `shadow` se ele está na sombra do planeta
// (o "cilindro" do lado oposto ao Sol).
float lowpoly_sunlight(Vector3 pos, Vector3 sun, float planet_radius, float shadow);

// ---------------------------------------------------------------------------
//  Formas prontas, para montar modelos mais ricos com pouco código. Todas em
//  eixos do modelo; com `mirror`, ganham o espelho em x (como lowpoly_tri).
// ---------------------------------------------------------------------------

// Seção transversal de um casco: elipse (polígono) com centro (x, y, z),
// meia-largura w e meia-altura h. w = h = 0 vira uma ponta.
typedef struct { float z, w, h, y; } LpRing;

// Casco ao longo de z, ligando as seções em ordem. As faces de cima ganham
// `top`, as de baixo `bottom`; `cap` (>= 0) fecha a primeira e a última seção.
// `x` desloca o casco para o lado; `twist` gira cada seção um pouco mais que
// a anterior (radianos por seção), para brocas em espiral.
void lp_loft(LowPoly *m, const LpRing *rings, int count, int sides, float x, int top, int bottom, int cap,
             float twist, bool mirror);

// Placa com espessura (asa, aleta, lâmina): contorno a (raiz, frente), b
// (ponta, frente), c (ponta, trás), d (raiz, trás). A espessura vai na
// direção `normal`, `thick_root` na raiz e `thick_tip` na ponta.
void lp_plate(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 normal, float thick_root,
              float thick_tip, int top, int bottom, int edge, bool mirror);

// Tubo (ou cone) de `a` até `b`, raios `ra` e `rb`. `cap` >= 0 fecha as pontas.
void lp_tube(LowPoly *m, Vector3 a, Vector3 b, float ra, float rb, int sides, int part, int cap, bool mirror);

// Caixa alinhada aos eixos.
void lp_box(LowPoly *m, Vector3 center, Vector3 half, int part, bool mirror);

// Espinho: cone de base `radius` em `base`, até `tip`.
void lp_spike(LowPoly *m, Vector3 base, Vector3 tip, float radius, int sides, int part, bool mirror);

// Anel com espessura em volta de `axis`: faces de frente e de trás em
// `face`, bordas em `edge`.
void lp_ring(LowPoly *m, Vector3 center, Vector3 axis, float r_in, float r_out, float depth, int sides, int face,
             int edge, bool mirror);

// Gira (e desloca) os triângulos montados desde `first`: para montar uma peça
// em eixos cômodos e depois pô-la no lugar.
void lp_transform(LowPoly *m, int first, Quaternion rot, Vector3 offset);

#endif
