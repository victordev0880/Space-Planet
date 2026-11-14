// Sessão online: sala, entrada, troca de comandos e conferência (marco 12).
//
// Estrela com o anfitrião no meio: cada cliente manda os seus comandos ao
// anfitrião; ele junta os de todos para cada passo e manda o pacote completo
// a todos (ele mesmo também joga). Cada computador roda a partida inteira com
// esses comandos (ver net/lockstep.h).
//
//   anfitrião: session_host -> LOBBY (clientes entram) -> session_start -> PLAYING
//   cliente:   session_join -> JOINING -> LOBBY (recebeu o número) -> PLAYING (o anfitrião começou)
//
// De vez em quando os clientes mandam o hash do estado; se não bater com o do
// anfitrião, a sessão marca `desync` (um aviso na tela: as partidas
// divergiram). Quem fica 8 s sem dar notícia cai: o anfitrião larga o jogador
// (a nave dele fica parada) e o cliente que perde o anfitrião volta ao título.
#ifndef NET_SESSION_H
#define NET_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "net/lockstep.h"
#include "net/udp.h"

typedef enum {
    SESSION_OFF,
    SESSION_JOINING,   // cliente: pedindo para entrar
    SESSION_LOBBY,     // na sala, esperando o anfitrião começar
    SESSION_PLAYING,
    SESSION_CLOSED,    // acabou (ver `status`)
} SessionPhase;

#define SESSION_HASHES 16

typedef struct {
    UdpSocket    sock;
    bool         host;
    SessionPhase phase;
    int          local;                        // o meu número de jogador
    int          players;                      // quantos há na sala / na partida
    NetAddress   peer[NET_MAX_PLAYERS];        // anfitrião: o endereço de cada cliente; cliente: peer[0] = anfitrião
    bool         joined[NET_MAX_PLAYERS];      // anfitrião: o jogador está na sala / na partida
    bool         started[NET_MAX_PLAYERS];     // anfitrião: o cliente já mandou comandos (recebeu o início)
    double       heard[NET_MAX_PLAYERS];       // quando chegou o último pacote de cada um
    double       sent_at;                      // quando mandei o último pacote de manutenção
    double       started_at;
    uint64_t     seed;

    Lockstep     ls;
    uint32_t     local_next;                   // o próximo passo para o qual eu mando comando
    InputState   mine[NET_WINDOW];             // os meus comandos (redundância)
    uint32_t     bundled;                      // anfitrião: passos já completos (de 0 a bundled - 1)
    InputState   bundle[NET_WINDOW][NET_MAX_PLAYERS];   // anfitrião: os completos (para repetir)

    uint32_t     hash_tick[SESSION_HASHES];    // anfitrião: os meus hashes, para comparar
    uint64_t     hash_value[SESSION_HASHES];
    bool         hash_has[SESSION_HASHES];
    uint32_t     peer_tick[NET_MAX_PLAYERS][SESSION_HASHES];    // anfitrião: os hashes que os clientes mandaram
    uint64_t     peer_value[NET_MAX_PLAYERS][SESSION_HASHES];
    bool         peer_has[NET_MAX_PLAYERS][SESSION_HASHES];
    bool         desync;
    char         status[96];                   // o que mostrar na tela
} NetSession;

// Abre uma sala na porta `port` (0 = a padrão). false se a porta está ocupada.
bool session_host(NetSession *s, uint16_t port);

// Pede para entrar na sala de `host`.
bool session_join(NetSession *s, NetAddress host);

// Recebe e responde o que chegou, e manda o que é periódico. Chamar a cada quadro.
void session_poll(NetSession *s, double now);

// Anfitrião: começa a partida com quem está na sala.
void session_start(NetSession *s, uint64_t seed, double now);

// O meu comando para o próximo passo que ainda falta mandar. Só aceita se não
// estiver adiantado demais (devolve false: espere o próximo quadro).
bool session_push(NetSession *s, const InputState *in, double now);

// Os comandos de todos para o próximo passo já chegaram?
bool session_ready(const NetSession *s);

// Quantos passos seguidos, a partir do próximo, já estão completos.
int session_backlog(const NetSession *s);

// Entrega os comandos do próximo passo e avança.
uint32_t session_take(NetSession *s, InputState out[NET_MAX_PLAYERS]);

// O estado no passo `tick` (para achar dessincronia; a cada 2 s basta).
void session_report_hash(NetSession *s, uint32_t tick, uint64_t hash);

// Sai (avisa os outros) e fecha.
void session_close(NetSession *s);

#endif
