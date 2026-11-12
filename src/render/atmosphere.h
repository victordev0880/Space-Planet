// Atmosfera do planeta, sem shader próprio.
//
// A ideia: o brilho da atmosfera em cada ponto da tela depende de quanto ar a
// linha de visão atravessa. Olhando para o meio do planeta, ela cruza só uma
// camada fina; olhando de raspão pela borda, cruza um caminho longo e o ar
// aparece brilhante; acima da borda o caminho encurta até sumir no topo da
// atmosfera. O resultado é a linha azul colada no contorno do planeta, que
// se apaga para fora.
//
// A malha é uma série de anéis concêntricos em volta do eixo de visão,
// apoiados na face da frente da "casca" de ar. Como o caminho pelo ar depende
// de onde a câmera está, as posições e a transparência dos vértices são
// recalculadas na CPU a cada quadro (cerca de 1500 vértices, custo desprezível).
// Só o lado iluminado pelo Sol brilha.
//
// Limitação conhecida: com a câmera DENTRO da atmosfera nada é desenhado.
// Será tratado no marco 4, quando a nave puder voar baixo.
#ifndef RENDER_ATMOSPHERE_H
#define RENDER_ATMOSPHERE_H

#include "raylib.h"

#include "game/tuning.h"

typedef struct {
    Mesh     mesh;
    Material material;
    bool     has_mesh;
} Atmosphere;

void atmosphere_init(Atmosphere *a);

// Chamar dentro de BeginMode3D, depois do planeta e das luzes de cidades.
// hp: fração de massa restante do planeta (1 = intacto). Conforme cai, a
// atmosfera fica mais fina e avermelhada.
void atmosphere_draw(Atmosphere *a, const Tuning *t, float planet_radius, Vector3 camera_pos, float hp);

void atmosphere_unload(Atmosphere *a);

#endif
