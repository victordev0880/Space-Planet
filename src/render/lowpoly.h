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
#ifndef RENDER_LOWPOLY_H
#define RENDER_LOWPOLY_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "core/vmath.h"

#define LOWPOLY_MAX_TRIS  320
#define LOWPOLY_MAX_PARTS 12

typedef struct {
    Vector3 a, b, c;
    uint8_t part;
} LowPolyTri;

typedef struct {
    LowPolyTri tris[LOWPOLY_MAX_TRIS];
    int        count;
    Color      colors[LOWPOLY_MAX_PARTS];
    bool       emissive[LOWPOLY_MAX_PARTS];
} LowPoly;

void lowpoly_clear(LowPoly *m);
void lowpoly_part(LowPoly *m, int part, Color color, bool emissive);

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
} LowPolyPose;

void lowpoly_draw(const LowPoly *m, const LowPolyPose *pose);

// Luz do Sol em um ponto: 1, ou `shadow` se ele está na sombra do planeta
// (o "cilindro" do lado oposto ao Sol).
float lowpoly_sunlight(Vector3 pos, Vector3 sun, float planet_radius, float shadow);

#endif
