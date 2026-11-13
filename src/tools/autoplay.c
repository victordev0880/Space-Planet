#include "tools/autoplay.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 256
#define MAX_TOKENS   10        // a instrução mais longa: at <quadro> ship x y z tx ty tz velocidade
#define MAX_FRAMES   1000000

static bool fail(Autoplay *ap, int line, const char *fmt, ...)
{
    if (ap->error[0] != '\0') return false;   // guarda só o primeiro erro

    char msg[AUTOPLAY_ERROR_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);

    if (line > 0) snprintf(ap->error, sizeof ap->error, "linha %d: %.120s", line, msg);
    else          snprintf(ap->error, sizeof ap->error, "%.150s", msg);
    return false;
}

// Quebra a linha em palavras separadas por espaço, modificando-a no lugar.
static int tokenize(char *line, char *tokens[], int max_tokens)
{
    int count = 0;
    char *p = line;
    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) p++;
        if (*p == '\0') break;
        if (count == max_tokens) return max_tokens + 1;   // palavras demais
        tokens[count++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) p++;
        if (*p != '\0') *p++ = '\0';
    }
    return count;
}

static bool parse_int(const char *s, long long *out)
{
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

static bool parse_float(const char *s, float *out)
{
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    *out = (float)v;
    return true;
}

static bool shot_name_ok(const char *s)
{
    if (*s == '\0' || strlen(s) >= AUTOPLAY_NAME_LEN) return false;
    for (; *s != '\0'; s++) {
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    }
    return true;
}

static bool parse_event(Autoplay *ap, char *tokens[], int count, int line_no)
{
    // tokens: at <quadro> <comando> <argumentos...>
    if (count < 3) return fail(ap, line_no, "esperado: at <quadro> <comando> ...");

    long long frame;
    if (!parse_int(tokens[1], &frame) || frame < 0 || frame >= MAX_FRAMES) {
        return fail(ap, line_no, "quadro invalido: \"%s\"", tokens[1]);
    }
    if (ap->event_count >= AUTOPLAY_MAX_EVENTS) {
        return fail(ap, line_no, "eventos demais (maximo %d)", AUTOPLAY_MAX_EVENTS);
    }

    AutoplayEvent e;
    memset(&e, 0, sizeof e);
    e.frame = (int)frame;

    const char *cmd = tokens[2];
    if (strcmp(cmd, "hold") == 0 || strcmp(cmd, "release") == 0 || strcmp(cmd, "press") == 0) {
        if (count != 4) return fail(ap, line_no, "esperado: at <quadro> %s <acao>", cmd);
        e.cmd = (cmd[0] == 'h') ? AP_HOLD : (cmd[0] == 'r') ? AP_RELEASE : AP_PRESS;
        e.action = action_from_name(tokens[3]);
        if (e.action < 0) return fail(ap, line_no, "acao desconhecida: \"%s\"", tokens[3]);
    } else if (strcmp(cmd, "look") == 0) {
        if (count != 5 || !parse_float(tokens[3], &e.dx) || !parse_float(tokens[4], &e.dy)) {
            return fail(ap, line_no, "esperado: at <quadro> look <dx> <dy>");
        }
        e.cmd = AP_LOOK;
    } else if (strcmp(cmd, "shot") == 0) {
        if (count != 4 || !shot_name_ok(tokens[3])) {
            return fail(ap, line_no, "esperado: at <quadro> shot <nome> (letras, numeros, _ e -)");
        }
        for (int i = 0; i < ap->event_count; i++) {
            if (ap->events[i].cmd == AP_SHOT && ap->events[i].frame == e.frame) {
                return fail(ap, line_no, "ja existe um shot no quadro %d", e.frame);
            }
        }
        e.cmd = AP_SHOT;
        strcpy(e.name, tokens[3]);
    } else if (strcmp(cmd, "cam") == 0 && count >= 4 && strcmp(tokens[3], "boss") == 0) {
        // Câmera que acompanha o boss: acima dele, atrás e de lado (distâncias).
        bool ok = (count == 6 || count == 7)
               && parse_float(tokens[4], &e.pos.x) && parse_float(tokens[5], &e.pos.y);
        if (ok && count == 7) ok = parse_float(tokens[6], &e.pos.z);
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> cam boss <acima> <atras> [<lado>]");
        e.cmd = AP_CAM;
        e.count = 1;
    } else if (strcmp(cmd, "cam") == 0) {
        // Posição obrigatória; o alvo é opcional e, se omitido, é a origem.
        bool ok = (count == 6 || count == 9)
               && parse_float(tokens[3], &e.pos.x) && parse_float(tokens[4], &e.pos.y)
               && parse_float(tokens[5], &e.pos.z);
        if (ok && count == 9) {
            ok = parse_float(tokens[6], &e.target.x) && parse_float(tokens[7], &e.target.y)
              && parse_float(tokens[8], &e.target.z);
        }
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> cam <x> <y> <z> [<alvo x> <alvo y> <alvo z>]");
        e.cmd = AP_CAM;
    } else if (strcmp(cmd, "crater") == 0) {
        bool ok = (count == 8)
               && parse_float(tokens[3], &e.pos.x) && parse_float(tokens[4], &e.pos.y)
               && parse_float(tokens[5], &e.pos.z)
               && parse_float(tokens[6], &e.rim_radius) && parse_float(tokens[7], &e.depth);
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> crater <x> <y> <z> <raio> <profundidade>");
        if (Vector3Length(e.pos) < 1e-4f) return fail(ap, line_no, "crater: a direcao nao pode ser (0 0 0)");
        if (!(e.rim_radius > 0.0f && e.depth > 0.0f)) {
            return fail(ap, line_no, "crater: raio e profundidade precisam ser positivos");
        }
        int same_frame = 0;
        for (int i = 0; i < ap->event_count; i++) {
            if (ap->events[i].cmd == AP_CRATER && ap->events[i].frame == e.frame) same_frame++;
        }
        if (same_frame >= AUTOPLAY_MAX_CRATERS) {
            return fail(ap, line_no, "no maximo %d comandos crater no mesmo quadro", AUTOPLAY_MAX_CRATERS);
        }
        e.cmd = AP_CRATER;
    } else if (strcmp(cmd, "barrage") == 0) {
        long long n = 0;
        bool ok = (count == 6) && parse_int(tokens[3], &n)
               && parse_float(tokens[4], &e.rim_radius) && parse_float(tokens[5], &e.depth);
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> barrage <quantidade> <raio> <profundidade>");
        if (n < 1 || n > AUTOPLAY_MAX_BARRAGE) {
            return fail(ap, line_no, "barrage: quantidade de 1 a %d", AUTOPLAY_MAX_BARRAGE);
        }
        if (!(e.rim_radius > 0.0f && e.depth > 0.0f)) {
            return fail(ap, line_no, "barrage: raio e profundidade precisam ser positivos");
        }
        e.count = (int)n;
        e.cmd = AP_BARRAGE;
    } else if (strcmp(cmd, "view") == 0) {
        if (count == 4 && strcmp(tokens[3], "ship") == 0)       e.count = (int)CAM_SHIP;
        else if (count == 4 && strcmp(tokens[3], "orbit") == 0) e.count = (int)CAM_ORBIT;
        else return fail(ap, line_no, "esperado: at <quadro> view ship|orbit");
        e.cmd = AP_VIEW;
    } else if (strcmp(cmd, "ship") == 0) {
        // Posição e alvo obrigatórios; a velocidade é opcional.
        bool ok = (count == 9 || count == 10)
               && parse_float(tokens[3], &e.pos.x) && parse_float(tokens[4], &e.pos.y)
               && parse_float(tokens[5], &e.pos.z)
               && parse_float(tokens[6], &e.target.x) && parse_float(tokens[7], &e.target.y)
               && parse_float(tokens[8], &e.target.z);
        if (ok && count == 10) ok = parse_float(tokens[9], &e.speed);
        if (!ok) {
            return fail(ap, line_no, "esperado: at <quadro> ship <x> <y> <z> <alvo x> <alvo y> <alvo z> [<velocidade>]");
        }
        if (Vector3Distance(e.pos, e.target) < 1e-4f) return fail(ap, line_no, "ship: o alvo nao pode ser a posicao");
        if (e.speed < 0.0f) return fail(ap, line_no, "ship: a velocidade nao pode ser negativa");
        e.cmd = AP_SHIP;
    } else if (strcmp(cmd, "waves") == 0 || strcmp(cmd, "autopilot") == 0) {
        if (count == 4 && strcmp(tokens[3], "on") == 0)       e.count = 1;
        else if (count == 4 && strcmp(tokens[3], "off") == 0) e.count = 0;
        else return fail(ap, line_no, "esperado: at <quadro> %s on|off", cmd);
        e.cmd = (cmd[0] == 'w') ? AP_WAVES : AP_AUTOPILOT;
    } else if (strcmp(cmd, "spawn") == 0) {
        long long n = 0, variant = -1;
        bool ok = (count == 8 || count == 9) && parse_int(tokens[4], &n)
               && parse_float(tokens[5], &e.pos.x) && parse_float(tokens[6], &e.pos.y)
               && parse_float(tokens[7], &e.pos.z);
        if (ok && count == 9) ok = parse_int(tokens[8], &variant) && variant >= 0 && variant < ENEMY_VARIANTS;
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> spawn <tipo> <quantidade> <x> <y> <z> [<variacao 0-2>]");
        e.variant = (int)variant;
        e.action = enemy_type_from_name(tokens[3]);
        if (e.action < 0) return fail(ap, line_no, "spawn: tipo de inimigo desconhecido: \"%s\"", tokens[3]);
        if (n < 1 || n > WAVE_GROUP_MAX_COUNT) return fail(ap, line_no, "spawn: quantidade de 1 a %d", WAVE_GROUP_MAX_COUNT);
        if (Vector3Length(e.pos) < 1e-3f) return fail(ap, line_no, "spawn: a posicao nao pode ser o centro do planeta");
        int same_frame = 0;
        for (int i = 0; i < ap->event_count; i++) {
            if (ap->events[i].cmd == AP_SPAWN && ap->events[i].frame == e.frame) same_frame++;
        }
        if (same_frame >= AUTOPLAY_MAX_SPAWNS) {
            return fail(ap, line_no, "no maximo %d comandos spawn no mesmo quadro", AUTOPLAY_MAX_SPAWNS);
        }
        e.count = (int)n;
        e.cmd = AP_SPAWN;
    } else if (strcmp(cmd, "clear") == 0) {
        if (count != 3) return fail(ap, line_no, "esperado: at <quadro> clear");
        e.cmd = AP_CLEAR;
    } else if (strcmp(cmd, "boss") == 0) {
        bool ok = (count == 7) && parse_float(tokens[4], &e.pos.x) && parse_float(tokens[5], &e.pos.y)
               && parse_float(tokens[6], &e.pos.z);
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> boss <tipo> <x> <y> <z>");
        e.action = boss_type_from_name(tokens[3]);
        if (e.action < 0) return fail(ap, line_no, "boss: tipo desconhecido: \"%s\"", tokens[3]);
        if (Vector3Length(e.pos) < 1e-4f) return fail(ap, line_no, "boss: a direcao nao pode ser (0 0 0)");
        e.cmd = AP_BOSS;
    } else if (strcmp(cmd, "rock") == 0) {
        bool ok = (count == 7 || count == 10) && parse_float(tokens[4], &e.pos.x) && parse_float(tokens[5], &e.pos.y)
               && parse_float(tokens[6], &e.pos.z);
        if (ok && count == 10) {
            ok = parse_float(tokens[7], &e.target.x) && parse_float(tokens[8], &e.target.y)
              && parse_float(tokens[9], &e.target.z);
        }
        if (!ok) return fail(ap, line_no, "esperado: at <quadro> rock meteor|fragment <x> <y> <z> [<vx> <vy> <vz>]");
        if (strcmp(tokens[3], "meteor") == 0)        e.action = ROCK_METEOR;
        else if (strcmp(tokens[3], "fragment") == 0) e.action = ROCK_FRAGMENT;
        else return fail(ap, line_no, "rock: tipo desconhecido: \"%s\" (meteor ou fragment)", tokens[3]);
        int same_frame = 0;
        for (int i = 0; i < ap->event_count; i++) {
            if (ap->events[i].cmd == AP_ROCK && ap->events[i].frame == e.frame) same_frame++;
        }
        if (same_frame >= AUTOPLAY_MAX_ROCKS) {
            return fail(ap, line_no, "no maximo %d comandos rock no mesmo quadro", AUTOPLAY_MAX_ROCKS);
        }
        e.cmd = AP_ROCK;
    } else if (strcmp(cmd, "bosshp") == 0) {
        if (count != 4 || !parse_float(tokens[3], &e.speed) || !(e.speed > 0.0f && e.speed <= 1.0f)) {
            return fail(ap, line_no, "esperado: at <quadro> bosshp <fracao da vida, de 0 a 1>");
        }
        e.cmd = AP_BOSS_HP;
    } else if (strcmp(cmd, "xp") == 0) {
        if (count != 4 || !parse_float(tokens[3], &e.speed) || !(e.speed > 0.0f)) {
            return fail(ap, line_no, "esperado: at <quadro> xp <quantidade positiva>");
        }
        e.cmd = AP_XP;
    } else if (strcmp(cmd, "evolve") == 0) {
        long long rank = 0;
        if (count != 5 || !parse_int(tokens[4], &rank) || rank < 0 || rank > 9) {
            return fail(ap, line_no, "esperado: at <quadro> evolve <nome> <nivel 0-9>");
        }
        e.action = evolution_from_key(tokens[3]);
        if (e.action < 0) return fail(ap, line_no, "evolve: evolucao desconhecida: \"%s\"", tokens[3]);
        e.count = (int)rank;
        e.cmd = AP_EVOLVE;
    } else {
        return fail(ap, line_no, "comando desconhecido: \"%s\"", cmd);
    }

    // Insere mantendo a lista ordenada por quadro; eventos do mesmo quadro
    // ficam na ordem em que aparecem no arquivo.
    int pos = ap->event_count;
    while (pos > 0 && ap->events[pos - 1].frame > e.frame) {
        ap->events[pos] = ap->events[pos - 1];
        pos--;
    }
    ap->events[pos] = e;
    ap->event_count++;
    return true;
}

static bool parse_line(Autoplay *ap, char *line, int line_no)
{
    // Pula a marca invisível (BOM) que alguns editores gravam no começo do arquivo.
    if (line_no == 1 && strncmp(line, "\xEF\xBB\xBF", 3) == 0) line += 3;

    char *hash = strchr(line, '#');
    if (hash != NULL) *hash = '\0';

    char *tokens[MAX_TOKENS];
    int count = tokenize(line, tokens, MAX_TOKENS);
    if (count == 0) return true;
    if (count > MAX_TOKENS) return fail(ap, line_no, "palavras demais na linha");

    if (strcmp(tokens[0], "frames") == 0) {
        long long n;
        if (count != 2 || !parse_int(tokens[1], &n) || n < 1 || n > MAX_FRAMES) {
            return fail(ap, line_no, "esperado: frames <1 a %d>", MAX_FRAMES);
        }
        ap->frames = (int)n;
        return true;
    }
    if (strcmp(tokens[0], "seed") == 0) {
        long long n;
        if (count != 2 || !parse_int(tokens[1], &n) || n < 0) {
            return fail(ap, line_no, "esperado: seed <numero>");
        }
        ap->has_seed = true;
        ap->seed = (uint64_t)n;
        return true;
    }
    if (strcmp(tokens[0], "at") == 0) return parse_event(ap, tokens, count, line_no);

    return fail(ap, line_no, "instrucao desconhecida: \"%s\"", tokens[0]);
}

static bool finish_parse(Autoplay *ap)
{
    if (ap->error[0] != '\0') return false;
    if (ap->frames < 1) return fail(ap, 0, "o roteiro precisa de uma linha \"frames <n>\"");
    for (int i = 0; i < ap->event_count; i++) {
        if (ap->events[i].frame >= ap->frames) {
            return fail(ap, 0, "evento no quadro %d, mas o roteiro so tem %d quadros",
                        ap->events[i].frame, ap->frames);
        }
    }
    return true;
}

bool autoplay_parse(Autoplay *ap, const char *text)
{
    memset(ap, 0, sizeof *ap);

    char line[LINE_MAX_LEN];
    int line_no = 0;
    const char *p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        line_no++;
        if (len >= sizeof line) {
            fail(ap, line_no, "linha longa demais");
        } else {
            memcpy(line, p, len);
            line[len] = '\0';
            parse_line(ap, line, line_no);
        }
        p += len;
        if (*p == '\n') p++;
    }
    return finish_parse(ap);
}

bool autoplay_load_file(Autoplay *ap, const char *path)
{
    memset(ap, 0, sizeof *ap);

    FILE *f = fopen(path, "rb");
    if (f == NULL) return fail(ap, 0, "nao consegui abrir \"%s\"", path);

    char line[LINE_MAX_LEN];
    int line_no = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        line_no++;
        size_t len = strlen(line);
        bool complete = (len > 0 && line[len - 1] == '\n') || feof(f);
        if (!complete) {
            fail(ap, line_no, "linha longa demais");
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') { }
            continue;
        }
        parse_line(ap, line, line_no);
    }
    fclose(f);
    return finish_parse(ap);
}

void autoplay_step(Autoplay *ap, int frame, AutoplayFrame *out)
{
    uint32_t pulse = 0u;   // ações de "press": valem só neste quadro
    out->shot = NULL;
    out->has_cam = false;
    out->cam_boss = false;
    out->has_view = false;
    out->has_ship = false;
    out->has_waves = false;
    out->clear = false;
    out->xp = 0.0f;
    out->boss = -1;
    out->boss_hp = -1.0f;
    out->rock_count = 0;
    out->evolve_count = 0;
    out->spawn_count = 0;
    out->crater_count = 0;
    out->barrage_count = 0;

    while (ap->cursor < ap->event_count && ap->events[ap->cursor].frame <= frame) {
        const AutoplayEvent *e = &ap->events[ap->cursor++];
        switch (e->cmd) {
        case AP_HOLD:    ap->held |= ACT_BIT(e->action); break;
        case AP_RELEASE: ap->held &= ~ACT_BIT(e->action); break;
        case AP_PRESS:   pulse |= ACT_BIT(e->action); break;
        case AP_LOOK:    ap->look_dx = e->dx; ap->look_dy = e->dy; break;
        case AP_SHOT:    out->shot = e->name; break;
        case AP_CAM:
            out->has_cam = true;
            out->cam_boss = (e->count == 1);
            out->cam_pos = e->pos;
            out->cam_target = e->target;
            break;
        case AP_CRATER:
            if (out->crater_count < AUTOPLAY_MAX_CRATERS) {
                AutoplayCrater *c = &out->craters[out->crater_count++];
                c->dir = e->pos;
                c->rim_radius = e->rim_radius;
                c->depth = e->depth;
            }
            break;
        case AP_BARRAGE:
            out->barrage_count = e->count;
            out->barrage_rim_radius = e->rim_radius;
            out->barrage_depth = e->depth;
            break;
        case AP_VIEW:
            out->has_view = true;
            out->view = (CamMode)e->count;
            break;
        case AP_SHIP:
            out->has_ship = true;
            out->ship_pos = e->pos;
            out->ship_target = e->target;
            out->ship_speed = e->speed;
            break;
        case AP_WAVES:
            out->has_waves = true;
            out->waves_on = (e->count != 0);
            break;
        case AP_AUTOPILOT:
            ap->autopilot = (e->count != 0);
            break;
        case AP_CLEAR:
            out->clear = true;
            out->spawn_count = 0;   // o que veio antes no mesmo quadro também sai
            break;
        case AP_XP:
            out->xp += e->speed;
            break;
        case AP_BOSS:
            out->boss = e->action;
            out->boss_dir = e->pos;
            break;
        case AP_BOSS_HP:
            out->boss_hp = e->speed;
            break;
        case AP_ROCK:
            if (out->rock_count < AUTOPLAY_MAX_ROCKS) {
                AutoplayRock *k = &out->rocks[out->rock_count++];
                k->kind = e->action;
                k->pos = e->pos;
                k->vel = e->target;
            }
            break;
        case AP_EVOLVE:
            if (out->evolve_count < EVO_COUNT) {
                out->evolve_id[out->evolve_count] = e->action;
                out->evolve_rank[out->evolve_count] = e->count;
                out->evolve_count++;
            }
            break;
        case AP_SPAWN:
            if (out->spawn_count < AUTOPLAY_MAX_SPAWNS) {
                AutoplaySpawn *s = &out->spawns[out->spawn_count++];
                s->type = (EnemyType)e->action;
                s->count = e->count;
                s->variant = e->variant;
                s->center = e->pos;
            }
            break;
        }
    }

    out->autopilot = ap->autopilot;
    out->input.held = ap->held | pulse;
    out->input.look_dx = ap->look_dx;
    out->input.look_dy = ap->look_dy;
}

void autoplay_apply(const AutoplayFrame *frame, World *w, const Tuning *t)
{
    // A troca de câmera e a nave vêm antes: um "cam" no mesmo quadro parte delas.
    if (frame->has_view) world_set_view(w, t, frame->view);
    if (frame->has_ship) world_place_ship(w, t, frame->ship_pos, frame->ship_target, frame->ship_speed);
    if (frame->has_cam && frame->cam_boss) {
        // Segue o boss: na carga e no disparo, olha para o meio do corte.
        const Boss *b = &w->boss;
        if (boss_active(b)) {
            Vector3 radial = Vector3Normalize(b->pos);
            Vector3 ahead = Vector3Normalize(Vector3CrossProduct(b->axis, radial));
            Vector3 pos = Vector3Add(b->pos, Vector3Scale(radial, frame->cam_pos.x));
            pos = Vector3Add(pos, Vector3Scale(ahead, -frame->cam_pos.y));
            pos = Vector3Add(pos, Vector3Scale(b->axis, frame->cam_pos.z));
            bool cutting = b->state == BOSS_FIGHT && (b->action == BREAKER_CHARGE || b->action == BREAKER_FIRE);
            Vector3 look = cutting ? Vector3Scale(boss_cut_direction(b, 0.5f), w->planet.params.radius) : b->pos;
            world_place_camera(w, pos, look);
        }
    } else if (frame->has_cam) {
        world_place_camera(w, frame->cam_pos, frame->cam_target);
    }
    if (frame->has_waves) world_set_waves(w, frame->waves_on);
    if (frame->clear) world_clear_enemies(w);
    for (int i = 0; i < frame->evolve_count; i++) {
        world_set_evolution(w, t, (EvolutionId)frame->evolve_id[i], frame->evolve_rank[i]);
    }
    if (frame->xp > 0.0f) world_give_xp(w, t, frame->xp);
    if (frame->boss >= 0) world_spawn_boss(w, t, (BossType)frame->boss, frame->boss_dir);
    if (frame->boss_hp > 0.0f) boss_set_health(&w->boss, t, frame->boss_hp, &w->events, (uint32_t)w->tick);
    for (int i = 0; i < frame->rock_count; i++) {
        world_spawn_rock(w, t, (RockKind)frame->rocks[i].kind, frame->rocks[i].pos, frame->rocks[i].vel);
    }
    for (int i = 0; i < frame->spawn_count; i++) {
        world_spawn_variant(w, t, frame->spawns[i].type, frame->spawns[i].count, frame->spawns[i].center,
                            frame->spawns[i].variant);
    }

    // Os impactos vêm de longe, apontados para o centro do planeta.
    float far = w->planet.params.radius * 4.0f;

    for (int i = 0; i < frame->crater_count; i++) {
        const AutoplayCrater *c = &frame->craters[i];
        Vector3 dir = Vector3Normalize(c->dir);
        world_impact(w, t, Vector3Scale(dir, far), Vector3Negate(dir), c->rim_radius, c->depth);
    }

    // As direções do bombardeio saem do gerador do mundo, então são as mesmas
    // em toda execução.
    for (int i = 0; i < frame->barrage_count; i++) {
        Vector3 dir = rng_unit_vec3(&w->rng);
        world_impact(w, t, Vector3Scale(dir, far), Vector3Negate(dir), frame->barrage_rim_radius,
                     frame->barrage_depth);
    }
}

void frame_stats_init(FrameStats *s, double budget_ms)
{
    memset(s, 0, sizeof *s);
    s->budget_ms = budget_ms;
}

void frame_stats_add(FrameStats *s, int frame, double frame_ms, double cpu_ms)
{
    s->frames++;
    s->sum_ms += frame_ms;
    s->sum_cpu_ms += cpu_ms;
    if (frame_ms > s->worst_ms) {
        s->worst_ms = frame_ms;
        s->worst_frame = frame;
    }
    if (cpu_ms > s->worst_cpu_ms) s->worst_cpu_ms = cpu_ms;
    if (frame_ms > s->budget_ms) {
        if (s->over_budget < FRAME_SLOW_LIST) s->slow_frames[s->over_budget] = frame;
        s->over_budget++;
    }

    int bucket = (frame_ms > 0.0) ? (int)(frame_ms / FRAME_HIST_STEP_MS) : 0;
    if (bucket >= FRAME_HIST_BUCKETS) bucket = FRAME_HIST_BUCKETS - 1;
    s->histogram[bucket]++;
}

void frame_stats_add_sim(FrameStats *s, double sim_ms)
{
    s->sum_sim_ms += sim_ms;
    if (sim_ms > s->worst_sim_ms) s->worst_sim_ms = sim_ms;
}

double frame_stats_percentile(const FrameStats *s, double fraction)
{
    if (s->frames == 0) return 0.0;

    // Quantos quadros precisam ficar abaixo do valor devolvido.
    int target = (int)(fraction * s->frames + 0.999999);
    if (target < 1) target = 1;
    if (target > s->frames) target = s->frames;

    int seen = 0;
    for (int i = 0; i < FRAME_HIST_BUCKETS - 1; i++) {
        seen += s->histogram[i];
        if (seen >= target) return (i + 1) * FRAME_HIST_STEP_MS;   // limite de cima da faixa
    }
    return s->worst_ms;   // caiu na última faixa, que junta tudo acima de 50 ms
}

bool autoplay_write_report(const char *path, const AutoplayReport *r)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;

    const FrameStats *s = &r->stats;
    double avg_ms = (s->frames > 0) ? s->sum_ms / s->frames : 0.0;
    double avg_cpu_ms = (s->frames > 0) ? s->sum_cpu_ms / s->frames : 0.0;

    if (r->realtime) {
        fprintf(f, "# Relatorio do autoplay em tempo real: relogio de verdade e vsync, como no\n");
        fprintf(f, "# jogo normal. Os tempos incluem a espera pelo monitor; o hash pode variar.\n");
    } else {
        fprintf(f, "# Relatorio do autoplay deterministico: um passo de logica por quadro,\n");
        fprintf(f, "# sem vsync nem limite de FPS. Os tempos mostram o custo real do quadro.\n");
    }
    fprintf(f, "# Medicoes ignoram os %d primeiros quadros (aquecimento) e os de screenshot.\n",
            r->warmup_frames);
    fprintf(f, "script = %s\n", r->script);
    fprintf(f, "mode = %s\n", r->realtime ? "tempo_real" : "deterministico");
    fprintf(f, "frames_planned = %d\n", r->frames_planned);
    fprintf(f, "frames_run = %d\n", r->frames_run);
    fprintf(f, "ticks = %llu\n", (unsigned long long)r->ticks);
    fprintf(f, "seed = %llu\n", (unsigned long long)r->seed);
    fprintf(f, "state_hash = %016llx\n", (unsigned long long)r->state_hash);
    fprintf(f, "screenshots = %d\n", r->screenshots);
    fprintf(f, "\n");
    fprintf(f, "frames_measured = %d\n", s->frames);
    fprintf(f, "fps_avg = %.1f\n", (avg_ms > 0.0) ? 1000.0 / avg_ms : 0.0);
    fprintf(f, "fps_min = %.1f\n", (s->worst_ms > 0.0) ? 1000.0 / s->worst_ms : 0.0);
    fprintf(f, "frame_ms_avg = %.3f\n", avg_ms);
    fprintf(f, "frame_ms_median = %.2f\n", frame_stats_percentile(s, 0.50));
    fprintf(f, "frame_ms_p99 = %.2f\n", frame_stats_percentile(s, 0.99));
    fprintf(f, "frame_ms_worst = %.3f\n", s->worst_ms);
    fprintf(f, "frame_worst_index = %d\n", s->worst_frame);
    fprintf(f, "frame_budget_ms = %.2f\n", s->budget_ms);
    fprintf(f, "frames_over_budget = %d\n", s->over_budget);
    if (s->over_budget > 0) {
        fprintf(f, "slow_frames =");
        for (int i = 0; i < s->over_budget && i < FRAME_SLOW_LIST; i++) fprintf(f, " %d", s->slow_frames[i]);
        if (s->over_budget > FRAME_SLOW_LIST) fprintf(f, " ...");
        fprintf(f, "\n");
    }
    fprintf(f, "cpu_ms_avg = %.3f\n", avg_cpu_ms);
    fprintf(f, "cpu_ms_worst = %.3f\n", s->worst_cpu_ms);
    fprintf(f, "sim_ms_avg = %.3f\n", (s->frames > 0) ? s->sum_sim_ms / s->frames : 0.0);
    fprintf(f, "sim_ms_worst = %.3f\n", s->worst_sim_ms);
    fprintf(f, "\n");
    fprintf(f, "world_init_ms = %.1f\n", r->world_init_ms);
    fprintf(f, "render_init_ms = %.1f\n", r->render_init_ms);
    fprintf(f, "planet_mesh_ms = %.1f\n", r->planet_mesh_ms);
    fprintf(f, "planet_meshes = %d\n", r->planet_meshes);
    fprintf(f, "planet_triangles = %d\n", r->planet_triangles);
    if (r->planet_meshes > 0) {
        fprintf(f, "planet_mesh_ms_per_chunk = %.3f\n", r->planet_mesh_ms / r->planet_meshes);
    }
    fprintf(f, "shader = %s\n", r->shaded ? "sim" : "nao");
    fprintf(f, "\n");
    fprintf(f, "planet_hp = %.4f\n", (double)r->planet_hp);
    fprintf(f, "core_hp = %.4f\n", (double)r->core_hp);
    fprintf(f, "collapsed = %s\n", r->collapsed ? "sim" : "nao");
    fprintf(f, "impacts = %d\n", r->impacts);
    fprintf(f, "particles_peak = %d\n", r->particles_peak);
    fprintf(f, "remeshed_total = %d\n", r->remeshed_total);
    if (r->remeshed_total > 0) {
        fprintf(f, "remesh_ms_per_chunk = %.3f\n", r->remesh_ms_total / r->remeshed_total);
    }
    fprintf(f, "remeshed_max_frame = %d\n", r->remeshed_max_frame);
    fprintf(f, "remesh_backlog_max = %d\n", r->remesh_backlog_max);
    fprintf(f, "remesh_ms_max = %.3f\n", r->remesh_ms_max);
    fprintf(f, "\n");
    if (r->waves_on) {
        fprintf(f, "game_state = %s\n", (r->game_state != NULL) ? r->game_state : "?");
        fprintf(f, "wave = %d / %d\n", r->wave, r->waves_total);
        fprintf(f, "score = %lld\n", (long long)r->score);
        fprintf(f, "kills = %d\n", r->kills);
        fprintf(f, "enemies_alive = %d\n", r->enemies_alive);
        fprintf(f, "\n");
    }
    if (r->boss_active || r->boss_downs > 0) {
        if (r->boss_active) fprintf(f, "boss = fase %d, vida %.2f\n", r->boss_phase, (double)r->boss_hp);
        fprintf(f, "bosses_down = %d\n", r->boss_downs);
        for (int i = 0; i < r->boss_downs && i < AUTOPLAY_MAX_BOSS_DOWNS; i++) {
            fprintf(f, "boss_down_frame = %d\n", r->boss_down_frames[i]);
        }
        fprintf(f, "ability = %s\n", r->ability_owned ? "sim" : "nao");
        fprintf(f, "\n");
    }
    if (r->rocks_delivered > 0 || r->rocks_alive > 0) {
        fprintf(f, "rocks_alive = %d\n", r->rocks_alive);
        fprintf(f, "rocks_delivered = %d\n", r->rocks_delivered);
        fprintf(f, "planet_restored = %.4f\n", (double)r->planet_restored);
        fprintf(f, "scars_open = %d\n", r->scars_open);
        fprintf(f, "\n");
    }
    fprintf(f, "warnings = %d\n", r->warnings);
    fprintf(f, "errors = %d\n", r->errors);
    if (r->first_problem != NULL && r->first_problem[0] != '\0') {
        fprintf(f, "first_problem = %s\n", r->first_problem);
    }

    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}
