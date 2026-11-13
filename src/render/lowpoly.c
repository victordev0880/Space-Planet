#include "raylib.h"
#include "rlgl.h"

#include "render/lowpoly.h"

#include <math.h>
#include <string.h>

#define RIM_POWER      3.0f    // quão colada na silhueta fica a luz de borda
#define BOUNCE_REACH   0.35f   // o planeta reflete luz mesmo um pouco além do terminador

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

void lowpoly_draw(const LowPoly *m, const LowPolyPose *pose)
{
    // O planeta fica no centro do mundo: a luz refletida vem de lá, e só se
    // o lado do planeta embaixo da nave estiver iluminado pelo Sol.
    Vector3 to_planet = Vector3Normalize(Vector3Negate(pose->pos));
    float planet_day = Clamp(Vector3DotProduct(Vector3Negate(to_planet), pose->sun) + BOUNCE_REACH, 0.0f, 1.0f);
    float tint[3] = { pose->tint.r / 255.0f, pose->tint.g / 255.0f, pose->tint.b / 255.0f };

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < m->count; i++) {
        const LowPolyTri *tri = &m->tris[i];
        Vector3 a = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->a, pose->scale), pose->rot));
        Vector3 b = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->b, pose->scale), pose->rot));
        Vector3 c = Vector3Add(pose->pos, Vector3RotateByQuaternion(Vector3Scale(tri->c, pose->scale), pose->rot));
        Color base = m->colors[tri->part];
        float rgb[3];

        if (m->emissive[tri->part]) {
            Color e = (m->glow_slot[tri->part] && pose->glow.a > 0) ? pose->glow : base;
            rgb[0] = e.r;
            rgb[1] = e.g;
            rgb[2] = e.b;
        } else {
            // A normal virada para a câmera: é o lado da face que aparece.
            Vector3 center = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
            Vector3 view = Vector3Normalize(Vector3Subtract(pose->eye, center));
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
            if (Vector3DotProduct(normal, view) < 0.0f) normal = Vector3Negate(normal);

            float facing = Vector3DotProduct(normal, pose->sun);
            float lit = Clamp((facing + pose->wrap) / (1.0f + pose->wrap), 0.0f, 1.0f) * pose->sunlight;
            float light = pose->ambient + (1.0f - pose->ambient) * lit;

            float spec = 0.0f;
            if (pose->specular > 0.0f && facing > 0.0f) {
                Vector3 half = Vector3Normalize(Vector3Add(pose->sun, view));
                spec = powf(fmaxf(Vector3DotProduct(normal, half), 0.0f), pose->shininess) * pose->specular
                     * pose->sunlight * 255.0f;
            }
            float rim = powf(1.0f - fmaxf(Vector3DotProduct(normal, view), 0.0f), RIM_POWER) * pose->rim;
            float bounce = fmaxf(Vector3DotProduct(normal, to_planet), 0.0f) * pose->bounce * planet_day;

            const unsigned char channel_base[3] = { base.r, base.g, base.b };
            const unsigned char rim_c[3] = { pose->rim_color.r, pose->rim_color.g, pose->rim_color.b };
            const unsigned char bounce_c[3] = { pose->bounce_color.r, pose->bounce_color.g, pose->bounce_color.b };
            for (int k = 0; k < 3; k++) {
                float albedo = channel_base[k] * tint[k];
                rgb[k] = albedo * light + albedo * bounce * (bounce_c[k] / 255.0f) + spec + rim_c[k] * rim;
            }
        }

        const unsigned char flash[3] = { pose->flash.r, pose->flash.g, pose->flash.b };
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
