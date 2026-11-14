#include "game/waves.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const TYPE_NAMES[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = "swarm",
    [ENEMY_BOMBER]   = "bomber",
    [ENEMY_KAMIKAZE] = "kamikaze",
    [ENEMY_DIGGER]   = "digger",
    [ENEMY_SIPHON]   = "siphon",
    [ENEMY_SNIPER]   = "sniper",
    [ENEMY_SHIELDER] = "shielder",
    [ENEMY_CARRIER]  = "carrier",
};

const char *enemy_type_name(EnemyType type)
{
    if ((int)type < 0 || type >= ENEMY_TYPE_COUNT) return "?";
    return TYPE_NAMES[type];
}

int enemy_type_from_name(const char *name)
{
    for (int i = 0; i < ENEMY_TYPE_COUNT; i++) {
        if (strcmp(TYPE_NAMES[i], name) == 0) return i;
    }
    return -1;
}

static const char *const BOSS_NAMES[BOSS_TYPE_COUNT] = {
    [BOSS_BREAKER] = "breaker",
    [BOSS_HIVE]    = "hive",
};

int boss_type_from_name(const char *name)
{
    for (int i = 0; i < BOSS_TYPE_COUNT; i++) {
        if (strcmp(BOSS_NAMES[i], name) == 0) return i;
    }
    return -1;
}

int wave_boss(const WaveDef *w)
{
    for (int g = 0; g < w->group_count; g++) {
        if (w->groups[g].type >= WAVE_BOSS_BASE) return w->groups[g].type - WAVE_BOSS_BASE;
    }
    return -1;
}

// Lê "wave.N.pause" ou "wave.N.group.M". Devolve 1 (pausa), 2 (grupo) ou 0.
static int parse_key(const char *key, int *wave, int *group)
{
    int used = 0;
    if (sscanf(key, "wave.%d.pause%n", wave, &used) == 1 && key[used] == '\0') return 1;
    used = 0;
    if (sscanf(key, "wave.%d.group.%d%n", wave, group, &used) == 2 && key[used] == '\0') return 2;
    return 0;
}

bool waves_parse(WavePlan *plan, const Config *c, char *err, size_t err_len)
{
    // static: a cópia de trabalho é grande para a pilha.
    static WavePlan p;
    memset(&p, 0, sizeof p);
    bool has_pause[WAVES_MAX] = { false };

    for (int i = 0; i < c->count; i++) {
        const ConfigEntry *e = &c->entries[i];
        int wave = 0, group = 0;
        int kind = parse_key(e->key, &wave, &group);
        if (kind == 0) {
            snprintf(err, err_len, "chave desconhecida: %s", e->key);
            return false;
        }
        if (wave < 1 || wave > WAVES_MAX) {
            snprintf(err, err_len, "%s: hordas vao de 1 a %d", e->key, WAVES_MAX);
            return false;
        }
        WaveDef *w = &p.waves[wave - 1];
        if (wave > p.count) p.count = wave;

        if (kind == 1) {
            if (!e->is_number || !(e->number >= 0.0 && e->number <= 600.0)) {
                snprintf(err, err_len, "%s = \"%s\": esperado um numero de 0 a 600", e->key, e->text);
                return false;
            }
            w->pause = (float)e->number;
            has_pause[wave - 1] = true;
            continue;
        }

        if (group < 1 || group > WAVE_GROUPS_MAX) {
            snprintf(err, err_len, "%s: grupos vao de 1 a %d", e->key, WAVE_GROUPS_MAX);
            return false;
        }
        char name[16];
        int count = 0;
        float delay = 0.0f, azimuth = 0.0f, elevation = 0.0f;
        char extra;
        if (sscanf(e->text, "%15s %d %f %f %f %c", name, &count, &delay, &azimuth, &elevation, &extra) != 5) {
            snprintf(err, err_len, "%s = \"%s\": esperado \"tipo quantidade atraso azimute elevacao\"", e->key,
                     e->text);
            return false;
        }
        int type = enemy_type_from_name(name);
        if (type < 0) {
            int boss = boss_type_from_name(name);
            if (boss < 0) {
                snprintf(err, err_len, "%s: tipo de inimigo desconhecido: \"%s\"", e->key, name);
                return false;
            }
            if (count != 1) {
                snprintf(err, err_len, "%s: um boss entra sozinho (quantidade 1)", e->key);
                return false;
            }
            type = WAVE_BOSS_BASE + boss;
        }
        if (count < 1 || count > WAVE_GROUP_MAX_COUNT || !(delay >= 0.0f && delay <= 600.0f)
            || !(elevation >= -90.0f && elevation <= 90.0f) || !isfinite(azimuth)) {
            snprintf(err, err_len, "%s = \"%s\": valores fora da faixa", e->key, e->text);
            return false;
        }
        WaveGroup *g = &w->groups[group - 1];
        if (g->count != 0) {
            snprintf(err, err_len, "%s repetido", e->key);
            return false;
        }
        g->type = (uint8_t)type;
        g->count = (uint8_t)count;
        g->delay = delay;
        g->azimuth = azimuth;
        g->elevation = elevation;
        if (group > w->group_count) w->group_count = group;
    }

    // Sem buracos na numeração, e toda horda com calmaria e algum grupo.
    for (int i = 0; i < p.count; i++) {
        const WaveDef *w = &p.waves[i];
        if (!has_pause[i] || w->group_count == 0) {
            snprintf(err, err_len, "a horda %d precisa de wave.%d.pause e de pelo menos um grupo", i + 1, i + 1);
            return false;
        }
        for (int g = 0; g < w->group_count; g++) {
            if (w->groups[g].count == 0) {
                snprintf(err, err_len, "falta wave.%d.group.%d", i + 1, g + 1);
                return false;
            }
        }
    }

    *plan = p;
    if (err_len > 0) err[0] = '\0';
    return true;
}

bool waves_load(WavePlan *plan, const char *path, char *err, size_t err_len)
{
    static Config config;
    if (!config_load_file(&config, path)) {
        snprintf(err, err_len, "%s", config.error);
        return false;
    }
    return waves_parse(plan, &config, err, err_len);
}

Vector3 wave_group_direction(const WaveGroup *g)
{
    float azimuth = g->azimuth * DEG2RAD;
    float elevation = g->elevation * DEG2RAD;
    return (Vector3){ cosf(elevation) * sinf(azimuth), sinf(elevation), cosf(elevation) * cosf(azimuth) };
}

int wave_enemy_count(const WaveDef *w, EnemyType type)
{
    int count = 0;
    for (int g = 0; g < w->group_count; g++) {
        if (w->groups[g].type == type) count += w->groups[g].count;
    }
    return count;
}

// ---------------------------------------------------------------------------

void director_start(WaveDirector *d, const WavePlan *plan)
{
    memset(d, 0, sizeof *d);
    if (plan->count == 0) {
        d->phase = DIRECTOR_OFF;
        return;
    }
    d->phase = DIRECTOR_PAUSE;
    d->timer = plan->waves[0].pause;
}

void director_stop(WaveDirector *d)
{
    memset(d, 0, sizeof *d);
    d->phase = DIRECTOR_OFF;
}

DirectorStep director_tick(WaveDirector *d, const WavePlan *plan, int enemies_alive, float dt)
{
    DirectorStep step;
    memset(&step, 0, sizeof step);

    if (d->phase == DIRECTOR_PAUSE) {
        d->timer -= dt;
        if (d->timer > 0.0f) return step;
        d->phase = DIRECTOR_ACTIVE;
        d->timer = 0.0f;
        d->spawned = 0u;
        step.wave_started = true;
    }
    if (d->phase != DIRECTOR_ACTIVE) return step;

    // Solta os grupos cujo atraso já passou.
    const WaveDef *w = &plan->waves[d->wave];
    bool all_in = true;
    for (int g = 0; g < w->group_count; g++) {
        if (d->spawned & (1u << g)) continue;
        if (d->timer >= w->groups[g].delay) {
            d->spawned |= 1u << g;
            step.spawn[step.spawn_count++] = &w->groups[g];
        } else {
            all_in = false;
        }
    }
    d->timer += dt;

    // A horda acaba quando todos os grupos entraram e nenhum inimigo sobrou.
    if (all_in && step.spawn_count == 0 && enemies_alive == 0) {
        step.wave_cleared = true;
        d->wave++;
        if (d->wave >= plan->count) {
            d->phase = DIRECTOR_DONE;
            step.finished = true;
        } else {
            d->phase = DIRECTOR_PAUSE;
            d->timer = plan->waves[d->wave].pause;
        }
    }
    return step;
}
