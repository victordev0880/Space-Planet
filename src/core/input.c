#include "core/input.h"

#include <string.h>

static const char *const ACTION_NAMES[ACT_COUNT] = {
    [ACT_FORWARD]    = "forward",
    [ACT_BACK]       = "back",
    [ACT_LEFT]       = "left",
    [ACT_RIGHT]      = "right",
    [ACT_ROLL_LEFT]  = "roll_left",
    [ACT_ROLL_RIGHT] = "roll_right",
    [ACT_BOOST]      = "boost",
    [ACT_DASH]       = "dash",
    [ACT_DRIFT]      = "drift",
    [ACT_FIRE]       = "fire",
    [ACT_ALT_FIRE]   = "alt_fire",
    [ACT_OVERLAY]    = "overlay",
    [ACT_FREECAM]    = "freecam",
    [ACT_WIREFRAME]  = "wireframe",
    [ACT_RELOAD]     = "reload",
    [ACT_RESET]      = "reset",
    [ACT_DEBUG_CRATER] = "debug_crater",
    [ACT_DEBUG_SIZE]   = "debug_size",
    [ACT_READY]        = "ready",
    [ACT_CARD1]        = "card1",
    [ACT_CARD2]        = "card2",
    [ACT_CARD3]        = "card3",
    [ACT_TRACTOR]      = "tractor",
    [ACT_BUY1]         = "buy1",
    [ACT_BUY2]         = "buy2",
    [ACT_BUY3]         = "buy3",
};

void input_init(InputTracker *t)
{
    memset(t, 0, sizeof *t);
}

void input_feed(InputTracker *t, const InputSample *sample)
{
    t->held_now = sample->held;
    t->seen |= sample->held;
    t->pending_dx += sample->look_dx;
    t->pending_dy += sample->look_dy;
}

InputState input_tick(InputTracker *t)
{
    // `seen` inclui teclas apertadas e soltas entre dois passos: o toque
    // rápido aparece como apertado neste passo e solto no seguinte.
    uint32_t down = t->held_now | t->seen;

    InputState s;
    s.held = down;
    s.pressed = down & ~t->held_at_tick;
    s.released = t->held_at_tick & ~down;
    s.look_dx = t->pending_dx;
    s.look_dy = t->pending_dy;

    t->held_at_tick = down;
    t->seen = 0u;
    t->pending_dx = 0.0f;
    t->pending_dy = 0.0f;
    return s;
}

const char *action_name(Action a)
{
    if ((int)a < 0 || a >= ACT_COUNT) return "?";
    return ACTION_NAMES[a];
}

int action_from_name(const char *name)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        if (strcmp(ACTION_NAMES[i], name) == 0) return i;
    }
    return -1;
}
