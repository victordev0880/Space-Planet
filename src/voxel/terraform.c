#include "voxel/terraform.h"

#include <math.h>
#include <string.h>

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Caixa de pontos da grade que a esfera (mais a faixa da borda) alcança.
// Retorna false se ela fica toda fora da grade.
static bool sphere_box(const Planet *p, Vector3 center, float reach, int lo[3], int hi[3])
{
    float g[3] = { center.x + p->center, center.y + p->center, center.z + p->center };
    const int last = p->size - 1;
    for (int k = 0; k < 3; k++) {
        if (g[k] + reach < 0.0f || g[k] - reach > (float)last) return false;
        lo[k] = clamp_int((int)floorf(g[k] - reach), 0, last);
        hi[k] = clamp_int((int)ceilf(g[k] + reach), 0, last);
    }
    return true;
}

// Peso do conserto a `distance` do centro: 1 dentro da esfera, caindo a 0 na
// faixa da borda.
static float restore_weight(float distance, float radius)
{
    if (distance <= radius) return 1.0f;
    return Clamp(1.0f - (distance - radius) / TERRAFORM_EDGE, 0.0f, 1.0f);
}

RestoreResult planet_restore(Planet *p, Vector3 center, float radius)
{
    RestoreResult result;
    memset(&result, 0, sizeof result);
    if (!(radius > 0.0f)) return result;

    int lo[3], hi[3];
    const float reach = radius + TERRAFORM_EDGE;
    if (!sphere_box(p, center, reach, lo, hi)) return result;
    const float gx = center.x + p->center, gy = center.y + p->center, gz = center.z + p->center;

    int min_x = p->size, min_y = p->size, min_z = p->size;
    int max_x = -1, max_y = -1, max_z = -1;
    for (int z = lo[2]; z <= hi[2]; z++) {
        for (int y = lo[1]; y <= hi[1]; y++) {
            for (int x = lo[0]; x <= hi[0]; x++) {
                float dx = (float)x - gx, dy = (float)y - gy, dz = (float)z - gz;
                float weight = restore_weight(sqrtf(dx * dx + dy * dy + dz * dz), radius);
                if (weight <= 0.0f) continue;

                int index = planet_index(p, x, y, z);
                int old = p->density[index];
                int goal = p->original_density[index];
                if (old >= goal) continue;   // nada a devolver (ou já está como era)

                int value = old + (int)((float)(goal - old) * weight + 0.5f);
                if (value <= old) continue;
                int old_material = p->material[index];
                int material = p->original_material[index];
                p->density[index] = (uint8_t)value;
                p->material[index] = (uint8_t)material;

                result.restored += value - old;
                int64_t core_before = (old_material == VMAT_CORE) ? old : 0;
                int64_t core_after = (material == VMAT_CORE) ? value : 0;
                result.restored_core += core_after - core_before;

                if (x < min_x) min_x = x;
                if (y < min_y) min_y = y;
                if (z < min_z) min_z = z;
                if (x > max_x) max_x = x;
                if (y > max_y) max_y = y;
                if (z > max_z) max_z = z;
            }
        }
    }
    if (result.restored == 0) return result;

    p->volume += result.restored;
    p->core_volume += result.restored_core;
    result.chunks_touched = planet_touch(p, min_x, min_y, min_z, max_x, max_y, max_z);
    return result;
}

int64_t planet_missing(const Planet *p, Vector3 center, float radius)
{
    if (!(radius > 0.0f)) return 0;
    int lo[3], hi[3];
    if (!sphere_box(p, center, radius, lo, hi)) return 0;
    const float gx = center.x + p->center, gy = center.y + p->center, gz = center.z + p->center;
    int64_t missing = 0;
    for (int z = lo[2]; z <= hi[2]; z++) {
        for (int y = lo[1]; y <= hi[1]; y++) {
            for (int x = lo[0]; x <= hi[0]; x++) {
                float dx = (float)x - gx, dy = (float)y - gy, dz = (float)z - gz;
                if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
                int index = planet_index(p, x, y, z);
                int gap = p->original_density[index] - p->density[index];
                if (gap > 0) missing += gap;
            }
        }
    }
    return missing;
}
