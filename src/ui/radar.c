#include "raylib.h"

#include "ui/radar.h"

#include <math.h>

#include "core/vmath.h"
#include "ui/hud.h"

#define IMPACT_RINGS  8       // anéis de impacto lembrados ao mesmo tempo
#define IMPACT_TIME   1.4f    // segundos que um anel de impacto leva para sumir

static const Color BACK_COLOR   = { 3, 8, 16, 235 };
static const Color CENTER_COLOR = { 16, 34, 52, 235 };   // o centro, mais claro: o chão embaixo da nave
static const Color RIM_COLOR    = { 110, 150, 190, 150 };
static const Color GRID_COLOR   = { 110, 150, 190, 45 };
static const Color SHIP_COLOR   = { 240, 244, 255, 255 };
static const Color SCAR_COLOR   = { 170, 120, 80, 200 };
static const Color DEFENSE_COLOR = { 120, 220, 255, 230 };
static const Color METEOR_COLOR = { 255, 150, 70, 255 };
static const Color ROCK_COLOR   = { 190, 180, 168, 200 };
static const Color BOMB_COLOR   = { 255, 70, 50, 255 };
static const Color BOSS_COLOR   = { 255, 84, 60, 255 };
static const Color IMPACT_COLOR = { 255, 120, 60, 255 };

// O referencial da nave: "cima" sai do planeta, "frente" é o nariz deitado no chão.
typedef struct {
    Vector3 up, forward, right;
    Vector2 center;
    float   radius;
} Frame;

// Anéis dos impactos recentes (estado só do desenho).
static struct {
    Vector3 pos;
    double  time;
    float   size;
} rings[IMPACT_RINGS];
static int      ring_next = 0;
static uint32_t impacts_seen = 0;

static Color fade(Color c, float alpha)
{
    c.a = (unsigned char)(Clamp(alpha, 0.0f, 1.0f) * (float)c.a);
    return c;
}

// Ponto do mundo -> ponto do disco. Só a direção (a partir do centro do
// planeta) importa: a altitude não aparece.
static Vector2 to_disc(const Frame *f, Vector3 p)
{
    float length = Vector3Length(p);
    if (length < 1e-4f) return f->center;
    Vector3 d = Vector3Scale(p, 1.0f / length);
    float c = Clamp(Vector3DotProduct(d, f->up), -1.0f, 1.0f);
    float angle = acosf(c);                          // 0 embaixo da nave, PI do outro lado
    Vector3 side = Vector3Subtract(d, Vector3Scale(f->up, c));
    float side_len = Vector3Length(side);
    float x = 0.0f, y = 0.0f;
    if (side_len > 1e-5f) {
        x = Vector3DotProduct(side, f->right) / side_len;
        y = Vector3DotProduct(side, f->forward) / side_len;
    }
    float r = angle / PI * f->radius;
    return (Vector2){ f->center.x + x * r, f->center.y - y * r };
}

static void dot(Vector2 p, float size, Color c)
{
    DrawCircleV(p, size, c);
}

static void diamond(Vector2 p, float size, Color c)
{
    DrawTriangle((Vector2){ p.x, p.y - size }, (Vector2){ p.x - size, p.y }, (Vector2){ p.x + size, p.y }, c);
    DrawTriangle((Vector2){ p.x - size, p.y }, (Vector2){ p.x, p.y + size }, (Vector2){ p.x + size, p.y }, c);
}

// Inimigos que estão ferindo o planeta agora (ou prestes a ferir).
static bool hurting_planet(const Enemy *e)
{
    switch ((EnemyState)e->state) {
    case DIGGER_DRILL:
    case SIPHON_DRAIN:
    case SWARM_DIVE:
    case KAMIKAZE_DIVE:
        return true;
    default:
        return e->type == ENEMY_BOMBER && e->arming > 0.0f;
    }
}

static void note_impacts(const World *w)
{
    if (w->impact_count < impacts_seen) impacts_seen = w->impact_count;   // partida nova
    for (uint32_t id = impacts_seen + 1; id <= w->impact_count; id++) {
        const Impact *impact = world_find_impact(w, id);
        if (impact == NULL || impact->quiet) continue;
        rings[ring_next].pos = impact->pos;
        rings[ring_next].time = GetTime();
        rings[ring_next].size = impact->radius;
        ring_next = (ring_next + 1) % IMPACT_RINGS;
    }
    impacts_seen = w->impact_count;
}

void radar_draw(const World *w, const Tuning *t, Vector2 center, float radius)
{
    note_impacts(w);
    if (!w->ship_active) return;

    Frame f;
    f.center = center;
    f.radius = radius;
    f.up = Vector3Normalize(w->ship.pos);
    Vector3 nose = ship_forward(&w->ship);
    f.forward = Vector3Subtract(nose, Vector3Scale(f.up, Vector3DotProduct(nose, f.up)));
    if (Vector3LengthSqr(f.forward) < 1e-6f) f.forward = quat_up(w->ship.rot);   // nariz apontando para o chão
    f.forward = Vector3Normalize(f.forward);
    f.right = Vector3CrossProduct(f.forward, f.up);

    double now = GetTime();
    float pulse = 0.5f + 0.5f * sinf((float)now * 10.0f);

    // Fundo: o disco, o anel do horizonte (o que está a um quarto de volta) e a borda.
    DrawCircleV(center, radius + 7.0f, (Color){ 0, 0, 0, 120 });
    DrawCircleGradient((int)center.x, (int)center.y, radius + 5.0f, CENTER_COLOR, BACK_COLOR);
    DrawCircleLinesV(center, radius * 0.5f, GRID_COLOR);
    DrawLineV((Vector2){ center.x - radius, center.y }, (Vector2){ center.x + radius, center.y }, GRID_COLOR);
    DrawLineV((Vector2){ center.x, center.y - radius }, (Vector2){ center.x, center.y + radius }, GRID_COLOR);
    float hp = Clamp((planet_hp(&w->planet) - t->defeat_planet_hp) / (1.0f - t->defeat_planet_hp), 0.0f, 1.0f);
    Color rim = (hp > 0.5f) ? RIM_COLOR : (Color){ 255, (unsigned char)(80 + 120 * hp), 70, 200 };
    DrawCircleLinesV(center, radius + 5.0f, rim);

    // Feridas do planeta (onde levar rochas) e defesas.
    for (int i = 0; i < REPAIR_SCARS; i++) {
        const Scar *s = &w->repair.scars[i];
        if (!s->used) continue;
        dot(to_disc(&f, s->dir), Clamp(s->radius * 0.15f, 2.0f, 5.0f), SCAR_COLOR);
    }
    for (int i = 0; i < DEFENSES_MAX; i++) {
        const Defense *d = &w->defenses.items[i];
        if (!d->alive) continue;
        Vector2 p = to_disc(&f, d->pos);
        if (d->type == DEF_SHIELD) DrawCircleLinesV(p, 5.0f, DEFENSE_COLOR);
        else                       DrawRectangle((int)p.x - 2, (int)p.y - 2, 4, 4, DEFENSE_COLOR);
    }

    // Rochas: meteoros em laranja (vale a pena ir buscar), fragmentos em cinza.
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *r = &w->rocks.items[i];
        if (!r->alive || r->state == ROCK_TOWED) continue;
        Vector2 p = to_disc(&f, r->pos);
        if (r->kind == ROCK_METEOR) dot(p, 3.5f, METEOR_COLOR);
        else                        dot(p, 2.2f, ROCK_COLOR);
    }

    // Anéis dos impactos recentes: alguma coisa está batendo no planeta ali.
    for (int i = 0; i < IMPACT_RINGS; i++) {
        float age = (float)(now - rings[i].time);
        if (rings[i].time <= 0.0 || age > IMPACT_TIME) continue;
        float k = age / IMPACT_TIME;
        float size = 3.0f + (6.0f + rings[i].size * 0.6f) * k;
        DrawCircleLinesV(to_disc(&f, rings[i].pos), size, fade(IMPACT_COLOR, 1.0f - k));
    }

    // Bombas caindo.
    const Projectiles *shots = &w->shots;
    for (int i = 0; i < shots->pool.capacity; i++) {
        if (!pool_alive(&shots->pool, i) || shots->items[i].kind != SHOT_BOMB) continue;
        dot(to_disc(&f, shots->items[i].pos), 2.5f, fade(BOMB_COLOR, 0.5f + 0.5f * pulse));
    }

    // Inimigos: os grandes maiores; quem está ferindo o planeta pisca com um anel.
    const Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i) || e->items[i].hp <= 0.0f) continue;
        const Enemy *en = &e->items[i];
        EnemyType type = (EnemyType)en->type;
        Vector2 p = to_disc(&f, en->pos);
        bool big = (type == ENEMY_BOMBER || type == ENEMY_CARRIER);
        Color c = hud_enemy_color(type);
        if (hurting_planet(en)) DrawCircleLinesV(p, 6.0f + 3.0f * pulse, fade(c, 0.4f + 0.6f * pulse));
        if (type == ENEMY_SHIELDER) DrawCircleLinesV(p, 3.5f, c);
        else                        dot(p, big ? 4.2f : 2.8f, c);
    }

    if (boss_active(&w->boss)) diamond(to_disc(&f, w->boss.pos), 5.0f + 1.5f * pulse, BOSS_COLOR);

    // A nave, no centro, apontando para a frente (para cima).
    DrawTriangle((Vector2){ center.x, center.y - 6.0f }, (Vector2){ center.x - 4.0f, center.y + 4.0f },
                 (Vector2){ center.x + 4.0f, center.y + 4.0f }, SHIP_COLOR);
}
