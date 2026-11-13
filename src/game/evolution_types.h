// As evoluções da nave: duas por linha (cada linha é um tipo de inimigo).
// Os números de cada uma ficam em data/evolutions.txt; nomes e textos, em
// game/evolution.c e ui/cards.c.
#ifndef GAME_EVOLUTION_TYPES_H
#define GAME_EVOLUTION_TYPES_H

typedef enum {
    EVO_ESCORT,          // Enxame: drones de escolta em volta da nave
    EVO_SPREAD,          // Enxame: tiro em leque
    EVO_BLAST_DASH,      // Kamikaze: o dash explode
    EVO_CHAIN,           // Kamikaze: inimigos abatidos explodem
    EVO_HEAVY_HULL,      // Bombardeiro: casco pesado
    EVO_WARHEADS,        // Bombardeiro: tiros com dano em área
    EVO_PIERCE,          // Escavador: tiros atravessam inimigos
    EVO_ARMOR_BREAK,     // Escavador: quebra-blindagem
    EVO_LIFESTEAL,       // Sifão: abates recuperam o casco
    EVO_ENERGY_SIPHON,   // Sifão: acertos recuperam energia
    EVO_CRIT,            // Atirador: acerto crítico
    EVO_LONG_RANGE,      // Atirador: mira longa
    EVO_BIG_SHIELD,      // Escudeiro: escudo maior
    EVO_REFLECT,         // Escudeiro: o escudo devolve tiros
    EVO_FIGHTERS,        // Porta-drones: caças aliados
    EVO_HANGAR,          // Porta-drones: aliados mais rápidos e um drone a mais
    EVO_COUNT
} EvolutionId;

#endif
