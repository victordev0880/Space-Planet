#include "game/events.h"

void events_push(Events *e, uint32_t tick, EventType type, uint8_t sub, Vector3 pos, float value)
{
    e->count++;
    GameEvent *ev = &e->ring[(e->count - 1) % EVENTS_CAPACITY];
    ev->id = e->count;
    ev->tick = tick;
    ev->type = (uint8_t)type;
    ev->sub = sub;
    ev->pos = pos;
    ev->value = value;
}

const GameEvent *events_find(const Events *e, uint32_t id)
{
    if (id == 0 || id > e->count) return NULL;
    if (e->count - id >= EVENTS_CAPACITY) return NULL;   // já foi sobrescrito
    return &e->ring[(id - 1) % EVENTS_CAPACITY];
}
