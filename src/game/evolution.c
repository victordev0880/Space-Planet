#include "game/evolution.h"

#include <math.h>
#include <string.h>

static const EvolutionInfo INFO[EVO_COUNT] = {
    [EVO_ESCORT]        = { "Drones de escolta", "escort", ENEMY_SWARM },
    [EVO_SPREAD]        = { "Tiro em leque", "spread", ENEMY_SWARM },
    [EVO_BLAST_DASH]    = { "Dash explosivo", "blast_dash", ENEMY_KAMIKAZE },
    [EVO_CHAIN]         = { "Reação em cadeia", "chain", ENEMY_KAMIKAZE },
    [EVO_HEAVY_HULL]    = { "Casco pesado", "heavy_hull", ENEMY_BOMBER },
    [EVO_WARHEADS]      = { "Ogivas", "warheads", ENEMY_BOMBER },
    [EVO_PIERCE]        = { "Tiro perfurante", "pierce", ENEMY_DIGGER },
    [EVO_ARMOR_BREAK]   = { "Quebra-blindagem", "armor_break", ENEMY_DIGGER },
    [EVO_LIFESTEAL]     = { "Roubo de vida", "lifesteal", ENEMY_SIPHON },
    [EVO_ENERGY_SIPHON] = { "Sifão de energia", "energy_siphon", ENEMY_SIPHON },
    [EVO_CRIT]          = { "Acerto crítico", "crit", ENEMY_SNIPER },
    [EVO_LONG_RANGE]    = { "Mira longa", "long_range", ENEMY_SNIPER },
    [EVO_BIG_SHIELD]    = { "Escudo maior", "big_shield", ENEMY_SHIELDER },
    [EVO_REFLECT]       = { "Reflexão", "reflect", ENEMY_SHIELDER },
    [EVO_FIGHTERS]      = { "Esquadrilha", "fighters", ENEMY_CARRIER },
    [EVO_HANGAR]        = { "Hangar rápido", "hangar", ENEMY_CARRIER },
    [EVO_SYN_SUICIDE]   = { "Drones suicidas", "suicide", ENEMY_SWARM, true, ENEMY_KAMIKAZE },
    [EVO_SYN_RESTORE]   = { "Feixe restaurador", "restore", ENEMY_SIPHON, true, ENEMY_DIGGER },
    [EVO_SYN_SALVAGE]   = { "Desmanche", "salvage", ENEMY_BOMBER, true, ENEMY_CARRIER },
    [EVO_SYN_POISE]     = { "Mira blindada", "poise", ENEMY_SNIPER, true, ENEMY_SHIELDER },
};

const EvolutionInfo *evolution_info(EvolutionId id)
{
    if ((int)id < 0 || id >= EVO_COUNT) id = EVO_ESCORT;
    return &INFO[id];
}

bool evolution_is_synergy(EvolutionId id)
{
    return (int)id >= 0 && id < EVO_COUNT && INFO[id].synergy;
}

bool evolution_has_line(const Evolution *e, EnemyType line)
{
    for (int i = 0; i < EVO_COUNT; i++) {
        if (!INFO[i].synergy && INFO[i].line == line && e->rank[i] > 0) return true;
    }
    return false;
}

int evolution_from_key(const char *key)
{
    for (int i = 0; i < EVO_COUNT; i++) {
        if (strcmp(INFO[i].key, key) == 0) return i;
    }
    return -1;
}

void evolution_reset(Evolution *e)
{
    memset(e, 0, sizeof *e);
    e->level = 1;
    for (int i = 0; i < EVO_CHOICES; i++) e->choices[i] = -1;
}

float evolution_xp_needed(const Tuning *t, int level)
{
    if (level < 1) level = 1;
    return t->xp_base * powf(t->xp_growth, (float)(level - 1));
}

int evolution_gain(Evolution *e, const Tuning *t, EnemyType type, float xp)
{
    if (xp <= 0.0f) return 0;
    if ((int)type >= 0 && type < ENEMY_TYPE_COUNT) e->essence[type] += xp;
    e->xp += xp;
    int gained = 0;
    // Vários níveis de uma vez são possíveis (um Porta-drones no começo).
    while (e->xp >= evolution_xp_needed(t, e->level)) {
        e->xp -= evolution_xp_needed(t, e->level);
        e->level++;
        e->pending++;
        gained++;
    }
    return gained;
}

float evolution_weight(const Evolution *e, const Tuning *t, EvolutionId id)
{
    if (e->rank[id] >= t->evo_max[id]) return 0.0f;
    if (INFO[id].synergy) {
        // Só com as duas linhas; aí, com peso alto: é a recompensa de misturar.
        bool both = evolution_has_line(e, INFO[id].line) && evolution_has_line(e, INFO[id].partner);
        return both ? t->cards_synergy_weight : 0.0f;
    }
    return t->cards_base_weight + t->cards_essence_weight * e->essence[INFO[id].line];
}

void evolution_offer(Evolution *e, const Tuning *t, Rng *rng)
{
    while (!e->choosing && e->pending > 0) {
        // Sorteio sem repetição: cada carta sai de um sorteio ponderado entre
        // as que ainda não saíram.
        float weights[EVO_COUNT];
        float total = 0.0f;
        for (int i = 0; i < EVO_COUNT; i++) {
            weights[i] = evolution_weight(e, t, (EvolutionId)i);
            total += weights[i];
        }
        e->choice_count = 0;
        for (int c = 0; c < EVO_CHOICES; c++) e->choices[c] = -1;
        for (int c = 0; c < EVO_CHOICES && total > 0.0f; c++) {
            float roll = rng_float(rng) * total;
            int picked = -1;
            for (int i = 0; i < EVO_COUNT; i++) {
                if (weights[i] <= 0.0f) continue;
                picked = i;   // sobra de arredondamento cai na última com peso
                if (roll < weights[i]) break;
                roll -= weights[i];
            }
            if (picked < 0) break;
            e->choices[e->choice_count++] = picked;
            total -= weights[picked];
            weights[picked] = 0.0f;
        }
        if (e->choice_count == 0) {
            // Tudo no máximo: o nível não tem o que oferecer.
            e->pending = 0;
            return;
        }
        e->choosing = true;
        e->armed = false;
    }
}

int evolution_pick(Evolution *e, const Tuning *t, Rng *rng, int index)
{
    if (!e->choosing || index < 0 || index >= e->choice_count) return -1;
    int id = e->choices[index];
    if (id < 0 || id >= EVO_COUNT) return -1;
    if (e->rank[id] < t->evo_max[id]) e->rank[id]++;
    if (e->history_count < EVO_HISTORY) e->history[e->history_count++] = (uint8_t)id;
    e->choosing = false;
    e->pending--;
    for (int c = 0; c < EVO_CHOICES; c++) e->choices[c] = -1;
    e->choice_count = 0;
    evolution_offer(e, t, rng);
    return id;
}

void evolution_tune(const Evolution *e, const Tuning *base, Tuning *out)
{
    *out = *base;
    float hull = (float)e->rank[EVO_HEAVY_HULL];
    float shield = (float)e->rank[EVO_BIG_SHIELD];
    float range = (float)e->rank[EVO_LONG_RANGE];
    out->ship_hull_max += base->heavy_hull_hull * hull;
    out->ship_shield_max += base->big_shield_shield * shield;
    out->ship_shield_regen *= 1.0f + base->big_shield_regen * shield;
    out->weapon_speed *= 1.0f + base->long_range_speed * range;
    out->weapon_range *= 1.0f + base->long_range_speed * range;
    out->weapon_assist_angle += base->long_range_assist * range;
    out->weapon_assist_range *= 1.0f + base->long_range_speed * range;
}

int evolution_dominant_line(const Evolution *e)
{
    int ranks[ENEMY_TYPE_COUNT] = { 0 };
    for (int i = 0; i < EVO_COUNT; i++) {
        if (!INFO[i].synergy) ranks[INFO[i].line] += e->rank[i];
    }
    int best = -1, most = 0;
    for (int line = 0; line < ENEMY_TYPE_COUNT; line++) {
        if (ranks[line] > most) {
            most = ranks[line];
            best = line;
        }
    }
    return best;
}

int evolution_total_ranks(const Evolution *e)
{
    int total = 0;
    for (int i = 0; i < EVO_COUNT; i++) total += e->rank[i];
    return total;
}
