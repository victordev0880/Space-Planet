#include "game/repair.h"

#include <math.h>
#include <string.h>

#include "voxel/terraform.h"

#define SCAR_MERGE       0.8f    // impactos a menos de (raio + raio) vezes isto se juntam numa cicatriz
#define SCAR_MAX_RADIUS  34.0f   // uma cicatriz não cresce além disto
#define SCAR_PROBE_DEPTH 1.5f    // o chão voltou se há matéria este tanto abaixo da superfície original
#define SCAR_PROBE_SPAN  0.5f    // os pontos conferidos ficam a esta fração do raio, em volta do centro

void repair_reset(Repair *r)
{
    memset(r, 0, sizeof *r);
}

float repair_original_top(const Planet *p, Vector3 dir)
{
    Terrain t = planet_terrain(&p->params, dir);
    return p->params.radius + (t.land ? t.elevation : 0.0f);
}

Vector3 repair_scar_point(const Planet *p, const Scar *s)
{
    return Vector3Scale(s->dir, repair_original_top(p, s->dir));
}

int repair_active_jobs(const Repair *r)
{
    int n = 0;
    for (int i = 0; i < REPAIR_JOBS; i++) n += r->jobs[i].active ? 1 : 0;
    return n;
}

void repair_note_damage(Repair *r, Vector3 ground, float radius)
{
    float distance = Vector3Length(ground);
    if (distance < 1e-3f || !(radius > 0.0f)) return;
    Vector3 dir = Vector3Scale(ground, 1.0f / distance);

    // Junta com a cicatriz mais perto, se ela estiver perto o bastante.
    int best = -1;
    float best_gap = 0.0f;
    for (int i = 0; i < REPAIR_SCARS; i++) {
        const Scar *s = &r->scars[i];
        if (!s->used) continue;
        float gap = distance * acosf(Clamp(Vector3DotProduct(s->dir, dir), -1.0f, 1.0f));   // distância pela superfície
        if (gap > (s->radius + radius) * SCAR_MERGE) continue;
        if (best < 0 || gap < best_gap) {
            best = i;
            best_gap = gap;
        }
    }
    if (best >= 0) {
        Scar *s = &r->scars[best];
        float grown = fminf(fmaxf(s->radius, 0.5f * (best_gap + radius + s->radius)), SCAR_MAX_RADIUS);
        float pull = radius * radius / (radius * radius + s->radius * s->radius);   // o centro anda pelo peso
        s->dir = Vector3Normalize(Vector3Lerp(s->dir, dir, pull));
        s->radius = grown;
        return;
    }

    // Cicatriz nova; sem lugar, troca pela menor (se esta for maior).
    int slot = -1;
    for (int i = 0; i < REPAIR_SCARS; i++) {
        if (!r->scars[i].used) {
            slot = i;
            break;
        }
        if (slot < 0 || r->scars[i].radius < r->scars[slot].radius) slot = i;
    }
    if (r->scars[slot].used && r->scars[slot].radius >= radius) return;
    r->scars[slot] = (Scar){ true, dir, fminf(radius, SCAR_MAX_RADIUS) };
}

bool repair_start(Repair *r, const Planet *p, const Tuning *t, Vector3 at, float fill)
{
    float distance = Vector3Length(at);
    if (distance < 1e-3f || !(fill > 0.0f)) return false;
    for (int i = 0; i < REPAIR_JOBS; i++) {
        RepairJob *j = &r->jobs[i];
        if (j->active) continue;
        Vector3 dir = Vector3Scale(at, 1.0f / distance);
        float top = repair_original_top(p, dir);
        memset(j, 0, sizeof *j);
        j->active = true;
        j->ground = at;
        j->radius = fill;
        j->center = Vector3Scale(dir, top - fill * t->repair_depth);
        return true;
    }
    return false;
}

// A cicatriz já foi consertada? Confere o centro e quatro pontos em volta,
// logo abaixo de onde era a superfície.
static bool scar_healed(const Planet *p, const Scar *s)
{
    Vector3 side = (fabsf(s->dir.y) > 0.9f) ? (Vector3){ 1.0f, 0.0f, 0.0f } : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 a = Vector3Normalize(Vector3CrossProduct(s->dir, side));
    Vector3 b = Vector3CrossProduct(s->dir, a);
    float top = repair_original_top(p, s->dir);
    float span = s->radius * SCAR_PROBE_SPAN / fmaxf(top, 1.0f);   // em radianos
    static const float OFFSETS[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int k = 0; k < 5; k++) {
        Vector3 dir = Vector3Normalize(Vector3Add(s->dir, Vector3Add(Vector3Scale(a, OFFSETS[k][0] * span),
                                                                     Vector3Scale(b, OFFSETS[k][1] * span))));
        Vector3 probe = Vector3Scale(dir, repair_original_top(p, dir) - SCAR_PROBE_DEPTH);
        if (planet_sample(p, probe) < VOXEL_ISO) return false;
    }
    return true;
}

void repair_tick(Repair *r, Planet *p, const Tuning *t, float dt)
{
    r->finished_count = 0;
    for (int i = 0; i < REPAIR_JOBS; i++) {
        RepairJob *j = &r->jobs[i];
        if (!j->active) continue;
        j->age += dt;
        int due = (int)(j->age / fmaxf(t->repair_time, 1e-3f) * (float)REPAIR_STEPS);
        if (due > REPAIR_STEPS) due = REPAIR_STEPS;
        while (j->step < due) {
            j->step++;
            RestoreResult res = planet_restore(p, j->center, j->radius * (float)j->step / (float)REPAIR_STEPS);
            j->restored += res.restored;
        }
        if (j->step >= REPAIR_STEPS) {
            r->restored_total += j->restored;
            r->finished[r->finished_count++] = *j;
            j->active = false;
        }
    }

    // Uma cicatriz por passo.
    r->cursor = (r->cursor + 1) % REPAIR_SCARS;
    Scar *s = &r->scars[r->cursor];
    if (s->used && scar_healed(p, s)) s->used = false;
}
