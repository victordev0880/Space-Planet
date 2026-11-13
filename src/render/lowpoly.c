#include "raylib.h"
#include "rlgl.h"

#include "render/lowpoly.h"

#include <math.h>
#include <string.h>

#define BOUNCE_REACH   0.35f   // o planeta reflete luz mesmo um pouco além do terminador
#define MAX_SIDES      24

void lowpoly_clear(LowPoly *m)
{
    memset(m, 0, sizeof *m);
}

void lowpoly_part(LowPoly *m, int part, Color color, bool emissive)
{
    if (part < 0 || part >= LOWPOLY_MAX_PARTS) return;
    m->colors[part] = color;
    m->emissive[part] = emissive;
}

void lowpoly_part_glow(LowPoly *m, int part, Color color)
{
    if (part < 0 || part >= LOWPOLY_MAX_PARTS) return;
    m->colors[part] = color;
    m->emissive[part] = true;
    m->glow_slot[part] = true;
}

void lowpoly_detail(LowPoly *m, bool on)
{
    m->detail_mode = on ? 1u : 0u;
}

void lowpoly_tri(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, int part, bool mirror)
{
    // Triângulos sem área (pontas que coincidem) não desenham nada.
    if (Vector3LengthSqr(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a))) < 1e-12f) return;
    int needed = mirror ? 2 : 1;
    if (m->count + needed > LOWPOLY_MAX_TRIS) {
        m->overflow = true;
        return;
    }
    m->tris[m->count++] = (LowPolyTri){ a, b, c, (uint8_t)part, m->detail_mode };
    if (mirror) {
        a.x = -a.x;
        b.x = -b.x;
        c.x = -c.x;
        m->tris[m->count++] = (LowPolyTri){ a, b, c, (uint8_t)part, m->detail_mode };
    }
}

void lowpoly_quad(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, int part, bool mirror)
{
    lowpoly_tri(m, a, b, c, part, mirror);
    lowpoly_tri(m, a, c, d, part, mirror);
}

// ---------------------------------------------------------------------------
//  Formas
// ---------------------------------------------------------------------------

// Dois eixos perpendiculares a `axis` (comprimento 1).
static void basis(Vector3 axis, Vector3 *u, Vector3 *v)
{
    Vector3 hint = (fabsf(axis.y) < 0.9f) ? (Vector3){ 0.0f, 1.0f, 0.0f } : (Vector3){ 1.0f, 0.0f, 0.0f };
    *u = Vector3Normalize(Vector3CrossProduct(axis, hint));
    *v = Vector3CrossProduct(axis, *u);
}

static Vector3 ring_point(const LpRing *r, float x, float angle)
{
    return (Vector3){ x + cosf(angle) * r->w, r->y + sinf(angle) * r->h, r->z };
}

void lp_loft(LowPoly *m, const LpRing *rings, int count, int sides, float x, int top, int bottom, int cap,
             float twist, bool mirror)
{
    if (count < 2 || sides < 3 || sides > MAX_SIDES) return;
    // Começa meio passo deslocado: assim há uma face reta em cima e outra embaixo.
    float phase = PI / (float)sides;
    for (int i = 0; i + 1 < count; i++) {
        const LpRing *r0 = &rings[i], *r1 = &rings[i + 1];
        float t0 = twist * (float)i, t1 = twist * (float)(i + 1);
        for (int k = 0; k < sides; k++) {
            float a0 = phase + 2.0f * PI * (float)k / (float)sides;
            float a1 = phase + 2.0f * PI * (float)(k + 1) / (float)sides;
            Vector3 p00 = ring_point(r0, x, a0 + t0), p01 = ring_point(r0, x, a1 + t0);
            Vector3 p10 = ring_point(r1, x, a0 + t1), p11 = ring_point(r1, x, a1 + t1);
            int part = (sinf((a0 + a1) * 0.5f) >= -0.05f) ? top : bottom;
            lowpoly_tri(m, p00, p10, p11, part, mirror);
            lowpoly_tri(m, p00, p11, p01, part, mirror);
        }
    }
    if (cap < 0) return;
    const LpRing *ends[2] = { &rings[0], &rings[count - 1] };
    for (int e = 0; e < 2; e++) {
        const LpRing *r = ends[e];
        if (r->w < 1e-4f && r->h < 1e-4f) continue;
        float t = twist * (float)(e == 0 ? 0 : count - 1);
        Vector3 center = { x, r->y, r->z };
        for (int k = 0; k < sides; k++) {
            float a0 = phase + 2.0f * PI * (float)k / (float)sides + t;
            float a1 = phase + 2.0f * PI * (float)(k + 1) / (float)sides + t;
            lowpoly_tri(m, center, ring_point(r, x, a0), ring_point(r, x, a1), cap, mirror);
        }
    }
}

void lp_plate(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 normal, float thick_root,
              float thick_tip, int top, int bottom, int edge, bool mirror)
{
    Vector3 n = Vector3Normalize(normal);
    Vector3 nr = Vector3Scale(n, thick_root * 0.5f), nt = Vector3Scale(n, thick_tip * 0.5f);
    Vector3 at = Vector3Add(a, nr), bt = Vector3Add(b, nt), ct = Vector3Add(c, nt), dt = Vector3Add(d, nr);
    Vector3 ab = Vector3Subtract(a, nr), bb = Vector3Subtract(b, nt), cb = Vector3Subtract(c, nt), db = Vector3Subtract(d, nr);
    lowpoly_quad(m, at, bt, ct, dt, top, mirror);
    lowpoly_quad(m, ab, db, cb, bb, bottom, mirror);
    if (thick_root <= 1e-4f && thick_tip <= 1e-4f) return;
    lowpoly_quad(m, at, ab, bb, bt, edge, mirror);   // bordo de ataque
    lowpoly_quad(m, bt, bb, cb, ct, edge, mirror);   // ponta
    lowpoly_quad(m, ct, cb, db, dt, edge, mirror);   // bordo de fuga
}

void lp_tube(LowPoly *m, Vector3 a, Vector3 b, float ra, float rb, int sides, int part, int cap, bool mirror)
{
    if (sides < 3 || sides > MAX_SIDES) return;
    Vector3 axis = Vector3Subtract(b, a);
    if (Vector3LengthSqr(axis) < 1e-10f) return;
    axis = Vector3Normalize(axis);
    Vector3 u, v;
    basis(axis, &u, &v);
    for (int k = 0; k < sides; k++) {
        float a0 = 2.0f * PI * (float)k / (float)sides, a1 = 2.0f * PI * (float)(k + 1) / (float)sides;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(a0)), Vector3Scale(v, sinf(a0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(a1)), Vector3Scale(v, sinf(a1)));
        Vector3 pa0 = Vector3Add(a, Vector3Scale(d0, ra)), pa1 = Vector3Add(a, Vector3Scale(d1, ra));
        Vector3 pb0 = Vector3Add(b, Vector3Scale(d0, rb)), pb1 = Vector3Add(b, Vector3Scale(d1, rb));
        lowpoly_tri(m, pa0, pb0, pb1, part, mirror);
        lowpoly_tri(m, pa0, pb1, pa1, part, mirror);
        if (cap >= 0) {
            lowpoly_tri(m, a, pa1, pa0, cap, mirror);
            lowpoly_tri(m, b, pb0, pb1, cap, mirror);
        }
    }
}

void lp_box(LowPoly *m, Vector3 c, Vector3 h, int part, bool mirror)
{
    Vector3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = (Vector3){ c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z) };
    }
    static const int FACES[6][4] = {
        { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 },
    };
    for (int f = 0; f < 6; f++) {
        lowpoly_quad(m, p[FACES[f][0]], p[FACES[f][1]], p[FACES[f][2]], p[FACES[f][3]], part, mirror);
    }
}

void lp_spike(LowPoly *m, Vector3 base, Vector3 tip, float radius, int sides, int part, bool mirror)
{
    lp_tube(m, base, tip, radius, 0.0f, sides, part, -1, mirror);
}

void lp_ring(LowPoly *m, Vector3 center, Vector3 axis, float r_in, float r_out, float depth, int sides, int face,
             int edge, bool mirror)
{
    if (sides < 3 || sides > MAX_SIDES) return;
    axis = Vector3Normalize(axis);
    Vector3 u, v;
    basis(axis, &u, &v);
    Vector3 f = Vector3Scale(axis, -depth * 0.5f), b = Vector3Scale(axis, depth * 0.5f);
    for (int k = 0; k < sides; k++) {
        float a0 = 2.0f * PI * (float)k / (float)sides, a1 = 2.0f * PI * (float)(k + 1) / (float)sides;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(a0)), Vector3Scale(v, sinf(a0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(a1)), Vector3Scale(v, sinf(a1)));
        Vector3 o0 = Vector3Add(center, Vector3Scale(d0, r_out)), o1 = Vector3Add(center, Vector3Scale(d1, r_out));
        Vector3 i0 = Vector3Add(center, Vector3Scale(d0, r_in)), i1 = Vector3Add(center, Vector3Scale(d1, r_in));
        lowpoly_quad(m, Vector3Add(o0, f), Vector3Add(i0, f), Vector3Add(i1, f), Vector3Add(o1, f), face, mirror);
        lowpoly_quad(m, Vector3Add(o0, b), Vector3Add(o1, b), Vector3Add(i1, b), Vector3Add(i0, b), face, mirror);
        if (depth > 1e-4f) {
            lowpoly_quad(m, Vector3Add(o0, f), Vector3Add(o1, f), Vector3Add(o1, b), Vector3Add(o0, b), edge, mirror);
            lowpoly_quad(m, Vector3Add(i0, f), Vector3Add(i0, b), Vector3Add(i1, b), Vector3Add(i1, f), edge, mirror);
        }
    }
}

void lp_transform(LowPoly *m, int first, Quaternion rot, Vector3 offset)
{
    for (int i = (first < 0 ? 0 : first); i < m->count; i++) {
        LowPolyTri *t = &m->tris[i];
        t->a = Vector3Add(Vector3RotateByQuaternion(t->a, rot), offset);
        t->b = Vector3Add(Vector3RotateByQuaternion(t->b, rot), offset);
        t->c = Vector3Add(Vector3RotateByQuaternion(t->c, rot), offset);
    }
}

// ---------------------------------------------------------------------------
//  Desenho
// ---------------------------------------------------------------------------

float lowpoly_sunlight(Vector3 pos, Vector3 sun, float planet_radius, float shadow)
{
    float along = Vector3DotProduct(pos, sun);
    float off_axis = Vector3Length(Vector3Subtract(pos, Vector3Scale(sun, along)));
    return (along < 0.0f && off_axis < planet_radius) ? shadow : 1.0f;
}

LowPolyPose lowpoly_pose(Vector3 pos, Quaternion rot, float scale, Vector3 sun, Vector3 eye)
{
    LowPolyPose p;
    memset(&p, 0, sizeof p);
    p.pos = pos;
    p.rot = rot;
    p.scale = scale;
    p.sun = sun;
    p.eye = eye;
    p.sunlight = 1.0f;
    p.ambient = 0.4f;
    p.shininess = 16.0f;
    p.flash = WHITE;
    p.tint = WHITE;
    p.rim_color = WHITE;
    p.bounce_color = WHITE;
    return p;
}

// Rotação e escala de um vértice, com a matriz montada uma vez por modelo.
typedef struct { float m[9]; Vector3 pos; } Xform;

static Xform make_xform(const LowPolyPose *pose)
{
    Xform x;
    Quaternion q = pose->rot;
    float s = pose->scale;
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    x.m[0] = (1.0f - 2.0f * (yy + zz)) * s; x.m[1] = 2.0f * (xy - wz) * s;          x.m[2] = 2.0f * (xz + wy) * s;
    x.m[3] = 2.0f * (xy + wz) * s;          x.m[4] = (1.0f - 2.0f * (xx + zz)) * s; x.m[5] = 2.0f * (yz - wx) * s;
    x.m[6] = 2.0f * (xz - wy) * s;          x.m[7] = 2.0f * (yz + wx) * s;          x.m[8] = (1.0f - 2.0f * (xx + yy)) * s;
    x.pos = pose->pos;
    return x;
}

static inline Vector3 apply(const Xform *x, Vector3 v)
{
    return (Vector3){
        x->pos.x + x->m[0] * v.x + x->m[1] * v.y + x->m[2] * v.z,
        x->pos.y + x->m[3] * v.x + x->m[4] * v.y + x->m[5] * v.z,
        x->pos.z + x->m[6] * v.x + x->m[7] * v.y + x->m[8] * v.z,
    };
}

void lowpoly_draw(const LowPoly *m, const LowPolyPose *pose)
{
    // O planeta fica no centro do mundo: a luz refletida vem de lá, e só se
    // o lado do planeta embaixo da nave estiver iluminado pelo Sol.
    Vector3 to_planet = Vector3Normalize(Vector3Negate(pose->pos));
    float planet_day = Clamp(Vector3DotProduct(Vector3Negate(to_planet), pose->sun) + BOUNCE_REACH, 0.0f, 1.0f);
    float tint[3] = { pose->tint.r / 255.0f, pose->tint.g / 255.0f, pose->tint.b / 255.0f };
    const float flash[3] = { pose->flash.r, pose->flash.g, pose->flash.b };
    const float rim_c[3] = { pose->rim_color.r, pose->rim_color.g, pose->rim_color.b };
    const float bounce_c[3] = { pose->bounce_color.r / 255.0f, pose->bounce_color.g / 255.0f,
                                pose->bounce_color.b / 255.0f };
    // A câmera está longe comparada ao tamanho do modelo: a direção até ela
    // é a mesma para todas as faces (economiza uma raiz quadrada por face).
    Vector3 view = Vector3Normalize(Vector3Subtract(pose->eye, pose->pos));
    Vector3 half = Vector3Normalize(Vector3Add(pose->sun, view));
    Xform x = make_xform(pose);

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < m->count; i++) {
        const LowPolyTri *tri = &m->tris[i];
        if (tri->detail && pose->far) continue;
        Vector3 a = apply(&x, tri->a), b = apply(&x, tri->b), c = apply(&x, tri->c);
        Color base = m->colors[tri->part];
        float rgb[3];

        if (m->emissive[tri->part]) {
            Color e = (m->glow_slot[tri->part] && pose->glow.a > 0) ? pose->glow : base;
            rgb[0] = e.r;
            rgb[1] = e.g;
            rgb[2] = e.b;
        } else {
            // A normal virada para a câmera: é o lado da face que aparece.
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
            float toward = Vector3DotProduct(normal, view);
            if (toward < 0.0f) {
                normal = Vector3Negate(normal);
                toward = -toward;
            }

            float facing = Vector3DotProduct(normal, pose->sun);
            float lit = Clamp((facing + pose->wrap) / (1.0f + pose->wrap), 0.0f, 1.0f) * pose->sunlight;
            float light = pose->ambient + (1.0f - pose->ambient) * lit;

            float spec = 0.0f;
            if (pose->specular > 0.0f && facing > 0.0f) {
                spec = powf(fmaxf(Vector3DotProduct(normal, half), 0.0f), pose->shininess) * pose->specular
                     * pose->sunlight * 255.0f;
            }
            float edge = 1.0f - toward;
            float rim = edge * edge * edge * pose->rim;
            float bounce = fmaxf(Vector3DotProduct(normal, to_planet), 0.0f) * pose->bounce * planet_day;

            const float channel_base[3] = { base.r, base.g, base.b };
            for (int k = 0; k < 3; k++) {
                float albedo = channel_base[k] * tint[k];
                rgb[k] = albedo * (light + bounce * bounce_c[k]) + spec + rim_c[k] * rim;
            }
        }

        unsigned char out[3];
        for (int k = 0; k < 3; k++) out[k] = (unsigned char)Clamp(Lerp(rgb[k], flash[k], pose->whiten), 0.0f, 255.0f);
        rlColor4ub(out[0], out[1], out[2], 255);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c.x, c.y, c.z);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
