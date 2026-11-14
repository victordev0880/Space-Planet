// Evolução da nave: XP, níveis, essências e cartas.
//
// Cada abate dá XP e a mesma quantidade de Essência do tipo derrotado. A cada
// nível, o jogador escolhe 1 entre 3 cartas; enquanto as cartas estão na
// tela, a partida fica congelada (ver game/world.c). As cartas são sorteadas
// sem repetição, com peso maior para as linhas (tipos de inimigo) de que o
// jogador tem mais essência: quem você mais derrota define quem você vira.
//
// Os efeitos que só mudam números (casco, escudo, cadência, alcance) entram
// em um tuning "efetivo" (evolution_tune), que o mundo usa no lugar do base.
// Os efeitos com comportamento próprio (tiro perfurante, reflexão, drones)
// leem os níveis direto de `rank`.
#ifndef GAME_EVOLUTION_H
#define GAME_EVOLUTION_H

#include <stdbool.h>
#include <stdint.h>

#include "core/rng.h"
#include "game/enemy_types.h"
#include "game/evolution_types.h"
#include "game/tuning.h"

#define EVO_CHOICES 3     // cartas por nível
#define EVO_HISTORY 48    // escolhas guardadas para a tela de fim

typedef struct {
    const char *name;     // nome da carta ("Drones de escolta")
    const char *key;      // nome nos dados e nos roteiros ("escort")
    EnemyType   line;     // linha (tipo de inimigo) a que pertence
    bool        synergy;  // sinergia: junta `line` e `partner`
    EnemyType   partner;
} EvolutionInfo;

// Uma sinergia? (Junta duas linhas e só aparece quando a nave tem as duas.)
bool evolution_is_synergy(EvolutionId id);

// A evolução de um nome de dados ("escort"), ou -1.
int evolution_from_key(const char *key);

typedef struct {
    int      level;                       // começa em 1
    float    xp;                          // XP acumulado dentro do nível atual
    float    essence[ENEMY_TYPE_COUNT];
    uint8_t  rank[EVO_COUNT];             // nível de cada evolução (0 = não tem)
    int      pending;                     // níveis ganhos ainda sem carta escolhida
    bool     choosing;                    // cartas na tela: a partida está congelada
    bool     armed;                       // as teclas das cartas foram soltas desde que elas abriram
    int      choices[EVO_CHOICES];        // EvolutionId de cada carta (-1 = vazia)
    int      choice_count;
    uint8_t  history[EVO_HISTORY];        // evoluções escolhidas, em ordem
    int      history_count;
} Evolution;

const EvolutionInfo *evolution_info(EvolutionId id);

// A nave já tem alguma evolução comum da linha `line`?
bool evolution_has_line(const Evolution *e, EnemyType line);

void evolution_reset(Evolution *e);

// XP que falta acumular para sair do nível `level` para o seguinte.
float evolution_xp_needed(const Tuning *t, int level);

// Soma XP e essência de `type`. Devolve quantos níveis foram ganhos.
int evolution_gain(Evolution *e, const Tuning *t, EnemyType type, float xp);

// Peso de uma evolução no sorteio (0 = já está no nível máximo).
float evolution_weight(const Evolution *e, const Tuning *t, EvolutionId id);

// Abre as cartas do próximo nível pendente (se houver e se ainda não estiver
// escolhendo). Sem nenhuma evolução disponível, o nível é perdido em silêncio.
void evolution_offer(Evolution *e, const Tuning *t, Rng *rng);

// Escolhe a carta `index` (0 a choice_count - 1). Devolve a evolução
// escolhida, ou -1 se não havia escolha aberta ou o índice é inválido. Se
// ainda houver níveis pendentes, as próximas cartas já ficam abertas.
int evolution_pick(Evolution *e, const Tuning *t, Rng *rng, int index);

// O tuning com os efeitos numéricos das evoluções aplicados sobre `base`.
void evolution_tune(const Evolution *e, const Tuning *base, Tuning *out);

// A linha com mais níveis de evolução (cor da nave); -1 se nenhuma.
int evolution_dominant_line(const Evolution *e);

// Total de níveis de evolução escolhidos.
int evolution_total_ranks(const Evolution *e);

#endif
