// Estado da simulação. Tudo que precisa ser determinístico mora aqui e só é
// alterado por world_tick, um passo fixo de cada vez.
//
// O mundo tem o planeta, a Lua, a nave do jogador, os inimigos, os
// projéteis, as partículas, a pontuação e a câmera. Há três câmeras: a de
// jogo (em terceira pessoa, atrás da nave), a de apresentação (gira sozinha
// em volta do planeta, com a nave fora de cena) e a livre de debug (F2).
//
// A partida (nave, inimigos, hordas) só anda na câmera de jogo. Nas outras
// ela fica congelada: o F2 funciona como pausa com câmera livre.
#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/input.h"
#include "core/rng.h"
#include "core/vmath.h"
#include "game/allies.h"
#include "game/boss.h"
#include "game/defenses.h"
#include "game/enemies.h"
#include "game/events.h"
#include "game/evolution.h"
#include "game/moon.h"
#include "game/particles.h"
#include "game/projectiles.h"
#include "game/repair.h"
#include "game/rocks.h"
#include "game/score.h"
#include "game/ship.h"
#include "game/tuning.h"
#include "game/waves.h"
#include "voxel/planet.h"

// Posição, orientação e campo de visão de uma câmera (ver core/vmath.h para
// a convenção de orientação).
typedef struct {
    Vector3    pos;
    Quaternion rot;
    float      fov;   // campo de visão vertical, em graus
} CamPose;

typedef enum {
    CAM_SHIP,    // jogo: terceira pessoa, atrás da nave
    CAM_ORBIT,   // apresentação: gira sozinha em volta do planeta
    CAM_FREE,    // debug: voa livre (F2)
} CamMode;

// Um impacto no planeta. Quem desenha consulta os mais recentes para reagir
// (tremer a câmera, apagar luzes de cidades).
typedef struct {
    Vector3  pos;      // ponto atingido
    float    radius;   // raio da boca da cratera
    uint32_t id;       // 1 para o primeiro impacto da partida, 2 para o segundo...
    bool     quiet;    // escavação contínua (broca, feixe): sem tremor nem som
} Impact;

#define WORLD_IMPACT_HISTORY 32

// Mísseis (botão direito): segurando, a mira trava até MISSILE_LOCKS alvos à
// frente, um de cada vez; soltando, sai um míssil teleguiado por trava (sem
// trava, um só, em frente). As cargas recarregam sozinhas.
#define MISSILE_LOCKS 4
typedef struct {
    int     charges;               // mísseis prontos
    float   recharge;              // segundos até a próxima carga
    float   lock_wait;             // segundos até a próxima trava
    int16_t lock[MISSILE_LOCKS];   // inimigos travados (índices no pool)
    int     lock_count;
    bool    painting;              // o botão está apertado
} Missiles;

// Núcleo Rompe-Mundos, a recompensa do primeiro boss: segurando Q, a nave
// carrega e solta um raio que atravessa tudo numa linha.
typedef struct {
    bool    owned;       // a nave tem o núcleo
    float   cooldown;    // segundos até poder usar de novo
    float   charge;      // segundos segurando o botão
    float   beam_left;   // segundos que o raio ainda fica na tela (só desenho)
    Vector3 beam_from, beam_to;
} Ability;

// Núcleo Colmeia, a recompensa do segundo boss: a tecla G solta uma revoada
// de ferrões que caçam os inimigos mais perto.
typedef struct {
    bool  owned;
    float cooldown;   // segundos até poder usar de novo
} Brood;

// Núcleo do Verme, a recompensa do terceiro boss: a tecla X faz a nave
// atravessar o planeta e sair do outro lado.
typedef struct {
    bool    owned;
    float   cooldown;
    float   flash;      // segundos do efeito na tela depois da travessia (só desenho)
    Vector3 from;       // de onde saiu (só desenho)
} Tunnel;

typedef enum {
    GAME_PLAYING,
    GAME_WON,      // todas as hordas vencidas
    GAME_LOST,     // colapso planetário
} GameState;

// Tudo o que é de um jogador (online, marco 12). O mundo guarda uma cópia de
// cada um em `players`; os campos de mesmo nome do World (ship, evo,
// missiles...) são sempre os do jogador `current`. A cada passo o mundo
// assume a vez de cada jogador (copia o dele para os campos), roda a parte
// dele, e devolve. No jogo solo há só o jogador 0, e tudo funciona como antes.
// Fora do passo, os campos são os do jogador `local` (o desta tela).
typedef struct {
    Ship       ship;
    InputState frozen_input;
    float      hit_marker, kill_marker;
    Ability    ability;
    Brood      brood;
    Tunnel     tunnel;
    float      suicide_wait;
    bool       building;
    Missiles   missiles;
    float      focus;
    bool       dodge_used;
    int        beam_ticks;
    Evolution  evo;
    Allies     allies;
    uint64_t   cards_at;
    int        towed;       // rocha no raio trator (índice; -1 = nenhuma)
} Player;

typedef struct {
    uint64_t  tick;         // quantos passos de lógica já rodaram
    Rng       rng;
    Planet    planet;
    Moon      moon;
    Particles particles;
    Impact    impacts[WORLD_IMPACT_HISTORY];   // os últimos impactos, em fila circular
    uint32_t  impact_count;                    // total de impactos desde o início
    bool      collapsed;    // o planeta passou do ponto de colapso (derrota)

    Ship      ship;
    bool      ship_active;  // false na câmera de apresentação: nave parada e fora de cena
    uint32_t  jolt_count;   // quantas pancadas a nave já levou (batidas, destruição)
    float     jolt_strength;   // força da mais recente, para o tremor de câmera

    // --- combate ---
    Enemies      enemies;
    Projectiles  shots;
    Events       events;
    Score        score;
    WavePlan     plan;          // a sequência de hordas desta partida
    WaveDirector director;
    bool         waves_enabled; // false = sem hordas (roteiros de teste)
    GameState    state;
    uint64_t     state_tick;    // passo em que a partida terminou
    int          hitstop;       // passos de congelamento restantes ("hit-stop")
    InputState   frozen_input;  // o que o jogador fez durante o congelamento (vale depois dele)
    float        hit_marker;    // segundos restantes do marcador de acerto na mira (HUD)
    float        kill_marker;   // ... e do de abate

    // --- boss ---
    Boss         boss;
    Ability      ability;
    Brood        brood;
    Tunnel       tunnel;
    float        suicide_wait;  // Drones suicidas (sinergia): segundos até o próximo
    bool         building;      // a barra de construção (E) está aberta
    Missiles     missiles;
    float        focus;         // esquiva perfeita: segundos de tiros mais fortes
    bool         dodge_used;    // ... e se este dash já valeu uma (uma por dash)
    int          beam_ticks;    // passos desde o último dano do laser do boss na nave

    // --- rochas e conserto ---
    Rocks        rocks;
    Repair       repair;
    float        debris;          // voxels arrancados por explosões grandes desde o último fragmento
    float        meteor_wait;     // segundos até o próximo meteoro no meio de uma horda
    int          meteor_queue;    // meteoros da calmaria ainda por vir...
    float        meteor_queue_wait;   // ... e os segundos até o próximo deles
    int          calm_seen;       // a calmaria (número da horda seguinte) que já recebeu meteoros

    // --- minério e defesas ---
    int          ore;             // minério da nave (a moeda das defesas)
    int          ore_total;       // minério coletado na partida (estatística)
    Defenses     defenses;

    // --- evolução ---
    Evolution    evo;
    Allies       allies;
    uint64_t     cards_at;      // passo em que as cartas do nível ganho abrem (0 = nenhum)
    Tuning       eff;           // o tuning com os efeitos das evoluções (ver world_tuning)

    CamMode   cam_mode;
    CamMode   cam_return;   // câmera para a qual o F2 volta ao sair da livre
    float     orbit_angle;  // radianos
    float     free_yaw;     // orientação da câmera livre, em radianos
    float     free_pitch;
    Vector3   free_vel;     // velocidade da câmera livre
    CamPose   view;         // pose depois do último passo
    CamPose   view_prev;    // pose um passo antes (para interpolar)

    // --- jogadores (online) ---
    Player    players[SHIPS_MAX];
    int       player_count;   // 1 no jogo solo
    int       local;          // o jogador desta tela
    int       current;        // de quem são os campos de jogador agora (ver Player)
    const Tuning *base;       // o tuning base do passo em curso (para o efetivo de cada jogador)
} World;

// Cria o mundo e gera o planeta, reservando a grade na arena. Retorna false
// se a arena não tiver espaço. Começa na câmera de jogo, com a nave pronta.
bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena);

// Um passo de lógica. `t` é o tuning base (o dos arquivos); a partir dele o
// mundo monta o efetivo, com as evoluções, e usa esse. `in` é o comando do
// jogador local (no online, os outros ficam parados: use world_tick_all).
void world_tick(World *w, const InputState *in, const Tuning *t);

// Um passo com o comando de cada jogador (`in[0]` a `in[player_count - 1]`).
void world_tick_all(World *w, const InputState in[], const Tuning *t);

// Partida para `count` jogadores (1 a SHIPS_MAX); esta tela é a do `local`.
// Recomeça a nave e a evolução de todos; chamar logo depois de world_init ou
// world_restart. A dificuldade cresce com o número de jogadores.
void world_set_players(World *w, const Tuning *t, int count, int local);

// Começa a partida online: tudo do zero a partir de `seed` (o anfitrião
// sorteia e manda), com as hordas ligadas, para `players` jogadores.
void world_start_online(World *w, const Tuning *t, uint64_t seed, int players, int local);

// Assume a vez do jogador `p`: os campos de jogador do World passam a ser os
// dele (os do anterior são guardados).
void world_use_player(World *w, int p);

// A nave do jogador `p` (a dos campos, se for a vez dele).
const Ship *world_ship(const World *w, int p);

// O jogador `p` leva dano (assume a vez dele e volta).
void world_hurt_player(World *w, const Tuning *t, int p, float amount, Vector3 point);

// O tuning efetivo (base + evoluções) do último passo: é o que vale para a
// nave e para quem a desenha (barras de casco e escudo, alcance da arma).
const Tuning *world_tuning(const World *w);

// Ferramentas de roteiro: dar XP à nave e pôr uma evolução direto em um nível.
void world_give_xp(World *w, const Tuning *t, float xp);
void world_set_evolution(World *w, const Tuning *t, EvolutionId id, int rank);

// Depois de recarregar o tuning: se a receita do planeta mudou, gera o
// planeta de novo. Retorna true quando isso aconteceu.
bool world_apply_tuning(World *w, const Tuning *t);

// Troca de câmera sem transição. CAM_SHIP põe a nave em jogo; CAM_ORBIT a
// tira de cena (usado em roteiros que só querem fotografar o planeta).
void world_set_view(World *w, const Tuning *t, CamMode mode);

// Ferramenta de debug e do autoplay: põe a câmera livre em `pos`, olhando
// para `target`.
void world_place_camera(World *w, Vector3 pos, Vector3 target);

// Ferramenta de debug e do autoplay: põe a nave em `pos`, inteira, com o nariz
// para `target` e o topo para longe do planeta, já voando a `speed` nessa
// direção. Passa para a câmera de jogo.
void world_place_ship(World *w, const Tuning *t, Vector3 pos, Vector3 target, float speed);

// Define a sequência de hordas e começa pela calmaria da primeira. Sem plano
// (o padrão), não há hordas.
void world_set_plan(World *w, const WavePlan *plan);

// Liga ou desliga as hordas (roteiros de teste que não querem inimigos).
// Ligar recomeça a sequência.
void world_set_waves(World *w, bool on);

// Ferramenta de debug e do autoplay: põe um grupo de inimigos em `center`.
int world_spawn(World *w, const Tuning *t, EnemyType type, int count, Vector3 center);

// Põe um boss em cena, chegando da direção `dir` (também usado pelas hordas).
void world_spawn_boss(World *w, const Tuning *t, BossType type, Vector3 dir);

// Contexto para o boss pensar neste passo.
BossContext world_boss_context(World *w, const Tuning *t);

// O mesmo, com a variação (desenho e cor) escolhida: -1 = sorteada, como no jogo.
int world_spawn_variant(World *w, const Tuning *t, EnemyType type, int count, Vector3 center, int variant);

// Ferramenta do autoplay: tira de cena todos os inimigos, tiros e rochas.
void world_clear_enemies(World *w);

// Um meteoro passando perto do planeta, do lado da nave (também usado pelas
// calmarias). Ferramenta do autoplay: com `pos` e `vel`, exatamente ali.
void world_spawn_meteor(World *w, const Tuning *t);
Rock *world_spawn_rock(World *w, const Tuning *t, RockKind kind, Vector3 pos, Vector3 vel);

// Um tiro da nave acertou a rocha `index` em `point` (`back` aponta de volta
// para quem atirou). Quebrada, ela solta cristais de minério.
void world_damage_rock(World *w, const Tuning *t, int index, float damage, Vector3 point, Vector3 back);

// Nova partida com o mesmo plano: planeta inteiro, sem inimigos, nave no
// ponto de partida, pontuação zerada.
void world_restart(World *w, const Tuning *t);

// A partida está andando (câmera de jogo, nave em cena)?
bool world_in_play(const World *w);

// Contexto para os inimigos pensarem (também usado nos testes).
EnemyContext world_enemy_context(World *w, const Tuning *t);

// Um impacto vindo de `origin` na direção `dir`: acha onde o raio toca o solo
// e abre ali uma cratera com a boca de raio `rim_radius` e a profundidade
// `depth`, soltando destroços. Retorna false se o raio não acerta o planeta.
bool world_impact(World *w, const Tuning *t, Vector3 origin, Vector3 dir, float rim_radius, float depth);

// A mesma cratera, mas num ponto dado (`ground`, em geral a superfície
// original), sem procurar o chão de agora: golpes repetidos no mesmo lugar não
// cavam mais fundo (a mordida do Devorador). Com `fragments` false, não
// solta fragmentos (o pedaço arrancado some).
bool world_impact_at(World *w, const Tuning *t, Vector3 ground, float rim_radius, float depth, bool fragments);

// Onde uma defesa comprada agora seria construída: o ponto do chão onde a
// mira toca o planeta, ou o chão embaixo da nave. false sem nave.
bool world_build_point(const World *w, Vector3 *ground);

// Retira uma esfera do planeta sem o estardalhaço de um impacto (a broca do
// Escavador, o feixe do Sifão): poucos destroços, sem tremor nem som.
void world_carve(World *w, const Tuning *t, Vector3 center, float radius);

// Ferramenta de debug: devolve o planeta ao estado inicial.
void world_reset_planet(World *w, const Tuning *t);

// Uma pancada na nave: quem desenha usa isto para tremer a câmera.
void world_jolt(World *w, float strength);

// Impacto de número `id` (1 = o primeiro), ou NULL se ele já saiu da fila.
const Impact *world_find_impact(const World *w, uint32_t id);

// Pose para desenhar: mistura a anterior com a atual. alpha em [0, 1].
CamPose world_view(const World *w, float alpha);

// Resumo numérico do estado. Duas execuções com a mesma seed e os mesmos
// inputs precisam chegar ao mesmo valor; é assim que se confere o determinismo.
uint64_t world_hash(const World *w);

static inline Vector3 cam_forward(CamPose p) { return quat_forward(p.rot); }
static inline Vector3 cam_right(CamPose p)   { return quat_right(p.rot); }
static inline Vector3 cam_up(CamPose p)      { return quat_up(p.rot); }

#endif
