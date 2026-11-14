// Lockstep: o coração do modo online (marco 12).
//
// A simulação é determinística: o mesmo estado e os mesmos comandos dão o
// mesmo resultado em qualquer computador. Então, em vez de mandar a posição
// de cada nave e de cada inimigo pela rede, os jogadores trocam só os
// COMANDOS (o InputState de cada passo, 20 bytes) e cada computador roda a
// partida inteira sozinho. Para isso, ninguém pode avançar o passo T antes de
// ter os comandos de todos para o passo T.
//
// Para a espera não travar o jogo, cada um manda o seu comando com alguns
// passos de antecedência (NET_DELAY): o que o jogador aperta agora vale daqui
// a ~67 ms. Em jogo cooperativo isso quase não se sente.
//
// Este módulo é só a contabilidade: guarda os comandos por passo e por
// jogador, diz quando o próximo passo está completo, e monta/lê os pacotes.
// Não abre sockets (net/udp.c) nem sabe quem é anfitrião (net/session.c).
#ifndef NET_LOCKSTEP_H
#define NET_LOCKSTEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/input.h"

#define NET_MAX_PLAYERS 4
#define NET_DELAY       4      // passos de antecedência dos comandos (~67 ms a 60 Hz)
#define NET_WINDOW      128    // passos guardados à frente (anel)
#define NET_REDUNDANCY  8      // cada pacote repete os últimos passos: um pacote perdido não trava ninguém
#define NET_PACKET_MAX  1200   // bytes; abaixo do MTU de qualquer rede

typedef struct {
    int        players;                                  // jogadores na partida
    uint32_t   next;                                     // próximo passo a simular
    InputState inputs[NET_WINDOW][NET_MAX_PLAYERS];
    uint32_t   tick_of[NET_WINDOW];                      // de que passo é cada posição do anel
    uint8_t    have[NET_WINDOW];                         // um bit por jogador: comando já chegou
} Lockstep;

// Começa no passo 0. Os primeiros NET_DELAY passos já vêm com comandos vazios
// (ninguém teve tempo de mandar nada para eles).
void lockstep_init(Lockstep *ls, int players);

// Guarda o comando do jogador `player` para o passo `tick`. Ignora passos já
// simulados, fora da janela, ou repetidos (a redundância manda o mesmo várias vezes).
void lockstep_put(Lockstep *ls, uint32_t tick, int player, const InputState *in);

// O comando já chegou?
bool lockstep_has(const Lockstep *ls, uint32_t tick, int player);

// O comando guardado (vazio se não chegou).
InputState lockstep_get(const Lockstep *ls, uint32_t tick, int player);

// Todos os comandos do próximo passo chegaram?
bool lockstep_ready(const Lockstep *ls);

// Entrega os comandos do próximo passo (um por jogador) e avança.
uint32_t lockstep_take(Lockstep *ls, InputState out[NET_MAX_PLAYERS]);

// Um jogador saiu: dali em diante os comandos dele valem vazio (a nave
// fica parada), para os outros não travarem esperando.
void lockstep_drop(Lockstep *ls, int player);

// --- pacotes ----------------------------------------------------------------
// Todos começam com "TF" e o tipo. Inteiros em little-endian.

typedef enum {
    NET_HELLO = 1,   // cliente -> anfitrião: quero entrar (versão)
    NET_WELCOME,     // anfitrião -> cliente: você é o jogador N
    NET_LOBBY,       // anfitrião -> todos: quantos jogadores há
    NET_START,       // anfitrião -> todos: a partida começou (seed, jogadores)
    NET_INPUT,       // cliente -> anfitrião: os meus comandos (passo inicial, quantos)
    NET_BUNDLE,      // anfitrião -> todos: os comandos de todos (passo inicial, quantos)
    NET_HASH,        // cliente -> anfitrião: o estado no passo T (para achar dessincronia)
    NET_BYE,         // saí (ou: a sala fechou)
    NET_FULL,        // anfitrião -> cliente: sala cheia ou partida já começou
} NetType;

#define NET_VERSION 1

typedef struct {
    uint8_t  type;
    uint8_t  player;     // quem manda (ou, no WELCOME, o número dado ao cliente)
    uint8_t  players;    // LOBBY, START, BUNDLE: quantos jogadores
    uint8_t  count;      // INPUT, BUNDLE: quantos passos
    uint32_t tick;       // INPUT, BUNDLE: o primeiro passo; HASH: o passo
    uint64_t value;      // START: a seed; HASH: o hash; HELLO: a versão
    InputState inputs[NET_REDUNDANCY * NET_MAX_PLAYERS];   // INPUT: [passo]; BUNDLE: [passo * players + jogador]
} NetMessage;

// Monta o pacote em `out`; devolve o tamanho (0 se não coube).
size_t net_encode(const NetMessage *m, uint8_t *out, size_t capacity);

// Lê um pacote; false se não é nosso ou está corrompido.
bool net_decode(const uint8_t *data, size_t size, NetMessage *m);

#endif
