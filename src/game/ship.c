#include "game/ship.h"

#include <math.h>
#include <string.h>

#include "voxel/damage.h"

// Direção, a partir do centro do planeta, em que a nave nasce.
static const Vector3 SPAWN_DIRECTION = { 0.2f, 0.35f, 1.0f };

#define BOOST_START_ENERGY 5.0f    // energia mínima para ligar o boost
#define OVERSPEED_DRAG     3.0f    // freio extra quando a nave passa da velocidade máxima
#define REVERSE_LIMIT      (-0.2f) // abaixo disto a nave está andando "de ré" em relação ao nariz
#define BLEND_SPEED        8.0f    // rapidez com que os efeitos de motor acendem e apagam
#define COLLISION_PASSES   4       // tentativas de desencostar do terreno por passo
#define COLLISION_SKIN     0.02f   // folga deixada ao desencostar
#define CONTACT_FRICTION   0.06f   // fração da velocidade perdida ao raspar no terreno
#define LEVEL_MIN_SIDE     0.2f    // voando quase na vertical, não há "para cima" a seguir
#define LEASH_RADII        9.0f    // além desta distância (em raios do planeta) a nave é trazida de volta
#define LEASH_PULL         0.6f    // força de volta por unidade de distância além do limite
#define LEASH_MAX_THRUSTS  1.5f    // teto dessa força, em "empuxos do motor"
#define SLING_FADE         1.5f    // nos últimos segundos, o ganho do estilingue vai sumindo
#define REVERSE_ENGAGE     2.0f    // abaixo desta velocidade para a frente, S passa de freio a ré

// O quanto a nave está em voo rasante, de 0 a 1: inteiro dentro da atmosfera,
// some aos poucos acima dela e também abaixo do nível do mar (dentro de
// crateras fundas e túneis o voo é livre).
static float graze_factor(const Tuning *t, float radius, float altitude)
{
    float air = radius * t->atmo_height;   // espessura da atmosfera
    if (air <= 0.0f) return 0.0f;
    float above = 1.0f - (altitude - air) / (air * t->ship_hug_fade);
    float below = 1.0f + altitude / air;
    return Clamp(fminf(above, below), 0.0f, 1.0f);
}

// Dados derivados da posição, usados pelo HUD, pela câmera e pelos efeitos.
static void update_status(Ship *s, const Tuning *t, const Planet *planet)
{
    float radius = planet->params.radius;
    float air = radius * t->atmo_height;
    s->altitude = Vector3Length(s->pos) - radius;
    s->in_atmosphere = s->altitude < air && s->altitude > -air;
    s->graze = graze_factor(t, radius, s->altitude);
}

static void reset(Ship *s, const Tuning *t, const Planet *planet, Vector3 pos, Quaternion rot)
{
    memset(s, 0, sizeof *s);
    s->pos = pos;
    s->rot = rot;
    s->pos_prev = pos;
    s->rot_prev = rot;
    s->hull = t->ship_hull_max;
    s->shield = t->ship_shield_max;
    s->energy = t->ship_energy_max;
    s->alive = true;
    update_status(s, t, planet);
}

void ship_spawn(Ship *s, const Tuning *t, const Planet *planet)
{
    Vector3 radial = Vector3Normalize(SPAWN_DIRECTION);
    Vector3 pos = Vector3Scale(radial, planet->params.radius * t->ship_spawn_distance);

    // De frente para o planeta, com o nariz um pouco acima do centro dele: a
    // primeira coisa que o jogador vê é a nave com o planeta inteiro ao fundo.
    Vector3 north = { 0.0f, 1.0f, 0.0f };
    Vector3 top = Vector3Normalize(Vector3Subtract(north, Vector3Scale(radial, Vector3DotProduct(north, radial))));
    float pitch = t->ship_spawn_pitch * DEG2RAD;
    Vector3 forward = Vector3Add(Vector3Scale(radial, -cosf(pitch)), Vector3Scale(top, sinf(pitch)));

    reset(s, t, planet, pos, quat_look(forward, top));
}

void ship_place(Ship *s, const Tuning *t, const Planet *planet, Vector3 pos, Vector3 forward, float speed)
{
    if (Vector3LengthSqr(forward) < 1e-8f) forward = (Vector3){ 0.0f, 0.0f, -1.0f };
    reset(s, t, planet, pos, quat_look(forward, pos));   // topo para longe do planeta
    s->vel = Vector3Scale(ship_forward(s), speed);
}

bool ship_damage(Ship *s, const Tuning *t, float amount)
{
    if (!s->alive || amount <= 0.0f || ship_invulnerable(s)) return false;

    s->shield_wait = t->ship_shield_delay;

    float absorbed = (amount < s->shield) ? amount : s->shield;
    s->shield -= absorbed;
    s->hull -= amount - absorbed;
    if (s->hull > 0.0f) return false;

    s->hull = 0.0f;
    s->alive = false;
    s->respawn_left = t->ship_respawn_time;
    s->vel = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->fall = (Vector3){ 0.0f, 0.0f, 0.0f };
    s->boosting = false;
    s->drifting = false;
    return true;
}

// Suavização exponencial: aproxima `value` de `target`, igual em qualquer dt.
static float approach(float value, float target, float speed, float dt)
{
    return value + (target - value) * (1.0f - expf(-speed * dt));
}

// --- giro -------------------------------------------------------------------
static void turn(Ship *s, const InputState *in, const Planet *planet, const Tuning *t, float dt)
{
    float max_rate = t->ship_turn_rate * DEG2RAD;
    float sens = t->ship_mouse_sens * DEG2RAD;

    // O mouse dá um ângulo por passo; dividido pelo passo vira velocidade de
    // giro. Sinais: girar em torno de +Y positivo vira para a esquerda, então
    // mouse para a direita é negativo; em torno de +X positivo levanta o
    // nariz, então mouse para baixo é negativo.
    float yaw = Clamp(-in->look_dx * sens / dt, -max_rate, max_rate);
    float pitch = Clamp(-in->look_dy * sens / dt, -max_rate, max_rate);

    float roll = 0.0f;
    if (in->held & ACT_BIT(ACT_ROLL_LEFT))  roll += t->ship_roll_rate * DEG2RAD;
    if (in->held & ACT_BIT(ACT_ROLL_RIGHT)) roll -= t->ship_roll_rate * DEG2RAD;

    // Sem rolagem manual, a nave se endireita sozinha: gira até o topo dela
    // apontar para longe do planeta. Quanto mais longe do planeta, mais fraco.
    if (roll == 0.0f && t->ship_auto_level > 0.0f) {
        Vector3 forward = ship_forward(s);
        float distance = Vector3Length(s->pos);
        float radius = planet->params.radius;
        float nearness = Clamp(1.0f - (distance - radius) / (radius * (t->ship_gravity_range - 1.0f)), 0.0f, 1.0f);

        if (distance > 1e-3f && nearness > 0.0f) {
            Vector3 away = Vector3Scale(s->pos, 1.0f / distance);
            // Parte de "para longe do planeta" que fica de lado em relação ao nariz.
            Vector3 side = Vector3Subtract(away, Vector3Scale(forward, Vector3DotProduct(away, forward)));
            if (Vector3Length(side) > LEVEL_MIN_SIDE) {
                side = Vector3Normalize(side);
                float error = atan2f(Vector3DotProduct(side, ship_right(s)), Vector3DotProduct(side, ship_up(s)));
                roll = Clamp(-error * t->ship_auto_level * nearness, -max_rate, max_rate);
            }
        }
    }

    Vector3 target = { pitch, yaw, roll };
    float blend = 1.0f - expf(-t->ship_turn_smooth * dt);
    s->turn = Vector3Lerp(s->turn, target, blend);

    // Multiplicar pela direita aplica o giro nos eixos da própria nave.
    Quaternion step = QuaternionFromEuler(s->turn.x * dt, s->turn.y * dt, s->turn.z * dt);
    s->rot = QuaternionNormalize(QuaternionMultiply(s->rot, step));
}

// --- colisão ----------------------------------------------------------------
// Pontos da nave onde a densidade é conferida, em eixos da nave e em raios
// de colisão: o centro, o nariz, a cauda, as pontas das asas, o topo e o fundo.
static const Vector3 PROBES[] = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, -1.85f },
    { 0.0f, 0.0f, 1.15f },
    { 1.55f, 0.0f, 0.85f },
    { -1.55f, 0.0f, 0.85f },
    { 0.0f, 0.55f, 0.35f },
    { 0.0f, -0.4f, 0.0f },
};
#define PROBE_COUNT ((int)(sizeof PROBES / sizeof PROBES[0]))

// A nave encostou em uma superfície de normal `normal`, no ponto `point`;
// `surface_vel` é a velocidade da própria superfície (a Lua anda). Registra a
// batida e tira da nave o movimento que ia contra a superfície.
static void bounce_off(Ship *s, const Tuning *t, Vector3 normal, Vector3 point, Vector3 surface_vel, ShipEvents *ev)
{
    // Velocidade com que a nave vinha contra a superfície (voo + queda).
    Vector3 relative = Vector3Subtract(Vector3Add(s->vel, s->fall), surface_vel);
    float into = Vector3DotProduct(relative, normal);
    if (into >= 0.0f) return;
    if (-into > ev->hit_speed) {
        ev->hit = true;
        ev->hit_speed = -into;
        ev->hit_point = point;
        ev->hit_normal = normal;
    }

    // A queda para ao tocar o chão. Do voo, sai a parte que ia contra a
    // superfície e volta um pouco dela (o quique).
    float fall_into = Vector3DotProduct(s->fall, normal);
    if (fall_into < 0.0f) s->fall = Vector3Subtract(s->fall, Vector3Scale(normal, fall_into));

    float vel_into = Vector3DotProduct(s->vel, normal);
    if (vel_into < 0.0f) {
        s->vel = Vector3Subtract(s->vel, Vector3Scale(normal, vel_into * (1.0f + t->ship_bounce)));
        s->vel = Vector3Scale(s->vel, 1.0f - CONTACT_FRICTION);
    }
}

// A Lua é uma esfera lisa: basta comparar distâncias.
static void collide_moon(Ship *s, const Moon *moon, const Tuning *t, float dt, ShipEvents *ev)
{
    Vector3 offset = Vector3Subtract(s->pos, moon->pos);
    float gap = Vector3Length(offset);
    float closest = moon->radius + t->ship_radius;
    if (gap >= closest || gap < 1e-4f) return;

    Vector3 normal = Vector3Scale(offset, 1.0f / gap);
    s->pos = Vector3Add(moon->pos, Vector3Scale(normal, closest + COLLISION_SKIN));
    Vector3 moon_vel = Vector3Scale(Vector3Subtract(moon->pos, moon->pos_prev), 1.0f / dt);
    bounce_off(s, t, normal, Vector3Add(moon->pos, Vector3Scale(normal, moon->radius)), moon_vel, ev);
}

static void collide(Ship *s, const Planet *planet, const Tuning *t, ShipEvents *ev)
{
    for (int pass = 0; pass < COLLISION_PASSES; pass++) {
        // Acha o ponto que está mais fundo dentro da matéria. Perto da
        // superfície a densidade cresce em proporção à profundidade, então
        // ela mesma diz o quanto o ponto entrou.
        float deepest = 0.0f;
        Vector3 deepest_point = s->pos;
        for (int i = 0; i < PROBE_COUNT; i++) {
            Vector3 local = Vector3Scale(PROBES[i], t->ship_radius);
            Vector3 point = Vector3Add(s->pos, Vector3RotateByQuaternion(local, s->rot));
            float depth = (planet_sample(planet, point) - VOXEL_ISO) / VOXEL_DENSITY_PER_UNIT;
            if (depth > deepest) {
                deepest = depth;
                deepest_point = point;
            }
        }
        if (deepest <= 0.0f) return;   // livre

        Vector3 normal = planet_normal(planet, deepest_point);
        s->pos = Vector3Add(s->pos, Vector3Scale(normal, deepest + COLLISION_SKIN));
        bounce_off(s, t, normal, deepest_point, (Vector3){ 0.0f, 0.0f, 0.0f }, ev);
    }
}

// --- passo ------------------------------------------------------------------
ShipEvents ship_tick(Ship *s, const InputState *in, const Planet *planet, const Moon *moon, const Tuning *t,
                     float dt)
{
    ShipEvents ev;
    memset(&ev, 0, sizeof ev);

    s->pos_prev = s->pos;
    s->rot_prev = s->rot;

    if (!s->alive) {
        s->respawn_left -= dt;
        if (s->respawn_left <= 0.0f) {
            ship_spawn(s, t, planet);
            ev.respawned = true;
        }
        return ev;
    }

    if (s->dash_left > 0.0f) s->dash_left -= dt;
    if (s->dash_wait > 0.0f) s->dash_wait -= dt;

    turn(s, in, planet, t, dt);
    Vector3 forward = ship_forward(s);
    Vector3 right = ship_right(s);

    // --- motores -------------------------------------------------------------
    // Se a energia acaba no meio do boost, ele só volta depois que o jogador
    // solta a tecla. Sem essa trava, segurar Shift com a energia no fim faria
    // o boost ligar e desligar várias vezes por segundo.
    bool wants_boost = (in->held & ACT_BIT(ACT_BOOST)) != 0u;
    if (!wants_boost) s->boost_spent = false;
    if (s->boosting && s->energy <= 0.0f) s->boost_spent = true;
    s->boosting = wants_boost && !s->boost_spent && (s->boosting || s->energy >= BOOST_START_ENERGY);
    s->drifting = (in->held & ACT_BIT(ACT_DRIFT)) != 0u;
    bool thrusting = s->boosting || (in->held & ACT_BIT(ACT_FORWARD)) != 0u;
    float speed_before = Vector3Length(s->vel);

    // S freia; com a nave já quase parada (ou andando para trás), dá ré.
    bool backing = (in->held & ACT_BIT(ACT_BACK)) != 0u;
    s->reversing = backing && !thrusting && Vector3DotProduct(s->vel, forward) < REVERSE_ENGAGE;

    // Estabilizadores: sem nenhum comando de movimento e devagar, a nave para
    // sozinha e fica no lugar, sem afundar. Em alta velocidade eles não
    // agem, e a nave segue pela inércia. No drift ficam desligados.
    const uint32_t moving = ACT_BIT(ACT_FORWARD) | ACT_BIT(ACT_BACK) | ACT_BIT(ACT_LEFT) | ACT_BIT(ACT_RIGHT)
                          | ACT_BIT(ACT_BOOST);
    bool idle = (in->held & moving) == 0u && !s->drifting;
    s->hover = idle ? Clamp(1.0f - speed_before / t->ship_hover_speed, 0.0f, 1.0f) : 0.0f;

    // --- Lua: puxão e estilingue ---------------------------------------------
    // Perto da Lua ela puxa a nave, o que curva a passagem. E quem passa em
    // velocidade arma o estilingue: a velocidade máxima fica maior enquanto a
    // nave está por perto e por mais alguns segundos depois de se afastar.
    s->near_moon = false;
    if (moon != NULL) {
        Vector3 to_moon = Vector3Subtract(moon->pos, s->pos);
        float gap = Vector3Length(to_moon);
        float reach = moon->radius * t->moon_sling_range;
        if (gap < reach && gap > 1e-3f) {
            s->near_moon = true;
            float nearness = Clamp((reach - gap) / (reach - moon->radius), 0.0f, 1.0f);
            // Como a gravidade do planeta, entra na velocidade de queda (e os
            // estabilizadores também a anulam).
            float pull = t->moon_gravity * nearness * (1.0f - s->hover);
            s->fall = Vector3Add(s->fall, Vector3Scale(to_moon, pull * dt / gap));
            if (speed_before >= t->ship_speed_max * t->moon_sling_speed) s->sling = t->moon_sling_time;
        }
    }
    if (!s->near_moon && s->sling > 0.0f) s->sling -= dt;
    float sling = (t->moon_sling_boost - 1.0f) * Clamp(s->sling / SLING_FADE, 0.0f, 1.0f);

    // O boost e o estilingue somam: os dois juntos são o mais rápido que a nave voa.
    float boost = (s->boosting ? t->ship_boost_mult : 1.0f) + sling;
    Vector3 accel = { 0.0f, 0.0f, 0.0f };
    if (thrusting) accel = Vector3Add(accel, Vector3Scale(forward, t->ship_thrust * boost));
    if (s->reversing) accel = Vector3Subtract(accel, Vector3Scale(forward, t->ship_reverse_thrust));
    if (in->held & ACT_BIT(ACT_RIGHT)) accel = Vector3Add(accel, Vector3Scale(right, t->ship_strafe));
    if (in->held & ACT_BIT(ACT_LEFT))  accel = Vector3Subtract(accel, Vector3Scale(right, t->ship_strafe));

    float distance = Vector3Length(s->pos);
    float radius = planet->params.radius;
    if (distance > 1e-3f) {
        Vector3 down = Vector3Scale(s->pos, -1.0f / distance);

        // Gravidade do planeta: máxima junto à superfície, zero a partir de
        // gravity_range raios. Ela não entra na velocidade de voo, e sim em
        // uma velocidade de queda separada, com arrasto próprio. Se entrasse
        // na de voo, a aderência (mais abaixo) a redirecionaria para a frente
        // e a nave parada sairia planando em vez de descer. Assim, sem motor
        // a nave afunda devagar, até uma velocidade limite baixa.
        float reach = radius * t->ship_gravity_range;
        float pull = t->ship_gravity * Clamp((reach - distance) / (reach - radius), 0.0f, 1.0f);

        // Sustentação: voando rápido ao longo da superfície a nave não
        // afunda, como um avião. A gravidade só pesa em quem está parado,
        // lento, ou subindo e descendo na vertical.
        Vector3 across = Vector3Subtract(s->vel, Vector3Scale(down, Vector3DotProduct(s->vel, down)));
        pull *= 1.0f - Clamp(Vector3Length(across) / t->ship_lift_speed, 0.0f, 1.0f);
        pull *= 1.0f - s->hover;   // os estabilizadores seguram a nave no lugar

        s->fall = Vector3Add(s->fall, Vector3Scale(down, pull * dt));

        // Muito longe do planeta, uma força traz a nave de volta. Cresce com
        // a distância, mas tem teto (maior que o do motor), para não virar
        // um elástico que arremessa a nave de volta.
        float leash = radius * LEASH_RADII;
        if (distance > leash) {
            float back = fminf((distance - leash) * LEASH_PULL, t->ship_thrust * LEASH_MAX_THRUSTS);
            accel = Vector3Add(accel, Vector3Scale(down, back));
        }
    }
    float fall_keep = 1.0f - (t->ship_fall_drag + s->hover * t->ship_hover_drag) * dt;
    s->fall = Vector3Scale(s->fall, (fall_keep > 0.0f) ? fall_keep : 0.0f);

    s->vel = Vector3Add(s->vel, Vector3Scale(accel, dt));

    // --- freio, arrasto e aderência -----------------------------------------
    float keep = 1.0f - (s->drifting ? t->ship_drift_drag : t->ship_drag) * dt;
    if (backing && !s->reversing) keep -= t->ship_brake * dt;
    keep -= s->hover * t->ship_hover_drag * dt;
    s->vel = Vector3Scale(s->vel, (keep > 0.0f) ? keep : 0.0f);

    float speed = Vector3Length(s->vel);
    if (!s->drifting && speed > 0.05f) {
        // Aderência: o rumo gira em direção ao nariz sem perder velocidade.
        // É o que faz "virar a nave" virar o voo.
        Vector3 heading = Vector3Scale(s->vel, 1.0f / speed);
        float grip = Clamp(t->ship_grip * dt, 0.0f, 1.0f);
        if (Vector3DotProduct(heading, forward) > REVERSE_LIMIT) {
            heading = Vector3Normalize(Vector3Lerp(heading, forward, grip));
            s->vel = Vector3Scale(heading, speed);
        } else {
            // Andando de ré (depois de um drift, por exemplo): só a parte
            // lateral da velocidade é amortecida, sem inverter o movimento.
            Vector3 along = Vector3Scale(forward, Vector3DotProduct(s->vel, forward));
            s->vel = Vector3Add(along, Vector3Scale(Vector3Subtract(s->vel, along), 1.0f - grip));
        }
    }

    // Velocidade máxima. O motor nunca empurra a nave além dela. Se a nave já
    // vinha mais rápida (fim do boost, depois de um dash), o excesso some aos
    // poucos em vez de bater em uma parede invisível. Durante o dash não há
    // limite.
    float max_speed = t->ship_speed_max * boost;
    float limit = max_speed;
    if (speed_before > max_speed) limit += (speed_before - max_speed) * expf(-OVERSPEED_DRAG * dt);
    speed = Vector3Length(s->vel);
    if (speed > limit && s->dash_left <= 0.0f) s->vel = Vector3Scale(s->vel, limit / speed);

    // De ré, o limite é bem menor: dá para recuar e manobrar, não para fugir.
    float backward = -Vector3DotProduct(s->vel, forward);
    if (s->reversing && backward > t->ship_reverse_max && s->dash_left <= 0.0f) {
        s->vel = Vector3Add(s->vel, Vector3Scale(forward, backward - t->ship_reverse_max));
    }

    // --- dash ----------------------------------------------------------------
    if ((in->pressed & ACT_BIT(ACT_DASH)) && s->dash_wait <= 0.0f && s->energy >= t->ship_dash_cost) {
        // Vai para onde o jogador está empurrando: de lado, para trás, ou em frente.
        Vector3 dir = forward;
        if (in->held & ACT_BIT(ACT_RIGHT))     dir = right;
        else if (in->held & ACT_BIT(ACT_LEFT)) dir = Vector3Negate(right);
        else if (in->held & ACT_BIT(ACT_BACK)) dir = Vector3Negate(forward);

        s->vel = Vector3Add(s->vel, Vector3Scale(dir, t->ship_dash_speed));
        s->dash_left = t->ship_dash_time;
        s->dash_wait = t->ship_dash_cooldown;
        s->energy -= t->ship_dash_cost;
        ev.dashed = true;
        ev.dash_dir = dir;
    }

    // --- energia e escudo ----------------------------------------------------
    if (s->boosting) {
        s->energy -= t->ship_boost_cost * dt;
        if (s->energy < 0.0f) s->energy = 0.0f;
    } else {
        s->energy += t->ship_energy_regen * dt;
        if (s->energy > t->ship_energy_max) s->energy = t->ship_energy_max;
    }

    // No voo rasante o escudo recarrega mais rápido: é a recompensa por
    // arriscar voar baixo.
    if (s->shield_wait > 0.0f) {
        s->shield_wait -= dt;
    } else if (s->shield < t->ship_shield_max) {
        float regen = t->ship_shield_regen * (s->in_atmosphere ? t->ship_graze_shield : 1.0f);
        s->shield += regen * dt;
        if (s->shield > t->ship_shield_max) s->shield = t->ship_shield_max;
    }
    s->hit_flash = fmaxf(s->hit_flash - dt, 0.0f);

    // --- movimento e colisão -------------------------------------------------
    // Em alta velocidade o passo é dividido, para a nave não atravessar uma
    // parede fina entre uma conferência e outra.
    float travel = Vector3Length(Vector3Add(s->vel, s->fall)) * dt;
    int substeps = 1 + (int)(travel / (t->ship_radius * 0.75f));
    if (substeps > 8) substeps = 8;
    for (int i = 0; i < substeps; i++) {
        Vector3 total = Vector3Add(s->vel, s->fall);   // relido a cada volta: a colisão muda os dois
        s->pos = Vector3Add(s->pos, Vector3Scale(total, dt / (float)substeps));
        collide(s, planet, t, &ev);
        if (moon != NULL) collide_moon(s, moon, t, dt, &ev);
    }

    if (ev.hit && ev.hit_speed > t->ship_crash_min) {
        ev.died = ship_damage(s, t, (ev.hit_speed - t->ship_crash_min) * t->ship_crash_damage);
    }

    // --- voo rasante: acompanhar a curvatura ---------------------------------
    // Em um planeta deste tamanho, voar em linha reta junto ao chão é sair
    // pela tangente em um segundo. Então, dentro da atmosfera, a nave gira
    // junto com o "para cima" do lugar por onde passa: o mesmo giro que leva
    // a vertical de onde ela estava à vertical de onde está agora é aplicado
    // à orientação e à velocidade. Voar nivelado passa a contornar o planeta.
    // Quem sobe ou desce na vertical não sente nada, porque a vertical não muda.
    update_status(s, t, planet);
    float hug = t->ship_hug * s->graze;
    float distance_now = Vector3Length(s->pos);
    if (s->alive && hug > 0.0f && distance > 1e-3f && distance_now > 1e-3f) {
        Vector3 before = Vector3Scale(s->pos_prev, 1.0f / distance);
        Vector3 after = Vector3Scale(s->pos, 1.0f / distance_now);
        Quaternion bend = QuaternionFromVector3ToVector3(before, after);
        bend = QuaternionSlerp(QuaternionIdentity(), bend, hug);
        s->rot = QuaternionNormalize(QuaternionMultiply(bend, s->rot));
        s->vel = Vector3RotateByQuaternion(s->vel, bend);
        s->fall = Vector3RotateByQuaternion(s->fall, bend);

        // O passo foi dado em linha reta, e uma reta tangente ao planeta
        // sempre se afasta dele um pouco. Em um arco, a distância ao centro
        // só mudaria pelo quanto a nave subiu ou desceu de propósito; a
        // posição é trazida de volta para essa distância.
        float climbed = Vector3DotProduct(Vector3Subtract(s->pos, s->pos_prev), before);
        float on_arc = distance + climbed;
        if (on_arc > 1e-3f) s->pos = Vector3Scale(after, Lerp(distance_now, on_arc, hug));
        update_status(s, t, planet);
    }

    // --- estado para os efeitos ----------------------------------------------
    s->boost_blend = approach(s->boost_blend, s->boosting ? 1.0f : 0.0f, BLEND_SPEED, dt);
    s->sling_blend = approach(s->sling_blend, Clamp(s->sling / SLING_FADE, 0.0f, 1.0f), BLEND_SPEED, dt);
    s->thrust_blend = approach(s->thrust_blend, thrusting ? 1.0f : 0.0f, BLEND_SPEED, dt);
    return ev;
}
