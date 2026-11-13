// Camada de input.
//
// A lógica do jogo nunca pergunta ao raylib "a tecla W está apertada?". Ela
// enxerga só AÇÕES (acelerar, atirar, boost...) em um InputState. Quem
// preenche esse estado pode ser o teclado e o mouse reais, um roteiro de
// autoplay ou, mais tarde, um gamepad.
//
// Fluxo:  uma amostra (InputSample) por quadro desenhado -> input_feed
//         um estado  (InputState)  por passo de lógica   -> input_tick
// As duas taxas são diferentes, então o InputTracker faz a ponte sem perder
// toques rápidos nem aplicar o mesmo movimento de mouse duas vezes.
#ifndef CORE_INPUT_H
#define CORE_INPUT_H

#include <stdint.h>

typedef enum {
    ACT_FORWARD,
    ACT_BACK,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_ROLL_LEFT,
    ACT_ROLL_RIGHT,
    ACT_BOOST,
    ACT_DASH,
    ACT_DRIFT,
    ACT_FIRE,
    ACT_ALT_FIRE,
    ACT_OVERLAY,     // F1
    ACT_FREECAM,     // F2
    ACT_WIREFRAME,   // F3
    ACT_RELOAD,      // F5
    ACT_RESET,       // R: nova partida depois do fim; com o F1 aberto, restaura o planeta
    ACT_DEBUG_CRATER,  // C (com o F1 aberto): abre uma cratera onde a nave mira
    ACT_DEBUG_SIZE,    // V (com o F1 aberto): troca o tamanho dessa cratera
    ACT_READY,         // Enter: na calmaria, chama a próxima horda
    ACT_COUNT
} Action;

#define ACT_BIT(action) (1u << (action))

// Estado bruto em um instante: o que está apertado e quanto o mouse andou
// desde a amostra anterior.
typedef struct {
    uint32_t held;       // máscara de ações apertadas
    float    look_dx;    // movimento do mouse, em pixels
    float    look_dy;
} InputSample;

// O que um passo de lógica enxerga.
typedef struct {
    uint32_t held;       // ações apertadas neste passo
    uint32_t pressed;    // ações que acabaram de ser apertadas
    uint32_t released;   // ações que acabaram de ser soltas
    float    look_dx;    // movimento do mouse acumulado desde o passo anterior
    float    look_dy;
} InputState;

typedef struct {
    uint32_t held_now;      // última amostra recebida
    uint32_t seen;          // tudo que esteve apertado desde o último passo
    uint32_t held_at_tick;  // o que o passo anterior enxergou como apertado
    float    pending_dx;
    float    pending_dy;
} InputTracker;

void       input_init(InputTracker *t);
void       input_feed(InputTracker *t, const InputSample *sample);
InputState input_tick(InputTracker *t);

const char *action_name(Action a);
int         action_from_name(const char *name);   // -1 se o nome não existe

#endif
