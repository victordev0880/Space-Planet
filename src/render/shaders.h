// Carregamento dos shaders GLSL do jogo (arquivos em assets/shaders/).
//
// Um shader é um pequeno programa que roda na placa de vídeo: o "de vértice"
// decide onde cada vértice aparece na tela e o "de fragmento" decide a cor de
// cada pixel. Sem shader próprio, o raylib só sabe pintar a cor do vértice.
//
// Regra do projeto: o jogo precisa funcionar sem eles (--no-shaders). Por
// isso carregar pode falhar sem derrubar o jogo: quem usa o shader confere
// `ready` e, se for false, segue pelo caminho sem shader.
#ifndef RENDER_SHADERS_H
#define RENDER_SHADERS_H

#include "raylib.h"

#include "game/tuning.h"

typedef struct {
    Shader shader;
    bool   ready;

    // Posições das variáveis ("uniforms") dentro do shader.
    int loc_sun_dir;
    int loc_sun_color;
    int loc_ambient;
    int loc_sun_wrap;
    int loc_camera;
    int loc_time;
    int loc_detail_scale;
    int loc_detail_strength;
    int loc_bump_strength;
    int loc_spec_strength;
    int loc_spec_power;
    int loc_pulse_amount;
    int loc_pulse_speed;
    int loc_cavity_darkness;
} PlanetShader;

// Lê e compila assets/shaders/planet.vs e planet.fs. Retorna false (e deixa
// ready = false) se os arquivos não existirem ou não compilarem.
bool planet_shader_load(PlanetShader *s);
void planet_shader_unload(PlanetShader *s);

// Ativa o shader e envia os valores que mudam a cada quadro. Chamar antes de
// desenhar os chunks; o chamador ainda precisa enviar a matriz mvp.
void planet_shader_begin(const PlanetShader *s, const Tuning *t, Vector3 camera_pos, float time);

#endif
