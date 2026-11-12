// Overlay de debug (F1): FPS, tempos de quadro, estado da simulação e um
// gráfico dos últimos quadros. Também mostra avisos (por exemplo, o resultado
// de recarregar o tuning com F5).
#ifndef UI_DEBUG_OVERLAY_H
#define UI_DEBUG_OVERLAY_H

#include "raylib.h"

#include <stdint.h>

#include "game/world.h"

#define OVERLAY_HISTORY 120

// Coluna da tela onde termina o painel do overlay (para o HUD desviar dele).
#define DEBUG_OVERLAY_RIGHT 652

typedef struct {
    bool     visible;
    float    frame_ms[OVERLAY_HISTORY];   // buffer circular dos últimos quadros
    float    cpu_ms[OVERLAY_HISTORY];
    int      head;
    int      filled;

    char     notice[160];
    bool     notice_is_error;             // erros ficam na tela até serem resolvidos
    uint64_t notice_until_tick;
} DebugOverlay;

// O que o overlay mostra além do próprio mundo.
typedef struct {
    int         steps;             // passos de lógica rodados neste quadro
    float       alpha;
    int         star_count;
    int         city_light_count;
    int         planet_meshes;     // chunks com malha
    int         planet_chunks;     // chunks no total
    int         planet_vertices;
    int         planet_triangles;
    int         remeshed;          // chunks remalhados neste quadro
    int         remesh_backlog;    // chunks esperando para ser remalhados
    float       remesh_ms;         // tempo gasto remalhando neste quadro
    int         particles;         // partículas vivas
    bool        shaded;            // desenhando com o shader do planeta
    uint64_t    seed;
    const char *autoplay_script;   // NULL fora do autoplay
    int         autoplay_frame;
    int         autoplay_frames;
} DebugInfo;

void debug_overlay_init(DebugOverlay *o);
void debug_overlay_record_frame(DebugOverlay *o, float frame_ms, float cpu_ms);

// Aviso temporário (some no tick indicado) ou erro (fica até ser limpo).
void debug_overlay_notify(DebugOverlay *o, const char *text, uint64_t until_tick);
void debug_overlay_error(DebugOverlay *o, const char *text);
void debug_overlay_clear_error(DebugOverlay *o);

void debug_overlay_draw(const DebugOverlay *o, const World *w, const DebugInfo *info);

#endif
