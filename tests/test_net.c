// Testes do marco 12: o lockstep do modo online (sem rede: só a contabilidade
// e os pacotes).
#include "test.h"
#include "test_util.h"

#include <string.h>

#include "net/lockstep.h"

static InputState key(uint32_t held, float dx)
{
    InputState in = { held, held, 0u, dx, -dx };
    return in;
}

// Os primeiros passos já vêm vazios; depois, o passo só avança com o comando
// de todos, e cada um recebe o seu de volta.
static void lockstep_waits_for_everyone(void)
{
    static Lockstep ls;
    lockstep_init(&ls, 2);
    InputState out[NET_MAX_PLAYERS];
    for (int t = 0; t < NET_DELAY; t++) {
        CHECK(lockstep_ready(&ls));
        CHECK(lockstep_take(&ls, out) == (uint32_t)t);
        CHECK(out[0].held == 0u && out[1].held == 0u);
    }
    CHECK(!lockstep_ready(&ls));   // ninguém mandou o passo NET_DELAY ainda
    InputState a = key(1u, 2.5f), b = key(4u, -1.0f);
    lockstep_put(&ls, NET_DELAY, 0, &a);
    CHECK(!lockstep_ready(&ls));   // falta o jogador 1
    lockstep_put(&ls, NET_DELAY, 1, &b);
    lockstep_put(&ls, NET_DELAY, 1, &a);   // repetido: fica o primeiro
    CHECK(lockstep_ready(&ls));
    lockstep_take(&ls, out);
    CHECK(out[0].held == 1u && out[0].look_dx == 2.5f);
    CHECK(out[1].held == 4u && out[1].look_dx == -1.0f);

    // Passos já simulados e longe demais no futuro são ignorados.
    lockstep_put(&ls, 0, 0, &a);
    CHECK(!lockstep_has(&ls, 0, 0));
    lockstep_put(&ls, ls.next + NET_WINDOW + 5, 0, &a);
    CHECK(!lockstep_has(&ls, ls.next + NET_WINDOW + 5, 0));
}

// Um jogador que sai deixa de travar os outros (os comandos dele valem vazio).
static void lockstep_dropped_player_does_not_block(void)
{
    static Lockstep ls;
    lockstep_init(&ls, 3);
    InputState out[NET_MAX_PLAYERS];
    for (int t = 0; t < NET_DELAY; t++) lockstep_take(&ls, out);
    InputState a = key(2u, 0.0f);
    lockstep_put(&ls, NET_DELAY, 0, &a);
    lockstep_put(&ls, NET_DELAY, 1, &a);
    CHECK(!lockstep_ready(&ls));
    lockstep_drop(&ls, 2);
    CHECK(lockstep_ready(&ls));
    lockstep_take(&ls, out);
    CHECK(out[2].held == 0u && out[0].held == 2u);
}

// O anel dá a volta sem misturar passos antigos com novos.
static void lockstep_ring_wraps(void)
{
    static Lockstep ls;
    lockstep_init(&ls, 1);
    InputState out[NET_MAX_PLAYERS];
    for (uint32_t t = 0; t < NET_WINDOW * 3; t++) {
        InputState in = key(t + 1u, (float)t);
        if (t >= NET_DELAY) lockstep_put(&ls, t, 0, &in);
        CHECK(lockstep_ready(&ls));
        uint32_t got = lockstep_take(&ls, out);
        CHECK(got == t);
        if (t >= NET_DELAY) CHECK(out[0].held == t + 1u);
    }
}

// Os pacotes vão e voltam iguais, e lixo é recusado.
static void net_packets_round_trip(void)
{
    static NetMessage m, back;
    memset(&m, 0, sizeof m);
    m.type = NET_BUNDLE;
    m.player = 0;
    m.players = 3;
    m.count = NET_REDUNDANCY;
    m.tick = 123456u;
    m.value = 0x0123456789abcdefull;
    for (int i = 0; i < m.count * m.players; i++) m.inputs[i] = key((uint32_t)i * 3u + 1u, (float)i * 0.25f);
    uint8_t buffer[NET_PACKET_MAX];
    size_t size = net_encode(&m, buffer, sizeof buffer);
    CHECK(size > 0 && size <= NET_PACKET_MAX);
    CHECK(net_decode(buffer, size, &back));
    CHECK(back.type == NET_BUNDLE && back.players == 3 && back.count == NET_REDUNDANCY && back.tick == 123456u);
    CHECK(back.value == m.value);
    for (int i = 0; i < m.count * m.players; i++) {
        CHECK(back.inputs[i].held == m.inputs[i].held && back.inputs[i].look_dx == m.inputs[i].look_dx);
    }
    CHECK(!net_decode(buffer, size - 1, &back));   // cortado
    buffer[0] = 'X';
    CHECK(!net_decode(buffer, size, &back));       // não é nosso
    uint8_t tiny[4];
    CHECK(net_encode(&m, tiny, sizeof tiny) == 0); // não cabe
}

void suite_net(void)
{
    RUN(lockstep_waits_for_everyone);
    RUN(lockstep_dropped_player_does_not_block);
    RUN(lockstep_ring_wraps);
    RUN(net_packets_round_trip);
}
