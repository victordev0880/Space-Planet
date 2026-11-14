// A nave do jogador: física de voo, colisão com o planeta e recursos.
//
// Voo "arcade" em seis eixos:
//   - o mouse gira o nariz (em relação à própria nave, não ao mundo);
//   - W acelera, S freia (e, com a nave quase parada, dá ré, mais devagar),
//     A/D empurram de lado, Q/E rolam;
//   - sem nenhum comando e devagar, os estabilizadores param a nave e a
//     seguram no lugar (sem afundar); em alta velocidade ela segue pela inércia;
//   - a velocidade tem inércia, mas o rumo acompanha o nariz aos poucos
//     ("aderência"), então virar a nave vira o voo;
//   - no drift (Ctrl) a aderência desliga: a nave segue em linha reta
//     enquanto o nariz gira livre;
//   - o boost (Shift) multiplica aceleração e velocidade máxima e gasta energia;
//   - o dash (Espaço) é um impulso curto, com invulnerabilidade enquanto dura;
//   - no voo rasante (dentro da atmosfera) a nave acompanha a curvatura do
//     planeta: voar "reto" contorna o planeta em vez de sair pela tangente;
//   - passar perto da Lua em velocidade dá o estilingue: a velocidade máxima
//     sobe por alguns segundos.
//
// A colisão não usa a malha do planeta: amostra o campo de densidade em
// alguns pontos da nave. Onde há matéria, a nave é empurrada para fora na
// direção da normal. É o mesmo campo que as crateras alteram, então dá para
// voar dentro delas e de túneis.
#ifndef GAME_SHIP_H
#define GAME_SHIP_H

#include <stdbool.h>

#include "core/input.h"
#include "core/vmath.h"
#include "game/moon.h"
#include "game/tuning.h"
#include "voxel/planet.h"

// Onde fica o bocal do motor direito, em eixos da nave (o esquerdo é o
// espelho, com x negativo). Daqui saem o rastro e o brilho dos motores.
#define SHIPS_MAX     4        // naves numa partida (o online junta até 4 jogadores)

#define SHIP_ENGINE_X 0.62f
#define SHIP_ENGINE_Y (-0.02f)
#define SHIP_ENGINE_Z 1.26f

typedef struct {
    Vector3    pos;
    Vector3    vel;            // velocidade de voo: a que os motores e a aderência controlam
    Vector3    fall;           // velocidade de queda, causada pela gravidade (ver ship.c)
    Quaternion rot;            // orientação (ver core/vmath.h)
    Vector3    pos_prev;       // pose um passo antes, para interpolar o desenho
    Quaternion rot_prev;
    Vector3    turn;           // giro atual em rad/s: x = arfagem, y = guinada (em volta do "para cima" do planeta)
    float      bank;           // inclinação só de desenho nas curvas, em radianos (a nave nunca rola de fato)
    float      bank_prev;

    float hull;                // casco: não se recupera sozinho
    float shield;              // escudo: recarrega depois de um tempo sem dano
    float energy;              // gasta em boost e dash, recarrega sozinha
    float shield_wait;         // segundos até o escudo voltar a carregar
    float dash_left;           // segundos restantes do dash (invulnerável enquanto > 0)
    float dash_wait;           // segundos até poder dar outro dash
    float respawn_left;        // segundos até renascer (só quando destruída)
    float phase_left;          // segundos de invulnerabilidade ao sair do Túnel (núcleo do Verme)
    float sling;               // segundos restantes do estilingue da Lua

    // arma principal (controlada por game/combat.c)
    float fire_wait;           // segundos até o próximo tiro
    float muzzle;              // segundos restantes do clarão no cano (desenho)
    int   gun;                 // canhão do próximo tiro: 0 = direito, 1 = esquerdo

    // dano recebido (só desenho e HUD)
    float hit_flash;           // segundos restantes do aviso de dano
    bool  hit_shield;          // o último golpe parou no escudo

    float boost_blend;         // 0 a 1, suavizado: o quanto o boost está "aceso"
    float sling_blend;         // 0 a 1, suavizado: o quanto o estilingue está valendo
    float thrust_blend;        // 0 a 1, suavizado: o quanto o motor está aceso
    float altitude;            // altura acima do nível do mar
    float graze;               // 0 a 1: o quanto a nave está em voo rasante (ver ship.c)
    bool  alive;
    bool  boosting;
    bool  boost_spent;         // a energia acabou no meio do boost: só rearma ao soltar a tecla
    bool  drifting;
    bool  reversing;           // dando ré (S com a nave já quase parada)
    float hover;               // 0 a 1: o quanto os estabilizadores estão segurando a nave parada
    bool  in_atmosphere;       // dentro da camada de ar: nem acima dela, nem no fundo de um túnel
    bool  near_moon;           // dentro do alcance do estilingue da Lua
} Ship;

// O que aconteceu com a nave em um passo, para o mundo reagir (partículas,
// tremor de câmera).
typedef struct {
    bool    hit;          // bateu no terreno
    float   hit_speed;    // velocidade com que veio contra a superfície
    Vector3 hit_point;
    Vector3 hit_normal;
    bool    dashed;
    Vector3 dash_dir;     // para onde foi o dash
    bool    died;
    bool    respawned;
} ShipEvents;

// Põe a nave no ponto de partida, inteira e parada, de frente para o planeta.
void ship_spawn(Ship *s, const Tuning *t, const Planet *planet);

// Põe a nave em `pos`, inteira, com o nariz na direção `forward` e o topo
// para longe do planeta, voando a `speed` nessa direção.
void ship_place(Ship *s, const Tuning *t, const Planet *planet, Vector3 pos, Vector3 forward, float speed);

// Avança a nave um passo. `moon` pode ser NULL (um mundo sem Lua).
ShipEvents ship_tick(Ship *s, const InputState *in, const Planet *planet, const Moon *moon, const Tuning *t,
                     float dt);

// Aplica dano: o escudo absorve primeiro, o resto vai para o casco. Durante o
// dash a nave é invulnerável. Retorna true se este dano destruiu a nave.
bool ship_damage(Ship *s, const Tuning *t, float amount);

static inline Vector3 ship_forward(const Ship *s) { return quat_forward(s->rot); }
static inline Vector3 ship_right(const Ship *s)   { return quat_right(s->rot); }
static inline Vector3 ship_up(const Ship *s)      { return quat_up(s->rot); }

static inline bool ship_invulnerable(const Ship *s) { return s->dash_left > 0.0f || s->phase_left > 0.0f; }

#endif
