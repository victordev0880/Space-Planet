// Telas fora da partida: título, pausa, opções e controles.
//
// O menu não conhece o resto do aplicativo: recebe o input já traduzido
// (setas, confirmar, voltar, mouse), mexe nas preferências e devolve um
// comando (jogar, continuar, sair...) que o main executa. Assim a mesma
// navegação serve a teclado, mouse e controle.
#ifndef UI_MENUS_H
#define UI_MENUS_H

#include <stdbool.h>
#include <stdint.h>

#include "raylib.h"

#include "core/settings.h"
#include "game/world.h"

typedef enum {
    SCREEN_GAME,       // sem menu: a partida na tela
    SCREEN_TITLE,
    SCREEN_PAUSE,
    SCREEN_OPTIONS,
    SCREEN_CONTROLS,
    SCREEN_ONLINE,     // criar sala ou entrar
    SCREEN_JOIN,       // digitar o IP do anfitrião
    SCREEN_LOBBY,      // na sala (anfitrião ou cliente)
    SCREEN_COUNT
} Screen;

typedef enum {
    MENU_NONE,
    MENU_PLAY,           // título: nova partida
    MENU_RESUME,         // pausa: volta ao jogo
    MENU_RESTART,        // pausa: recomeça a partida
    MENU_TO_TITLE,       // pausa: abandona a partida e volta ao título
    MENU_QUIT,           // fecha o jogo
    MENU_SETTINGS,       // uma preferência mudou (aplicar já)
    MENU_SETTINGS_DONE,  // saiu das opções (aplicar e gravar)
    MENU_HOST,           // online: abrir uma sala
    MENU_JOIN,           // online: entrar na sala do IP digitado (Menu.ip)
    MENU_START_ONLINE,   // online: o anfitrião começa a partida
    MENU_LEAVE_ONLINE,   // online: sair da sala (ou da partida)
} MenuCommand;

// Som de interface que o menu pede a cada quadro.
typedef enum { MENU_SOUND_NONE, MENU_SOUND_MOVE, MENU_SOUND_CONFIRM, MENU_SOUND_BACK } MenuSound;

typedef struct {
    bool    up, down, left, right;   // um toque (já com repetição ao segurar)
    bool    confirm, back;
    Vector2 mouse;
    bool    mouse_moved;
    bool    click;          // botão esquerdo acabou de descer
    bool    mouse_down;     // botão esquerdo apertado (arrastar slider)
    bool    gamepad;        // este quadro teve input do controle
    bool    escape;         // Esc ou B (o "voltar" que não apaga texto)
    bool    erase;          // Backspace (no campo de texto, apaga)
    char    chars[8];       // o que foi digitado neste quadro
    int     char_count;
    const char *paste;      // Ctrl+V: o texto colado (ou NULL)
} MenuInput;

#define MENU_MAX_ITEMS 12

typedef struct {
    Screen    screen;
    Screen    parent;         // de onde opções e controles vieram (título ou pausa)
    int       cursor;
    float     age;            // segundos desde que a tela abriu
    float     glow[MENU_MAX_ITEMS];   // destaque animado de cada item
    int       dragging;       // slider sendo arrastado (-1 = nenhum)
    bool      gamepad;        // a última navegação veio do controle (muda as dicas)
    MenuSound sound;          // som a tocar neste quadro
    char      ip[40];         // o IP digitado na tela de entrar
    bool      online;         // a partida é online (a pausa não congela; sem "recomeçar")
    bool      host;           // na sala: sou o anfitrião (só ele tem "começar")
    int       lobby_players;  // na sala: quantos jogadores
} Menu;

typedef struct {
    int64_t     best_score;
    // A sala (online).
    const char *net_status;     // o que está acontecendo ("esperando jogadores"...)
    char        ips[4][24];     // anfitrião: os IPs desta máquina, para passar aos amigos
    int         ip_count;
    int         port;
} MenuView;

void menu_open(Menu *m, Screen screen);

// A partida está congelada nesta tela? (no título ela só gira a câmera)
bool menu_freezes_game(const Menu *m);

MenuCommand menu_update(Menu *m, const MenuInput *in, Settings *settings, float dt);
void        menu_draw(const Menu *m, const World *w, const Settings *settings, const MenuView *view);

#endif
