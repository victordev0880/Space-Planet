#include "voxel/damage.h"

#include <math.h>
#include <string.h>

#define RAYCAST_STEP      0.4f   // passo do raio, em voxels
#define RAYCAST_REFINEMENTS 6     // divisões ao meio para afinar o ponto de impacto

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

CarveResult planet_carve(Planet *p, Vector3 center, float radius)
{
    CarveResult result;
    memset(&result, 0, sizeof result);
    if (!(radius > 0.0f)) return result;

    // Além desta distância do centro a esfera não altera nada.
    const float band = VOXEL_ISO / VOXEL_DENSITY_PER_UNIT;
    const float reach = radius + band;

    // Caixa de pontos da grade que podem ser afetados.
    const float gx = center.x + p->center, gy = center.y + p->center, gz = center.z + p->center;
    const int last = p->size - 1;
    if (gx + reach < 0.0f || gy + reach < 0.0f || gz + reach < 0.0f) return result;
    if (gx - reach > (float)last || gy - reach > (float)last || gz - reach > (float)last) return result;

    int x0 = clamp_int((int)floorf(gx - reach), 0, last), x1 = clamp_int((int)ceilf(gx + reach), 0, last);
    int y0 = clamp_int((int)floorf(gy - reach), 0, last), y1 = clamp_int((int)ceilf(gy + reach), 0, last);
    int z0 = clamp_int((int)floorf(gz - reach), 0, last), z1 = clamp_int((int)ceilf(gz + reach), 0, last);

    // Faixa dos pontos realmente alterados, para marcar os chunks certos.
    int min_x = p->size, min_y = p->size, min_z = p->size;
    int max_x = -1, max_y = -1, max_z = -1;

    for (int z = z0; z <= z1; z++) {
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float dx = (float)x - gx, dy = (float)y - gy, dz = (float)z - gz;
                float distance = sqrtf(dx * dx + dy * dy + dz * dz);
                if (distance >= reach) continue;

                // Densidade máxima permitida aqui: zero bem dentro da esfera,
                // VOXEL_ISO exatamente na superfície dela, subindo para fora.
                float limit = VOXEL_ISO + (distance - radius) * VOXEL_DENSITY_PER_UNIT;
                int allowed = (int)(Clamp(limit, 0.0f, 255.0f) + 0.5f);

                int index = planet_index(p, x, y, z);
                int old = p->density[index];
                if (old <= allowed) continue;

                int material = p->material[index];
                int64_t removed = old - allowed;
                p->density[index] = (uint8_t)allowed;
                if (allowed == 0) p->material[index] = (uint8_t)VMAT_NONE;

                result.removed += removed;
                if (material < VMAT_COUNT) result.removed_by_material[material] += removed;
                if (material == VMAT_CORE) result.removed_core += removed;

                if (x < min_x) min_x = x;
                if (y < min_y) min_y = y;
                if (z < min_z) min_z = z;
                if (x > max_x) max_x = x;
                if (y > max_y) max_y = y;
                if (z > max_z) max_z = z;
            }
        }
    }

    if (result.removed == 0) return result;

    p->volume -= result.removed;
    p->core_volume -= result.removed_core;
    result.chunks_touched = planet_touch(p, min_x, min_y, min_z, max_x, max_y, max_z);
    return result;
}

Vector3 crater_sphere(Vector3 ground, Vector3 normal, float rim_radius, float depth, float *sphere_radius)
{
    if (rim_radius < 0.01f) rim_radius = 0.01f;
    if (depth < 0.01f) depth = 0.01f;

    // Geometria de uma calota: a esfera de raio R que passa pela borda (a uma
    // distância rim_radius do eixo) e pelo fundo (a `depth` abaixo do solo)
    // tem R = (rim² + depth²) / (2 · depth), e o centro fica R - depth acima
    // do solo (abaixo, se der negativo).
    float radius = (rim_radius * rim_radius + depth * depth) / (2.0f * depth);
    *sphere_radius = radius;
    return Vector3Add(ground, Vector3Scale(normal, radius - depth));
}

Vector3 planet_normal(const Planet *p, Vector3 surface_point)
{
    Vector3 gradient = planet_gradient(p, surface_point);
    float length = Vector3Length(gradient);
    if (length > 1e-4f) return Vector3Scale(gradient, -1.0f / length);

    // Sem gradiente utilizável: assume o "para fora" do planeta.
    float distance = Vector3Length(surface_point);
    return (distance > 1e-4f) ? Vector3Scale(surface_point, 1.0f / distance) : (Vector3){ 0.0f, 1.0f, 0.0f };
}

bool planet_raycast(const Planet *p, Vector3 origin, Vector3 dir, float max_distance, Vector3 *hit)
{
    float length = Vector3Length(dir);
    if (!(length > 1e-6f)) return false;
    dir = Vector3Scale(dir, 1.0f / length);

    // A matéria do planeta nunca passa desta esfera; fora dela não há o que
    // procurar. Calcula o trecho do raio que fica dentro da esfera.
    float bound = p->params.radius + p->params.land_height + 1.0f;
    float b = Vector3DotProduct(origin, dir);
    float c = Vector3DotProduct(origin, origin) - bound * bound;
    float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;   // o raio passa longe do planeta
    float root = sqrtf(discriminant);
    float start = -b - root;
    float end = -b + root;
    if (end < 0.0f) return false;            // o planeta ficou para trás
    if (start < 0.0f) start = 0.0f;
    if (end > max_distance) end = max_distance;
    if (start > end) return false;

    float previous = start;
    for (float t = start; t <= end + RAYCAST_STEP; t += RAYCAST_STEP) {
        Vector3 point = Vector3Add(origin, Vector3Scale(dir, t));
        if (planet_sample(p, point) < VOXEL_ISO) {
            previous = t;
            continue;
        }

        // Achou matéria entre `previous` (vazio) e `t` (sólido): divide o
        // trecho ao meio algumas vezes para chegar perto da superfície.
        float lo = previous, hi = t;
        for (int i = 0; i < RAYCAST_REFINEMENTS; i++) {
            float mid = 0.5f * (lo + hi);
            if (planet_sample(p, Vector3Add(origin, Vector3Scale(dir, mid))) >= VOXEL_ISO) hi = mid;
            else lo = mid;
        }
        *hit = Vector3Add(origin, Vector3Scale(dir, hi));
        return true;
    }
    return false;
}
