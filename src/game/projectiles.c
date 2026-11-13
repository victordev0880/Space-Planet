#include "game/projectiles.h"

#include <string.h>

bool projectiles_init(Projectiles *p, Arena *arena)
{
    memset(p, 0, sizeof *p);
    p->items = ARENA_ARRAY(arena, Shot, PROJECTILES_CAPACITY);
    return p->items != NULL && pool_init(&p->pool, PROJECTILES_CAPACITY, arena);
}

void projectiles_clear(Projectiles *p)
{
    pool_clear(&p->pool);
}

Shot *projectiles_spawn(Projectiles *p, ShotKind kind, Vector3 pos, Vector3 vel, float life)
{
    int index = pool_acquire(&p->pool);
    if (index < 0) return NULL;
    Shot *s = &p->items[index];
    memset(s, 0, sizeof *s);
    s->kind = (uint8_t)kind;
    s->pos = pos;
    s->pos_prev = pos;
    s->vel = vel;
    s->life = life;
    s->last_hit = -1;
    return s;
}

void projectiles_remove(Projectiles *p, int index)
{
    pool_release(&p->pool, index);
}

void projectiles_move(Projectiles *p, float dt)
{
    for (int i = 0; i < p->pool.capacity; i++) {
        if (!pool_alive(&p->pool, i)) continue;
        Shot *s = &p->items[i];
        s->age += dt;
        if (s->age >= s->life) {
            pool_release(&p->pool, i);
            continue;
        }
        if (s->kind == SHOT_BOMB) {
            // A bomba acelera em direção ao alvo, então cai exatamente onde
            // a marca no solo avisou.
            Vector3 to_target = Vector3Subtract(s->target, s->pos);
            float distance = Vector3Length(to_target);
            if (distance > 1e-3f) s->vel = Vector3Add(s->vel, Vector3Scale(to_target, s->accel * dt / distance));
        }
        s->pos_prev = s->pos;
        s->pos = Vector3Add(s->pos, Vector3Scale(s->vel, dt));
    }
}

int projectiles_count(const Projectiles *p, ShotKind kind)
{
    int count = 0;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (pool_alive(&p->pool, i) && p->items[i].kind == kind) count++;
    }
    return count;
}
