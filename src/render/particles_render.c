#include "raylib.h"
#include "rlgl.h"

#include "render/particles_render.h"

#include <math.h>

#include "core/vmath.h"

#define DUST_OPACITY   0.26f   // transparência máxima de uma nuvem de poeira
#define DUST_GROWTH    1.6f    // quanto a nuvem cresce ao longo da vida
#define DUST_FADE_IN   0.12f   // fração da vida que a poeira leva para aparecer
#define NIGHT_SHADE    0.10f   // brilho mínimo de pedras e poeira no lado da noite
#define TRAIL_SHRINK   0.6f    // quanto o rastro de motor encolhe até sumir
#define TRAIL_OPACITY  0.40f   // brilho de cada ponto do rastro
#define RING_SIZE      64      // lado da textura do anel, em pixels
#define RING_PEAK      0.82f   // raio do anel na textura (1 = borda)
#define RING_WIDTH     0.10f   // espessura do anel na textura

static Texture2D ring_texture;
static bool      ring_ready;

// Anel de borda suave: brilho máximo em RING_PEAK, apagando para os dois lados.
Texture2D particles_ring_texture(void)
{
    if (ring_ready) return ring_texture;
    unsigned char *pixels = MemAlloc(RING_SIZE * RING_SIZE * 4);
    for (int y = 0; y < RING_SIZE; y++) {
        for (int x = 0; x < RING_SIZE; x++) {
            float dx = ((float)x + 0.5f) / RING_SIZE * 2.0f - 1.0f;
            float dy = ((float)y + 0.5f) / RING_SIZE * 2.0f - 1.0f;
            float off = (sqrtf(dx * dx + dy * dy) - RING_PEAK) / RING_WIDTH;
            float a = expf(-off * off);
            unsigned char *px = &pixels[(y * RING_SIZE + x) * 4];
            px[0] = px[1] = px[2] = 255;
            px[3] = (unsigned char)(a * 255.0f);
        }
    }
    Image image = { pixels, RING_SIZE, RING_SIZE, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    ring_texture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(ring_texture, TEXTURE_FILTER_BILINEAR);
    ring_ready = true;
    return ring_texture;
}

void particles_render_unload(void)
{
    if (ring_ready) UnloadTexture(ring_texture);
    ring_ready = false;
}

typedef struct {
    Vector3 right;   // eixos da tela, no mundo
    Vector3 up;
    Vector3 sun;
    float   back;    // quanto recuar cada partícula ao longo da velocidade (interpolação)
} DrawContext;

static void emit_quad(const DrawContext *ctx, const Particle *p, float size, Color color)
{
    // A simulação está um pouco à frente do que deve aparecer neste quadro:
    // recua a partícula ao longo da própria velocidade.
    Vector3 center = Vector3Subtract(p->pos, Vector3Scale(p->vel, ctx->back));
    Vector3 r = Vector3Scale(ctx->right, size * 0.5f);
    Vector3 u = Vector3Scale(ctx->up, size * 0.5f);

    Vector3 bl = Vector3Subtract(Vector3Subtract(center, r), u);
    Vector3 br = Vector3Subtract(Vector3Add(center, r), u);
    Vector3 tr = Vector3Add(Vector3Add(center, r), u);
    Vector3 tl = Vector3Add(Vector3Subtract(center, r), u);

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(bl.x, bl.y, bl.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(br.x, br.y, br.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(tr.x, tr.y, tr.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(tl.x, tl.y, tl.z);
}

// Pedras e poeira refletem a luz do Sol: claras de dia, quase pretas à noite.
static Color sunlit(const DrawContext *ctx, const Particle *p, float opacity)
{
    float facing = Vector3DotProduct(Vector3Normalize(p->pos), ctx->sun);
    float light = Clamp(facing * 1.5f + 0.5f, NIGHT_SHADE, 1.0f);
    return (Color){
        (unsigned char)((float)p->r * light),
        (unsigned char)((float)p->g * light),
        (unsigned char)((float)p->b * light),
        (unsigned char)(Clamp(opacity, 0.0f, 1.0f) * 255.0f),
    };
}

void particles_draw(const Particles *ps, Camera3D camera, Texture2D glow, const Tuning *t, float alpha)
{
    if (ps->count <= 0) return;

    DrawContext ctx;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    ctx.right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    ctx.up = Vector3CrossProduct(ctx.right, forward);
    ctx.sun = tuning_sun_direction(t);
    ctx.back = (1.0f - alpha) / (float)t->sim_hz;

    rlDrawRenderBatchActive();
    rlDisableDepthMask();   // testam a profundidade, mas não a escrevem

    // Primeira passada: pedras e poeira, com transparência comum.
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < ps->count; i++) {
        const Particle *p = &ps->items[i];
        float life = p->age / p->life;   // 0 ao nascer, 1 ao morrer
        if (p->kind == PARTICLE_ROCK) {
            // Some nos últimos instantes, em vez de desaparecer de repente.
            emit_quad(&ctx, p, p->size, sunlit(&ctx, p, (1.0f - life) * 6.0f));
        } else if (p->kind == PARTICLE_DUST) {
            float fade_in = Clamp(life / DUST_FADE_IN, 0.0f, 1.0f);
            float opacity = DUST_OPACITY * fade_in * (1.0f - life);
            emit_quad(&ctx, p, p->size * (1.0f + DUST_GROWTH * life), sunlit(&ctx, p, opacity));
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();

    // Segunda passada: brasas e clarões somam luz à imagem.
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(glow.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < ps->count; i++) {
        const Particle *p = &ps->items[i];
        float life = p->age / p->life;
        if (p->kind == PARTICLE_EMBER) {
            Color color = { p->r, p->g, p->b, (unsigned char)((1.0f - life * life) * 255.0f) };
            emit_quad(&ctx, p, p->size, color);
        } else if (p->kind == PARTICLE_FLASH) {
            float fade = (1.0f - life) * (1.0f - life);
            Color color = { p->r, p->g, p->b, (unsigned char)(fade * 255.0f) };
            emit_quad(&ctx, p, p->size * (0.55f + 0.45f * life), color);
        } else if (p->kind == PARTICLE_TRAIL) {
            // Rastro de motor: apaga e encolhe ao longo da vida. Os pontos se
            // sobrepõem e somam luz, então cada um é fraco.
            Color color = { p->r, p->g, p->b, (unsigned char)((1.0f - life) * TRAIL_OPACITY * 255.0f) };
            emit_quad(&ctx, p, p->size * (1.0f - TRAIL_SHRINK * life), color);
        }
    }
    rlEnd();
    rlSetTexture(0);

    // Terceira passada: ondas de choque, com a textura de anel. Crescem
    // depressa no começo e vão apagando.
    rlDrawRenderBatchActive();
    rlSetTexture(particles_ring_texture().id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < ps->count; i++) {
        const Particle *p = &ps->items[i];
        if (p->kind != PARTICLE_RING) continue;
        float life = p->age / p->life;
        float grow = 1.0f - (1.0f - life) * (1.0f - life);
        float fade = (1.0f - life) * (1.0f - life);
        emit_quad(&ctx, p, p->size * (0.2f + 0.8f * grow), (Color){ p->r, p->g, p->b, (unsigned char)(fade * 255.0f) });
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();   // envia a passada para a placa antes de restaurar os estados

    rlEnableDepthMask();
}
