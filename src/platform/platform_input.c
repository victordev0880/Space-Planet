#include "raylib.h"

#include "platform/platform_input.h"

#include "core/vmath.h"

#define PAD            0       // só o primeiro controle conectado
#define MENU_STICK     0.5f    // analógico nos menus: a partir daqui conta como seta
#define REPEAT_DELAY   0.38f   // segurando uma direção no menu: espera antes de repetir...
#define REPEAT_RATE    0.11f   // ... e o intervalo entre as repetições
#define DEVICE_STICK   0.5f    // mexer um analógico além disto "pega" o controle

typedef struct {
    int    key;
    Action action;
} KeyBinding;

static const KeyBinding KEYS[] = {
    { KEY_W,            ACT_FORWARD },
    { KEY_S,            ACT_BACK },
    { KEY_A,            ACT_LEFT },
    { KEY_D,            ACT_RIGHT },
    { KEY_Q,            ACT_ABILITY1 },
    { KEY_E,            ACT_BUILD },
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
    { KEY_G,            ACT_ABILITY2 },
    { KEY_X,            ACT_ABILITY3 },
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

typedef struct {
    int      button;
    uint32_t actions;
} PadBinding;

// O direcional serve às compras e às cartas: com as cartas na tela a partida
// congela e as compras não valem; fora delas, as cartas não valem.
static const PadBinding BUTTONS[] = {
    { GAMEPAD_BUTTON_RIGHT_FACE_DOWN,  ACT_BIT(ACT_DASH) },
    { GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, ACT_BIT(ACT_DRIFT) },
    { GAMEPAD_BUTTON_RIGHT_FACE_LEFT,  ACT_BIT(ACT_TRACTOR) },
    { GAMEPAD_BUTTON_RIGHT_FACE_UP,    ACT_BIT(ACT_READY) },
    { GAMEPAD_BUTTON_LEFT_TRIGGER_1,   ACT_BIT(ACT_ABILITY1) },
    { GAMEPAD_BUTTON_RIGHT_TRIGGER_1,  ACT_BIT(ACT_BUILD) },
    { GAMEPAD_BUTTON_RIGHT_THUMB,      ACT_BIT(ACT_ABILITY2) },
    { GAMEPAD_BUTTON_LEFT_FACE_DOWN,   ACT_BIT(ACT_ABILITY3) },
    { GAMEPAD_BUTTON_LEFT_FACE_LEFT,   ACT_BIT(ACT_BUY1) | ACT_BIT(ACT_CARD1) },
    { GAMEPAD_BUTTON_LEFT_FACE_UP,     ACT_BIT(ACT_BUY2) | ACT_BIT(ACT_CARD2) },
    { GAMEPAD_BUTTON_LEFT_FACE_RIGHT,  ACT_BIT(ACT_BUY3) | ACT_BIT(ACT_CARD3) },
};

static bool using_pad = false;
static bool boost_latched = false;

static bool pad_ready(void)
{
    return IsGamepadAvailable(PAD);
}

static float axis(int a)
{
    return GetGamepadAxisMovement(PAD, a);
}

// Gatilhos vão de -1 (solto) a 1 (no fundo); devolve a fração apertada.
static float pull(int a)
{
    return Clamp((axis(a) + 1.0f) * 0.5f, 0.0f, 1.0f);
}

void platform_input_update(void)
{
    // Qualquer tecla, clique ou movimento de mouse devolve as dicas ao teclado;
    // qualquer botão ou analógico do controle, ao controle.
    Vector2 mouse = GetMouseDelta();
    if (GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
        || fabsf(mouse.x) + fabsf(mouse.y) > 2.0f) {
        using_pad = false;
    }
    if (!pad_ready()) {
        using_pad = false;
        return;
    }
    for (int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; b++) {
        if (IsGamepadButtonPressed(PAD, b)) using_pad = true;
    }
    if (fabsf(axis(GAMEPAD_AXIS_LEFT_X)) > DEVICE_STICK || fabsf(axis(GAMEPAD_AXIS_LEFT_Y)) > DEVICE_STICK
        || fabsf(axis(GAMEPAD_AXIS_RIGHT_X)) > DEVICE_STICK || fabsf(axis(GAMEPAD_AXIS_RIGHT_Y)) > DEVICE_STICK
        || pull(GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f || pull(GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f) {
        using_pad = true;
    }
}

bool platform_using_gamepad(void)
{
    return using_pad;
}

static void poll_gamepad(InputSample *s, const Settings *settings, const Tuning *t)
{
    if (!pad_ready()) {
        boost_latched = false;
        return;
    }
    for (int i = 0; i < (int)(sizeof BUTTONS / sizeof BUTTONS[0]); i++) {
        if (IsGamepadButtonDown(PAD, BUTTONS[i].button)) s->held |= BUTTONS[i].actions;
    }
    if (pull(GAMEPAD_AXIS_RIGHT_TRIGGER) > t->gamepad_trigger) s->held |= ACT_BIT(ACT_FIRE);
    if (pull(GAMEPAD_AXIS_LEFT_TRIGGER) > t->gamepad_trigger)  s->held |= ACT_BIT(ACT_ALT_FIRE);

    // Movimento: o analógico esquerdo vira W/A/S/D.
    float lx = axis(GAMEPAD_AXIS_LEFT_X), ly = axis(GAMEPAD_AXIS_LEFT_Y);
    if (ly < -t->gamepad_move) s->held |= ACT_BIT(ACT_FORWARD);
    if (ly > t->gamepad_move)  s->held |= ACT_BIT(ACT_BACK);
    if (lx < -t->gamepad_move) s->held |= ACT_BIT(ACT_LEFT);
    if (lx > t->gamepad_move)  s->held |= ACT_BIT(ACT_RIGHT);

    // Boost no L3: um toque liga, e ele fica até o jogador parar de ir para a
    // frente (segurar o analógico apertado cansa o polegar).
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_LEFT_THUMB)) boost_latched = !boost_latched;
    if (ly > -t->gamepad_move) boost_latched = false;
    if (boost_latched || IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_THUMB)) s->held |= ACT_BIT(ACT_BOOST);

    // Mira: o analógico direito vira "pixels de mouse" por segundo.
    float rx, ry;
    input_stick(axis(GAMEPAD_AXIS_RIGHT_X), axis(GAMEPAD_AXIS_RIGHT_Y), t->gamepad_deadzone, t->gamepad_curve, &rx, &ry);
    float speed = t->gamepad_look_speed * settings->stick * GetFrameTime();
    s->look_dx += rx * speed;
    s->look_dy += ry * speed * (settings->invert_y ? -1.0f : 1.0f);
}

InputSample platform_poll_input(bool use_mouse_look, const Settings *settings, const Tuning *t)
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
    poll_gamepad(&s, settings, t);
    return s;
}

static bool key_hit(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

// Direção segurada no controle: dispara ao apertar e, depois de uma espera,
// repete (como uma tecla segurada).
static bool repeat(bool down, float *timer, float dt)
{
    if (!down) {
        *timer = -1.0f;
        return false;
    }
    if (*timer < 0.0f) {
        *timer = 0.0f;
        return true;
    }
    *timer += dt;
    if (*timer >= REPEAT_DELAY) {
        *timer -= REPEAT_RATE;
        return true;
    }
    return false;
}

MenuInput platform_menu_input(void)
{
    static Vector2 last_mouse = { -1.0f, -1.0f };
    static float timers[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
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

    if (pad_ready()) {
        float dt = GetFrameTime();
        float lx = axis(GAMEPAD_AXIS_LEFT_X), ly = axis(GAMEPAD_AXIS_LEFT_Y);
        bool dirs[4] = {
            IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_UP) || ly < -MENU_STICK,
            IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ly > MENU_STICK,
            IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || lx < -MENU_STICK,
            IsGamepadButtonDown(PAD, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || lx > MENU_STICK,
        };
        bool hit[4];
        for (int i = 0; i < 4; i++) hit[i] = repeat(dirs[i], &timers[i], dt);
        bool confirm = IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        bool back = IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
                 || IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_RIGHT);
        in.up |= hit[0];
        in.down |= hit[1];
        in.left |= hit[2];
        in.right |= hit[3];
        in.confirm |= confirm;
        in.back |= back;
        in.gamepad = hit[0] || hit[1] || hit[2] || hit[3] || confirm || back;
    }
    return in;
}

bool platform_pause_pressed(void)
{
    return IsKeyPressed(KEY_ESCAPE) || (pad_ready() && IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_MIDDLE_RIGHT));
}

int platform_end_choice(void)
{
    if (!pad_ready()) return 0;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) return 1;
    if (IsGamepadButtonPressed(PAD, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) return 2;
    return 0;
}
