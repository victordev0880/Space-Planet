#include "game/particles.h"

#include <math.h>
#include <string.h>

// Limites por impacto, para uma explosão enorme não esgotar o array sozinha.
#define ROCKS_MIN  4
#define ROCKS_MAX  400
#define DUST_MIN   3
#define DUST_MAX   36
#define EMBERS_MAX 200

// Nuvens de poeira translúcidas custam caro para a placa de vídeo: cada uma
// repinta todos os pixels que cobre. Por isso há um teto para o tamanho de
// cada nuvem e outro para quantas existem ao mesmo tempo. Em um bombardeio,
// os impactos além do teto soltam pedras e brasas, mas não mais poeira.
#define DUST_SIZE_MAX  4.5f
#define DUST_ALIVE_MAX 420

// Cor para a qual a poeira é puxada, e quanto. Clara demais, a poeira parada
// sobre uma cratera parece um reflexo e faz o buraco parecer uma bola.
#define DUST_TINT_R      150.0f
#define DUST_TINT_G      138.0f
#define DUST_TINT_B      124.0f
#define DUST_TINT_AMOUNT 0.45f

#define DUST_DRAG         1.6f    // fração da velocidade que a poeira perde por segundo
#define EMBER_GRAVITY     0.35f   // brasas são leves: sentem só parte da gravidade
#define LANDING_GRACE     0.15f   // segundos iniciais em que a partícula ainda não "pousa"
#define HOT_FRACTION_MIN  0.02f   // parcela mínima de manto/núcleo arrancado para soltar brasas

bool particles_init(Particles *ps, Arena *arena)
{
    memset(ps, 0, sizeof *ps);
    ps->items = ARENA_ARRAY(arena, Particle, PARTICLES_CAPACITY);
    if (ps->items == NULL) return false;
    ps->capacity = PARTICLES_CAPACITY;
    return true;
}

void particles_clear(Particles *ps)
{
    ps->count = 0;
    ps->dust = 0;
}

Particle *particles_spawn(Particles *ps)
{
    if (ps->count >= ps->capacity) return NULL;
    Particle *p = &ps->items[ps->count++];
    memset(p, 0, sizeof *p);
    if (ps->count > ps->peak) ps->peak = ps->count;
    return p;
}

void particles_tick(Particles *ps, const Planet *planet, const Tuning *t, float dt)
{
    int i = 0;
    while (i < ps->count) {
        Particle *p = &ps->items[i];
        p->age += dt;
        bool dead = p->age >= p->life;

        if (!dead && p->kind != PARTICLE_FLASH) {
            if (p->kind == PARTICLE_DUST) {
                float keep = 1.0f - DUST_DRAG * dt;
                p->vel = Vector3Scale(p->vel, (keep > 0.0f) ? keep : 0.0f);
            } else {
                // Gravidade: puxa para o centro do planeta (a origem).
                float pull = t->particles_gravity * ((p->kind == PARTICLE_EMBER) ? EMBER_GRAVITY : 1.0f);
                float distance = Vector3Length(p->pos);
                if (distance > 1e-3f) {
                    p->vel = Vector3Add(p->vel, Vector3Scale(p->pos, -pull * dt / distance));
                }
            }
            p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));

            // Pedras e brasas somem ao bater no chão.
            if (p->kind != PARTICLE_DUST && p->age > LANDING_GRACE
                && planet_sample(planet, p->pos) >= VOXEL_ISO) {
                dead = true;
            }
        }

        if (dead) {
            if (p->kind == PARTICLE_DUST) ps->dust--;
            ps->items[i] = ps->items[--ps->count];   // a última viva ocupa o lugar
        } else {
            i++;
        }
    }
}

static uint8_t to_byte(float v)
{
    return (uint8_t)Clamp(v + 0.5f, 0.0f, 255.0f);
}

// Sorteia um dos materiais arrancados, com chance proporcional à quantidade.
static int pick_material(Rng *rng, const CarveResult *carve)
{
    if (carve->removed <= 0) return VMAT_CRUST;
    double target = (double)rng_float(rng) * (double)carve->removed;
    double sum = 0.0;
    for (int m = 1; m < VMAT_COUNT; m++) {
        sum += (double)carve->removed_by_material[m];
        if (target < sum) return m;
    }
    return VMAT_CRUST;
}

// Direção de lançamento: para fora do solo, com espalhamento.
static Vector3 launch_direction(Rng *rng, Vector3 normal, float upward, float spread)
{
    Vector3 dir = Vector3Add(Vector3Scale(normal, upward), Vector3Scale(rng_unit_vec3(rng), spread));
    // Nada é lançado para dentro do chão.
    float outward = Vector3DotProduct(dir, normal);
    if (outward < 0.1f) dir = Vector3Add(dir, Vector3Scale(normal, 0.1f - outward));
    return Vector3Normalize(dir);
}

void particles_impact(Particles *ps, Rng *rng, const Tuning *t, Vector3 ground, Vector3 normal,
                      float rim_radius, const CarveResult *carve)
{
    MeshStyle style = tuning_mesh_style(t);
    float scale = sqrtf(rim_radius);   // explosões maiores lançam mais longe, mas não em proporção direta

    int64_t hot = carve->removed_by_material[VMAT_MANTLE] + carve->removed_by_material[VMAT_CORE];
    float hot_fraction = (carve->removed > 0) ? (float)((double)hot / (double)carve->removed) : 0.0f;
    bool is_hot = hot_fraction > HOT_FRACTION_MIN;

    // Clarão.
    Particle *flash = particles_spawn(ps);
    if (flash != NULL) {
        flash->kind = PARTICLE_FLASH;
        flash->pos = Vector3Add(ground, Vector3Scale(normal, rim_radius * 0.3f));
        flash->size = rim_radius * t->particles_flash_size;
        flash->life = t->particles_flash_life * (is_hot ? 1.6f : 1.0f);
        flash->r = 255;
        flash->g = is_hot ? 200 : 238;
        flash->b = is_hot ? 120 : 205;
    }

    // Pedras, da cor dos materiais arrancados.
    int rocks = (int)(rim_radius * rim_radius * t->particles_rock_count);
    if (rocks < ROCKS_MIN) rocks = ROCKS_MIN;
    if (rocks > ROCKS_MAX) rocks = ROCKS_MAX;
    for (int i = 0; i < rocks; i++) {
        Particle *p = particles_spawn(ps);
        if (p == NULL) break;
        Rgb8 color = style.palette[pick_material(rng, carve)];
        float shade = rng_range(rng, 0.65f, 1.1f);
        p->kind = PARTICLE_ROCK;
        p->pos = Vector3Add(ground, Vector3Scale(rng_unit_vec3(rng), rim_radius * 0.5f * rng_float(rng)));
        p->pos = Vector3Add(p->pos, Vector3Scale(normal, 0.4f));
        p->vel = Vector3Scale(launch_direction(rng, normal, rng_range(rng, 0.5f, 1.5f), 0.9f),
                              t->particles_rock_speed * scale * rng_range(rng, 0.35f, 1.0f));
        p->size = t->particles_rock_size * rng_range(rng, 0.5f, 1.6f);
        p->life = t->particles_rock_life * rng_range(rng, 0.5f, 1.2f);
        p->r = to_byte(color.r * shade);
        p->g = to_byte(color.g * shade);
        p->b = to_byte(color.b * shade);
    }

    // Poeira: nuvens grandes e lentas, em um tom claro do material.
    int dust = (int)(rim_radius * t->particles_dust_count);
    if (dust < DUST_MIN) dust = DUST_MIN;
    if (dust > DUST_MAX) dust = DUST_MAX;
    for (int i = 0; i < dust && ps->dust < DUST_ALIVE_MAX; i++) {
        Particle *p = particles_spawn(ps);
        if (p == NULL) break;
        Rgb8 color = style.palette[pick_material(rng, carve)];
        ps->dust++;
        p->kind = PARTICLE_DUST;
        p->pos = Vector3Add(ground, Vector3Scale(rng_unit_vec3(rng), rim_radius * 0.6f * rng_float(rng)));
        p->pos = Vector3Add(p->pos, Vector3Scale(normal, 0.5f));
        p->vel = Vector3Scale(launch_direction(rng, normal, rng_range(rng, 0.2f, 1.0f), 1.0f),
                              t->particles_rock_speed * 0.45f * scale * rng_range(rng, 0.3f, 1.0f));
        p->size = fminf(rim_radius * t->particles_dust_size, DUST_SIZE_MAX) * rng_range(rng, 0.5f, 1.2f);
        p->life = t->particles_dust_life * rng_range(rng, 0.6f, 1.2f);
        // Tom de terra: a cor do material puxada para um cinza-acastanhado.
        p->r = to_byte(Lerp((float)color.r, DUST_TINT_R, DUST_TINT_AMOUNT));
        p->g = to_byte(Lerp((float)color.g, DUST_TINT_G, DUST_TINT_AMOUNT));
        p->b = to_byte(Lerp((float)color.b, DUST_TINT_B, DUST_TINT_AMOUNT));
    }

    // Brasas: só quando o impacto arranca manto ou núcleo.
    if (!is_hot) return;
    int embers = (int)(rim_radius * rim_radius * t->particles_ember_count * hot_fraction);
    if (embers > EMBERS_MAX) embers = EMBERS_MAX;
    for (int i = 0; i < embers; i++) {
        Particle *p = particles_spawn(ps);
        if (p == NULL) break;
        bool from_core = rng_float(rng) * (float)hot < (float)carve->removed_by_material[VMAT_CORE];
        Rgb8 color = style.palette[from_core ? VMAT_CORE : VMAT_MANTLE];
        p->kind = PARTICLE_EMBER;
        p->pos = Vector3Add(ground, Vector3Scale(rng_unit_vec3(rng), rim_radius * 0.4f * rng_float(rng)));
        p->pos = Vector3Add(p->pos, Vector3Scale(normal, 0.4f));
        p->vel = Vector3Scale(launch_direction(rng, normal, rng_range(rng, 0.6f, 1.6f), 0.8f),
                              t->particles_rock_speed * 0.9f * scale * rng_range(rng, 0.4f, 1.0f));
        p->size = t->particles_rock_size * rng_range(rng, 0.6f, 1.3f);
        p->life = t->particles_rock_life * rng_range(rng, 0.4f, 0.9f);
        p->r = color.r;
        p->g = color.g;
        p->b = color.b;
    }
}
