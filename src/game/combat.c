#include "game/combat.h"

#include <math.h>

#include "voxel/damage.h"

#define MUZZLE_TIME      0.05f    // clarão no cano a cada tiro
#define HIT_MARKER_TIME  0.12f    // marcador de acerto na mira
#define KILL_MARKER_TIME 0.30f    // ... e de abate
#define HIT_FLASH_TIME   0.35f    // aviso de dano na nave
#define SHIP_HIT_SCALE   1.2f     // os tiros inimigos acertam a nave um pouco além do raio de colisão
#define BIG_HIT          12.0f    // a partir deste dano, a nave congela por um instante
#define JOLT_PER_DAMAGE  0.8f     // tremor de câmera por ponto de dano
#define WRECK_EMBERS     140
#define WRECK_FLASH_SIZE 14.0f
#define WRECK_JOLT       60.0f
#define RAM_SPEED_FULL   20.0f    // trombar com o Bombardeiro a esta velocidade dá o dano inteiro
#define RAM_MIN_SPEED    2.0f
#define BOMB_EXPLOSION   1.2f     // tamanho da explosão de uma bomba derrubada
#define DETONATE_SIZE    2.0f     // a explosão do Kamikaze junto da nave é maior que a de um abate
#define DETONATE_MIN     0.35f    // mesmo na borda da explosão, o dano não cai abaixo desta fração
#define BIG_KILL_SCALE   2.5f     // inimigos deste tamanho para cima congelam o jogo ao morrer
#define BLAST_MIN        0.4f     // explosões da nave: na borda, o dano não cai abaixo desta fração
#define BLAST_VISUAL     6.0f     // tamanho do efeito de uma explosão da nave, em raios por unidade
#define REFLECT_DAMAGE   2.0f     // um tiro refletido vale este tanto de tiros da nave
#define BEAM_TICKS       6        // o laser do boss fere a nave a cada tantos passos (não a cada um)
#define ABILITY_BEAM_SHOW 0.45f   // segundos que o raio do núcleo fica na tela
#define ABILITY_JOLT     8.0f     // tranco na câmera quando o raio sai
#define BROOD_MAX        16       // ferrões de uma Ninhada, no máximo
#define BROOD_SPREAD     0.55f    // os ferrões saem num leque desta abertura
#define BROOD_SEEK       220.0f   // e procuram alvos até esta distância
#define SPORE_JOLT       3.0f
#define TUNNEL_FLASH     0.5f     // segundos do clarão na tela depois do Túnel
#define TUNNEL_JOLT      10.0f

// Os canhões, em eixos da nave: nas raízes das asas, um pouco à frente.
static const Vector3 GUNS[2] = { { 0.55f, -0.05f, -0.45f }, { -0.55f, -0.05f, -0.45f } };

// Cores dos efeitos. Linguagem visual: o jogador é azul e branco; os
// inimigos, violeta e laranja.
static const Rgb8 BOLT_SPARK    = { 150, 220, 255 };
static const Rgb8 ARMOR_SPARK   = { 255, 210, 150 };
static const Rgb8 WEAK_SPARK    = { 255, 120, 220 };
static const Rgb8 SHIELD_SPARK  = { 120, 200, 255 };
static const Rgb8 HULL_SPARK    = { 255, 170, 110 };
static const Rgb8 PLASMA_SPARK  = { 220, 120, 255 };
static const Rgb8 WRECK_HOT     = { 255, 214, 150 };
static const Rgb8 WRECK_COOL    = { 110, 190, 255 };

typedef struct { Rgb8 hot, glow, debris; } Blast;
static const Blast BLASTS[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = { { 210, 255, 200 }, { 160, 90, 255 }, { 70, 46, 92 } },
    [ENEMY_BOMBER]   = { { 255, 226, 170 }, { 255, 120, 60 }, { 92, 62, 56 } },
    [ENEMY_KAMIKAZE] = { { 255, 230, 160 }, { 255, 70, 40 }, { 90, 40, 36 } },
    [ENEMY_DIGGER]   = { { 255, 220, 150 }, { 255, 160, 60 }, { 110, 84, 60 } },
    [ENEMY_SIPHON]   = { { 220, 255, 220 }, { 90, 255, 170 }, { 50, 80, 70 } },
    [ENEMY_SNIPER]   = { { 255, 220, 220 }, { 255, 60, 90 }, { 70, 60, 70 } },
    [ENEMY_SHIELDER] = { { 220, 255, 255 }, { 80, 230, 255 }, { 50, 76, 90 } },
    [ENEMY_CARRIER]  = { { 255, 236, 190 }, { 200, 120, 255 }, { 80, 70, 96 } },
};
static const Rgb8 SHIELD_BLOCK = { 120, 240, 255 };
static const Rgb8 CRIT_SPARK   = { 255, 250, 170 };
static const Rgb8 SPORE_HOT    = { 230, 255, 190 };
static const Rgb8 SPORE_GLOW   = { 140, 255, 90 };
static const Rgb8 SPORE_GOO    = { 70, 120, 50 };
static const Rgb8 TUNNEL_HOT   = { 255, 220, 170 };
static const Rgb8 TUNNEL_GLOW  = { 255, 140, 60 };
static const Rgb8 TUNNEL_DUST  = { 120, 100, 80 };
static const Blast SHIP_BLAST  = { { 230, 250, 255 }, { 90, 180, 255 }, { 60, 90, 130 } };

// Pesados empurram a nave quando ela tromba neles; os leves se despedaçam.
static bool is_heavy(EnemyType type)
{
    return type == ENEMY_BOMBER || type == ENEMY_CARRIER || type == ENEMY_DIGGER || type == ENEMY_SIPHON;
}
static const Blast BOMB_BLAST = { { 255, 210, 170 }, { 255, 70, 60 }, { 80, 40, 40 } };

static int seconds_to_ticks(const Tuning *t, float seconds)
{
    return (int)lroundf(seconds * (float)t->sim_hz);
}

float segment_sphere(Vector3 a, Vector3 b, Vector3 center, float radius)
{
    Vector3 d = Vector3Subtract(b, a);
    Vector3 f = Vector3Subtract(a, center);
    float c = Vector3DotProduct(f, f) - radius * radius;
    if (c <= 0.0f) return 0.0f;   // já começou dentro
    float aa = Vector3DotProduct(d, d);
    if (aa < 1e-9f) return -1.0f;
    float bb = 2.0f * Vector3DotProduct(f, d);
    float disc = bb * bb - 4.0f * aa * c;
    if (disc < 0.0f) return -1.0f;
    float hit = (-bb - sqrtf(disc)) / (2.0f * aa);
    return (hit >= 0.0f && hit <= 1.0f) ? hit : -1.0f;
}

static bool playing(const World *w)
{
    return w->state == GAME_PLAYING;
}

// XP e essência de um abate; avisa quando a nave sobe de nível.
static void gain_xp(World *w, const Tuning *t, EnemyType type, float xp, Vector3 where)
{
    if (evolution_gain(&w->evo, t, type, xp) > 0) {
        events_push(&w->events, (uint32_t)w->tick, EV_LEVEL_UP, (uint8_t)type, where, (float)w->evo.level);
    }
}

// ---------------------------------------------------------------------------
//  Arma
// ---------------------------------------------------------------------------

typedef struct {
    float   best_dot;
    int     enemy, bomb;
    Vector3 point;
} AssistSearch;

static void consider(AssistSearch *search, const Ship *s, const Tuning *t, Vector3 forward, Vector3 pos, Vector3 vel,
                     float radius, int enemy, int bomb)
{
    Vector3 to = Vector3Subtract(pos, s->pos);
    float distance = Vector3Length(to);
    if (distance < 1.0f || distance > t->weapon_assist_range) return;

    // Onde o alvo vai estar quando o tiro chegar. Os tiros levam junto a
    // velocidade da nave, então o que conta é o movimento relativo.
    Vector3 relative = Vector3Subtract(vel, Vector3Add(s->vel, s->fall));
    Vector3 predicted = Vector3Add(pos, Vector3Scale(relative, distance / t->weapon_speed));
    Vector3 dir = Vector3Normalize(Vector3Subtract(predicted, s->pos));

    // Alvos grandes ou perto ocupam mais da tela: o cone cresce com eles.
    float allowed = t->weapon_assist_angle * DEG2RAD + atanf(radius / distance);
    float dot = Vector3DotProduct(forward, dir);
    if (dot < cosf(allowed) || dot <= search->best_dot) return;
    search->best_dot = dot;
    search->enemy = enemy;
    search->bomb = bomb;
    search->point = predicted;
}

Vector3 combat_lead_point(const World *w, const Tuning *t, Vector3 pos, Vector3 vel)
{
    const Ship *s = &w->ship;
    float distance = Vector3Distance(pos, s->pos);
    Vector3 relative = Vector3Subtract(vel, Vector3Add(s->vel, s->fall));
    return Vector3Add(pos, Vector3Scale(relative, distance / t->weapon_speed));
}

int combat_lead_target(const World *w, const Tuning *t, float max_angle_deg)
{
    const Ship *s = &w->ship;
    if (!s->alive) return -1;
    Vector3 forward = ship_forward(s);
    float best = cosf(max_angle_deg * DEG2RAD);
    int found = -1;
    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        Vector3 to = Vector3Subtract(e->items[i].pos, s->pos);
        float distance = Vector3Length(to);
        if (distance < 1.0f || distance > t->weapon_range * 0.85f) continue;
        float dot = Vector3DotProduct(forward, Vector3Scale(to, 1.0f / distance));
        if (dot > best) {
            best = dot;
            found = i;
        }
    }
    return found;
}

bool combat_aim_assist(const World *w, const Tuning *t, AimAssist *out)
{
    out->enemy = -1;
    out->bomb = -1;
    const Ship *s = &w->ship;
    if (!s->alive || t->weapon_assist_angle <= 0.0f) return false;

    AssistSearch search = { -2.0f, -1, -1, { 0.0f, 0.0f, 0.0f } };
    Vector3 forward = ship_forward(s);

    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        consider(&search, s, t, forward, en->pos, en->vel, t->enemy[en->type].radius, i, -1);
    }
    const Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        consider(&search, s, t, forward, p->items[i].pos, p->items[i].vel, t->bomber_bomb_radius, -1, i);
    }

    out->enemy = search.enemy;
    out->bomb = search.bomb;
    out->point = search.point;
    return search.enemy >= 0 || search.bomb >= 0;
}

void combat_fire(World *w, const InputState *in, const Tuning *t, float dt)
{
    Ship *s = &w->ship;
    s->muzzle = fmaxf(s->muzzle - dt, 0.0f);
    s->fire_wait -= dt;

    bool trigger = s->alive && (in->held & ACT_BIT(ACT_FIRE)) != 0u;
    if (!trigger) {
        // Solto o botão, o próximo tiro sai assim que ele for apertado.
        if (s->fire_wait < 0.0f) s->fire_wait = 0.0f;
        return;
    }

    float rate = t->weapon_rate * (s->in_atmosphere ? t->weapon_graze_rate : 1.0f);
    AimAssist assist;
    bool assisted = combat_aim_assist(w, t, &assist);
    Vector3 forward = ship_forward(s);
    Vector3 converge = Vector3Add(s->pos, Vector3Scale(forward, COMBAT_AIM_DISTANCE));

    Vector3 carried = Vector3Add(s->vel, s->fall);
    float life = t->weapon_range / t->weapon_speed;
    int fan = w->evo.rank[EVO_SPREAD];
    uint8_t pierce = w->evo.rank[EVO_PIERCE];

    while (s->fire_wait <= 0.0f) {
        Vector3 muzzle = Vector3Add(s->pos, Vector3RotateByQuaternion(GUNS[s->gun], s->rot));
        Vector3 aim = assisted ? assist.point : converge;
        Vector3 dir = Vector3Normalize(Vector3Subtract(aim, muzzle));
        Vector3 vel = Vector3Add(Vector3Scale(dir, t->weapon_speed), carried);

        Shot *bolt = projectiles_spawn(&w->shots, SHOT_BOLT, muzzle, vel, life);
        if (bolt != NULL) {
            bolt->damage = t->weapon_damage;
            bolt->pierce = pierce;
        }
        // Tiro em leque: pares de tiros mais fracos, abertos para os lados.
        for (int k = 1; k <= fan; k++) {
            for (int side = -1; side <= 1; side += 2) {
                Quaternion turn = QuaternionFromAxisAngle(ship_up(s), (float)(side * k) * t->spread_angle * DEG2RAD);
                Vector3 spread = Vector3RotateByQuaternion(dir, turn);
                Shot *extra = projectiles_spawn(&w->shots, SHOT_BOLT, muzzle,
                                                Vector3Add(Vector3Scale(spread, t->weapon_speed), carried), life);
                if (extra == NULL) continue;
                extra->damage = t->weapon_damage * t->spread_damage;
                extra->pierce = pierce;
            }
        }
        events_push(&w->events, (uint32_t)w->tick, EV_SHOT, (uint8_t)s->gun, muzzle, 0.0f);

        s->gun ^= 1;
        s->muzzle = MUZZLE_TIME;
        s->fire_wait += 1.0f / rate;
    }
}

// ---------------------------------------------------------------------------
//  Dano na nave
// ---------------------------------------------------------------------------

void combat_ship_wreck(World *w)
{
    const Ship *s = &w->ship;
    Vector3 still = { 0.0f, 0.0f, 0.0f };
    Particle *flash = particles_spawn(&w->particles);
    if (flash != NULL) {
        flash->kind = PARTICLE_FLASH;
        flash->pos = s->pos;
        flash->vel = still;
        flash->size = WRECK_FLASH_SIZE;
        flash->life = 0.45f;
        flash->r = WRECK_HOT.r;
        flash->g = WRECK_HOT.g;
        flash->b = WRECK_HOT.b;
    }
    for (int i = 0; i < WRECK_EMBERS; i++) {
        Particle *p = particles_spawn(&w->particles);
        if (p == NULL) break;
        Rgb8 c = (i % 3 == 0) ? WRECK_COOL : WRECK_HOT;
        p->kind = PARTICLE_EMBER;
        p->pos = s->pos;
        p->vel = Vector3Scale(rng_unit_vec3(&w->rng), rng_range(&w->rng, 3.0f, 22.0f));
        p->size = rng_range(&w->rng, 0.2f, 0.55f);
        p->life = rng_range(&w->rng, 0.6f, 2.2f);
        p->r = c.r;
        p->g = c.g;
        p->b = c.b;
    }
    world_jolt(w, WRECK_JOLT);
    events_push(&w->events, (uint32_t)w->tick, EV_PLAYER_DOWN, 0, s->pos, 0.0f);
}

void combat_damage_ship(World *w, const Tuning *t, float amount, Vector3 point)
{
    Ship *s = &w->ship;
    if (!s->alive || ship_invulnerable(s) || amount <= 0.0f) return;

    bool held = amount <= s->shield;
    bool died = ship_damage(s, t, amount);
    s->hit_flash = HIT_FLASH_TIME;
    s->hit_shield = held;

    events_push(&w->events, (uint32_t)w->tick, EV_PLAYER_HIT, held ? 1 : 0, point, amount);
    Vector3 normal = Vector3Normalize(Vector3Subtract(point, s->pos));
    particles_sparks(&w->particles, &w->rng, point, normal, 10, 12.0f, held ? SHIELD_SPARK : HULL_SPARK);
    world_jolt(w, amount * JOLT_PER_DAMAGE);
    if (amount >= BIG_HIT) {
        int ticks = seconds_to_ticks(t, t->combat_hitstop_player);
        if (ticks > w->hitstop) w->hitstop = ticks;
    }
    if (died) combat_ship_wreck(w);
}

// ---------------------------------------------------------------------------
//  Quem acerta quem
// ---------------------------------------------------------------------------

static void blow_up_bomb(World *w, const Tuning *t, int index, bool by_player)
{
    Shot *b = &w->shots.items[index];
    particles_explosion(&w->particles, &w->rng, b->pos, b->vel, BOMB_EXPLOSION, BOMB_BLAST.hot, BOMB_BLAST.glow,
                        BOMB_BLAST.debris);
    if (by_player && playing(w)) {
        int gained = score_kill(&w->score, t, t->bomber_bomb_points);
        w->score.bombs_down++;
        w->kill_marker = KILL_MARKER_TIME;
        events_push(&w->events, (uint32_t)w->tick, EV_BOMB_DOWN, ENEMY_BOMBER, b->pos, (float)gained);
        gain_xp(w, t, ENEMY_BOMBER, t->xp_bomb, b->pos);
    }
    projectiles_remove(&w->shots, index);
}

// Explosão da nave (dash explosivo, reação em cadeia, ogivas): fere os
// inimigos em volta (menos `skip`), com o dano caindo para a borda, e
// derruba bombas. Protegidos por um Escudeiro não sofrem nada.
static void ship_blast(World *w, const Tuning *t, Vector3 center, float radius, float damage, int skip, bool show)
{
    if (damage <= 0.0f) return;
    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (i == skip || !pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        float reach = radius + t->enemy[en->type].radius;
        float d = Vector3Distance(en->pos, center);
        if (d > reach || enemies_shielded_by(e, t, i) >= 0) continue;
        enemies_damage(e, i, damage * Clamp(1.0f - d / reach, BLAST_MIN, 1.0f), DEATH_PLAYER);
    }
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        if (Vector3Distance(p->items[i].pos, center) > radius + t->bomber_bomb_radius) continue;
        p->items[i].hp -= damage;
        if (p->items[i].hp <= 0.0f) blow_up_bomb(w, t, i, true);
    }
    if (show) {
        particles_explosion(&w->particles, &w->rng, center, (Vector3){ 0.0f, 0.0f, 0.0f }, radius / BLAST_VISUAL,
                            SHIP_BLAST.hot, SHIP_BLAST.glow, SHIP_BLAST.debris);
        events_push(&w->events, (uint32_t)w->tick, EV_BLAST, 0, center, radius);
    }
}

void combat_dash_blast(World *w, const Tuning *t)
{
    int rank = w->evo.rank[EVO_BLAST_DASH];
    if (rank <= 0 || !w->ship.alive) return;
    ship_blast(w, t, w->ship.pos, t->blast_dash_radius, t->blast_dash_damage * (float)rank, -1, true);
}

// Reflexão: com escudo, um tiro inimigo pode voltar como tiro da nave.
static bool reflect(World *w, const Tuning *t, Shot *s, Vector3 point)
{
    int rank = w->evo.rank[EVO_REFLECT];
    if (rank <= 0 || w->ship.shield <= 0.0f) return false;
    if (rng_float(&w->rng) >= t->reflect_chance * (float)rank) return false;
    Vector3 back = Vector3Normalize(Vector3Negate(s->vel));
    s->kind = SHOT_BOLT;
    s->vel = Vector3Scale(back, t->weapon_speed);
    s->pos = point;
    s->pos_prev = point;
    s->age = 0.0f;
    s->life = t->weapon_range / t->weapon_speed;
    s->damage = t->weapon_damage * REFLECT_DAMAGE;
    s->crater_radius = 0.0f;
    s->pierce = 0;
    s->last_hit = -1;
    particles_sparks(&w->particles, &w->rng, point, back, 12, 16.0f, SHIELD_SPARK);
    events_push(&w->events, (uint32_t)w->tick, EV_REFLECT, 0, point, 0.0f);
    return true;
}

// Tiro da nave numa peça do boss: alvo (capacitor, bolsa...), núcleo (blindado
// fora da janela de ataque) ou casco (só faísca).
static void hit_boss(World *w, const Tuning *t, const Shot *s, const BossPart *part, Vector3 point, Vector3 back)
{
    Boss *b = &w->boss;
    uint32_t tick = (uint32_t)w->tick;
    float damage = s->damage * (1.0f + t->armor_break_bonus * (float)w->evo.rank[EVO_ARMOR_BREAK]);
    if (w->evo.rank[EVO_CRIT] > 0 && rng_float(&w->rng) < t->crit_chance * (float)w->evo.rank[EVO_CRIT]) {
        damage *= t->crit_mult;
        events_push(&w->events, tick, EV_CRIT, ENEMY_BOMBER, point, damage);
    }
    float applied = 0.0f;
    bool open = part->open;
    if (part->role != PART_HULL) applied = boss_hit(b, t, part->id, damage, &w->events, tick);
    if (applied > 0.0f) b->flash = 0.06f;
    particles_sparks(&w->particles, &w->rng, point, back, open ? 9 : 5, 14.0f, open ? WEAK_SPARK : ARMOR_SPARK);
    events_push(&w->events, tick, open ? EV_WEAK_HIT : EV_ARMOR_HIT, ENEMY_BOMBER, point, applied);
    w->hit_marker = HIT_MARKER_TIME;
}

// Tiro da nave: acha o primeiro obstáculo no trecho que ele percorreu.
static void resolve_bolt(World *w, const Tuning *t, int index)
{
    Shot *s = &w->shots.items[index];
    Vector3 a = s->pos_prev, b = s->pos;
    float first = 2.0f;
    int enemy = -1, bomb = -1;
    bool weak = false, ground = false;

    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f || i == s->last_hit) continue;
        const Enemy *en = &e->items[i];
        float hit = segment_sphere(a, b, en->pos, t->enemy[en->type].radius + t->weapon_hit_margin);
        bool on_weak = false;
        if (en->type == ENEMY_BOMBER) {
            // O motor sai um pouco para fora do casco: vindo de trás, o tiro
            // o acerta antes de chegar ao resto.
            float weak_hit = segment_sphere(a, b, enemy_weak_spot(en, t), t->bomber_weak_radius + t->weapon_hit_margin);
            if (weak_hit >= 0.0f && (hit < 0.0f || weak_hit <= hit)) {
                hit = weak_hit;
                on_weak = true;
            }
        }
        if (hit >= 0.0f && hit < first) {
            first = hit;
            enemy = i;
            weak = on_weak;
        }
    }

    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        float hit = segment_sphere(a, b, p->items[i].pos, t->bomber_bomb_radius + t->weapon_hit_margin);
        if (hit >= 0.0f && hit < first) {
            first = hit;
            bomb = i;
            enemy = -1;
        }
    }

    Vector3 travel = Vector3Subtract(b, a);
    float length = Vector3Length(travel);
    Vector3 ground_point;
    if (length > 1e-4f && planet_raycast(&w->planet, a, travel, length, &ground_point)) {
        float hit = Vector3Distance(a, ground_point) / length;
        if (hit < first) {
            first = hit;
            enemy = bomb = -1;
            ground = true;
        }
    }
    // O boss: alvos e núcleo (com a folga de mira) e o casco (que só bloqueia).
    int boss_part = -1;
    BossPart parts[BOSS_PARTS_MAX];
    Boss *bs = &w->boss;
    int part_count = boss_parts(bs, t, parts);
    for (int k = 0; k < part_count; k++) {
        float radius = parts[k].radius + ((parts[k].role != PART_HULL) ? t->weapon_hit_margin : 0.0f);
        float hit = segment_sphere(a, b, parts[k].center, radius);
        if (hit >= 0.0f && hit < first) {
            first = hit;
            boss_part = k;
            enemy = bomb = -1;
            ground = false;
        }
    }

    float moon_hit = segment_sphere(a, b, w->moon.pos, w->moon.radius);
    if (moon_hit >= 0.0f && moon_hit < first) {
        first = moon_hit;
        enemy = bomb = -1;
        boss_part = -1;
        ground = true;
    }

    // Rochas: só os tiros do jogador as quebram, e nunca a que ele reboca (ou
    // acabou de soltar), que fica bem na frente da arma.
    int rock = -1;
    if (s->source == BOLT_PLAYER) {
        for (int i = 0; i < ROCKS_MAX; i++) {
            const Rock *k = &w->rocks.items[i];
            if (!k->alive || k->state == ROCK_TOWED || k->state == ROCK_FALLING) continue;
            float hit = segment_sphere(a, b, k->pos, k->radius + t->weapon_hit_margin * 0.5f);
            if (hit >= 0.0f && hit < first) {
                first = hit;
                rock = i;
                enemy = bomb = boss_part = -1;
                ground = false;
            }
        }
    }

    if (first > 1.0f) return;   // não acertou nada neste passo
    Vector3 point = Vector3Lerp(a, b, first);
    Vector3 back = Vector3Normalize(Vector3Negate(travel));

    if (enemy >= 0 && enemies_shielded_by(e, t, enemy) >= 0) {
        // Coberto por um Escudeiro: o tiro para no escudo.
        particles_sparks(&w->particles, &w->rng, point, back, 6, 10.0f, SHIELD_BLOCK);
        events_push(&w->events, (uint32_t)w->tick, EV_SHIELDED, e->items[enemy].type, point, 0.0f);
    } else if (enemy >= 0) {
        Enemy *en = &e->items[enemy];
        const uint8_t *rank = w->evo.rank;
        float damage = s->damage;
        EventType kind = EV_HIT;
        Rgb8 spark = BOLT_SPARK;
        if (en->type == ENEMY_BOMBER) {
            // Com Quebra-blindagem, a frente blindada não segura mais o tiro.
            float armor = (rank[EVO_ARMOR_BREAK] > 0) ? 1.0f : t->bomber_armor;
            kind = weak ? EV_WEAK_HIT : EV_ARMOR_HIT;
            damage *= weak ? t->bomber_weak_mult : armor;
            spark = weak ? WEAK_SPARK : ARMOR_SPARK;
        }
        if (is_heavy((EnemyType)en->type)) damage *= 1.0f + t->armor_break_bonus * (float)rank[EVO_ARMOR_BREAK];

        // Acerto crítico: chance maior contra quem está longe.
        if (rank[EVO_CRIT] > 0) {
            float chance = t->crit_chance * (float)rank[EVO_CRIT];
            if (Vector3Distance(point, w->ship.pos) > t->crit_far) chance *= 2.0f;
            if (rng_float(&w->rng) < chance) {
                damage *= t->crit_mult;
                spark = CRIT_SPARK;
                events_push(&w->events, (uint32_t)w->tick, EV_CRIT, en->type, point, damage);
            }
        }

        enemies_damage(e, enemy, damage, DEATH_PLAYER);
        particles_sparks(&w->particles, &w->rng, point, back, weak ? 8 : 5, 14.0f, spark);
        events_push(&w->events, (uint32_t)w->tick, kind, en->type, point, damage);
        w->hit_marker = HIT_MARKER_TIME;

        // Ogivas: quem está em volta também leva uma parte.
        if (rank[EVO_WARHEADS] > 0) {
            float splash = damage * t->warheads_fraction * (float)rank[EVO_WARHEADS];
            ship_blast(w, t, point, t->warheads_radius, splash, enemy, false);
        }
        // Sifão de energia: cada acerto devolve energia.
        if (rank[EVO_ENERGY_SIPHON] > 0 && w->ship.alive) {
            w->ship.energy = fminf(w->ship.energy + t->energy_siphon_energy * (float)rank[EVO_ENERGY_SIPHON],
                                   t->ship_energy_max);
        }
        // Tiro perfurante: atravessa e segue.
        if (s->pierce > 0) {
            s->pierce--;
            s->last_hit = (int16_t)enemy;
            return;
        }
    } else if (boss_part >= 0) {
        hit_boss(w, t, s, &parts[boss_part], point, back);
    } else if (rock >= 0) {
        world_damage_rock(w, t, rock, s->damage, point, back);
    } else if (bomb >= 0) {
        Shot *target = &p->items[bomb];
        target->hp -= s->damage;
        particles_sparks(&w->particles, &w->rng, point, back, 5, 12.0f, ARMOR_SPARK);
        events_push(&w->events, (uint32_t)w->tick, EV_HIT, ENEMY_BOMBER, point, s->damage);
        w->hit_marker = HIT_MARKER_TIME;
        if (target->hp <= 0.0f) blow_up_bomb(w, t, bomb, true);
    } else if (ground) {
        particles_sparks(&w->particles, &w->rng, point, back, 4, 8.0f, BOLT_SPARK);
        events_push(&w->events, (uint32_t)w->tick, EV_SPARK, 0, point, 0.0f);
    }
    projectiles_remove(p, index);
}

// Tiro de plasma ou bomba: acerta a nave, o planeta ou a Lua.
static void resolve_enemy_shot(World *w, const Tuning *t, int index)
{
    Shot *s = &w->shots.items[index];
    Vector3 a = s->pos_prev, b = s->pos;
    Ship *ship = &w->ship;
    bool bomb = (s->kind == SHOT_BOMB);

    if (ship->alive && w->ship_active) {
        float reach = bomb ? t->ship_radius + t->bomber_bomb_radius : t->ship_radius * SHIP_HIT_SCALE;
        float hit = segment_sphere(a, b, ship->pos, reach);
        if (hit >= 0.0f && !ship_invulnerable(ship)) {
            if (!bomb && reflect(w, t, s, Vector3Lerp(a, b, hit))) return;
            combat_damage_ship(w, t, s->damage, Vector3Lerp(a, b, hit));
            if (bomb) blow_up_bomb(w, t, index, false);
            else      projectiles_remove(&w->shots, index);
            return;
        }
    }

    Vector3 travel = Vector3Subtract(b, a);
    float length = Vector3Length(travel);
    Vector3 ground;
    if (length > 1e-4f && planet_raycast(&w->planet, a, travel, length, &ground)) {
        if (s->crater_radius > 0.0f) {
            world_impact(w, t, a, travel, s->crater_radius, s->crater_depth);
        } else {
            particles_sparks(&w->particles, &w->rng, ground, Vector3Normalize(ground), 4, 8.0f, PLASMA_SPARK);
        }
        if (bomb) events_push(&w->events, (uint32_t)w->tick, EV_BOMB_IMPACT, ENEMY_BOMBER, ground, 0.0f);
        projectiles_remove(&w->shots, index);
        return;
    }

    if (segment_sphere(a, b, w->moon.pos, w->moon.radius) >= 0.0f) {
        if (bomb) blow_up_bomb(w, t, index, false);
        else      projectiles_remove(&w->shots, index);
    }
}

// Distância de `p` ao segmento de `a` até `b`.
static float point_segment(Vector3 p, Vector3 a, Vector3 b)
{
    Vector3 ab = Vector3Subtract(b, a);
    float len2 = Vector3LengthSqr(ab);
    float u = (len2 > 1e-8f) ? Clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / len2, 0.0f, 1.0f) : 0.0f;
    return Vector3Distance(p, Vector3Add(a, Vector3Scale(ab, u)));
}

void combat_boss(World *w, const Tuning *t)
{
    Boss *b = &w->boss;
    Ship *s = &w->ship;
    if (!boss_active(b) || !s->alive || !w->ship_active) return;

    // Laser: queima a nave que estiver no feixe (de tempos em tempos, não a
    // cada passo, para o aviso de dano não piscar sem parar).
    if (b->type == BOSS_BREAKER && b->state == BOSS_FIGHT && b->action == BREAKER_FIRE) {
        Vector3 from = boss_point(b, t, BREAKER_MUZZLE);
        if (point_segment(s->pos, from, b->breaker.beam_point) < t->breaker_beam_width + t->ship_radius) {
            if (++w->beam_ticks >= BEAM_TICKS) {
                w->beam_ticks = 0;
                combat_damage_ship(w, t, t->breaker_beam_damage * (float)BEAM_TICKS / (float)t->sim_hz, s->pos);
            }
        } else {
            w->beam_ticks = BEAM_TICKS - 1;   // entrou no feixe: o primeiro dano é na hora
        }
    }

    // O casco é um muro: a nave é empurrada para fora e quica.
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_parts(b, t, parts);
    for (int k = 0; k < count; k++) {
        if (!parts[k].solid) continue;
        Vector3 center = parts[k].center;
        float reach = parts[k].radius + t->ship_radius;
        Vector3 offset = Vector3Subtract(s->pos, center);
        float distance = Vector3Length(offset);
        if (distance >= reach || distance < 1e-4f) continue;
        Vector3 normal = Vector3Scale(offset, 1.0f / distance);
        s->pos = Vector3Add(center, Vector3Scale(normal, reach + 0.05f));
        Vector3 relative = Vector3Subtract(Vector3Add(s->vel, s->fall), b->vel);
        float into = -Vector3DotProduct(relative, normal);
        if (into > 0.0f) {
            s->vel = Vector3Add(s->vel, Vector3Scale(normal, into * (1.0f + t->ship_bounce)));
            if (into > RAM_MIN_SPEED) {
                combat_damage_ship(w, t, t->breaker_ram_damage * Clamp(into / RAM_SPEED_FULL, 0.3f, 1.0f),
                                   Vector3Add(center, Vector3Scale(normal, reach - t->ship_radius)));
            }
        }
    }
}

void combat_ability(World *w, const InputState *in, const Tuning *t, float dt)
{
    Ability *a = &w->ability;
    Ship *s = &w->ship;
    a->cooldown = fmaxf(a->cooldown - dt, 0.0f);
    a->beam_left = fmaxf(a->beam_left - dt, 0.0f);
    bool held = (in->held & ACT_BIT(ACT_ALT_FIRE)) != 0u;
    if (!a->owned || !s->alive || !held || a->cooldown > 0.0f) {
        a->charge = 0.0f;
        return;
    }
    a->charge += dt;
    if (a->charge < t->ability_charge) return;

    // O raio: reto à frente do nariz, até o alcance ou até o planeta.
    a->charge = 0.0f;
    a->cooldown = t->ability_cooldown;
    Vector3 dir = ship_forward(s);
    Vector3 from = Vector3Add(s->pos, Vector3Scale(dir, 2.0f));
    float length = t->ability_range;
    Vector3 hit;
    if (planet_raycast(&w->planet, from, dir, length, &hit)) length = Vector3Distance(from, hit);
    Vector3 to = Vector3Add(from, Vector3Scale(dir, length));
    a->beam_from = from;
    a->beam_to = to;
    a->beam_left = ABILITY_BEAM_SHOW;
    uint32_t tick = (uint32_t)w->tick;

    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        if (point_segment(en->pos, from, to) > t->ability_width + t->enemy[en->type].radius) continue;
        if (enemies_shielded_by(e, t, i) >= 0) continue;
        enemies_damage(e, i, t->ability_damage, DEATH_PLAYER);
        particles_sparks(&w->particles, &w->rng, en->pos, Vector3Negate(dir), 10, 18.0f, CRIT_SPARK);
    }
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i) || p->items[i].kind != SHOT_BOMB) continue;
        if (point_segment(p->items[i].pos, from, to) < t->ability_width + t->bomber_bomb_radius) blow_up_bomb(w, t, i, true);
    }
    Boss *b = &w->boss;
    if (boss_vulnerable(b)) {
        BossPart parts[BOSS_PARTS_MAX];
        int count = boss_parts(b, t, parts);
        for (int k = 0; k < count; k++) {
            if (parts[k].role == PART_HULL) continue;
            if (point_segment(parts[k].center, from, to) < parts[k].radius + t->ability_width) {
                boss_hit(b, t, parts[k].id, t->ability_damage, &w->events, tick);
            }
        }
    }
    world_jolt(w, ABILITY_JOLT);
    events_push(&w->events, tick, EV_ABILITY, 0, from, length);
}

// ---------------------------------------------------------------------------
//  Núcleo Colmeia: a Ninhada
// ---------------------------------------------------------------------------

// O inimigo vivo (e sem escudo) mais perto de `pos`, até `range`; -1 se nenhum.
static int nearest_enemy(const World *w, const Tuning *t, Vector3 pos, float range)
{
    const Enemies *e = &w->enemies;
    int best = -1;
    float best_d = range;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        if (enemies_shielded_by(e, t, i) >= 0) continue;
        float d = Vector3Distance(pos, e->items[i].pos);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

// Para onde um ferrão vira: o inimigo dele, o mais perto, ou um ponto fraco
// aberto do boss.
static bool brood_target(World *w, const Tuning *t, Shot *s, Vector3 *out)
{
    const Enemies *e = &w->enemies;
    if (s->homing >= 0 && (!pool_alive(&e->pool, s->homing) || e->items[s->homing].hp <= 0.0f)) s->homing = -1;
    if (s->homing < 0) s->homing = (int16_t)nearest_enemy(w, t, s->pos, BROOD_SEEK);
    if (s->homing >= 0) {
        *out = e->items[s->homing].pos;
        return true;
    }
    BossPart parts[BOSS_PARTS_MAX];
    int count = boss_vulnerable(&w->boss) ? boss_parts(&w->boss, t, parts) : 0;
    float best = BROOD_SEEK;
    bool found = false;
    for (int k = 0; k < count; k++) {
        if (!parts[k].open) continue;
        float d = Vector3Distance(s->pos, parts[k].center);
        if (d < best) {
            best = d;
            *out = parts[k].center;
            found = true;
        }
    }
    return found;
}

void combat_brood(World *w, const InputState *in, const Tuning *t, float dt)
{
    Brood *br = &w->brood;
    Ship *s = &w->ship;
    br->cooldown = fmaxf(br->cooldown - dt, 0.0f);

    // Os ferrões no ar viram aos poucos para o alvo.
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        Shot *sh = &p->items[i];
        if (sh->kind != SHOT_BOLT || sh->source != BOLT_BROOD) continue;
        Vector3 target = sh->pos;
        if (!brood_target(w, t, sh, &target)) continue;
        float speed = Vector3Length(sh->vel);
        Vector3 desired = Vector3Normalize(Vector3Subtract(target, sh->pos));
        Vector3 dir = Vector3Normalize(Vector3Lerp(Vector3Scale(sh->vel, 1.0f / fmaxf(speed, 1e-3f)), desired,
                                                   Clamp(t->brood_turn * dt, 0.0f, 1.0f)));
        sh->vel = Vector3Scale(dir, fmaxf(speed, t->brood_speed));
    }

    if (!br->owned || !s->alive || !(in->pressed & ACT_BIT(ACT_ABILITY2)) || br->cooldown > 0.0f) return;

    // Solta: cada ferrão fica com um dos inimigos mais perto, em rodízio.
    int targets[BROOD_MAX], found = 0;
    for (int n = 0; n < t->brood_count && n < BROOD_MAX; n++) {
        int best = -1;
        float best_d = BROOD_SEEK;
        const Enemies *e = &w->enemies;
        for (int i = 0; i < e->pool.capacity; i++) {
            if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f || enemies_shielded_by(e, t, i) >= 0) continue;
            bool taken = false;
            for (int k = 0; k < found; k++) taken = taken || targets[k] == i;
            float d = Vector3Distance(s->pos, e->items[i].pos);
            if (!taken && d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best < 0) break;
        targets[found++] = best;
    }
    Vector3 forward = ship_forward(s), up = ship_up(s);
    Vector3 right = Vector3CrossProduct(forward, up);
    int count = (t->brood_count < BROOD_MAX) ? t->brood_count : BROOD_MAX;
    for (int i = 0; i < count; i++) {
        // Um leque à frente da nave, um pouco para cima e para baixo.
        float across = ((float)i + 0.5f) / (float)count * 2.0f - 1.0f;
        Vector3 dir = Vector3Normalize(Vector3Add(forward, Vector3Add(Vector3Scale(right, across * BROOD_SPREAD),
                                                                      Vector3Scale(up, sinf((float)i * 2.1f) * 0.3f))));
        Vector3 vel = Vector3Add(Vector3Scale(dir, t->brood_speed), Vector3Add(s->vel, s->fall));
        Shot *sh = projectiles_spawn(p, SHOT_BOLT, Vector3Add(s->pos, Vector3Scale(dir, 2.0f)), vel, t->brood_life);
        if (sh == NULL) break;
        sh->source = BOLT_BROOD;
        sh->damage = t->brood_damage;
        sh->homing = (int16_t)((found > 0) ? targets[i % found] : -1);
    }
    br->cooldown = t->brood_cooldown;
    events_push(&w->events, (uint32_t)w->tick, EV_BROOD, 0, s->pos, (float)count);
}

// ---------------------------------------------------------------------------
//  Núcleo do Verme: o Túnel
// ---------------------------------------------------------------------------

void combat_tunnel(World *w, const InputState *in, const Tuning *t, float dt)
{
    Tunnel *tu = &w->tunnel;
    Ship *s = &w->ship;
    tu->cooldown = fmaxf(tu->cooldown - dt, 0.0f);
    tu->flash = fmaxf(tu->flash - dt, 0.0f);
    s->phase_left = fmaxf(s->phase_left - dt, 0.0f);
    if (!tu->owned || !s->alive || !(in->pressed & ACT_BIT(ACT_ABILITY3)) || tu->cooldown > 0.0f) return;

    // Do outro lado do planeta, na mesma altura relativa: o chão de lá é
    // achado por um raio vindo de fora.
    float radius = w->planet.params.radius;
    Vector3 dir = Vector3Negate(Vector3Normalize(s->pos));
    Vector3 outside = Vector3Scale(dir, radius * 2.0f);
    Vector3 ground = Vector3Scale(dir, radius);
    planet_raycast(&w->planet, outside, Vector3Negate(dir), radius * 2.0f, &ground);
    Vector3 to = Vector3Add(ground, Vector3Scale(dir, t->tunnel_altitude));

    // A frente continua a mesma (o plano do chão lá é paralelo ao daqui).
    Vector3 forward = ship_forward(s);
    forward = Vector3Subtract(forward, Vector3Scale(dir, Vector3DotProduct(forward, dir)));
    if (Vector3LengthSqr(forward) < 1e-6f) forward = Vector3Perpendicular(dir);
    forward = Vector3Normalize(forward);
    float speed = Vector3Length(s->vel);

    tu->from = s->pos;
    particles_explosion(&w->particles, &w->rng, s->pos, (Vector3){ 0.0f, 0.0f, 0.0f }, 3.0f, TUNNEL_HOT, TUNNEL_GLOW,
                        TUNNEL_DUST);
    // A rocha a reboque vai junto: o mesmo lugar em relação à nave, com o
    // "para baixo" virado (lá, baixo é o contrário daqui).
    if (w->rocks.towed >= 0) {
        Rock *k = &w->rocks.items[w->rocks.towed];
        Vector3 offset = Vector3Subtract(k->pos, s->pos);
        offset = Vector3Subtract(offset, Vector3Scale(dir, 2.0f * Vector3DotProduct(offset, dir)));
        k->pos = k->pos_prev = Vector3Add(to, offset);
        k->vel = Vector3Scale(forward, speed);
    }
    s->pos = s->pos_prev = to;
    s->rot = s->rot_prev = quat_look(forward, dir);
    s->vel = Vector3Scale(forward, speed);
    s->fall = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->phase_left = t->tunnel_time;
    tu->cooldown = t->tunnel_cooldown;
    tu->flash = TUNNEL_FLASH;
    particles_explosion(&w->particles, &w->rng, to, Vector3Scale(dir, 6.0f), 3.5f, TUNNEL_HOT, TUNNEL_GLOW, TUNNEL_DUST);
    world_set_view(w, t, CAM_SHIP);   // a câmera vai junto, sem deslizar pelo planeta
    world_jolt(w, TUNNEL_JOLT);
    events_push(&w->events, (uint32_t)w->tick, EV_TUNNEL, 0, to, 0.0f);
}

// ---------------------------------------------------------------------------
//  Esporos da Colmeia
// ---------------------------------------------------------------------------

void combat_spore_blast(World *w, const Tuning *t, int index, bool thrown)
{
    Rock *k = &w->rocks.items[index];
    if (!k->alive) return;
    uint32_t tick = (uint32_t)w->tick;
    Vector3 at = k->pos;
    float blast = t->hive_spore_blast;
    k->alive = false;
    if (w->rocks.towed == index) w->rocks.towed = -1;
    particles_explosion(&w->particles, &w->rng, at, Vector3Scale(k->vel, 0.2f), blast * 0.35f, SPORE_HOT, SPORE_GLOW,
                        SPORE_GOO);
    world_jolt(w, SPORE_JOLT);

    // Estoura bolsas e propulsores em volta; arremessado, fere o coração mesmo fechado.
    bool core = false;
    Boss *b = &w->boss;
    if (boss_vulnerable(b)) {
        BossPart parts[BOSS_PARTS_MAX];
        int count = boss_parts(b, t, parts);
        for (int i = 0; i < count; i++) {
            if (parts[i].role == PART_HULL) continue;
            if (Vector3Distance(parts[i].center, at) - parts[i].radius > blast) continue;
            if (parts[i].role == PART_CORE) {
                if (!thrown) continue;
                boss_hit_core_raw(b, t, t->hive_spore_damage, &w->events, tick);
                core = true;
            } else {
                boss_hit(b, t, parts[i].id, t->hive_spore_damage, &w->events, tick);
            }
        }
    }
    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        if (Vector3Distance(e->items[i].pos, at) > blast + t->enemy[e->items[i].type].radius) continue;
        enemies_damage(e, i, t->hive_spore_damage * 0.5f, DEATH_PLAYER);
    }
    // Estourado de perto (a tiro), respinga na nave.
    Ship *s = &w->ship;
    if (!thrown && s->alive && w->ship_active && Vector3Distance(s->pos, at) < blast * 0.7f) {
        combat_damage_ship(w, t, t->hive_spore_damage * 0.3f, at);
    }
    if (core) w->hit_marker = HIT_MARKER_TIME;
    events_push(&w->events, tick, EV_SPORE_BLAST, 0, at, core ? 1.0f : 0.0f);
}

void combat_spores(World *w, const Tuning *t)
{
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive || k->kind != ROCK_SPORE || k->state != ROCK_THROWN) continue;
        Vector3 a = k->pos_prev, b = k->pos;
        bool hit = planet_sample(&w->planet, b) >= VOXEL_ISO
                || Vector3Distance(b, w->moon.pos) < w->moon.radius + k->radius;
        BossPart parts[BOSS_PARTS_MAX];
        int count = boss_parts(&w->boss, t, parts);
        for (int p = 0; p < count && !hit; p++) hit = segment_sphere(a, b, parts[p].center, parts[p].radius + k->radius) >= 0.0f;
        const Enemies *e = &w->enemies;
        for (int n = 0; n < e->pool.capacity && !hit; n++) {
            if (!pool_alive(&e->pool, n) || e->items[n].hp <= 0.0f) continue;
            hit = segment_sphere(a, b, e->items[n].pos, t->enemy[e->items[n].type].radius + k->radius) >= 0.0f;
        }
        if (hit) combat_spore_blast(w, t, i, true);
    }
}

// Inimigos que trombam com a nave.
static void resolve_rams(World *w, const Tuning *t)
{
    Ship *s = &w->ship;
    if (!s->alive || !w->ship_active) return;
    Enemies *e = &w->enemies;

    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        Enemy *en = &e->items[i];
        const EnemyStats *stats = &t->enemy[en->type];
        Vector3 offset = Vector3Subtract(s->pos, en->pos);
        float distance = Vector3Length(offset);
        float reach = stats->radius + t->ship_radius;
        if (distance >= reach || distance < 1e-4f) continue;

        Vector3 normal = Vector3Scale(offset, 1.0f / distance);
        Vector3 point = Vector3Add(en->pos, Vector3Scale(normal, stats->radius));
        if (en->type == ENEMY_KAMIKAZE) {
            enemies_damage(e, i, en->hp, DEATH_DETONATE);   // a explosão cuida do dano
            continue;
        }
        if (!is_heavy((EnemyType)en->type)) {
            // O inimigo leve se despedaça na nave (e conta como abate).
            enemies_damage(e, i, en->hp, DEATH_PLAYER);
            combat_damage_ship(w, t, stats->ram_damage, point);
            continue;
        }

        // Os pesados são um muro: a nave é empurrada para fora e quica.
        s->pos = Vector3Add(en->pos, Vector3Scale(normal, reach + 0.05f));
        Vector3 relative = Vector3Subtract(Vector3Add(s->vel, s->fall), en->vel);
        float into = -Vector3DotProduct(relative, normal);
        if (into > 0.0f) {
            s->vel = Vector3Add(s->vel, Vector3Scale(normal, into * (1.0f + t->ship_bounce)));
            if (into > RAM_MIN_SPEED) {
                combat_damage_ship(w, t, stats->ram_damage * Clamp(into / RAM_SPEED_FULL, 0.3f, 1.0f), point);
            }
        }
    }
}

void combat_resolve(World *w, const Tuning *t)
{
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        if (p->items[i].kind == SHOT_BOLT) resolve_bolt(w, t, i);
        else                               resolve_enemy_shot(w, t, i);
    }
    resolve_rams(w, t);
}

// ---------------------------------------------------------------------------
//  Mortes
// ---------------------------------------------------------------------------

void combat_sweep(World *w, const Tuning *t)
{
    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp > 0.0f) continue;
        Enemy *en = &e->items[i];
        EnemyType type = (EnemyType)en->type;
        const Blast *blast = &BLASTS[type];
        bool detonated = (en->death == DEATH_DETONATE);
        float size = t->enemy[type].scale * (detonated ? DETONATE_SIZE : 1.0f);
        particles_explosion(&w->particles, &w->rng, en->pos, en->vel, size, blast->hot, blast->glow, blast->debris);

        if (detonated) {
            // Kamikaze junto da nave: o dano cai com a distância.
            const Ship *s = &w->ship;
            float d = Vector3Distance(s->pos, en->pos);
            if (s->alive && d < t->kamikaze_blast_radius) {
                float falloff = Clamp(1.0f - d / t->kamikaze_blast_radius, DETONATE_MIN, 1.0f);
                combat_damage_ship(w, t, t->enemy[type].ram_damage * falloff, en->pos);
            }
            events_push(&w->events, (uint32_t)w->tick, EV_DETONATE, (uint8_t)type, en->pos, 0.0f);
        } else if (en->death == DEATH_PLAYER && playing(w)) {
            int gained = score_kill(&w->score, t, t->enemy[type].points);
            w->score.kills_by_type[type]++;
            w->kill_marker = KILL_MARKER_TIME;
            events_push(&w->events, (uint32_t)w->tick, EV_KILL, (uint8_t)type, en->pos, (float)gained);
            if (t->enemy[type].scale >= BIG_KILL_SCALE) {
                int ticks = seconds_to_ticks(t, t->combat_hitstop_big);
                if (ticks > w->hitstop) w->hitstop = ticks;
            }
            gain_xp(w, t, type, t->xp_kill[type], en->pos);

            // Roubo de vida: cada abate conserta um pouco o casco.
            const uint8_t *rank = w->evo.rank;
            if (rank[EVO_LIFESTEAL] > 0 && w->ship.alive) {
                w->ship.hull = fminf(w->ship.hull + t->lifesteal_hull * (float)rank[EVO_LIFESTEAL], t->ship_hull_max);
            }
            // Reação em cadeia: o inimigo abatido explode em quem está perto.
            if (rank[EVO_CHAIN] > 0) {
                ship_blast(w, t, en->pos, t->chain_radius, t->chain_damage * (float)rank[EVO_CHAIN], i, true);
            }
        } else {
            events_push(&w->events, (uint32_t)w->tick, EV_CRASH, (uint8_t)type, en->pos, 0.0f);
            // Caiu no planeta: abre uma cratera onde bateu.
            if (planet_sample(&w->planet, en->pos) >= VOXEL_ISO && en->state != DIGGER_DRILL) {
                Vector3 up = Vector3Normalize(en->pos);
                float radius, depth;
                enemy_crater(t, type, &radius, &depth);
                world_impact(w, t, Vector3Add(en->pos, Vector3Scale(up, 6.0f)), Vector3Negate(up), radius, depth);
            }
        }
        enemies_remove(e, i);
    }
}
