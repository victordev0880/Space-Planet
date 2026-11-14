// Eventos do jogo: o que aconteceu em um passo e merece uma reação de quem
// não faz parte da simulação (som, tremor de câmera, textos na tela).
//
// A lógica só anota; quem desenha e quem toca os sons leem depois. Cada
// evento tem um número de ordem (1 para o primeiro da partida, 2 para o
// seguinte...). Cada leitor guarda até qual número já reagiu e, a cada
// quadro, trata os novos. Os eventos ficam em uma fila circular: os mais
// antigos vão sendo sobrescritos, e o passo em que aconteceram permite saber
// a idade de cada um (os textos na tela duram alguns segundos).
#ifndef GAME_EVENTS_H
#define GAME_EVENTS_H

#include <stdint.h>

#include "core/vmath.h"

typedef enum {
    EV_SHOT,           // a nave atirou                     (pos = cano)
    EV_ENEMY_SHOT,     // um inimigo atirou                 (sub = tipo)
    EV_HIT,            // tiro da nave acertou um inimigo   (sub = tipo; value = dano)
    EV_ARMOR_HIT,      // ... mas na blindagem
    EV_WEAK_HIT,       // ... no ponto fraco
    EV_KILL,           // inimigo destruído pela nave       (sub = tipo; value = pontos ganhos)
    EV_CRASH,          // inimigo destruído sem a nave      (sub = tipo)
    EV_SPARK,          // tiro da nave bateu no planeta ou na Lua
    EV_BOMB_ARMING,    // bombardeiro marcou um alvo        (pos = alvo no solo; value = segundos até soltar)
    EV_BOMB_DROP,      // a bomba saiu                      (pos = bombardeiro)
    EV_BOMB_DOWN,      // bomba derrubada no ar             (value = pontos ganhos)
    EV_BOMB_IMPACT,    // bomba atingiu o planeta
    EV_PLAYER_HIT,     // a nave levou dano                 (value = dano; sub = 1 se o escudo segurou tudo)
    EV_PLAYER_DOWN,    // a nave foi destruída
    EV_SPAWN,          // um grupo entrou em cena           (pos = centro; sub = tipo; value = quantidade)
    EV_WAVE_START,     // começou uma horda                 (value = número da horda, a partir de 1)
    EV_WAVE_CLEAR,     // horda vencida                     (value = bônus de planeta, 0 se não houve)
    EV_GAME_WON,
    EV_GAME_LOST,
    EV_DASH,
    EV_BUMP,           // a nave bateu no terreno           (value = velocidade contra o chão)
    EV_LOCK,           // um Kamikaze travou na nave
    EV_DETONATE,       // um Kamikaze explodiu junto da nave
    EV_DIG_START,      // um Escavador começou a cavar      (pos = boca do túnel)
    EV_DRAIN_START,    // um Sifão começou a drenar         (pos = onde o feixe toca o solo)
    EV_SNIPER_CHARGE,  // um Atirador começou a carregar    (value = segundos até o tiro)
    EV_SHIELDED,       // tiro da nave parado por um escudo
    EV_LAUNCH,         // o Porta-drones soltou um bando    (value = quantos)
    EV_CALM_SKIP,      // o jogador chamou a próxima horda antes da hora
    EV_ALLY_SHOT,      // tiro de um aliado                 (sub = AllyKind)
    EV_LEVEL_UP,       // a nave subiu de nível             (value = nível novo)
    EV_EVOLVE,         // uma evolução foi escolhida        (sub = EvolutionId, value = nível dela)
    EV_REFLECT,        // o escudo devolveu um tiro inimigo
    EV_BLAST,          // explosão da nave (dash explosivo, reação em cadeia; value = raio)
    EV_CRIT,           // acerto crítico
    EV_BOSS_ENTER,     // um boss chegou                    (sub = BossType)
    EV_BOSS_CHARGE,    // o Rompe-Mundos começou a carregar (value = segundos de carga)
    EV_BOSS_FIRE,      // ... e disparou o laser
    EV_BOSS_CAPACITOR, // um capacitor foi destruído        (value = quantos sobram)
    EV_BOSS_STAGGER,   // o boss ficou atordoado (núcleo aberto)
    EV_BOSS_PHASE,     // o boss mudou de fase              (value = fase nova)
    EV_BOSS_BLAST,     // explosão no casco do boss morrendo
    EV_BOSS_DOWN,      // o boss foi destruído              (value = pontos)
    EV_REWARD,         // a nave ganhou o núcleo do boss
    EV_ABILITY,        // o raio do núcleo saiu             (value = alcance)
    EV_METEOR,         // um meteoro apareceu               (pos = onde ele está)
    EV_FRAGMENT,       // um fragmento saiu de uma cratera
    EV_TRACTOR_GRAB,   // o raio trator pegou uma rocha     (sub = RockKind)
    EV_TRACTOR_MISS,   // ... tentou, mas não havia rocha ao alcance
    EV_TRACTOR_RELEASE,// a nave soltou a rocha
    EV_TRACTOR_BREAK,  // o raio arrebentou                 (value = 1 se esticou demais, 0 se a nave caiu)
    EV_DEPOSIT,        // uma rocha tocou o chão: o conserto começou (value = raio dele)
    EV_REPAIR,         // um conserto terminou              (pos = centro; value = fração do planeta devolvida)
    EV_ROCK_HIT,       // a nave bateu numa rocha           (value = velocidade da batida)
    EV_ROCK_SHOT,      // tiro da nave numa rocha           (sub = RockKind)
    EV_ROCK_MINED,     // a rocha quebrou em minério        (sub = RockKind; value = cristais)
    EV_ORE,            // a nave coletou minério            (value = quantos cristais neste passo)
    EV_BUY,            // uma defesa foi comprada           (sub = DefenseType)
    EV_BUY_FAIL,       // ... ou não                        (sub = DefenseType; value = 0 sem minério, 1 no limite)
    EV_TURRET_SHOT,    // uma torre atirou
    EV_INTERCEPT,      // um satélite derrubou um tiro ou bomba (pos = onde)
    EV_SHIELD_BLOCK,   // um escudo segurou uma cratera     (value = raio dela)
    EV_SHIELD_DOWN,    // um escudo acabou                  (value = 1 se quebrou, 0 se o tempo acabou)
    EV_HIVE_INFLATE,   // uma bolsa da Colmeia começou a inchar (pos = bolsa; value = segundos até soltar)
    EV_HIVE_POP,       // a nave estourou uma bolsa         (value = quantas sobram)
    EV_HIVE_BIRTH,     // uma bolsa soltou drones           (value = quantos)
    EV_HIVE_THRUSTER,  // um propulsor foi destruído        (value = quantos sobram)
    EV_HIVE_PUSH,      // a Colmeia começou (value = 1) ou parou (0) de empurrar a Lua
    EV_HIVE_SPIT_WARN, // o coração vai cuspir ácido        (value = segundos)
    EV_SPORE_BLAST,    // um esporo explodiu                (value = 1 se acertou o coração)
    EV_BROOD,          // a Ninhada saiu (núcleo Colmeia)   (value = ferrões)
    EV_WORM_RUMBLE,    // o chão racha: o Verme vai sair    (pos = saída; value = segundos)
    EV_WORM_BREACH,    // o Verme saltou para fora          (pos = saída)
    EV_WORM_DIVE,      // ... e mergulhou de volta          (pos = mergulho)
    EV_WORM_CORE,      // o Verme mordeu o núcleo           (pos = mordida)
    EV_WORM_NODULE,    // um nódulo do dorso foi destruído  (value = quantos sobram)
    EV_TUNNEL,         // a nave atravessou o planeta (núcleo do Verme) (pos = saída)
    EV_DEV_AIM,        // o Devorador marcou a mordida      (pos = ponto no chão; value = segundos)
    EV_DEV_BITE,       // ... mordeu                        (pos = ponto)
    EV_DEV_MISS,       // ... errou (olhos acertados)
    EV_DEV_BROKEN,     // ... quebrou os dentes num escudo
    EV_DEV_SWALLOW,    // ... engoliu uma rocha             (value = fração da vida curada)
    EV_DEV_LATCH,      // ... agarrou o planeta (banquete)
    EV_DEV_TORN,       // ... foi arrancado do planeta
    EV_DEV_TEETH,      // ... cuspiu uma rajada de dentes
    EV_TYPE_COUNT
} EventType;

typedef struct {
    uint32_t id;      // 1 = o primeiro evento da partida
    uint32_t tick;    // passo em que aconteceu
    uint8_t  type;    // EventType
    uint8_t  sub;
    Vector3  pos;
    float    value;
} GameEvent;

// Folga para alguns segundos de combate intenso: os avisos na tela leem
// eventos de até 3 segundos atrás.
#define EVENTS_CAPACITY 1024

typedef struct {
    GameEvent ring[EVENTS_CAPACITY];
    uint32_t  count;   // total desde o começo; o último evento tem id == count
} Events;

void events_push(Events *e, uint32_t tick, EventType type, uint8_t sub, Vector3 pos, float value);

// Evento de número `id`, ou NULL se ainda não aconteceu ou já foi sobrescrito.
const GameEvent *events_find(const Events *e, uint32_t id);

#endif
