// Hordas: a sequência de ataques de uma partida, lida de data/waves.txt.
//
// Formato (chave = valor):
//   wave.1.pause   = 6                     calmaria, em segundos, antes da horda 1
//   wave.1.group.1 = swarm 6 0 40 10       tipo, quantidade, atraso (s), azimute e
//   wave.1.group.2 = swarm 6 8 -30 25      elevação (graus) da direção de chegada
//   wave.5.group.1 = breaker 1 0 20 20     um boss entra como um grupo de um
//                                          (a horda só acaba quando ele cai)
// As hordas são numeradas de 1 em diante, sem pular; os grupos também.
// Azimute e elevação seguem a convenção do Sol (sun.azimuth/sun.elevation).
//
// O diretor anda pela sequência: espera a calmaria, solta os grupos da horda
// conforme os atrasos e, quando todos entraram e não sobra nenhum inimigo,
// passa para a calmaria da seguinte. Depois da última, a partida está ganha.
#ifndef GAME_WAVES_H
#define GAME_WAVES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/config.h"
#include "core/vmath.h"
#include "game/boss_types.h"
#include "game/enemy_types.h"

#define WAVES_MAX       24
#define WAVE_GROUPS_MAX 12
#define WAVE_GROUP_MAX_COUNT 24   // inimigos em um grupo

typedef struct {
    uint8_t type;        // EnemyType
    uint8_t count;
    float   delay;       // segundos depois do começo da horda
    float   azimuth;     // de onde vem, em graus
    float   elevation;
} WaveGroup;

typedef struct {
    float     pause;     // calmaria antes desta horda
    int       group_count;
    WaveGroup groups[WAVE_GROUPS_MAX];
} WaveDef;

typedef struct {
    int     count;
    WaveDef waves[WAVES_MAX];
} WavePlan;

// Retornam false e descrevem o problema em `err` se o arquivo estiver errado.
bool waves_parse(WavePlan *plan, const Config *c, char *err, size_t err_len);
bool waves_load(WavePlan *plan, const char *path, char *err, size_t err_len);

// Direção de chegada de um grupo, do centro do planeta para fora (comprimento 1).
Vector3 wave_group_direction(const WaveGroup *g);

int wave_enemy_count(const WaveDef *w, EnemyType type);

const char *enemy_type_name(EnemyType type);   // nome no arquivo: "swarm", "bomber"
int         enemy_type_from_name(const char *name);   // -1 se o nome não existe
int         boss_type_from_name(const char *name);    // "breaker" -> BOSS_BREAKER; -1 se não existe

// O boss da horda (BossType), ou -1 se ela não tem boss.
int wave_boss(const WaveDef *w);

// --- diretor ----------------------------------------------------------------

typedef enum {
    DIRECTOR_OFF,      // sem hordas (roteiros de teste, ou plano vazio)
    DIRECTOR_PAUSE,    // calmaria antes da horda `wave`
    DIRECTOR_ACTIVE,   // horda `wave` em andamento
    DIRECTOR_DONE,     // todas vencidas
} DirectorPhase;

typedef struct {
    uint8_t  phase;     // DirectorPhase
    int      wave;      // horda atual ou a próxima (0 = a primeira)
    float    timer;     // na calmaria: segundos até começar; na horda: segundos desde o começo
    uint32_t spawned;   // grupos da horda atual que já entraram (um bit por grupo)
} WaveDirector;

// O que o diretor pede em um passo.
typedef struct {
    int              spawn_count;
    const WaveGroup *spawn[WAVE_GROUPS_MAX];   // grupos que entram agora
    bool             wave_started;
    bool             wave_cleared;
    bool             finished;                 // a última horda acabou de ser vencida
} DirectorStep;

void director_start(WaveDirector *d, const WavePlan *plan);   // começa pela calmaria da horda 1
void director_stop(WaveDirector *d);

// `enemies_alive`: inimigos em cena agora (a horda só acaba quando zera).
DirectorStep director_tick(WaveDirector *d, const WavePlan *plan, int enemies_alive, float dt);

#endif
