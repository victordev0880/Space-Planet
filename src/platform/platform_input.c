#include "raylib.h"

#include "platform/platform_input.h"

#include "core/vmath.h"

typedef struct {
    int    key;
    Action action;
} KeyBinding;

static const KeyBinding KEYS[] = {
    { KEY_W,            ACT_FORWARD },
    { KEY_S,            ACT_BACK },
    { KEY_A,            ACT_LEFT },
    { KEY_D,            ACT_RIGHT },
    { KEY_Q,            ACT_ROLL_LEFT },
    { KEY_E,            ACT_ROLL_RIGHT },
    { KEY_LEFT_SHIFT,   ACT_BOOST },
    { KEY_SPACE,        ACT_DASH },
    { KEY_LEFT_CONTROL, ACT_DRIFT },
    { KEY_F1,           ACT_OVERLAY },
    { KEY_F2,           ACT_FREECAM },
    { KEY_F3,           ACT_WIREFRAME },
    { KEY_F5,           ACT_RELOAD },
    { KEY_R,            ACT_RESET },
    { KEY_C,            ACT_DEBUG_CRATER },
    { KEY_V,            ACT_DEBUG_SIZE },
    { KEY_ENTER,        ACT_READY },
    { KEY_F,            ACT_TRACTOR },
    { KEY_FOUR,         ACT_BUY1 },
    { KEY_FIVE,         ACT_BUY2 },
    { KEY_SIX,          ACT_BUY3 },
    { KEY_KP_4,         ACT_BUY1 },
    { KEY_KP_5,         ACT_BUY2 },
    { KEY_KP_6,         ACT_BUY3 },
    { KEY_ONE,          ACT_CARD1 },
    { KEY_TWO,          ACT_CARD2 },
    { KEY_THREE,        ACT_CARD3 },
    { KEY_KP_1,         ACT_CARD1 },
    { KEY_KP_2,         ACT_CARD2 },
    { KEY_KP_3,         ACT_CARD3 },
};

InputSample platform_poll_input(bool use_mouse_look, const Settings *settings)
{
    InputSample s = { 0u, 0.0f, 0.0f };

    for (int i = 0; i < (int)(sizeof KEYS / sizeof KEYS[0]); i++) {
        if (IsKeyDown(KEYS[i].key)) s.held |= ACT_BIT(KEYS[i].action);
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))  s.held |= ACT_BIT(ACT_FIRE);
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) s.held |= ACT_BIT(ACT_ALT_FIRE);

    if (use_mouse_look) {
        Vector2 delta = GetMouseDelta();
        s.look_dx = delta.x * settings->mouse;
        s.look_dy = delta.y * settings->mouse * (settings->invert_y ? -1.0f : 1.0f);
    }
    return s;
}

static bool key_hit(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

MenuInput platform_menu_input(void)
{
    static Vector2 last_mouse = { -1.0f, -1.0f };
    MenuInput in = { 0 };
    in.up = key_hit(KEY_UP) || key_hit(KEY_W);
    in.down = key_hit(KEY_DOWN) || key_hit(KEY_S);
    in.left = key_hit(KEY_LEFT) || key_hit(KEY_A);
    in.right = key_hit(KEY_RIGHT) || key_hit(KEY_D);
    in.confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    in.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);

    in.mouse = GetMousePosition();
    in.mouse_moved = (last_mouse.x >= 0.0f) && Vector2Distance(in.mouse, last_mouse) > 0.5f;
    last_mouse = in.mouse;
    in.click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    return in;
}

bool platform_pause_pressed(void)
{
    return IsKeyPressed(KEY_ESCAPE);
}
