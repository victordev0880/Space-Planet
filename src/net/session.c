#include "net/session.h"

#include <stdio.h>
#include <string.h>

#define KEEPALIVE   0.25    // segundos entre pacotes de manutenção (pedido de entrada, sala, início)
#define TIMEOUT     8.0     // sem notícia por este tanto, o outro caiu
#define HASH_EVERY  120u    // passos entre conferências de hash

static void send_msg(NetSession *s, NetAddress to, const NetMessage *m)
{
    uint8_t buffer[NET_PACKET_MAX];
    size_t size = net_encode(m, buffer, sizeof buffer);
    if (size > 0) udp_send(&s->sock, to, buffer, size);
}

static void set_status(NetSession *s, const char *text)
{
    snprintf(s->status, sizeof s->status, "%s", text);
}

static void reset(NetSession *s)
{
    memset(s, 0, sizeof *s);
    s->phase = SESSION_OFF;
}

bool session_host(NetSession *s, uint16_t port)
{
    reset(s);
    if (!udp_open(&s->sock, port ? port : NET_DEFAULT_PORT)) {
        set_status(s, "a porta está ocupada (outro jogo aberto?)");
        s->phase = SESSION_CLOSED;
        return false;
    }
    s->host = true;
    s->phase = SESSION_LOBBY;
    s->local = 0;
    s->players = 1;
    s->joined[0] = true;
    set_status(s, "sala aberta: esperando jogadores");
    return true;
}

bool session_join(NetSession *s, NetAddress host)
{
    reset(s);
    if (!udp_open(&s->sock, 0)) {
        set_status(s, "não consegui abrir a rede");
        s->phase = SESSION_CLOSED;
        return false;
    }
    s->host = false;
    s->phase = SESSION_JOINING;
    s->peer[0] = host;
    s->sent_at = -1.0;
    set_status(s, "procurando a sala...");
    return true;
}

// --- começo da partida ----------------------------------------------------------

static void begin_playing(NetSession *s, int players, uint64_t seed, double now)
{
    s->players = players;
    s->seed = seed;
    s->phase = SESSION_PLAYING;
    s->started_at = now;
    lockstep_init(&s->ls, players);
    s->local_next = NET_DELAY;
    s->bundled = 0;
    for (int p = 0; p < NET_MAX_PLAYERS; p++) s->heard[p] = now;
    set_status(s, "partida em andamento");
}

void session_start(NetSession *s, uint64_t seed, double now)
{
    if (!s->host || s->phase != SESSION_LOBBY) return;
    begin_playing(s, s->players, seed, now);
}

// --- anfitrião ----------------------------------------------------------------------

static void compare_hash(NetSession *s, uint64_t mine, uint64_t theirs)
{
    if (mine == theirs || s->desync) return;
    s->desync = true;
    set_status(s, "as partidas divergiram (dessincronia)");
}

static int find_peer(const NetSession *s, NetAddress from)
{
    for (int p = 1; p < s->players; p++) {
        if (s->joined[p] && udp_same(s->peer[p], from)) return p;
    }
    return -1;
}

static void host_broadcast(NetSession *s, const NetMessage *m)
{
    for (int p = 1; p < s->players; p++) {
        if (s->joined[p]) send_msg(s, s->peer[p], m);
    }
}

// Junta os passos que ficaram completos e manda o pacote com os mais recentes.
static void host_bundle(NetSession *s)
{
    bool advanced = false;
    while (s->bundled < s->ls.next + NET_WINDOW) {
        bool complete = true;
        for (int p = 0; p < s->players && complete; p++) complete = lockstep_has(&s->ls, s->bundled, p);
        if (!complete) break;
        for (int p = 0; p < s->players; p++) s->bundle[s->bundled % NET_WINDOW][p] = lockstep_get(&s->ls, s->bundled, p);
        s->bundled++;
        advanced = true;
    }
    (void)advanced;
    if (s->bundled == 0) return;
    NetMessage m;
    memset(&m, 0, sizeof m);
    m.type = NET_BUNDLE;
    m.players = (uint8_t)s->players;
    uint32_t first = (s->bundled > NET_REDUNDANCY) ? s->bundled - NET_REDUNDANCY : 0u;
    m.tick = first;
    m.count = (uint8_t)(s->bundled - first);
    for (int k = 0; k < m.count; k++) {
        for (int p = 0; p < s->players; p++) m.inputs[k * s->players + p] = s->bundle[(first + (uint32_t)k) % NET_WINDOW][p];
    }
    host_broadcast(s, &m);
}

static void host_receive(NetSession *s, NetAddress from, const NetMessage *m, double now)
{
    int p = find_peer(s, from);
    if (m->type == NET_HELLO) {
        if (p < 0) {
            if (s->phase != SESSION_LOBBY || s->players >= NET_MAX_PLAYERS || m->value != NET_VERSION) {
                NetMessage no;
                memset(&no, 0, sizeof no);
                no.type = NET_FULL;
                send_msg(s, from, &no);
                return;
            }
            p = s->players++;
            s->peer[p] = from;
            s->joined[p] = true;
            set_status(s, "um jogador entrou");
        }
        s->heard[p] = now;
        NetMessage welcome;
        memset(&welcome, 0, sizeof welcome);
        welcome.type = NET_WELCOME;
        welcome.player = (uint8_t)p;
        welcome.players = (uint8_t)s->players;
        send_msg(s, from, &welcome);
        return;
    }
    if (p < 0) return;   // não é da sala
    s->heard[p] = now;
    switch (m->type) {
    case NET_INPUT:
        if (s->phase != SESSION_PLAYING) break;
        s->started[p] = true;   // (recebeu o início: não precisa mais repeti-lo)
        for (int k = 0; k < m->count; k++) lockstep_put(&s->ls, m->tick + (uint32_t)k, p, &m->inputs[k]);
        host_bundle(s);
        break;
    case NET_HASH: {
        // Guarda o do cliente; se o meu do mesmo passo já existe, compara.
        int slot = (int)((m->tick / HASH_EVERY) % SESSION_HASHES);
        s->peer_tick[p][slot] = m->tick;
        s->peer_value[p][slot] = m->value;
        s->peer_has[p][slot] = true;
        if (s->hash_tick[slot] == m->tick && s->hash_has[slot]) compare_hash(s, s->hash_value[slot], m->value);
        break;
    }
    case NET_BYE:
        s->joined[p] = false;
        if (s->phase == SESSION_PLAYING) lockstep_drop(&s->ls, p);
        set_status(s, "um jogador saiu");
        break;
    default:
        break;
    }
}

// --- cliente ------------------------------------------------------------------------

static void client_receive(NetSession *s, NetAddress from, const NetMessage *m, double now)
{
    if (!udp_same(from, s->peer[0])) return;
    s->heard[0] = now;
    switch (m->type) {
    case NET_WELCOME:
        if (s->phase == SESSION_JOINING || s->phase == SESSION_LOBBY) {
            s->local = m->player;
            s->players = m->players;
            s->phase = SESSION_LOBBY;
            set_status(s, "na sala: esperando o anfitrião começar");
        }
        break;
    case NET_LOBBY:
        if (s->phase == SESSION_LOBBY) s->players = m->players;
        break;
    case NET_START:
        if (s->phase == SESSION_LOBBY) begin_playing(s, m->players, m->value, now);
        break;
    case NET_BUNDLE:
        if (s->phase != SESSION_PLAYING || m->players != s->players) break;
        for (int k = 0; k < m->count; k++) {
            for (int p = 0; p < s->players; p++) lockstep_put(&s->ls, m->tick + (uint32_t)k, p, &m->inputs[k * s->players + p]);
        }
        break;
    case NET_FULL:
        s->phase = SESSION_CLOSED;
        set_status(s, "a sala está cheia ou a partida já começou");
        break;
    case NET_BYE:
        s->phase = SESSION_CLOSED;
        set_status(s, "o anfitrião fechou a sala");
        break;
    default:
        break;
    }
}

static void client_send_inputs(NetSession *s)
{
    if (s->local_next <= NET_DELAY) return;
    NetMessage m;
    memset(&m, 0, sizeof m);
    m.type = NET_INPUT;
    m.player = (uint8_t)s->local;
    uint32_t first = (s->local_next - NET_DELAY > NET_REDUNDANCY) ? s->local_next - NET_REDUNDANCY : NET_DELAY;
    m.tick = first;
    m.count = (uint8_t)(s->local_next - first);
    for (int k = 0; k < m.count; k++) m.inputs[k] = s->mine[(first + (uint32_t)k) % NET_WINDOW];
    send_msg(s, s->peer[0], &m);
}

// --- a cada quadro -----------------------------------------------------------------

void session_poll(NetSession *s, double now)
{
    if (s->phase == SESSION_OFF || s->phase == SESSION_CLOSED) return;
    uint8_t buffer[NET_PACKET_MAX];
    NetAddress from;
    size_t size;
    int guard = 256;
    while (guard-- > 0 && (size = udp_recv(&s->sock, &from, buffer, sizeof buffer)) > 0) {
        NetMessage m;
        if (!net_decode(buffer, size, &m)) continue;
        if (s->host) host_receive(s, from, &m, now);
        else         client_receive(s, from, &m, now);
        if (s->phase == SESSION_CLOSED) return;
    }

    bool periodic = now - s->sent_at >= KEEPALIVE;
    if (periodic) s->sent_at = now;

    if (s->host) {
        // Sala: avisa a todos quantos são. Começo: repete o início a quem ainda
        // não mandou nenhum comando (um pacote pode se perder, e o cliente pode
        // estar ocupado gerando o planeta).
        NetMessage m;
        memset(&m, 0, sizeof m);
        m.players = (uint8_t)s->players;
        if (periodic && s->phase == SESSION_LOBBY) {
            m.type = NET_LOBBY;
            host_broadcast(s, &m);
        }
        if (periodic && s->phase == SESSION_PLAYING) {
            m.type = NET_START;
            m.value = s->seed;
            for (int p = 1; p < s->players; p++) {
                if (s->joined[p] && !s->started[p]) send_msg(s, s->peer[p], &m);
            }
        }
        if (periodic && s->phase == SESSION_PLAYING) host_bundle(s);   // mantém vivo e repete o último
        for (int p = 1; p < s->players; p++) {
            if (s->joined[p] && now - s->heard[p] > TIMEOUT) {
                s->joined[p] = false;
                if (s->phase == SESSION_PLAYING) lockstep_drop(&s->ls, p);
                set_status(s, "um jogador caiu (sem resposta)");
            }
        }
    } else {
        if (periodic && s->phase == SESSION_JOINING) {
            NetMessage hello;
            memset(&hello, 0, sizeof hello);
            hello.type = NET_HELLO;
            hello.value = NET_VERSION;
            send_msg(s, s->peer[0], &hello);
            if (s->started_at == 0.0) s->started_at = now;   // (começou a procurar agora)
        }
        if (s->phase == SESSION_JOINING && s->started_at > 0.0 && now - s->started_at > TIMEOUT) {
            s->phase = SESSION_CLOSED;
            set_status(s, "não encontrei a sala (o IP está certo? a porta está aberta?)");
            return;
        }
        if (periodic && s->phase == SESSION_LOBBY) {   // mantém vivo
            NetMessage hello;
            memset(&hello, 0, sizeof hello);
            hello.type = NET_HELLO;
            hello.value = NET_VERSION;
            send_msg(s, s->peer[0], &hello);
        }
        if (periodic && s->phase == SESSION_PLAYING) client_send_inputs(s);
        bool silent = (s->phase == SESSION_JOINING) ? false : (s->heard[0] > 0.0 && now - s->heard[0] > TIMEOUT);
        if (silent) {
            s->phase = SESSION_CLOSED;
            set_status(s, "a conexão com o anfitrião caiu");
        }
    }
}

bool session_push(NetSession *s, const InputState *in, double now)
{
    (void)now;
    if (s->phase != SESSION_PLAYING) return false;
    if (s->local_next >= s->ls.next + NET_DELAY) return false;   // adiantado demais: espera os outros
    uint32_t tick = s->local_next++;
    s->mine[tick % NET_WINDOW] = *in;
    lockstep_put(&s->ls, tick, s->local, in);
    if (s->host) host_bundle(s);
    else         client_send_inputs(s);
    return true;
}

bool session_ready(const NetSession *s)
{
    return s->phase == SESSION_PLAYING && lockstep_ready(&s->ls);
}

uint32_t session_take(NetSession *s, InputState out[NET_MAX_PLAYERS])
{
    // O anfitrião guarda o passo no pacote dos clientes antes de usá-lo (ao
    // usar, o lockstep o apaga; sem isso, um passo que ficou pronto sem
    // nenhum pacote novo nunca chegaria a eles).
    if (s->host) host_bundle(s);
    return lockstep_take(&s->ls, out);
}

void session_report_hash(NetSession *s, uint32_t tick, uint64_t hash)
{
    if (s->phase != SESSION_PLAYING || tick % HASH_EVERY != 0u) return;
    if (s->host) {
        int slot = (int)((tick / HASH_EVERY) % SESSION_HASHES);
        s->hash_tick[slot] = tick;
        s->hash_value[slot] = hash;
        s->hash_has[slot] = true;
        for (int p = 1; p < s->players; p++) {   // os clientes que já mandaram o deste passo
            if (s->peer_has[p][slot] && s->peer_tick[p][slot] == tick) compare_hash(s, hash, s->peer_value[p][slot]);
        }
        return;
    }
    NetMessage m;
    memset(&m, 0, sizeof m);
    m.type = NET_HASH;
    m.player = (uint8_t)s->local;
    m.tick = tick;
    m.value = hash;
    send_msg(s, s->peer[0], &m);
}

void session_close(NetSession *s)
{
    if (s->sock.open && s->phase != SESSION_OFF) {
        NetMessage bye;
        memset(&bye, 0, sizeof bye);
        bye.type = NET_BYE;
        bye.player = (uint8_t)s->local;
        for (int k = 0; k < 3; k++) {   // sem garantia de entrega: manda algumas vezes
            if (s->host) host_broadcast(s, &bye);
            else         send_msg(s, s->peer[0], &bye);
        }
    }
    udp_close(&s->sock);
    s->phase = SESSION_OFF;
}

int session_backlog(const NetSession *s)
{
    if (s->phase != SESSION_PLAYING) return 0;
    int n = 0;
    for (uint32_t tick = s->ls.next; tick < s->ls.next + NET_WINDOW; tick++) {
        for (int p = 0; p < s->ls.players; p++) {
            if (!lockstep_has(&s->ls, tick, p)) return n;
        }
        n++;
    }
    return n;
}
