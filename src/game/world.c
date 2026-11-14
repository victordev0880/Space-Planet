#include "game/world.h"

#include <math.h>
#include <string.h>

#include "game/combat.h"
#include "voxel/damage.h"
#include "voxel/terraform.h"

#define PITCH_LIMIT (89.0f * DEG2RAD)

// Alcance máximo do raio de um impacto, em unidades do mundo.
#define IMPACT_RAY_LENGTH 5000.0f
#define CARVE_DEBRIS      0.3f     // destroços de uma escavação, em proporção a uma cratera do mesmo tamanho

// --- câmera em terceira pessoa ---
#define CHASE_MAX_STRETCH 2.0f   // o quanto a câmera pode ficar para trás, em distâncias normais
#define CHASE_MIN_STRETCH 0.8f   // ... e o quanto a nave pode chegar perto dela (de ré)
#define CHASE_PATH_STEPS  10     // pontos conferidos entre a nave e a câmera
#define CHASE_MIN_FRACTION 0.45f // mais perto que isto (em distâncias normais), a câmera sobe por cima
#define SLING_FOV         0.6f   // quanto o estilingue abre o campo de visão, em relação ao boost
#define RUSH_MAX          1.4f   // teto da abertura com boost e estilingue juntos

// --- efeitos da nave ---
static const Vector3 ENGINES[2] = {   // em eixos da nave
    { SHIP_ENGINE_X, SHIP_ENGINE_Y, SHIP_ENGINE_Z }, { -SHIP_ENGINE_X, SHIP_ENGINE_Y, SHIP_ENGINE_Z },
};
#define TRAIL_SPEED       7.0f     // velocidade com que o rastro sai para trás
#define TRAIL_LIFE        0.30f
#define TRAIL_SIZE        0.28f
#define TRAIL_BOOST_SIZE  0.40f
#define TRAIL_INHERIT     0.5f     // fração da velocidade da nave que o rastro leva junto
#define TRAIL_SPACING     0.55f    // distância entre dois pontos do rastro, em tamanhos
#define TRAIL_MAX_POINTS  5        // pontos soltos por motor em um passo
#define SPARKS_MIN_SPEED  2.5f     // batidas mais leves que isto não soltam faíscas
#define SPARKS_PER_SPEED  0.9f
#define SPARKS_MAX        50
#define DASH_STREAKS         36     // pontos de brilho soltos por um dash
#define DASH_STREAK_SPAN     1.3f   // espalhados por esta distância em volta do centro da nave
#define DASH_STREAK_SPEED    16.0f  // velocidade máxima com que ficam para trás
#define DASH_STREAK_INHERIT  0.55f  // fração da velocidade da nave que levam junto

static const Rgb8 TRAIL_COLOR       = { 110, 190, 255 };
static const Rgb8 TRAIL_BOOST_COLOR = { 255, 196, 120 };
static const Rgb8 TRAIL_SLING_COLOR = { 190, 150, 255 };
static const Rgb8 SPARK_COLOR       = { 255, 214, 150 };
static const Rgb8 BOSS_BLAST_HOT    = { 255, 236, 200 };
static const Rgb8 BOSS_BLAST_GLOW   = { 255, 90, 60 };
static const Rgb8 BOSS_BLAST_DEBRIS = { 70, 66, 80 };
static const Rgb8 SHIELD_SPARK      = { 130, 210, 255 };
#define BOSS_BLAST_JOLT 6.0f
#define SPORE_DRIFT     3.0f     // velocidade com que o esporo sai da bolsa estourada
#define MOON_RETURN     0.01f    // sem a Colmeia, a Lua recupera este tanto da distância por segundo
#define WORM_CRATER_FROM 12.0f   // as crateras do Verme são "impactos" vindos deste tanto acima do chão
#define WORM_DUST        6       // poeira de cada tremor em cima do Verme
#define DEV_BITE_JOLT    9.0f
#define DEV_BROKEN_JOLT  5.0f
#define DEV_GULP         0.55f   // engole o que chegar a este tanto (em escalas) da boca
#define DEV_SWIRL_DRAG   0.8f    // as rochas sugadas perdem velocidade de lado (espiral, não órbita)
#define DEV_REGURGITATE_BASE 6   // na morte, devolve isto mais o que engoliu (até dev.regurgitate)
#define DEV_RAIN_HEIGHT  30.0f   // as rochas devolvidas caem desta altura...
#define DEV_RAIN_STEP    4.0f    // ... uma um pouco mais alta que a outra (chegam em sequência)
#define DEV_RAIN_SPEED   6.0f
static const Rgb8 WORM_DUST_COLOR = { 140, 118, 96 };

// ---------------------------------------------------------------------------
//  Câmeras
// ---------------------------------------------------------------------------

static Vector3 angles_to_forward(float yaw, float pitch)
{
    float cp = cosf(pitch);
    return (Vector3){ sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp };
}

// Câmera de apresentação: anda em um círculo e olha sempre para o centro.
static CamPose orbit_pose(float angle, const Tuning *t)
{
    CamPose p;
    p.pos = (Vector3){ t->orbit_radius * sinf(angle), t->orbit_height, t->orbit_radius * cosf(angle) };
    p.rot = quat_look(Vector3Negate(p.pos), (Vector3){ 0.0f, 1.0f, 0.0f });
    p.fov = t->camera_fov;
    return p;
}

static CamPose free_pose(const World *w, const Tuning *t)
{
    CamPose p;
    p.pos = w->view.pos;
    p.rot = quat_look(angles_to_forward(w->free_yaw, w->free_pitch), (Vector3){ 0.0f, 1.0f, 0.0f });
    p.fov = t->camera_fov;
    return p;
}

// Anda da nave até `to` e devolve o último ponto livre antes do primeiro
// obstáculo (terreno, Lua ou o corpo do chefe, em `hull`).
static Vector3 clear_camera_path(const World *w, Vector3 from, Vector3 to, float clearance, const BossPart *hull, int hull_count)
{
    // Densidade que o campo tem a `clearance` de distância da superfície.
    float blocked = VOXEL_ISO - clearance * VOXEL_DENSITY_PER_UNIT;
    float moon_reach = w->moon.radius + clearance;
    Vector3 last_free = from;
    for (int i = 1; i <= CHASE_PATH_STEPS; i++) {
        Vector3 point = Vector3Lerp(from, to, (float)i / CHASE_PATH_STEPS);
        if (planet_sample(&w->planet, point) >= blocked) return last_free;
        if (Vector3Distance(point, w->moon.pos) < moon_reach) return last_free;
        for (int k = 0; k < hull_count; k++) {
            if (Vector3Distance(point, hull[k].center) < hull[k].radius + clearance) return last_free;
        }
        last_free = point;
    }
    return to;
}

// Lugar da câmera: o desejado, recuado se houver terreno no caminho. Se o
// recuo a deixaria colada na nave (dentro do modelo, com a nave pousada no
// chão ou no fundo de um buraco), ela sobe por cima da nave. Os chefes são
// enormes: a câmera também não entra no corpo deles (por dentro, a tela vira
// uma parede de polígonos).
static Vector3 place_chase_camera(const World *w, const Tuning *t, Vector3 ship, Vector3 desired)
{
    // Só as peças sólidas que não envolvem a própria nave (colada no casco,
    // a câmera ficaria presa nela).
    BossPart parts[BOSS_PARTS_MAX], hull[BOSS_PARTS_MAX];
    int hull_count = 0;
    if (boss_active(&w->boss)) {
        int count = boss_parts(&w->boss, t, parts);
        for (int k = 0; k < count; k++) {
            bool wraps_ship = Vector3Distance(ship, parts[k].center) < parts[k].radius + t->chase_clearance;
            if (parts[k].solid && !wraps_ship) hull[hull_count++] = parts[k];
        }
    }
    Vector3 pos = clear_camera_path(w, ship, desired, t->chase_clearance, hull, hull_count);
    if (Vector3Distance(pos, ship) >= t->chase_distance * CHASE_MIN_FRACTION) return pos;

    Vector3 up = Vector3Normalize(ship);
    Vector3 above = Vector3Add(ship, Vector3Add(Vector3Scale(up, t->chase_distance),
                                                Vector3Scale(Vector3Subtract(desired, ship), 0.4f)));
    Vector3 lifted = clear_camera_path(w, ship, above, t->chase_clearance, hull, hull_count);
    return (Vector3Distance(lifted, ship) > Vector3Distance(pos, ship)) ? lifted : pos;
}

// Câmera de jogo: fica atrás e acima da nave, com atraso. O atraso é o que
// dá a sensação de aceleração; no boost o campo de visão também abre.
static void tick_chase_camera(World *w, const Tuning *t, float dt, bool snap)
{
    const Ship *s = &w->ship;
    if (!s->alive && !snap) return;   // destruída: a câmera fica olhando o lugar

    Vector3 forward = ship_forward(s), up = ship_up(s);
    Vector3 desired = Vector3Add(s->pos, Vector3Add(Vector3Scale(forward, -t->chase_distance),
                                                    Vector3Scale(up, t->chase_height)));
    Vector3 pos = desired;
    if (!snap) {
        pos = Vector3Lerp(w->view.pos, desired, 1.0f - expf(-t->chase_pos_smooth * dt));

        // Em alta velocidade o atraso cresce; aqui ele tem um teto. E de ré a
        // nave vem em direção à câmera: ela recua junto, sem deixar a nave
        // chegar perto demais.
        Vector3 offset = Vector3Subtract(pos, s->pos);
        float length = Vector3Length(offset);
        float longest = t->chase_distance * CHASE_MAX_STRETCH;
        float shortest = t->chase_distance * CHASE_MIN_STRETCH;
        if (length > longest) pos = Vector3Add(s->pos, Vector3Scale(offset, longest / length));
        else if (length < shortest && length > 1e-3f) pos = Vector3Add(s->pos, Vector3Scale(offset, shortest / length));
    }
    pos = place_chase_camera(w, t, s->pos, pos);

    // Olha para um ponto à frente da nave, e não para ela: assim sobra tela
    // para ver aonde se está indo.
    Vector3 look = Vector3Add(s->pos, Vector3Scale(forward, t->chase_look_ahead));

    // No voo rasante esse ponto desce em direção ao planeta. Sem isso, como o
    // planeta é pequeno, o chão some logo abaixo da tela e não se vê o
    // relevo passando.
    float distance = Vector3Length(s->pos);
    if (s->graze > 0.0f && distance > 1e-3f) {
        float tilt = t->chase_ground_tilt * DEG2RAD * s->graze;
        float drop = tanf(tilt) * (t->chase_look_ahead + t->chase_distance);
        look = Vector3Subtract(look, Vector3Scale(s->pos, drop / distance));
    }
    Quaternion target = quat_look(Vector3Subtract(look, pos), up);

    w->view.pos = pos;
    w->view.rot = snap ? target : QuaternionSlerp(w->view.rot, target, 1.0f - expf(-t->chase_rot_smooth * dt));
    // O campo de visão abre com a velocidade extra: no boost e no estilingue.
    float rush = Clamp(s->boost_blend + s->sling_blend * SLING_FOV, 0.0f, RUSH_MAX);
    w->view.fov = t->camera_fov + rush * t->chase_fov_boost;
}

static void tick_free_camera(World *w, const InputState *in, const Tuning *t, float dt)
{
    float sens = t->freecam_mouse_sens * DEG2RAD;
    w->free_yaw += in->look_dx * sens;
    w->free_pitch -= in->look_dy * sens;   // mouse para baixo (dy positivo) olha para baixo
    w->free_pitch = Clamp(w->free_pitch, -PITCH_LIMIT, PITCH_LIMIT);

    Vector3 forward = angles_to_forward(w->free_yaw, w->free_pitch);
    Vector3 right = { cosf(w->free_yaw), 0.0f, sinf(w->free_yaw) };
    Vector3 wish = { 0.0f, 0.0f, 0.0f };
    if (in->held & ACT_BIT(ACT_FORWARD)) wish = Vector3Add(wish, forward);
    if (in->held & ACT_BIT(ACT_BACK))    wish = Vector3Subtract(wish, forward);
    if (in->held & ACT_BIT(ACT_RIGHT))   wish = Vector3Add(wish, right);
    if (in->held & ACT_BIT(ACT_LEFT))    wish = Vector3Subtract(wish, right);
    if (in->held & ACT_BIT(ACT_DASH))    wish.y += 1.0f;
    if (in->held & ACT_BIT(ACT_DRIFT))   wish.y -= 1.0f;

    float speed = t->freecam_speed;
    if (in->held & ACT_BIT(ACT_BOOST)) speed *= t->freecam_boost;
    Vector3 target = Vector3Scale(Vector3Normalize(wish), speed);

    // Suavização exponencial: a velocidade persegue o alvo em vez de saltar
    // para ele. A fórmula com exp() dá o mesmo resultado em qualquer dt.
    float blend = 1.0f - expf(-t->freecam_smooth * dt);
    w->free_vel = Vector3Lerp(w->free_vel, target, blend);
    w->view.pos = Vector3Add(w->view.pos, Vector3Scale(w->free_vel, dt));
    w->view = free_pose(w, t);
}

// Entra na câmera livre a partir de onde a câmera atual está olhando.
static void enter_free_camera(World *w)
{
    Vector3 forward = cam_forward(w->view);
    w->free_pitch = Clamp(asinf(Clamp(forward.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    w->free_yaw = atan2f(forward.x, -forward.z);
    w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    if (w->cam_mode != CAM_FREE) w->cam_return = w->cam_mode;
    w->cam_mode = CAM_FREE;
}

void world_set_view(World *w, const Tuning *t, CamMode mode)
{
    if (mode == CAM_FREE) {
        enter_free_camera(w);
        w->view = free_pose(w, t);
    } else if (mode == CAM_ORBIT) {
        w->cam_mode = CAM_ORBIT;
        w->cam_return = CAM_ORBIT;
        w->ship_active = false;
        w->view = orbit_pose(w->orbit_angle, t);
    } else {
        w->cam_mode = CAM_SHIP;
        w->cam_return = CAM_SHIP;
        w->ship_active = true;
        tick_chase_camera(w, t, 0.0f, true);
    }
    w->view_prev = w->view;   // corte seco, sem interpolar o salto
}

void world_place_camera(World *w, Vector3 pos, Vector3 target)
{
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, pos));
    if (Vector3LengthSqr(dir) < 0.5f) dir = (Vector3){ 0.0f, 0.0f, -1.0f };   // pos == target

    if (w->cam_mode != CAM_FREE) w->cam_return = w->cam_mode;
    w->cam_mode = CAM_FREE;
    w->free_vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    // Inverso de angles_to_forward: de uma direção para yaw e pitch.
    w->free_pitch = Clamp(asinf(Clamp(dir.y, -1.0f, 1.0f)), -PITCH_LIMIT, PITCH_LIMIT);
    w->free_yaw = atan2f(dir.x, -dir.z);

    float fov = w->view.fov;
    w->view.pos = pos;
    w->view.rot = quat_look(angles_to_forward(w->free_yaw, w->free_pitch), (Vector3){ 0.0f, 1.0f, 0.0f });
    w->view.fov = fov;
    w->view_prev = w->view;   // corte seco, sem interpolar o salto
}

void world_place_ship(World *w, const Tuning *t, Vector3 pos, Vector3 target, float speed)
{
    ship_place(&w->ship, t, &w->planet, pos, Vector3Subtract(target, pos), speed);
    world_set_view(w, t, CAM_SHIP);
}

// ---------------------------------------------------------------------------
//  Planeta
// ---------------------------------------------------------------------------

// O planeta morre se perder massa demais ou se o núcleo for destruído.
static void check_collapse(World *w, const Tuning *t)
{
    if (planet_hp(&w->planet) < t->defeat_planet_hp || planet_core_hp(&w->planet) < t->defeat_core_hp) {
        w->collapsed = true;
    }
}

// Material de superfície que mais saiu numa cratera (a cor do "topo" dos
// fragmentos): grama, areia, gelo... Sem nenhum, rocha da crosta.
static uint8_t surface_material(const CarveResult *carve)
{
    int best = VMAT_CRUST;
    int64_t most = 0;
    for (int m = VMAT_WATER_SHALLOW; m <= VMAT_SNOW; m++) {
        if (carve->removed_by_material[m] > most) {
            most = carve->removed_by_material[m];
            best = m;
        }
    }
    return (uint8_t)best;
}

// Explosões grandes arrancam pedaços do planeta, que sobem e ficam em órbita:
// um fragmento a cada fragment_volume voxels arrancados (somando impactos).
static void eject_fragments(World *w, const Tuning *t, Vector3 ground, float radius, const CarveResult *carve)
{
    if (radius < t->fragment_min_radius || carve->removed <= 0) return;
    w->debris += (float)carve->removed / 255.0f;
    int made = 0;
    while (w->debris >= t->fragment_volume && made < t->fragment_per_impact) {
        if (rocks_count(&w->rocks, ROCK_FRAGMENT) >= t->fragment_max) {
            w->debris = fminf(w->debris, t->fragment_volume);   // cinturão cheio: não acumula sem fim
            return;
        }
        Rock *k = rocks_spawn_fragment(&w->rocks, &w->rng, t, w->planet.params.radius, ground, surface_material(carve));
        if (k == NULL) return;
        events_push(&w->events, (uint32_t)w->tick, EV_FRAGMENT, 0, ground, k->radius);
        w->debris -= t->fragment_volume;
        made++;
    }
}

// Um escudo local cobre o lugar? Então ele segura o golpe (e faísca).
static bool shielded(World *w, const Tuning *t, Vector3 ground, float radius)
{
    if (!defenses_absorb(&w->defenses, t, w->planet.params.radius, ground, radius, &w->events, (uint32_t)w->tick)) {
        return false;
    }
    Vector3 up = Vector3Normalize(ground);
    particles_sparks(&w->particles, &w->rng, Vector3Add(ground, Vector3Scale(up, 2.0f)), up, 10, 10.0f, SHIELD_SPARK);
    return true;
}

static bool impact_at(World *w, const Tuning *t, Vector3 ground, Vector3 normal, float rim_radius, float depth,
                      bool fragments);

bool world_impact(World *w, const Tuning *t, Vector3 origin, Vector3 dir, float rim_radius, float depth)
{
    Vector3 ground;
    if (!planet_raycast(&w->planet, origin, dir, IMPACT_RAY_LENGTH, &ground)) return false;
    return impact_at(w, t, ground, planet_normal(&w->planet, ground), rim_radius, depth, true);
}

bool world_impact_at(World *w, const Tuning *t, Vector3 ground, float rim_radius, float depth, bool fragments)
{
    return impact_at(w, t, ground, Vector3Normalize(ground), rim_radius, depth, fragments);
}

static bool impact_at(World *w, const Tuning *t, Vector3 ground, Vector3 normal, float rim_radius, float depth,
                      bool fragments)
{
    if (shielded(w, t, ground, rim_radius)) return true;

    float sphere_radius = 0.0f;
    Vector3 center = crater_sphere(ground, normal, rim_radius, depth, &sphere_radius);
    CarveResult carve = planet_carve(&w->planet, center, sphere_radius);
    if (carve.removed == 0) return true;   // acertou, mas não havia mais o que tirar

    particles_impact(&w->particles, &w->rng, t, ground, normal, rim_radius, &carve);
    repair_note_damage(&w->repair, ground, rim_radius);
    if (fragments) eject_fragments(w, t, ground, rim_radius, &carve);

    Impact *impact = &w->impacts[w->impact_count % WORLD_IMPACT_HISTORY];
    w->impact_count++;
    impact->pos = ground;
    impact->radius = rim_radius;
    impact->id = w->impact_count;
    impact->quiet = false;

    check_collapse(w, t);
    return true;
}

void world_carve(World *w, const Tuning *t, Vector3 center, float radius)
{
    // Coberta por um escudo, a escavação para (só se havia o que cavar ali:
    // o escudo não gasta vida segurando o vazio).
    if (planet_sample(&w->planet, center) > 0.0f
        && shielded(w, t, Vector3Add(center, Vector3Scale(Vector3Normalize(center), radius * 0.5f)), radius)) {
        return;
    }
    CarveResult carve = planet_carve(&w->planet, center, radius);
    if (carve.removed == 0) return;

    // Poucos destroços: a escavação é contínua, e cada mordida é pequena.
    Vector3 normal = Vector3Normalize(center);
    particles_impact(&w->particles, &w->rng, t, center, normal, radius * CARVE_DEBRIS, &carve);
    repair_note_damage(&w->repair, center, radius);
    eject_fragments(w, t, Vector3Add(center, Vector3Scale(normal, radius * 0.5f)), radius, &carve);

    Impact *impact = &w->impacts[w->impact_count % WORLD_IMPACT_HISTORY];
    w->impact_count++;
    impact->pos = center;
    impact->radius = radius;
    impact->id = w->impact_count;
    impact->quiet = true;

    check_collapse(w, t);
}

const Impact *world_find_impact(const World *w, uint32_t id)
{
    if (id == 0 || id > w->impact_count) return NULL;
    if (w->impact_count - id >= WORLD_IMPACT_HISTORY) return NULL;   // já foi sobrescrito
    return &w->impacts[(id - 1) % WORLD_IMPACT_HISTORY];
}

// Receita do planeta para a grade que já existe. O tamanho da grade só vale
// ao iniciar; montar a receita com outro tamanho calcularia o raio (que é uma
// fração da grade) para uma grade que não é a nossa.
static PlanetParams current_planet_params(const World *w, const Tuning *t)
{
    Tuning fixed = *t;
    fixed.planet_grid = w->planet.size;
    fixed.planet_chunk = w->planet.chunk_size;
    return tuning_planet_params(&fixed);
}

void world_reset_planet(World *w, const Tuning *t)
{
    PlanetParams params = current_planet_params(w, t);
    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    repair_reset(&w->repair);   // cicatrizes e consertos eram do planeta antigo
    w->collapsed = false;
}

bool world_apply_tuning(World *w, const Tuning *t)
{
    PlanetParams params = current_planet_params(w, t);
    if (planet_params_equal(&params, &w->planet.params)) return false;

    planet_generate(&w->planet, &params);
    particles_clear(&w->particles);
    repair_reset(&w->repair);
    w->collapsed = false;
    return true;
}

// ---------------------------------------------------------------------------
//  Efeitos da nave
// ---------------------------------------------------------------------------

static Particle *spawn_glow(World *w, ParticleKind kind, Vector3 pos, Vector3 vel, float size, float life, Rgb8 color)
{
    Particle *p = particles_spawn(&w->particles);
    if (p == NULL) return NULL;
    p->kind = (uint8_t)kind;
    p->pos = pos;
    p->vel = vel;
    p->size = size;
    p->life = life;
    p->r = color.r;
    p->g = color.g;
    p->b = color.b;
    return p;
}

void world_jolt(World *w, float strength)
{
    w->jolt_count++;
    w->jolt_strength = strength;
}

// O rastro puxa para a cor da linha de evolução dominante (a nave "vira"
// aquilo que mais derrotou). As cores são as mesmas do HUD.
static const Rgb8 LINE_TRAIL[ENEMY_TYPE_COUNT] = {
    [ENEMY_SWARM]    = { 196, 130, 255 }, [ENEMY_BOMBER]   = { 255, 150, 70 },
    [ENEMY_KAMIKAZE] = { 255, 96, 72 },   [ENEMY_DIGGER]   = { 255, 186, 80 },
    [ENEMY_SIPHON]   = { 120, 255, 190 }, [ENEMY_SNIPER]   = { 255, 110, 140 },
    [ENEMY_SHIELDER] = { 110, 230, 255 }, [ENEMY_CARRIER]  = { 214, 150, 255 },
};

static Rgb8 trail_color(const Evolution *evo)
{
    int line = evolution_dominant_line(evo);
    if (line < 0) return TRAIL_COLOR;
    float pull = Clamp(0.35f + 0.08f * (float)evolution_total_ranks(evo), 0.0f, 0.8f);
    Rgb8 lc = LINE_TRAIL[line];
    return (Rgb8){ (uint8_t)Lerp((float)TRAIL_COLOR.r, (float)lc.r, pull), (uint8_t)Lerp((float)TRAIL_COLOR.g, (float)lc.g, pull),
                   (uint8_t)Lerp((float)TRAIL_COLOR.b, (float)lc.b, pull) };
}

static void ship_effects(World *w, const ShipEvents *ev)
{
    const Ship *s = &w->ship;

    // Rastro dos motores: mais forte e de outra cor no boost. O rastro fica
    // quase parado no espaço enquanto a nave segue, então em alta velocidade
    // ele vira um risco comprido. Para o risco não ter falhas, cada passo
    // solta vários pontos ao longo do trecho que o motor percorreu.
    if (s->alive && s->thrust_blend > 0.15f) {
        Vector3 back = Vector3Negate(ship_forward(s));
        Rgb8 color = s->boosting ? TRAIL_BOOST_COLOR : (s->sling > 0.0f) ? TRAIL_SLING_COLOR : trail_color(&w->evo);
        float size = Lerp(TRAIL_SIZE, TRAIL_BOOST_SIZE, fmaxf(s->boost_blend, s->sling_blend)) * s->thrust_blend;
        Vector3 carried = Vector3Scale(Vector3Add(s->vel, s->fall), TRAIL_INHERIT);

        float left_behind = Vector3Distance(s->pos, s->pos_prev) * (1.0f - TRAIL_INHERIT);
        int points = 1 + (int)(left_behind / (size * TRAIL_SPACING));
        if (points > TRAIL_MAX_POINTS) points = TRAIL_MAX_POINTS;

        for (int i = 0; i < 2; i++) {
            Vector3 now = Vector3Add(s->pos, Vector3RotateByQuaternion(ENGINES[i], s->rot));
            Vector3 before = Vector3Add(s->pos_prev, Vector3RotateByQuaternion(ENGINES[i], s->rot_prev));
            for (int k = 0; k < points; k++) {
                Vector3 at = Vector3Lerp(before, now, ((float)k + rng_float(&w->rng)) / (float)points);
                Vector3 vel = Vector3Add(carried, Vector3Scale(back, TRAIL_SPEED * rng_range(&w->rng, 0.9f, 1.1f)));
                spawn_glow(w, PARTICLE_TRAIL, at, vel, size, TRAIL_LIFE * rng_range(&w->rng, 0.7f, 1.2f), color);
            }
        }
    }

    // Dash: riscos de brilho que ficam para trás, saindo de toda a nave, na
    // direção contrária à do impulso.
    if (ev->dashed) {
        Vector3 carried = Vector3Scale(s->vel, DASH_STREAK_INHERIT);
        for (int i = 0; i < DASH_STREAKS; i++) {
            Vector3 local = {
                rng_range(&w->rng, -DASH_STREAK_SPAN, DASH_STREAK_SPAN),
                rng_range(&w->rng, -0.2f, 0.3f),
                rng_range(&w->rng, -DASH_STREAK_SPAN, DASH_STREAK_SPAN),
            };
            Vector3 at = Vector3Add(s->pos, Vector3RotateByQuaternion(local, s->rot));
            Vector3 vel = Vector3Subtract(carried, Vector3Scale(ev->dash_dir, rng_range(&w->rng, 2.0f, DASH_STREAK_SPEED)));
            spawn_glow(w, PARTICLE_TRAIL, at, vel, rng_range(&w->rng, 0.25f, 0.5f), rng_range(&w->rng, 0.2f, 0.45f),
                       TRAIL_COLOR);
        }
        events_push(&w->events, (uint32_t)w->tick, EV_DASH, 0, s->pos, 0.0f);
    }

    // Batida no terreno: faíscas saltando da superfície.
    if (ev->hit && ev->hit_speed > SPARKS_MIN_SPEED) {
        int sparks = (int)(ev->hit_speed * SPARKS_PER_SPEED);
        if (sparks > SPARKS_MAX) sparks = SPARKS_MAX;
        for (int i = 0; i < sparks; i++) {
            Vector3 dir = Vector3Normalize(Vector3Add(ev->hit_normal, Vector3Scale(rng_unit_vec3(&w->rng), 0.9f)));
            spawn_glow(w, PARTICLE_EMBER, ev->hit_point,
                       Vector3Scale(dir, ev->hit_speed * rng_range(&w->rng, 0.2f, 0.7f)),
                       rng_range(&w->rng, 0.18f, 0.4f), rng_range(&w->rng, 0.3f, 0.9f), SPARK_COLOR);
        }
        world_jolt(w, ev->hit_speed);
        events_push(&w->events, (uint32_t)w->tick, EV_BUMP, 0, ev->hit_point, ev->hit_speed);
    }

    // Destruída: clarão e destroços em todas as direções.
    if (ev->died) combat_ship_wreck(w);
}

// ---------------------------------------------------------------------------
//  Mundo
// ---------------------------------------------------------------------------

bool world_init(World *w, const Tuning *t, uint64_t seed, Arena *arena)
{
    memset(w, 0, sizeof *w);
    rng_seed(&w->rng, seed, 1u);

    PlanetParams params = tuning_planet_params(t);
    if (!planet_init(&w->planet, &params, arena) || !particles_init(&w->particles, arena)) return false;
    if (!enemies_init(&w->enemies, arena) || !projectiles_init(&w->shots, arena)) return false;

    moon_init(&w->moon, t, w->planet.params.radius);
    ship_spawn(&w->ship, t, &w->planet);
    score_reset(&w->score);
    evolution_reset(&w->evo);
    allies_reset(&w->allies);
    boss_reset(&w->boss);
    rocks_reset(&w->rocks);
    repair_reset(&w->repair);
    defenses_reset(&w->defenses);
    w->meteor_wait = t->meteor_interval;
    w->calm_seen = -1;
    w->eff = *t;
    director_stop(&w->director);
    w->state = GAME_PLAYING;
    w->view.rot = QuaternionIdentity();
    w->view.fov = t->camera_fov;
    world_set_view(w, t, CAM_SHIP);
    return true;
}

// ---------------------------------------------------------------------------
//  Partida
// ---------------------------------------------------------------------------

bool world_in_play(const World *w)
{
    return w->cam_mode == CAM_SHIP && w->ship_active;
}

EnemyContext world_enemy_context(World *w, const Tuning *t)
{
    EnemyContext c;
    c.planet = &w->planet;
    c.moon = &w->moon;
    c.ship = (w->ship_active && w->ship.alive) ? &w->ship : NULL;
    c.shots = &w->shots;
    c.events = &w->events;
    c.rng = &w->rng;
    c.t = t;
    c.tick = (uint32_t)w->tick;
    c.dt = 1.0f / (float)t->sim_hz;
    // A cada horda, um pouco mais de vida (sem hordas, como na primeira).
    c.hp_scale = 1.0f + t->waves_hp_growth * (float)(w->waves_enabled ? w->director.wave : 0);
    return c;
}

int world_spawn(World *w, const Tuning *t, EnemyType type, int count, Vector3 center)
{
    EnemyContext ctx = world_enemy_context(w, t);
    return enemies_spawn_group(&w->enemies, &ctx, type, count, center);
}

int world_spawn_variant(World *w, const Tuning *t, EnemyType type, int count, Vector3 center, int variant)
{
    w->enemies.forced_variant = variant;
    int spawned = world_spawn(w, t, type, count, center);
    w->enemies.forced_variant = -1;
    return spawned;
}

void world_clear_enemies(World *w)
{
    enemies_clear(&w->enemies);
    projectiles_clear(&w->shots);
    boss_reset(&w->boss);
    rocks_reset(&w->rocks);
}

BossContext world_boss_context(World *w, const Tuning *t)
{
    BossContext c;
    c.planet = &w->planet;
    c.moon = &w->moon;
    c.drones = w->enemies.alive_by_type[ENEMY_SWARM];
    c.ship = (w->ship_active && w->ship.alive) ? &w->ship : NULL;
    c.shots = &w->shots;
    c.events = &w->events;
    c.rng = &w->rng;
    c.t = t;
    c.tick = (uint32_t)w->tick;
    c.dt = 1.0f / (float)t->sim_hz;
    return c;
}

void world_spawn_boss(World *w, const Tuning *t, BossType type, Vector3 dir)
{
    BossContext ctx = world_boss_context(w, t);
    boss_spawn(&w->boss, type, &ctx, dir);
}

// O Verme: crateras de saída e de mergulho, trechos do túnel que desmoronam,
// a erupção que pega a nave e a poeira no chão em cima dele.
static void tick_worm_requests(World *w, const Tuning *t)
{
    const WormState *m = &w->boss.worm;
    for (int i = 0; i < m->crater_count; i++) {
        Vector3 up = Vector3Normalize(m->crater_at[i]);
        world_impact(w, t, Vector3Add(m->crater_at[i], Vector3Scale(up, WORM_CRATER_FROM)), Vector3Negate(up),
                     t->worm_breach_radius, t->worm_breach_depth);
    }
    // Um pouco maior que a mordida, para cobrir o túnel entre duas mordidas.
    float refill = sqrtf(t->worm_tunnel_radius * t->worm_tunnel_radius + 0.25f * t->worm_tunnel_spacing * t->worm_tunnel_spacing);
    for (int i = 0; i < m->restore_count; i++) planet_restore(&w->planet, m->restores[i], refill + 0.5f);
    if (m->ship_damage > 0.0f && w->ship.alive && w->ship_active) combat_damage_ship(w, t, m->ship_damage, w->ship.pos);
    if (m->dust_now && worm_underground(&w->boss, &w->planet)) {
        // Tremor: poeira subindo do chão em cima da cabeça.
        Vector3 up = Vector3Normalize(w->boss.pos);
        Terrain ground = planet_terrain(&w->planet.params, up);
        Vector3 at = Vector3Scale(up, w->planet.params.radius + (ground.land ? ground.elevation : 0.0f));
        for (int k = 0; k < WORM_DUST; k++) {
            Vector3 dir = Vector3Normalize(Vector3Add(up, Vector3Scale(rng_unit_vec3(&w->rng), 0.7f)));
            spawn_glow(w, PARTICLE_DUST, at, Vector3Scale(dir, rng_range(&w->rng, 2.0f, 7.0f)),
                       rng_range(&w->rng, 1.2f, 2.6f), rng_range(&w->rng, 0.8f, 1.6f), WORM_DUST_COLOR);
        }
    }
}

// O Devorador: a mordida (um escudo local no ponto quebra os dentes dele) e o
// redemoinho, que puxa rochas e minério para a boca e cura quem engole.
static void tick_devourer_requests(World *w, const Tuning *t, float dt)
{
    Boss *b = &w->boss;
    uint32_t tick = (uint32_t)w->tick;
    if (b->dev.bite_now) {
        Vector3 dir = b->dev.bite_dir;
        Terrain terrain = planet_terrain(&w->planet.params, dir);
        Vector3 ground = Vector3Scale(dir, w->planet.params.radius + (terrain.land ? terrain.elevation : 0.0f));
        bool blocked = shielded(w, t, ground, t->dev_bite_radius);
        if (!blocked) {
            // Do chão original (não aprofunda), e sem fragmentos: o pedaço vai para a boca dele.
            world_impact_at(w, t, ground, t->dev_bite_radius, t->dev_bite_depth, false);
        }
        world_jolt(w, blocked ? DEV_BROKEN_JOLT : DEV_BITE_JOLT);
        devourer_bite_result(b, t, blocked, &w->events, tick);
    }
    if (b->state != BOSS_FIGHT) return;

    Vector3 mouth = devourer_mouth(b, t);
    float gulp = b->scale * DEV_GULP;
    for (int i = 0; i < ROCKS_MAX; i++) {
        Rock *k = &w->rocks.items[i];
        if (!k->alive || k->kind == ROCK_SPORE || i == w->rocks.towed) continue;   // a rocha no raio trator resiste
        Vector3 to = Vector3Subtract(mouth, k->pos);
        float distance = Vector3Length(to);
        if (distance > t->dev_vortex_range) continue;
        if (distance < gulp + k->radius) {
            float heal = (k->kind == ROCK_METEOR) ? t->dev_heal_meteor : t->dev_heal_fragment;
            devourer_swallow(b, heal, true);
            events_push(&w->events, tick, EV_DEV_SWALLOW, k->kind, k->pos, heal);
            k->alive = false;
            continue;
        }
        // Sugada: solta da órbita e voa em espiral até a boca.
        if (k->state != ROCK_FLYBY && k->state != ROCK_THROWN) k->state = ROCK_FLYBY;
        Vector3 dir = Vector3Scale(to, 1.0f / distance);
        k->vel = Vector3Add(Vector3Scale(k->vel, 1.0f - Clamp(DEV_SWIRL_DRAG * dt, 0.0f, 1.0f)),
                            Vector3Scale(dir, t->dev_vortex_pull * dt * (1.0f + 2.0f * (1.0f - distance / t->dev_vortex_range))));
    }
    for (int i = 0; i < ORE_MAX; i++) {
        Ore *o = &w->rocks.ore[i];
        if (!o->alive) continue;
        Vector3 to = Vector3Subtract(mouth, o->pos);
        float distance = Vector3Length(to);
        if (distance > t->dev_vortex_range) continue;
        if (distance < gulp) {
            devourer_swallow(b, t->dev_heal_ore, false);
            o->alive = false;
            continue;
        }
        o->vel = Vector3Add(o->vel, Vector3Scale(to, t->dev_vortex_pull * dt / distance));
    }
}

// Morto, o Devorador devolve o que comeu: rochas chovem sobre as crateras
// (as maiores primeiro) e consertam o planeta ao tocar o chão.
static void devourer_regurgitate(World *w, const Tuning *t)
{
    int count = w->boss.dev.eaten + DEV_REGURGITATE_BASE;
    if (count > t->dev_regurgitate) count = t->dev_regurgitate;
    float radius = w->planet.params.radius;

    // As cicatrizes abertas, da maior para a menor (ordenação por inserção:
    // são poucas).
    int order[REPAIR_SCARS], scars = 0;
    for (int s = 0; s < REPAIR_SCARS; s++) {
        if (!w->repair.scars[s].used) continue;
        int at = scars++;
        while (at > 0 && w->repair.scars[order[at - 1]].radius < w->repair.scars[s].radius) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = s;
    }
    for (int i = 0; i < count; i++) {
        // Uma rocha em cada cicatriz, das maiores para as menores (dando a
        // volta se sobrarem rochas); sem cicatrizes, um ponto sorteado.
        Vector3 dir = (scars > 0) ? w->repair.scars[order[i % scars]].dir : rng_unit_vec3(&w->rng);
        Vector3 ground = Vector3Scale(dir, radius);
        Rock *k = rocks_spawn_fragment(&w->rocks, &w->rng, t, radius, ground, VMAT_GRASS);
        if (k == NULL) break;
        k->state = ROCK_FALLING;
        k->pos = k->pos_prev = Vector3Scale(dir, radius + DEV_RAIN_HEIGHT + DEV_RAIN_STEP * (float)i);
        k->vel = Vector3Scale(dir, -DEV_RAIN_SPEED);
    }
}

// O boss pensa; o mundo executa o que ele pede (cortes no planeta, drones,
// explosões da morte) e, se ele acabou de cair, paga a recompensa.
static void tick_boss(World *w, const Tuning *t)
{
    Boss *b = &w->boss;
    BossContext ctx = world_boss_context(w, t);
    boss_tick(b, &ctx);
    for (int i = 0; i < b->carve_count; i++) world_carve(w, t, b->carves[i].center, b->carves[i].radius);
    if (b->launch_count > 0) world_spawn(w, t, (EnemyType)b->launch_type, b->launch_count, b->launch_from);
    for (int i = 0; i < b->hive.spore_count; i++) {
        Vector3 out = Vector3Normalize(Vector3Subtract(b->hive.spore_at[i], b->pos));
        rocks_spawn_spore(&w->rocks, &w->rng, t, b->hive.spore_at[i], Vector3Scale(out, SPORE_DRIFT));
    }
    b->hive.spore_count = 0;
    if (b->type == BOSS_WORM) tick_worm_requests(w, t);
    if (b->type == BOSS_DEVOURER) tick_devourer_requests(w, t, 1.0f / (float)t->sim_hz);
    // A Colmeia empurra a Lua; sem ela, a Lua volta devagar à órbita.
    float dt = 1.0f / (float)t->sim_hz;
    if (boss_active(b) && b->type == BOSS_HIVE) w->moon.scale = b->hive.moon_scale;
    else if (w->moon.scale < 1.0f)             w->moon.scale = fminf(w->moon.scale + MOON_RETURN * dt, 1.0f);
    if (b->blast_now) {
        particles_explosion(&w->particles, &w->rng, b->blast_at, b->vel, b->scale * 0.45f, BOSS_BLAST_HOT,
                            BOSS_BLAST_GLOW, BOSS_BLAST_DEBRIS);
        world_jolt(w, BOSS_BLAST_JOLT);
    }
    if (!b->defeated) return;
    b->defeated = false;
    if (b->type == BOSS_DEVOURER) devourer_regurgitate(w, t);
    particles_explosion(&w->particles, &w->rng, b->pos, b->vel, b->scale * 1.6f, BOSS_BLAST_HOT, BOSS_BLAST_GLOW,
                        BOSS_BLAST_DEBRIS);
    world_jolt(w, BOSS_BLAST_JOLT * 4.0f);
    if (w->state != GAME_PLAYING) return;
    score_kill(&w->score, t, boss_points((BossType)b->type, t));
    w->score.bosses++;
    world_give_xp(w, t, boss_xp((BossType)b->type, t));
    // A recompensa: o núcleo do boss (cada um dá uma habilidade). O Devorador
    // é o último: a recompensa dele é a chuva de rochas (e a vitória).
    if (b->type == BOSS_DEVOURER) return;
    bool *owned = (b->type == BOSS_HIVE) ? &w->brood.owned : (b->type == BOSS_WORM) ? &w->tunnel.owned : &w->ability.owned;
    if (!*owned) {
        *owned = true;
        events_push(&w->events, (uint32_t)w->tick, EV_REWARD, b->type, w->ship.pos, 0.0f);
    }
}

void world_set_plan(World *w, const WavePlan *plan)
{
    w->plan = *plan;
    w->waves_enabled = true;
    director_start(&w->director, &w->plan);
}

void world_set_waves(World *w, bool on)
{
    w->waves_enabled = on;
    w->meteor_queue = 0;
    w->calm_seen = -1;
    if (on) director_start(&w->director, &w->plan);
    else    director_stop(&w->director);
}

void world_restart(World *w, const Tuning *t)
{
    world_reset_planet(w, t);
    enemies_clear(&w->enemies);
    projectiles_clear(&w->shots);
    score_reset(&w->score);
    evolution_reset(&w->evo);
    allies_reset(&w->allies);
    boss_reset(&w->boss);
    memset(&w->ability, 0, sizeof w->ability);
    memset(&w->brood, 0, sizeof w->brood);
    memset(&w->tunnel, 0, sizeof w->tunnel);
    w->moon.scale = 1.0f;
    rocks_reset(&w->rocks);
    defenses_reset(&w->defenses);
    w->ore = 0;
    w->ore_total = 0;
    w->debris = 0.0f;
    w->meteor_wait = t->meteor_interval;
    w->meteor_queue = 0;
    w->calm_seen = -1;
    w->cards_at = 0;
    w->eff = *t;
    ship_spawn(&w->ship, t, &w->planet);
    w->state = GAME_PLAYING;
    w->state_tick = 0;
    w->hitstop = 0;
    memset(&w->frozen_input, 0, sizeof w->frozen_input);
    w->hit_marker = 0.0f;
    w->kill_marker = 0.0f;
    if (w->waves_enabled) director_start(&w->director, &w->plan);
    else                  director_stop(&w->director);
    world_set_view(w, t, CAM_SHIP);
}

// Faz a sequência de hordas andar: solta os grupos na hora e paga o bônus
// de planeta intacto ao fim de cada horda.
static void run_director(World *w, const InputState *in, const Tuning *t, float dt)
{
    if (w->state != GAME_PLAYING || w->director.phase == DIRECTOR_OFF) return;
    uint32_t tick = (uint32_t)w->tick;

    // Enter na calmaria: a próxima horda vem já.
    if ((in->pressed & ACT_BIT(ACT_READY)) && w->director.phase == DIRECTOR_PAUSE && w->director.timer > 0.0f) {
        w->director.timer = 0.0f;
        events_push(&w->events, tick, EV_CALM_SKIP, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, 0.0f);
    }
    int alive = enemies_alive(&w->enemies) + (boss_active(&w->boss) ? 1 : 0);   // o boss também conta
    DirectorStep step = director_tick(&w->director, &w->plan, alive, dt);

    if (step.wave_started) {
        events_push(&w->events, tick, EV_WAVE_START, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, (float)(w->director.wave + 1));
    }
    float distance = t->enemies_spawn_distance * w->planet.params.radius;
    for (int i = 0; i < step.spawn_count; i++) {
        const WaveGroup *g = step.spawn[i];
        if (g->type >= WAVE_BOSS_BASE) {
            world_spawn_boss(w, t, (BossType)(g->type - WAVE_BOSS_BASE), wave_group_direction(g));
            continue;
        }
        world_spawn(w, t, (EnemyType)g->type, g->count, Vector3Scale(wave_group_direction(g), distance));
    }
    if (step.wave_cleared) {
        float bonus = (planet_hp(&w->planet) >= t->score_planet_bonus_hp) ? t->score_planet_bonus : 0.0f;
        score_bonus(&w->score, (int64_t)bonus);
        w->score.planet_bonus += (int64_t)bonus;
        events_push(&w->events, tick, EV_WAVE_CLEAR, 0, (Vector3){ 0.0f, 0.0f, 0.0f }, bonus);
    }
}

static void update_game_state(World *w)
{
    if (w->state != GAME_PLAYING) return;
    uint32_t tick = (uint32_t)w->tick;
    Vector3 origin = { 0.0f, 0.0f, 0.0f };
    if (w->collapsed) {
        w->state = GAME_LOST;
        w->state_tick = w->tick;
        events_push(&w->events, tick, EV_GAME_LOST, 0, origin, 0.0f);
    } else if (w->director.phase == DIRECTOR_DONE && enemies_alive(&w->enemies) == 0 && !boss_active(&w->boss)) {
        w->state = GAME_WON;
        w->state_tick = w->tick;
        events_push(&w->events, tick, EV_GAME_WON, 0, origin, 0.0f);
    }
}

// Deixa parado tudo que é desenhado com interpolação: com a posição anterior
// igual à atual, nada desliza na tela enquanto a simulação está congelada.
static void hold_still(World *w)
{
    w->ship.pos_prev = w->ship.pos;
    w->ship.rot_prev = w->ship.rot;
    Enemies *e = &w->enemies;
    for (int i = 0; i < e->pool.capacity; i++) {
        if (!pool_alive(&e->pool, i)) continue;
        e->items[i].pos_prev = e->items[i].pos;
        e->items[i].rot_prev = e->items[i].rot;
    }
    Projectiles *p = &w->shots;
    for (int i = 0; i < p->pool.capacity; i++) {
        if (pool_alive(&p->pool, i)) p->items[i].pos_prev = p->items[i].pos;
    }
    for (int i = 0; i < ALLIES_MAX; i++) {
        w->allies.items[i].pos_prev = w->allies.items[i].pos;
        w->allies.items[i].rot_prev = w->allies.items[i].rot;
    }
    w->boss.pos_prev = w->boss.pos;
    w->boss.rot_prev = w->boss.rot;
    for (int i = 0; i < ROCKS_MAX; i++) {
        w->rocks.items[i].pos_prev = w->rocks.items[i].pos;
        w->rocks.items[i].rot_prev = w->rocks.items[i].rot;
    }
    for (int i = 0; i < ORE_MAX; i++) w->rocks.ore[i].pos_prev = w->rocks.ore[i].pos;
    for (int i = 0; i < DEFENSES_MAX; i++) w->defenses.items[i].pos_prev = w->defenses.items[i].pos;
}

// ---------------------------------------------------------------------------
//  Rochas e conserto
// ---------------------------------------------------------------------------

#define REPAIR_SPARKS      36       // brilhos verdes quando uma rocha toca o chão
#define REPAIR_DONE_SPARKS 24       // ... e quando o conserto termina
#define ROCK_DUST          10
#define ROCK_HIT_JOLT      0.25f    // tremor por unidade de velocidade da batida numa rocha
#define ROCK_BOUNCE        0.4f
#define RESTORE_DOTS       34       // Feixe restaurador: pontos de luz da rocha até a cratera
#define RESTORE_DOT_SIZE   3.2f
#define RESTORE_DRIFT      0.6f     // ... que correm em direção à cratera (frações da distância por segundo)

static const Rgb8 REPAIR_GLOW  = { 120, 255, 170 };
static const Rgb8 REPAIR_SPARK = { 190, 255, 210 };
static const Rgb8 ROCK_DUST_COLOR = { 120, 108, 96 };
static const Rgb8 METEOR_EMBER    = { 255, 150, 60 };
static const Rgb8 ROCK_SPARK      = { 200, 180, 150 };
static const Rgb8 ORE_HOT         = { 255, 244, 200 };
static const Rgb8 ORE_GLOW        = { 255, 206, 90 };

// Uma direção perpendicular a `v` (comprimento 1), sorteada.
static Vector3 perpendicular(Rng *rng, Vector3 v)
{
    Vector3 p = Vector3CrossProduct(v, rng_unit_vec3(rng));
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(p) < 1e-6f) p = Vector3CrossProduct(v, (Vector3){ 1.0f, 0.0f, 0.0f });
    return Vector3Normalize(p);
}

void world_spawn_meteor(World *w, const Tuning *t)
{
    float radius = w->planet.params.radius;
    // Passa do lado da nave: o ponto mais perto do planeta fica a até
    // meteor_aim graus da direção dela, vista do centro.
    Vector3 side = (Vector3Length(w->ship.pos) > 1e-3f) ? Vector3Normalize(w->ship.pos) : rng_unit_vec3(&w->rng);
    float angle = rng_range(&w->rng, 0.0f, t->meteor_aim) * DEG2RAD;
    Vector3 dir = Vector3Normalize(Vector3RotateByAxisAngle(side, perpendicular(&w->rng, side), angle));
    Vector3 pass = Vector3Scale(dir, radius + rng_range(&w->rng, t->meteor_pass_min, t->meteor_pass_max));
    Vector3 travel = perpendicular(&w->rng, dir);
    Vector3 start = Vector3Subtract(pass, Vector3Scale(travel, t->meteor_distance * radius));
    Rock *k = rocks_spawn_meteor(&w->rocks, &w->rng, t, start, Vector3Scale(travel, t->meteor_speed));
    if (k != NULL) events_push(&w->events, (uint32_t)w->tick, EV_METEOR, 0, k->pos, k->radius);
}

Rock *world_spawn_rock(World *w, const Tuning *t, RockKind kind, Vector3 pos, Vector3 vel)
{
    if (kind == ROCK_METEOR) {
        Rock *k = rocks_spawn_meteor(&w->rocks, &w->rng, t, pos, vel);
        if (k != NULL) events_push(&w->events, (uint32_t)w->tick, EV_METEOR, 0, k->pos, k->radius);
        return k;
    }
    if (kind == ROCK_SPORE) return rocks_spawn_spore(&w->rocks, &w->rng, t, pos, vel);
    // Fragmento já em órbita, passando por `pos`.
    Rock *k = rocks_spawn_fragment(&w->rocks, &w->rng, t, w->planet.params.radius, pos, VMAT_GRASS);
    if (k == NULL) return NULL;
    k->state = ROCK_ORBIT;
    k->orbit = pos;
    k->orbit_rate = t->fragment_speed / fmaxf(Vector3Length(pos), 1.0f);
    return k;
}

// Meteoros: alguns em cada calmaria (um a cada calm_gap segundos) e, no meio
// das hordas, um a cada meteor_interval segundos.
static void tick_meteors(World *w, const Tuning *t, float dt)
{
    const WaveDirector *d = &w->director;
    if (d->phase == DIRECTOR_OFF || w->state != GAME_PLAYING) return;
    if (d->phase == DIRECTOR_PAUSE && d->wave != w->calm_seen) {
        w->calm_seen = d->wave;
        w->meteor_queue = t->meteor_calm_count;
        w->meteor_queue_wait = 1.0f;
    }
    if (w->meteor_queue > 0) {
        w->meteor_queue_wait -= dt;
        if (w->meteor_queue_wait <= 0.0f) {
            world_spawn_meteor(w, t);
            w->meteor_queue--;
            w->meteor_queue_wait = t->meteor_calm_gap;
        }
    }
    if (d->phase == DIRECTOR_ACTIVE) {
        w->meteor_wait -= dt;
        if (w->meteor_wait <= 0.0f) {
            world_spawn_meteor(w, t);
            w->meteor_wait = t->meteor_interval;
        }
    }
}

// F: puxa a rocha à frente; com uma a reboque, solta.
static void tick_tractor(World *w, const InputState *in, const Tuning *t)
{
    if (!(in->pressed & ACT_BIT(ACT_TRACTOR)) || !w->ship.alive) return;
    uint32_t tick = (uint32_t)w->tick;
    const Rock *towed = rocks_towed(&w->rocks);
    if (towed != NULL) {
        events_push(&w->events, tick, EV_TRACTOR_RELEASE, towed->kind, towed->pos, 0.0f);
        if (towed->kind == ROCK_SPORE) {
            // O esporo sai arremessado para onde o nariz aponta.
            Vector3 vel = Vector3Add(Vector3Scale(ship_forward(&w->ship), t->hive_spore_throw),
                                     Vector3Add(w->ship.vel, w->ship.fall));
            rocks_throw(&w->rocks, vel);
        } else {
            rocks_release(&w->rocks);
        }
        return;
    }
    int index = rocks_find_grab(&w->rocks, &w->ship, t);
    if (index < 0) {
        events_push(&w->events, tick, EV_TRACTOR_MISS, 0, w->ship.pos, 0.0f);
        return;
    }
    rocks_grab(&w->rocks, index);
    events_push(&w->events, tick, EV_TRACTOR_GRAB, w->rocks.items[index].kind, w->rocks.items[index].pos, 0.0f);
}

// Rochas que tocaram o chão começam um conserto; os que terminaram pagam.
static void tick_repairs(World *w, const Tuning *t, float dt)
{
    uint32_t tick = (uint32_t)w->tick;
    for (int i = 0; i < w->rocks.deposit_count; i++) {
        const RockDeposit *d = &w->rocks.deposits[i];
        repair_start(&w->repair, &w->planet, t, d->pos, d->fill);
        Vector3 up = Vector3Normalize(d->pos);
        particles_sparks(&w->particles, &w->rng, d->pos, up, REPAIR_SPARKS, d->fill * 1.2f, REPAIR_SPARK);
        spawn_glow(w, PARTICLE_RING, d->pos, (Vector3){ 0.0f, 0.0f, 0.0f }, d->fill * 0.5f, 0.7f, REPAIR_GLOW);
        spawn_glow(w, PARTICLE_FLASH, d->pos, (Vector3){ 0.0f, 0.0f, 0.0f }, d->fill * 0.35f, 0.25f, REPAIR_SPARK);
        for (int k = 0; k < ROCK_DUST; k++) {
            Vector3 dir = Vector3Normalize(Vector3Add(up, Vector3Scale(rng_unit_vec3(&w->rng), 0.8f)));
            spawn_glow(w, PARTICLE_DUST, d->pos, Vector3Scale(dir, rng_range(&w->rng, 2.0f, 6.0f)),
                       rng_range(&w->rng, 1.0f, 2.2f), rng_range(&w->rng, 0.8f, 1.6f), ROCK_DUST_COLOR);
        }
        events_push(&w->events, tick, EV_DEPOSIT, d->kind, d->pos, d->fill);
    }
    w->rocks.deposit_count = 0;

    repair_tick(&w->repair, &w->planet, t, dt);
    for (int i = 0; i < w->repair.finished_count; i++) {
        const RepairJob *j = &w->repair.finished[i];
        float fraction = (float)((double)j->restored / (double)w->planet.initial_volume);
        if (w->state == GAME_PLAYING && fraction > 0.0f) {
            float percent = fraction * 100.0f;
            score_bonus(&w->score, (int64_t)(percent * t->repair_points));
            world_give_xp(w, t, percent * t->repair_xp);
        }
        Vector3 up = Vector3Normalize(j->ground);
        particles_sparks(&w->particles, &w->rng, j->ground, up, REPAIR_DONE_SPARKS, j->radius * 0.8f, REPAIR_GLOW);
        events_push(&w->events, tick, EV_REPAIR, 0, j->ground, fraction);
    }
}

// A nave bate nas rochas soltas (não na que ela mesma reboca ou acabou de
// soltar): é empurrada para fora e, rápido demais, leva dano.
static void rock_collisions(World *w, const Tuning *t)
{
    Ship *s = &w->ship;
    if (!s->alive) return;
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive || k->state == ROCK_TOWED || k->state == ROCK_FALLING || k->state == ROCK_THROWN) continue;
        Vector3 away = Vector3Subtract(s->pos, k->pos);
        float distance = Vector3Length(away);
        float reach = k->radius + t->ship_radius;
        if (distance >= reach || distance < 1e-4f) continue;
        Vector3 n = Vector3Scale(away, 1.0f / distance);
        s->pos = Vector3Add(k->pos, Vector3Scale(n, reach));
        float closing = Vector3DotProduct(Vector3Subtract(Vector3Add(s->vel, s->fall), k->vel), n);
        if (closing >= 0.0f) continue;   // já se afastando
        s->vel = Vector3Subtract(s->vel, Vector3Scale(n, closing * (1.0f + ROCK_BOUNCE)));
        float speed = -closing;
        Vector3 point = Vector3Add(k->pos, Vector3Scale(n, k->radius));
        if (speed > t->rock_hit_speed) combat_damage_ship(w, t, t->rock_hit_damage, point);
        particles_sparks(&w->particles, &w->rng, point, n, 12, speed * 0.5f, ROCK_DUST_COLOR);
        world_jolt(w, speed * ROCK_HIT_JOLT);
        events_push(&w->events, (uint32_t)w->tick, EV_ROCK_HIT, k->kind, point, speed);
    }
}

// Feixe restaurador (sinergia Sifão + Escavador): a rocha quebrada a tiros
// também conserta a maior cratera aberta. Um rastro de luz verde vai da rocha
// até lá.
static void restore_beam(World *w, const Tuning *t, Vector3 from, float fill)
{
    int best = -1;
    for (int s = 0; s < REPAIR_SCARS; s++) {
        const Scar *scar = &w->repair.scars[s];
        if (scar->used && (best < 0 || scar->radius > w->repair.scars[best].radius)) best = s;
    }
    if (best < 0 || fill <= 0.0f) return;
    Vector3 to = repair_scar_point(&w->planet, &w->repair.scars[best]);
    if (!repair_start(&w->repair, &w->planet, t, to, fill)) return;
    Vector3 span = Vector3Subtract(to, from);
    for (int i = 0; i < RESTORE_DOTS; i++) {
        float u = (float)i / (float)(RESTORE_DOTS - 1);
        Vector3 drift = Vector3Scale(span, (1.0f - u) * RESTORE_DRIFT);   // os de trás correm atrás dos da frente
        spawn_glow(w, PARTICLE_TRAIL, Vector3Lerp(from, to, u), drift, RESTORE_DOT_SIZE, 0.35f + 0.5f * u, REPAIR_GLOW);
    }
    spawn_glow(w, PARTICLE_RING, to, (Vector3){ 0.0f, 0.0f, 0.0f }, fill * 0.5f, 0.7f, REPAIR_GLOW);
    events_push(&w->events, (uint32_t)w->tick, EV_RESTORE_BEAM, 0, to, fill);
}

void world_damage_rock(World *w, const Tuning *t, int index, float damage, Vector3 point, Vector3 back)
{
    if (index < 0 || index >= ROCKS_MAX || !w->rocks.items[index].alive) return;
    const Rock *k = &w->rocks.items[index];
    if (k->kind == ROCK_SPORE) {
        combat_spore_blast(w, t, index, false);   // um tiro estoura o esporo
        return;
    }
    uint8_t kind = k->kind;
    Vector3 pos = k->pos, vel = k->vel;
    float radius = k->radius, fill = k->fill;
    uint32_t tick = (uint32_t)w->tick;
    particles_sparks(&w->particles, &w->rng, point, back, 5, 10.0f, ROCK_SPARK);
    events_push(&w->events, tick, EV_ROCK_SHOT, kind, point, damage);
    if (!rocks_damage(&w->rocks, &w->rng, t, index, damage)) return;
    // Quebrou: estilhaços e os cristais (que a nave atrai de perto).
    particles_explosion(&w->particles, &w->rng, pos, vel, radius * 0.8f, ORE_HOT, ORE_GLOW, ROCK_DUST_COLOR);
    int count = (kind == ROCK_METEOR) ? t->ore_meteor : t->ore_fragment;
    events_push(&w->events, tick, EV_ROCK_MINED, kind, pos, (float)count);
    if (w->evo.rank[EVO_SYN_RESTORE] > 0) restore_beam(w, t, pos, fill * t->restore_fraction);
}

// Teclas 4, 5 e 6: compra uma defesa e a instala onde a nave está.
static void tick_shop(World *w, const InputState *in, const Tuning *t)
{
    if (!w->ship.alive) return;
    uint32_t tick = (uint32_t)w->tick;
    for (int k = 0; k < DEF_TYPE_COUNT; k++) {
        if (!(in->pressed & ACT_BIT(ACT_BUY1 + k))) continue;
        DefenseType type = (DefenseType)k;
        int cost = defense_cost(t, type);
        if (defenses_count(&w->defenses, type) >= defense_max(t, type)) {
            events_push(&w->events, tick, EV_BUY_FAIL, (uint8_t)k, w->ship.pos, 1.0f);
            continue;
        }
        if (w->ore < cost) {
            events_push(&w->events, tick, EV_BUY_FAIL, (uint8_t)k, w->ship.pos, 0.0f);
            continue;
        }
        Defense *f = defenses_build(&w->defenses, t, type, w->ship.pos, ship_forward(&w->ship), w->planet.params.radius,
                                    &w->rng);
        if (f == NULL) continue;
        w->ore -= cost;
        spawn_glow(w, PARTICLE_RING, f->pos, (Vector3){ 0.0f, 0.0f, 0.0f }, 3.0f, 0.6f, ORE_GLOW);
        events_push(&w->events, tick, EV_BUY, (uint8_t)k, f->pos, (float)cost);
    }
}

static void tick_defenses(World *w, const Tuning *t, float dt)
{
    DefenseContext ctx;
    ctx.planet_radius = w->planet.params.radius;
    ctx.enemies = &w->enemies;
    ctx.shots = &w->shots;
    ctx.events = &w->events;
    ctx.t = t;
    ctx.tick = (uint32_t)w->tick;
    ctx.dt = dt;
    defenses_tick(&w->defenses, &ctx);
}

static void tick_rocks(World *w, const Tuning *t, float dt)
{
    RockContext ctx;
    ctx.planet = &w->planet;
    ctx.ship = (w->ship_active && w->ship.alive) ? &w->ship : NULL;
    ctx.events = &w->events;
    ctx.t = t;
    ctx.tick = (uint32_t)w->tick;
    ctx.dt = dt;
    rocks_tick(&w->rocks, &ctx);
    if (w->rocks.collected > 0) {
        w->ore += w->rocks.collected;
        w->ore_total += w->rocks.collected;
        events_push(&w->events, (uint32_t)w->tick, EV_ORE, 0, w->ship.pos, (float)w->rocks.collected);
    }
    tick_repairs(w, t, dt);
    rock_collisions(w, t);

    // Brasas soltas pelos meteoros em voo.
    for (int i = 0; i < ROCKS_MAX; i++) {
        const Rock *k = &w->rocks.items[i];
        if (!k->alive || k->kind != ROCK_METEOR || k->state != ROCK_FLYBY) continue;
        float speed = Vector3Length(k->vel);
        if (speed < 1e-3f) continue;
        Vector3 back = Vector3Scale(k->vel, -1.0f / speed);
        Vector3 at = Vector3Add(k->pos, Vector3Add(Vector3Scale(back, k->radius * 0.8f),
                                                   Vector3Scale(rng_unit_vec3(&w->rng), k->radius * 0.5f)));
        Vector3 vel = Vector3Add(Vector3Scale(k->vel, 0.5f), Vector3Scale(back, rng_range(&w->rng, 2.0f, 6.0f)));
        spawn_glow(w, PARTICLE_EMBER, at, vel, rng_range(&w->rng, 0.25f, 0.55f), rng_range(&w->rng, 0.4f, 0.9f),
                   METEOR_EMBER);
    }
}

// ---------------------------------------------------------------------------
//  Evolução
// ---------------------------------------------------------------------------

#define CARDS_DELAY 0.45f   // segundos entre subir de nível e as cartas abrirem (dá para ver a explosão)

const Tuning *world_tuning(const World *w)
{
    return &w->eff;
}

// O que muda na hora em que a evolução é escolhida (o resto vale pelo tuning
// efetivo, a partir do próximo passo).
static void evolve_now(World *w, const Tuning *t, int id)
{
    Ship *s = &w->ship;
    if (id == EVO_HEAVY_HULL) s->hull += t->heavy_hull_hull;          // o casco novo já vem consertado
    if (id == EVO_BIG_SHIELD) s->shield += t->big_shield_shield;      // e o escudo novo, carregado
    events_push(&w->events, (uint32_t)w->tick, EV_EVOLVE, (uint8_t)id, s->pos, (float)w->evo.rank[id]);
}

// Cartas na tela: espera as teclas 1, 2 ou 3. Uma tecla já apertada quando
// as cartas abrem não conta: é preciso soltar e apertar de novo (sem isso,
// segurar uma tecla escolheria cartas sem querer).
static void choose_card(World *w, const InputState *in, const Tuning *t)
{
    const uint32_t keys = ACT_BIT(ACT_CARD1) | ACT_BIT(ACT_CARD2) | ACT_BIT(ACT_CARD3);
    if ((in->held & keys) == 0u) {
        w->evo.armed = true;
        return;
    }
    if (!w->evo.armed) return;
    for (int i = 0; i < EVO_CHOICES; i++) {
        if ((in->held & ACT_BIT(ACT_CARD1 + i)) == 0u) continue;
        int id = evolution_pick(&w->evo, t, &w->rng, i);
        if (id >= 0) evolve_now(w, t, id);
        return;
    }
}

// Quantos aliados a evolução dá, e a cadência deles.
static void tick_allies(World *w, const Tuning *t, float dt)
{
    const uint8_t *rank = w->evo.rank;
    allies_set_count(&w->allies, &w->ship, rank[EVO_ESCORT] + rank[EVO_HANGAR], rank[EVO_FIGHTERS]);
    if (allies_alive(&w->allies) == 0) return;
    AllyContext ctx;
    ctx.ship = &w->ship;
    ctx.planet = &w->planet;
    ctx.enemies = &w->enemies;
    ctx.shots = &w->shots;
    ctx.events = &w->events;
    ctx.rng = &w->rng;
    ctx.t = t;
    ctx.tick = (uint32_t)w->tick;
    ctx.dt = dt;
    ctx.rate_mult = 1.0f + t->hangar_rate * (float)rank[EVO_HANGAR];
    allies_tick(&w->allies, &ctx);
}

// Nível ganho: as cartas abrem um instante depois (e só com a partida andando).
static void open_cards(World *w, const Tuning *t)
{
    if (w->evo.pending <= 0 || w->evo.choosing || w->state != GAME_PLAYING) return;
    if (w->cards_at == 0) {
        w->cards_at = w->tick + (uint64_t)(CARDS_DELAY * (float)t->sim_hz);
    } else if (w->tick >= w->cards_at) {
        evolution_offer(&w->evo, t, &w->rng);
        w->cards_at = 0;
    }
}

void world_give_xp(World *w, const Tuning *t, float xp)
{
    int gained = evolution_gain(&w->evo, t, ENEMY_TYPE_COUNT, xp);   // sem tipo: só XP, nenhuma essência
    if (gained > 0) events_push(&w->events, (uint32_t)w->tick, EV_LEVEL_UP, 0, w->ship.pos, (float)w->evo.level);
}

void world_set_evolution(World *w, const Tuning *t, EvolutionId id, int rank)
{
    if ((int)id < 0 || id >= EVO_COUNT) return;
    if (rank < 0) rank = 0;
    if (rank > t->evo_max[id]) rank = t->evo_max[id];
    int before = w->evo.rank[id];
    w->evo.rank[id] = (uint8_t)rank;
    if (id == EVO_HEAVY_HULL) w->ship.hull += t->heavy_hull_hull * (float)(rank - before);
    if (id == EVO_BIG_SHIELD) w->ship.shield += t->big_shield_shield * (float)(rank - before);
}

// Guarda o que o jogador fez durante o congelamento, para valer depois dele:
// sem isso, um movimento de mouse ou um toque de tecla nesse instante se perderia.
static void hold_input(InputState *frozen, const InputState *in)
{
    frozen->held = in->held;
    frozen->pressed |= in->pressed;
    frozen->released |= in->released;
    frozen->look_dx += in->look_dx;
    frozen->look_dy += in->look_dy;
}

void world_tick(World *w, const InputState *in, const Tuning *base)
{
    // Daqui em diante vale o tuning com as evoluções. Uma rocha a reboque pesa:
    // a nave acelera menos e chega a uma velocidade menor.
    evolution_tune(&w->evo, base, &w->eff);
    float load = rocks_tow_load(&w->rocks, &w->eff);
    if (load > 0.0f) {
        float keep = 1.0f - w->eff.tractor_slow * load;
        w->eff.ship_speed_max *= keep;
        w->eff.ship_thrust *= keep;
    }
    const Tuning *t = &w->eff;
    float dt = 1.0f / (float)t->sim_hz;

    // Cartas de evolução na tela: a partida congela até a escolha.
    if (w->evo.choosing) {
        choose_card(w, in, t);
        w->view_prev = w->view;
        hold_still(w);
        w->moon.pos_prev = w->moon.pos;
        w->tick++;
        return;
    }

    // Hit-stop: nos golpes mais fortes o jogo congela por alguns passos. O
    // tempo de jogo continua (as animações e o tremor de câmera seguem).
    if (w->hitstop > 0) {
        w->hitstop--;
        hold_input(&w->frozen_input, in);
        w->view_prev = w->view;
        hold_still(w);
        w->moon.pos_prev = w->moon.pos;
        w->tick++;
        return;
    }
    InputState input = *in;
    input.pressed |= w->frozen_input.pressed;
    input.released |= w->frozen_input.released;
    input.look_dx += w->frozen_input.look_dx;
    input.look_dy += w->frozen_input.look_dy;
    memset(&w->frozen_input, 0, sizeof w->frozen_input);

    bool snap = false;
    w->view_prev = w->view;

    // F2 alterna entre a câmera livre e a câmera em que se estava.
    if (input.pressed & ACT_BIT(ACT_FREECAM)) {
        if (w->cam_mode != CAM_FREE) {
            enter_free_camera(w);   // parte de onde a outra câmera estava
        } else {
            w->cam_mode = w->cam_return;
            snap = true;            // corte seco: não interpolar através do salto
        }
    }

    moon_tick(&w->moon, t, w->planet.params.radius, dt);

    // A partida só anda na câmera de jogo. Nas outras ela fica congelada.
    if (world_in_play(w)) {
        ShipEvents ev = ship_tick(&w->ship, &input, &w->planet, &w->moon, t, dt);
        ship_effects(w, &ev);
        if (ev.dashed) combat_dash_blast(w, t);
        tick_tractor(w, &input, t);
        tick_shop(w, &input, t);
        combat_fire(w, &input, t, dt);
        combat_ability(w, &input, t, dt);
        combat_brood(w, &input, t, dt);
        combat_suicide(w, t, dt);
        combat_tunnel(w, &input, t, dt);
        tick_allies(w, t, dt);
        run_director(w, &input, t, dt);
        tick_meteors(w, t, dt);

        EnemyContext ctx = world_enemy_context(w, t);
        enemies_tick(&w->enemies, &ctx);
        for (int i = 0; i < w->enemies.carve_count; i++) {
            world_carve(w, t, w->enemies.carves[i].center, w->enemies.carves[i].radius);
        }
        w->enemies.carve_count = 0;
        tick_boss(w, t);
        tick_rocks(w, t, dt);
        combat_spores(w, t);
        tick_defenses(w, t, dt);
        projectiles_move(&w->shots, dt);
        combat_resolve(w, t);
        combat_boss(w, t);
        combat_sweep(w, t);
        open_cards(w, t);

        score_tick(&w->score, dt);
        w->hit_marker = fmaxf(w->hit_marker - dt, 0.0f);
        w->kill_marker = fmaxf(w->kill_marker - dt, 0.0f);

        tick_chase_camera(w, t, dt, snap || ev.respawned);
        if (ev.respawned) snap = true;
    } else {
        hold_still(w);
        if (w->cam_mode == CAM_ORBIT) {
            w->orbit_angle += t->orbit_speed * DEG2RAD * dt;
            w->view = orbit_pose(w->orbit_angle, t);
        } else if (w->cam_mode == CAM_FREE) {
            tick_free_camera(w, &input, t, dt);
        } else {
            tick_chase_camera(w, t, dt, snap);   // câmera de jogo com a nave fora de cena
        }
    }

    if (snap) w->view_prev = w->view;

    particles_tick(&w->particles, &w->planet, t, dt);
    check_collapse(w, t);
    update_game_state(w);
    w->tick++;
}

CamPose world_view(const World *w, float alpha)
{
    CamPose p;
    p.pos = Vector3Lerp(w->view_prev.pos, w->view.pos, alpha);
    p.rot = QuaternionSlerp(w->view_prev.rot, w->view.rot, alpha);
    p.fov = Lerp(w->view_prev.fov, w->view.fov, alpha);
    return p;
}

// FNV-1a de 64 bits. Cada campo entra separado (e não a struct inteira de uma
// vez) para os bytes de preenchimento entre campos não contaminarem o resultado.
static uint64_t hash_bytes(uint64_t h, const void *data, size_t len)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

#define HASH(h, field) ((h) = hash_bytes((h), &(field), sizeof(field)))

static uint64_t hash_vec3(uint64_t h, Vector3 v)
{
    HASH(h, v.x);
    HASH(h, v.y);
    HASH(h, v.z);
    return h;
}

static uint64_t hash_quat(uint64_t h, Quaternion q)
{
    HASH(h, q.x);
    HASH(h, q.y);
    HASH(h, q.z);
    HASH(h, q.w);
    return h;
}

uint64_t world_hash(const World *w)
{
    uint64_t h = 14695981039346656037ULL;
    int mode = (int)w->cam_mode;
    HASH(h, w->tick);
    HASH(h, w->rng.state);
    HASH(h, w->rng.inc);
    HASH(h, mode);
    HASH(h, w->orbit_angle);
    h = hash_vec3(h, w->free_vel);
    h = hash_vec3(h, w->view.pos);
    h = hash_quat(h, w->view.rot);
    HASH(h, w->view.fov);

    h = hash_vec3(h, w->ship.pos);
    h = hash_vec3(h, w->ship.vel);
    h = hash_vec3(h, w->ship.fall);
    h = hash_quat(h, w->ship.rot);
    HASH(h, w->ship.hull);
    HASH(h, w->ship.shield);
    HASH(h, w->ship.energy);
    HASH(h, w->ship.sling);
    HASH(h, w->jolt_count);
    HASH(h, w->moon.angle);

    HASH(h, w->planet.volume);
    HASH(h, w->planet.core_volume);
    HASH(h, w->impact_count);

    int state = (int)w->state;
    HASH(h, state);
    HASH(h, w->hitstop);
    HASH(h, w->events.count);
    HASH(h, w->score.points);
    HASH(h, w->score.kills);
    HASH(h, w->director.phase);
    HASH(h, w->director.wave);
    HASH(h, w->director.timer);
    HASH(h, w->enemies.pool.live);
    for (int i = 0; i < w->enemies.pool.capacity; i++) {
        if (!pool_alive(&w->enemies.pool, i)) continue;
        h = hash_vec3(h, w->enemies.items[i].pos);
        h = hash_quat(h, w->enemies.items[i].rot);   // a orientação decide de onde saem alguns tiros
        HASH(h, w->enemies.items[i].hp);
    }
    HASH(h, w->shots.pool.live);
    for (int i = 0; i < w->shots.pool.capacity; i++) {
        if (pool_alive(&w->shots.pool, i)) h = hash_vec3(h, w->shots.items[i].pos);
    }

    HASH(h, w->boss.state);
    if (boss_active(&w->boss)) {
        h = hash_vec3(h, w->boss.pos);
        HASH(h, w->boss.hp);
        HASH(h, w->boss.action);
        HASH(h, w->boss.breaker.cap_hp);
        if (w->boss.type == BOSS_HIVE) {
            HASH(h, w->boss.type);
            HASH(h, w->boss.hive.sac_hp);
            HASH(h, w->boss.hive.thr_hp);
            HASH(h, w->boss.hive.moon_scale);
            HASH(h, w->boss.hive.inflating);
        }
        if (w->boss.type == BOSS_DEVOURER) {
            HASH(h, w->boss.type);
            HASH(h, w->boss.dev.eaten);
            HASH(h, w->boss.dev.flinch);
            HASH(h, w->boss.phase);
        }
        if (w->boss.type == BOSS_WORM) {
            HASH(h, w->boss.type);
            HASH(h, w->boss.worm.nodule_hp);
            HASH(h, w->boss.worm.tail_hp);
            HASH(h, w->boss.worm.tunnel_count);
            h = hash_vec3(h, w->boss.worm.seg[WORM_SEGMENTS - 1]);
        }
    }
    // Só entram no resumo quando existem (os roteiros antigos não mudam de hash).
    if (w->moon.scale < 1.0f) HASH(h, w->moon.scale);
    if (w->brood.owned) HASH(h, w->brood.cooldown);
    HASH(h, w->ability.owned);
    HASH(h, w->evo.level);
    HASH(h, w->evo.xp);
    HASH(h, w->evo.rank);
    HASH(h, w->evo.choosing);
    for (int i = 0; i < ALLIES_MAX; i++) {
        if (w->allies.items[i].alive) h = hash_vec3(h, w->allies.items[i].pos);
    }
    HASH(h, w->rocks.towed);
    HASH(h, w->rocks.delivered);
    for (int i = 0; i < ROCKS_MAX; i++) {
        if (!w->rocks.items[i].alive) continue;
        h = hash_vec3(h, w->rocks.items[i].pos);
        HASH(h, w->rocks.items[i].state);
    }
    HASH(h, w->repair.restored_total);
    HASH(h, w->debris);
    HASH(h, w->ore);
    for (int i = 0; i < ORE_MAX; i++) {
        if (w->rocks.ore[i].alive) h = hash_vec3(h, w->rocks.ore[i].pos);
    }
    for (int i = 0; i < DEFENSES_MAX; i++) {
        const Defense *f = &w->defenses.items[i];
        if (!f->alive) continue;
        h = hash_vec3(h, f->pos);
        HASH(h, f->hp);
        HASH(h, f->wait);
    }

    HASH(h, w->particles.count);
    // As partículas entram no resumo pela soma das posições: barato, e
    // suficiente para denunciar qualquer diferença na simulação delas.
    Vector3 sum = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < w->particles.count; i++) sum = Vector3Add(sum, w->particles.items[i].pos);
    h = hash_vec3(h, sum);
    return h;
}
