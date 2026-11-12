// Desenho da cena 3D. Lê o estado do mundo, nunca o altera.
#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H

#include "raylib.h"

#include "game/tuning.h"
#include "game/world.h"
#include "render/atmosphere.h"
#include "render/city_lights.h"
#include "render/planet_render.h"
#include "render/stars.h"

typedef struct {
    Stars        stars;
    PlanetRender planet;
    CityLights   cities;
    Atmosphere   atmosphere;
    bool         wireframe;   // F3: desenha só as arestas dos triângulos do planeta

    // Reação aos impactos no planeta.
    uint32_t     impacts_seen;   // até qual impacto do mundo já reagimos
    float        shake;          // tremor atual da câmera, de 0 a 1
    float        last_time;      // tempo de jogo do quadro anterior
} Renderer;

// Retorna false se faltar memória para as malhas. use_shaders = false é o
// modo de --no-shaders.
bool render_init(Renderer *r, const World *w, const Tuning *t, bool use_shaders);

// Refaz o que depende do tuning ou do planeta (depois de um F5 bem-sucedido,
// ou quando o planeta é restaurado de uma vez).
void render_apply_tuning(Renderer *r, const World *w, const Tuning *t);

// Câmera do mundo para um quadro, sem o tremor. Serve também para converter
// um ponto da tela em um raio (o clique que abre crateras).
Camera3D render_camera(const World *w, const Tuning *t, float alpha);

// Desenha a cena. Chamar entre BeginDrawing e EndDrawing.
// alpha: fração entre o passo de lógica anterior e o atual (0 a 1).
// debug: desenha também os eixos de referência do mundo.
void render_scene(Renderer *r, const World *w, const Tuning *t, float alpha, bool debug);

void render_shutdown(Renderer *r);

#endif
