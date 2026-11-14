// As evoluções da nave: duas por linha (cada linha é um tipo de inimigo), e
// as sinergias, que juntam duas linhas. Os números de cada uma ficam em
// data/evolutions.txt; nomes e textos, em game/evolution.c e ui/cards.c.
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

    // Sinergias: aparecem nas cartas quando a nave já tem evoluções das duas linhas.
    EVO_SYN_SUICIDE,     // Enxame + Kamikaze: drones suicidas teleguiados
    EVO_SYN_RESTORE,     // Sifão + Escavador: minerar também conserta o planeta
    EVO_SYN_SALVAGE,     // Bombardeiro + Porta-drones: abates deixam minério
    EVO_SYN_POISE,       // Atirador + Escudeiro: escudo cheio, tiros mais fortes
    EVO_COUNT
} EvolutionId;

#define EVO_FIRST_SYNERGY EVO_SYN_SUICIDE

#endif
