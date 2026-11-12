// Loop de passo fixo.
//
// A lógica do jogo sempre avança em fatias iguais de tempo (1/60 s), não
// importa a taxa de quadros da tela. Isso deixa a física idêntica em qualquer
// máquina e torna a simulação reproduzível.
//
// A cada quadro desenhado, o tempo real que passou entra em um "acumulador";
// enquanto houver pelo menos uma fatia inteira lá dentro, roda-se um passo de
// lógica. O que sobra (uma fração de fatia) vira o `alpha`, usado para
// desenhar um ponto intermediário entre o estado anterior e o atual.
#ifndef CORE_LOOP_H
#define CORE_LOOP_H

typedef struct {
    double dt;            // duração de um passo de lógica, em segundos
    double accumulator;   // tempo real ainda não simulado
    int    max_steps;     // teto de passos por quadro
} FixedLoop;

void fixed_loop_init(FixedLoop *l, int hz, int max_steps);

// Soma o tempo do quadro e devolve quantos passos de lógica rodar agora.
// Se o jogo atrasar muito (um travamento), o excedente é descartado em vez de
// tentar "correr atrás" para sempre.
int fixed_loop_advance(FixedLoop *l, double frame_seconds);

// Fração do próximo passo já decorrida, em [0, 1).
double fixed_loop_alpha(const FixedLoop *l);

#endif
