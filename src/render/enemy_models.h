// Modelos dos inimigos: três desenhos por tipo, montados em código.
//
// Cada tipo tem uma silhueta reconhecível (o jogador precisa bater o olho e
// saber o que é), e cada um dos três desenhos varia a forma dentro dela: o
// Enxame pode ser uma vespa, uma lâmina em crescente ou um tridente; o
// Bombardeiro, uma arraia, um tubarão-martelo ou um besouro; e assim por
// diante. A variação do inimigo (Enemy.variant) escolhe o desenho e a
// paleta. Os pontos que a lógica usa (ponto fraco do Bombardeiro, cano do
// Atirador, hangar do Porta-drones) ficam no mesmo lugar nos três.
#ifndef RENDER_ENEMY_MODELS_H
#define RENDER_ENEMY_MODELS_H

#include <stdbool.h>

#include "game/enemy_types.h"
#include "game/enemies.h"
#include "render/lowpoly.h"

#define MODEL_LIGHTS_MAX 6

// Peças comuns a todos os modelos de inimigo (a cor vem da paleta do tipo).
enum {
    P_HULL,     // casco
    P_HULL2,    // placas claras
    P_DARK,     // barriga e metal escuro
    P_ACCENT,   // detalhes de contraste (lâminas, faixas, espinhos)
    P_METAL,    // metal cinza (motores, canos)
    P_GLASS,    // vidro escuro
    P_GLOW,     // aceso, na cor da variação
    P_HOT,      // aceso, quase branco
    P_CORE,     // aceso, cor própria do tipo (o ponto fraco do Bombardeiro)
};

// Brilho (billboard) preso ao modelo: motores, olhos, emissores.
typedef struct {
    Vector3 pos;    // em eixos do modelo (vezes a escala do inimigo)
    float   size;   // em unidades do modelo
} ModelLight;

typedef struct {
    LowPoly    mesh;
    ModelLight lights[MODEL_LIGHTS_MAX];
    int        light_count;
    Vector3    beam;    // Sifão: de onde sai o feixe
    bool       spins;   // gira em volta de z (a broca cavando, a estrela do Kamikaze)
} EnemyModel;

// O modelo de um tipo e desenho (0 a ENEMY_VARIANTS - 1). Monta todos na
// primeira chamada.
const EnemyModel *enemy_model(EnemyType type, int variant);

// Quantos triângulos os modelos montados têm, no total e no maior (para o
// relatório e para conferir se nenhum estourou o limite).
int enemy_models_triangles(int *largest, bool *overflow);

#endif
