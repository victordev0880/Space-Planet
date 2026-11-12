// Luzes de cidades no lado noturno do planeta.
//
// Cada luz é um quadradinho brilhante deitado sobre o solo. Todas formam uma
// única malha, desenhada em uma chamada com mistura aditiva. As luzes são
// sorteadas em aglomerados ("metrópoles") sobre terra habitável, e só existem
// onde é noite: como o Sol é fixo, isso é decidido uma vez, ao gerar a malha.
#ifndef RENDER_CITY_LIGHTS_H
#define RENDER_CITY_LIGHTS_H

#include "raylib.h"

#include "game/tuning.h"
#include "voxel/planet.h"

typedef struct {
    Mesh     mesh;
    Material material;
    float   *ground;        // 3 por luz: o ponto do solo em que ela está apoiada
    int      count;
    int      lit;           // quantas ainda estão acesas
    bool     has_mesh;
    bool     has_material;
} CityLights;

// `glow` é a textura de brilho redondo (a mesma das estrelas). Ela continua
// pertencendo a quem a criou.
void city_lights_build(CityLights *c, const Planet *p, const Tuning *t, Texture2D glow);

// Depois de um impacto em `pos`: apaga as luzes cujo chão foi destruído.
void city_lights_impact(CityLights *c, const Planet *p, Vector3 pos, float radius);

// Chamar dentro de BeginMode3D, depois do planeta.
void city_lights_draw(const CityLights *c);

void city_lights_unload(CityLights *c);

#endif
