// Os tipos de inimigo e os números que todos eles têm em comum.
//
// Fica separado de game/enemies.h porque o tuning (que guarda esses números)
// precisa conhecer os tipos, e o módulo de inimigos precisa do tuning.
#ifndef GAME_ENEMY_TYPES_H
#define GAME_ENEMY_TYPES_H

typedef enum {
    ENEMY_SWARM,     // Enxame: drones em bando, fazem passadas rasantes atirando no solo
    ENEMY_BOMBER,    // Bombardeiro: lento, blindado, solta bombas avisadas
    ENEMY_KAMIKAZE,  // Kamikaze: persegue a nave e explode, ou mergulha no planeta
    ENEMY_DIGGER,    // Escavador: pousa e cava um túnel rumo ao núcleo
    ENEMY_SIPHON,    // Sifão: paira e drena o solo com um feixe
    ENEMY_SNIPER,    // Atirador: de longe, carrega tiros fortes contra a nave
    ENEMY_SHIELDER,  // Escudeiro: protege os aliados perto dele
    ENEMY_CARRIER,   // Porta-drones: orbita alto e solta bandos de Enxame
    ENEMY_TYPE_COUNT
} EnemyType;

typedef struct {
    float hp;           // tiros (de dano 1) para destruir
    float radius;       // raio de colisão
    float scale;        // tamanho do modelo
    float speed;        // velocidade de cruzeiro
    float accel;        // agilidade: quanto muda de velocidade por segundo
    float points;       // pontos ao ser destruído pelo jogador
    float ram_damage;   // dano na nave do jogador ao colidir com ela (no Kamikaze, a explosão)
} EnemyStats;

#endif
