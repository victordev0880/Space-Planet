#include "raylib.h"
#include "rlgl.h"

#include "render/lowpoly.h"

#include <math.h>
#include <string.h>

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

void lowpoly_tri(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, int part, bool mirror)
{
    int needed = mirror ? 2 : 1;
    if (m->count + needed > LOWPOLY_MAX_TRIS) return;
    m->tris[m->count++] = (LowPolyTri){ a, b, c, (uint8_t)part };
    if (mirror) {
        a.x = -a.x;
        b.x = -b.x;
        c.x = -c.x;
        m->tris[m->count++] = (LowPolyTri){ a, b, c, (uint8_t)part };
    }
}

void lowpoly_quad(LowPoly *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, int part, bool mirror)
{
    lowpoly_tri(m, a, b, c, part, mirror);
    lowpoly_tri(m, a, c, d, part, mirror);
}

float lowpoly_sunlight(Vector3 pos, Vector3 sun, float planet_radius, float shadow)
{
    float along = Vector3DotProduct(pos, sun);
    float off_axis = Vector3Length(Vector3Subtract(pos, Vector3Scale(sun, along)));
    return (along < 0.0f && off_axis < planet_radius) ? shadow : 1.0f;
}

static unsigned char channel(float base, float light, float whiten, float flash)
{
    return (unsigned char)Clamp(Lerp(base * light, flash, whiten), 0.0f, 255.0f);
}

void lowpoly_draw(const LowPoly *m, const LowPolyPose *pose)
{
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < m->count; i++) {
        const LowPolyTri *tri = &m->tris[i];
        Vector3 a = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->a, pose->scale), pose->rot));
        Vector3 b = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->b, pose->scale), pose->rot));
        Vector3 c = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->c, pose->scale), pose->rot));

        float light = 1.0f;
        if (!m->emissive[tri->part]) {
            // A normal virada para a câmera: é o lado da face que aparece.
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
            if (Vector3DotProduct(normal, Vector3Subtract(pose->eye, a)) < 0.0f) normal = Vector3Negate(normal);
            float facing = Vector3DotProduct(normal, pose->sun);
            float lit = Clamp((facing + pose->wrap) / (1.0f + pose->wrap), 0.0f, 1.0f);
            light = pose->ambient + (1.0f - pose->ambient) * lit * pose->sunlight;
        }

        Color base = m->colors[tri->part];
        rlColor4ub(channel(base.r, light, pose->whiten, pose->flash.r), channel(base.g, light, pose->whiten, pose->flash.g),
                   channel(base.b, light, pose->whiten, pose->flash.b), 255);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c.x, c.y, c.z);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
