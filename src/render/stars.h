// Campo de estrelas.
//
// Todas as estrelas formam UMA malha estática, enviada à placa de vídeo uma
// vez e desenhada em uma única chamada. Cada estrela é um quadradinho com uma
// textura de brilho redondo, colado em uma esfera centrada na câmera: como a
// esfera anda junto com a câmera, as estrelas parecem infinitamente distantes.
// Não usa shader próprio.
#ifndef RENDER_STARS_H
#define RENDER_STARS_H

#include "raylib.h"

#include "game/tuning.h"

typedef struct {
    Mesh     mesh;
    Material material;
    int      count;
    bool     has_mesh;
    bool     has_material;
} Stars;

// Gera (ou regenera, depois de um F5) a malha a partir do tuning.
void stars_build(Stars *s, const Tuning *t);

// Chamar dentro de BeginMode3D, antes de qualquer outro objeto.
void stars_draw(const Stars *s, Vector3 camera_pos, float radius);

// Desenha o Sol no céu: um disco claro e um brilho largo em volta. Como as
// estrelas, fica no fundo; o planeta, desenhado depois, passa na frente.
void stars_draw_sun(const Stars *s, Camera3D camera, const Tuning *t, float radius);

// Textura de brilho redondo, para outros efeitos reutilizarem.
Texture2D stars_glow_texture(const Stars *s);

void stars_unload(Stars *s);

#endif
