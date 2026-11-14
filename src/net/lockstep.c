#include "net/lockstep.h"

#include <string.h>

static int slot(uint32_t tick)
{
    return (int)(tick % NET_WINDOW);
}

void lockstep_init(Lockstep *ls, int players)
{
    memset(ls, 0, sizeof *ls);
    ls->players = (players < 1) ? 1 : (players > NET_MAX_PLAYERS) ? NET_MAX_PLAYERS : players;
    InputState empty;
    memset(&empty, 0, sizeof empty);
    for (uint32_t t = 0; t < NET_DELAY; t++) {
        for (int p = 0; p < ls->players; p++) lockstep_put(ls, t, p, &empty);
    }
}

// Abre a posição do anel para `tick` (apagando o que era de uma volta antiga).
static int claim(Lockstep *ls, uint32_t tick)
{
    int s = slot(tick);
    if (ls->tick_of[s] != tick) {
        ls->have[s] = 0;
        memset(ls->inputs[s], 0, sizeof ls->inputs[s]);
        ls->tick_of[s] = tick;
    }
    return s;
}

void lockstep_put(Lockstep *ls, uint32_t tick, int player, const InputState *in)
{
    if (player < 0 || player >= ls->players) return;
    if (tick < ls->next || tick >= ls->next + NET_WINDOW) return;
    int s = claim(ls, tick);
    uint8_t bit = (uint8_t)(1u << player);
    if (ls->have[s] & bit) return;   // já tinha (a redundância repete)
    ls->inputs[s][player] = *in;
    ls->have[s] |= bit;
}

bool lockstep_has(const Lockstep *ls, uint32_t tick, int player)
{
    if (player < 0 || player >= ls->players) return false;
    int s = slot(tick);
    return ls->tick_of[s] == tick && (ls->have[s] & (1u << player)) != 0;
}

InputState lockstep_get(const Lockstep *ls, uint32_t tick, int player)
{
    InputState none;
    memset(&none, 0, sizeof none);
    if (!lockstep_has(ls, tick, player)) return none;
    return ls->inputs[slot(tick)][player];
}

bool lockstep_ready(const Lockstep *ls)
{
    int s = slot(ls->next);
    uint8_t all = (uint8_t)((1u << ls->players) - 1u);
    return ls->tick_of[s] == ls->next && (ls->have[s] & all) == all;
}

uint32_t lockstep_take(Lockstep *ls, InputState out[NET_MAX_PLAYERS])
{
    int s = slot(ls->next);
    for (int p = 0; p < NET_MAX_PLAYERS; p++) {
        if (p < ls->players) out[p] = ls->inputs[s][p];
        else memset(&out[p], 0, sizeof out[p]);
    }
    ls->have[s] = 0;
    return ls->next++;
}

void lockstep_drop(Lockstep *ls, int player)
{
    InputState empty;
    memset(&empty, 0, sizeof empty);
    for (uint32_t t = ls->next; t < ls->next + NET_WINDOW; t++) lockstep_put(ls, t, player, &empty);
}

// --- pacotes ----------------------------------------------------------------

typedef struct {
    uint8_t *data;
    size_t   size, capacity;
    bool     ok;
} Writer;

static void put8(Writer *w, uint8_t v)
{
    if (w->size + 1 > w->capacity) {
        w->ok = false;
        return;
    }
    w->data[w->size++] = v;
}

static void put32(Writer *w, uint32_t v)
{
    for (int i = 0; i < 4; i++) put8(w, (uint8_t)(v >> (8 * i)));
}

static void put64(Writer *w, uint64_t v)
{
    for (int i = 0; i < 8; i++) put8(w, (uint8_t)(v >> (8 * i)));
}

static void put_float(Writer *w, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    put32(w, bits);
}

static void put_input(Writer *w, const InputState *in)
{
    put32(w, in->held);
    put32(w, in->pressed);
    put32(w, in->released);
    put_float(w, in->look_dx);
    put_float(w, in->look_dy);
}

typedef struct {
    const uint8_t *data;
    size_t         size, at;
    bool           ok;
} Reader;

static uint8_t get8(Reader *r)
{
    if (r->at + 1 > r->size) {
        r->ok = false;
        return 0;
    }
    return r->data[r->at++];
}

static uint32_t get32(Reader *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)get8(r) << (8 * i);
    return v;
}

static uint64_t get64(Reader *r)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)get8(r) << (8 * i);
    return v;
}

static float get_float(Reader *r)
{
    uint32_t bits = get32(r);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static void get_input(Reader *r, InputState *in)
{
    in->held = get32(r);
    in->pressed = get32(r);
    in->released = get32(r);
    in->look_dx = get_float(r);
    in->look_dy = get_float(r);
}

// Quantos comandos o pacote leva.
static int input_count(const NetMessage *m)
{
    if (m->type == NET_INPUT) return m->count;
    if (m->type == NET_BUNDLE) return m->count * m->players;
    return 0;
}

size_t net_encode(const NetMessage *m, uint8_t *out, size_t capacity)
{
    Writer w = { out, 0, capacity, true };
    int n = input_count(m);
    if (n < 0 || n > NET_REDUNDANCY * NET_MAX_PLAYERS) return 0;
    put8(&w, 'T');
    put8(&w, 'F');
    put8(&w, m->type);
    put8(&w, m->player);
    put8(&w, m->players);
    put8(&w, m->count);
    put32(&w, m->tick);
    put64(&w, m->value);
    for (int i = 0; i < n; i++) put_input(&w, &m->inputs[i]);
    return w.ok ? w.size : 0;
}

bool net_decode(const uint8_t *data, size_t size, NetMessage *m)
{
    Reader r = { data, size, 0, true };
    memset(m, 0, sizeof *m);
    if (get8(&r) != 'T' || get8(&r) != 'F') return false;
    m->type = get8(&r);
    m->player = get8(&r);
    m->players = get8(&r);
    m->count = get8(&r);
    m->tick = get32(&r);
    m->value = get64(&r);
    if (!r.ok || m->type < NET_HELLO || m->type > NET_FULL) return false;
    if (m->player >= NET_MAX_PLAYERS || m->players > NET_MAX_PLAYERS || m->count > NET_REDUNDANCY) return false;
    int n = input_count(m);
    for (int i = 0; i < n; i++) get_input(&r, &m->inputs[i]);
    return r.ok && r.at == size;
}
