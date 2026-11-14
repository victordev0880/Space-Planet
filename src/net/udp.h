// Socket UDP mínimo, sem bloquear (Winsock no Windows, BSD no resto).
//
// UDP porque o lockstep não precisa de entrega garantida: cada pacote repete
// os últimos passos, então um pacote perdido é coberto pelo seguinte, sem a
// espera de reenvio que o TCP faria.
#ifndef NET_UDP_H
#define NET_UDP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_DEFAULT_PORT 27015

typedef struct {
    uint32_t ip;     // em ordem de rede
    uint16_t port;   // em ordem de rede
} NetAddress;

typedef struct {
    bool     open;
    intptr_t handle;
    int      drop_every;   // teste: joga fora um pacote a cada tantos enviados (0 = nenhum)
    int      sent;
} UdpSocket;

// Abre e prende numa porta (0 = qualquer uma livre).
bool udp_open(UdpSocket *s, uint16_t port);
void udp_close(UdpSocket *s);

bool udp_send(UdpSocket *s, NetAddress to, const void *data, size_t size);

// Lê um pacote, se houver; devolve o tamanho (0 = nada chegou).
size_t udp_recv(UdpSocket *s, NetAddress *from, void *data, size_t capacity);

// "192.168.0.10" ou "192.168.0.10:27015" -> endereço. false se inválido.
bool udp_parse(const char *text, NetAddress *out);

// Endereço -> "192.168.0.10:27015".
const char *udp_format(NetAddress a);

bool udp_same(NetAddress a, NetAddress b);

// Teste: os próximos sockets só escutam no 127.0.0.1 (sem acionar o firewall).
void udp_loopback_only(bool on);

// Os IPs desta máquina (para o anfitrião passar aos amigos). Devolve quantos.
int udp_local_ips(char out[][24], int max);

#endif
