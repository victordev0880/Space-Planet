#include "raylib.h"

#include "audio/audio.h"

#include <math.h>
#include <string.h>

#include "core/vmath.h"

#define SAMPLE_RATE  44100
#define MAX_SAMPLES  (SAMPLE_RATE * 26 / 10)   // o som mais longo tem 2,6 segundos
#define PER_FRAME    2       // no máximo este tanto de cópias do mesmo som por quadro
#define PITCH_SPREAD 0.06f   // variação aleatória de tom, para repetições não soarem iguais

// ---------------------------------------------------------------------------
//  Síntese
// ---------------------------------------------------------------------------

static float buffer[MAX_SAMPLES];
static uint32_t synth_seed = 12345u;

static float noise(void)
{
    synth_seed = synth_seed * 1664525u + 1013904223u;
    return (float)(synth_seed >> 8) / 8388608.0f - 1.0f;
}

static float square(float phase)  { return (fmodf(phase, 1.0f) < 0.5f) ? 1.0f : -1.0f; }
static float saw(float phase)     { return 2.0f * fmodf(phase, 1.0f) - 1.0f; }
static float sine(float phase)    { return sinf(2.0f * PI * phase); }

// Envelope: sobe em `attack` segundos e decai exponencialmente com `decay`.
static float envelope(float time, float attack, float decay)
{
    float rise = (attack > 0.0f) ? fminf(time / attack, 1.0f) : 1.0f;
    return rise * expf(-time * decay);
}

// Frequência que desliza exponencialmente de `from` a `to` ao longo de `length` segundos.
static float sweep(float from, float to, float time, float length)
{
    return from * powf(to / from, fminf(time / length, 1.0f));
}

// Filtro passa-baixa de um polo: corta o que for mais agudo que `cutoff` Hz.
typedef struct { float state; } LowPass;
static float lowpass(LowPass *f, float in, float cutoff)
{
    float k = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);
    f->state += (in - f->state) * k;
    return f->state;
}

typedef void (*SynthFn)(float *out, int count);

static void synth_shot(float *out, int n)
{
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(1600.0f, 420.0f, t, 0.10f) / SAMPLE_RATE;
        float click = (t < 0.008f) ? noise() * 0.6f : 0.0f;
        out[i] = (0.65f * sine(phase) + 0.25f * square(phase) + click) * envelope(t, 0.002f, 32.0f);
    }
}

static void synth_enemy_shot(float *out, int n)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float wobble = 1.0f + 0.08f * sinf(2.0f * PI * 32.0f * t);
        phase += sweep(760.0f, 220.0f, t, 0.16f) * wobble / SAMPLE_RATE;
        out[i] = lowpass(&lp, saw(phase), 2200.0f) * envelope(t, 0.004f, 16.0f);
    }
}

static void synth_hit(float *out, int n)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = noise();
        float bright = x - lowpass(&lp, x, 2500.0f);   // só o ruído agudo
        phase += 2100.0f / SAMPLE_RATE;
        out[i] = (0.7f * bright + 0.5f * sine(phase)) * envelope(t, 0.001f, 45.0f);
    }
}

static void synth_armor(float *out, int n)
{
    // "Clang": frequências que não são múltiplas uma da outra soam metálicas.
    static const float PARTIALS[3] = { 1180.0f, 1730.0f, 2650.0f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = 0.0f;
        for (int k = 0; k < 3; k++) v += sine(PARTIALS[k] * t) * expf(-t * (22.0f + 10.0f * (float)k));
        float click = (t < 0.004f) ? noise() : 0.0f;
        out[i] = (0.4f * v + 0.4f * click) * envelope(t, 0.001f, 6.0f);
    }
}

static void synth_weak(float *out, int n)
{
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(1500.0f, 2600.0f, t, 0.08f) / SAMPLE_RATE;
        out[i] = (0.6f * sine(phase) + 0.3f * sine(phase * 1.5f)) * envelope(t, 0.002f, 16.0f);
    }
}

// Explosão: ruído cada vez mais grave, um "tum" embaixo e estalos.
static void explosion(float *out, int n, float bright, float decay, float thump, float crackle)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float cutoff = sweep(bright, 90.0f, t, length);
        float body = lowpass(&lp, noise(), cutoff) * 2.2f;
        phase += sweep(thump, thump * 0.45f, t, length * 0.5f) / SAMPLE_RATE;
        float sub = sine(phase) * expf(-t * 7.0f);
        float pop = (noise() > 1.0f - crackle * expf(-t * 4.0f)) ? noise() : 0.0f;
        out[i] = (body + 0.7f * sub + 0.5f * pop) * envelope(t, 0.003f, decay);
    }
}

static void synth_kill_small(float *out, int n) { explosion(out, n, 3200.0f, 7.0f, 95.0f, 0.004f); }
static void synth_kill_big(float *out, int n)   { explosion(out, n, 2400.0f, 2.6f, 62.0f, 0.02f); }
static void synth_bomb_down(float *out, int n)  { explosion(out, n, 4200.0f, 6.0f, 120.0f, 0.01f); }
static void synth_impact_small(float *out, int n) { explosion(out, n, 900.0f, 6.0f, 80.0f, 0.0f); }
static void synth_impact_big(float *out, int n)   { explosion(out, n, 700.0f, 1.9f, 48.0f, 0.006f); }

static void synth_bomb_arm(float *out, int n)
{
    // Três bipes alternando duas notas: o alarme de bomba.
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        int beep = (int)(t / 0.13f);
        float inside = fmodf(t, 0.13f);
        float on = (beep < 3 && inside < 0.09f) ? 1.0f : 0.0f;
        phase += ((beep % 2 == 0) ? 880.0f : 1320.0f) / SAMPLE_RATE;
        out[i] = lowpass(&lp, square(phase), 3500.0f) * on * 0.6f;
    }
}

static void synth_bomb_drop(float *out, int n)
{
    // Assobio descendo: a bomba caindo.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float vibrato = 1.0f + 0.01f * sinf(2.0f * PI * 6.0f * t);
        phase += sweep(1500.0f, 480.0f, t, 1.0f) * vibrato / SAMPLE_RATE;
        out[i] = sine(phase) * envelope(t, 0.05f, 1.8f) * 0.7f;
    }
}

static void synth_shield(float *out, int n)
{
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(700.0f, 1900.0f, t, 0.2f) / SAMPLE_RATE;
        out[i] = (0.6f * sine(phase) + 0.25f * noise()) * envelope(t, 0.002f, 12.0f);
    }
}

static void synth_hull(float *out, int n)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(150.0f, 60.0f, t, 0.25f) / SAMPLE_RATE;
        float crunch = lowpass(&lp, noise(), sweep(1800.0f, 300.0f, t, 0.3f)) * 2.0f;
        out[i] = (crunch + 0.6f * square(phase)) * envelope(t, 0.002f, 9.0f) * 0.7f;
    }
}

static void synth_player_down(float *out, int n)
{
    explosion(out, n, 2600.0f, 2.2f, 58.0f, 0.02f);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(620.0f, 70.0f, t, 1.4f) / SAMPLE_RATE;
        out[i] += 0.35f * saw(phase) * envelope(t, 0.01f, 2.0f);
    }
}

static void synth_horn(float *out, int n)
{
    // Trompa grave e ameaçadora, em quinta, com tremolo: a horda chegando.
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = saw(110.0f * t) + 0.8f * saw(164.8f * t) + 0.5f * saw(55.0f * t);
        float tremolo = 0.8f + 0.2f * sinf(2.0f * PI * 5.0f * t);
        float shape = fminf(t / 0.18f, 1.0f) * fminf((length - t) / 0.5f, 1.0f);
        out[i] = lowpass(&lp, v, 900.0f) * tremolo * shape * 0.45f;
    }
}

// Notas que entram uma depois da outra e ressoam.
static void arpeggio(float *out, int n, const float *notes, int count, float gap, float decay)
{
    memset(out, 0, sizeof(float) * (size_t)n);
    for (int k = 0; k < count; k++) {
        for (int i = (int)(gap * (float)k * SAMPLE_RATE); i < n; i++) {
            float t = (float)i / SAMPLE_RATE - gap * (float)k;
            float v = sine(notes[k] * t) + 0.3f * sine(notes[k] * 2.0f * t);
            out[i] += v * envelope(t, 0.004f, decay) * 0.45f;
        }
    }
}

static void synth_chime(float *out, int n)
{
    static const float NOTES[3] = { 659.3f, 880.0f, 1318.5f };
    arpeggio(out, n, NOTES, 3, 0.11f, 4.5f);
}

static void synth_win(float *out, int n)
{
    static const float NOTES[5] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1318.5f };
    arpeggio(out, n, NOTES, 5, 0.14f, 1.8f);
}

static void synth_lose(float *out, int n)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(220.0f, 55.0f, t, 2.2f) / SAMPLE_RATE;
        float v = saw(phase) + 0.6f * saw(phase * 1.19f) + 0.4f * noise();
        out[i] = lowpass(&lp, v, sweep(1200.0f, 150.0f, t, 2.2f)) * envelope(t, 0.05f, 1.0f) * 0.6f;
    }
}

static void synth_dash(float *out, int n)
{
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float cutoff = 300.0f + 3500.0f * sinf(PI * fminf(t / length, 1.0f));
        out[i] = lowpass(&lp, noise(), cutoff) * 1.6f * envelope(t, 0.03f, 6.0f);
    }
}

static void synth_bump(float *out, int n)
{
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(130.0f, 55.0f, t, 0.15f) / SAMPLE_RATE;
        out[i] = (lowpass(&lp, noise(), 600.0f) * 1.5f + sine(phase)) * envelope(t, 0.002f, 14.0f) * 0.8f;
    }
}

static void synth_warp(float *out, int n)
{
    // Chegada de um grupo: um brilho subindo, com ruído arejado.
    float phase = 0.0f;
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(180.0f, 1400.0f, t, length) / SAMPLE_RATE;
        float air = lowpass(&lp, noise(), 2500.0f);
        float shape = fminf(t / (length * 0.7f), 1.0f) * fminf((length - t) / 0.08f, 1.0f);
        out[i] = (0.45f * sine(phase) + 0.25f * sine(phase * 1.5f) + 0.6f * air) * shape * 0.6f;
    }
}

static void synth_lock(float *out, int n)
{
    // Kamikaze travou: guincho agudo em dois toques rápidos, subindo.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float inside = fmodf(t, 0.11f);
        float on = (t < 0.22f && inside < 0.08f) ? 1.0f : 0.0f;
        phase += sweep(1300.0f, 2400.0f, inside, 0.08f) / SAMPLE_RATE;
        out[i] = (0.6f * square(phase) + 0.4f * sine(phase * 0.5f)) * on * 0.5f;
    }
}

static void synth_detonate(float *out, int n)
{
    // Explosão seca e brilhante, com muitos estalos.
    explosion(out, n, 5200.0f, 4.0f, 110.0f, 0.03f);
}

static void synth_dig(float *out, int n)
{
    // Broca mordendo a rocha: ruído grave pulsando rápido, com um ronco.
    LowPass lp = { 0 };
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float chop = 0.55f + 0.45f * square(38.0f * t);
        float grind = lowpass(&lp, noise(), 900.0f) * 2.4f * chop;
        phase += sweep(70.0f, 52.0f, t, length) / SAMPLE_RATE;
        float shape = fminf(t / 0.05f, 1.0f) * fminf((length - t) / 0.3f, 1.0f);
        out[i] = (grind + 0.5f * saw(phase)) * shape;
    }
}

static void synth_drain(float *out, int n)
{
    // Feixe do Sifão: um zumbido que oscila e desce, como algo sendo sugado.
    float phase = 0.0f;
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float wobble = 1.0f + 0.12f * sinf(2.0f * PI * 9.0f * t);
        phase += sweep(520.0f, 180.0f, t, length) * wobble / SAMPLE_RATE;
        float shape = fminf(t / 0.12f, 1.0f) * fminf((length - t) / 0.3f, 1.0f);
        out[i] = lowpass(&lp, saw(phase) + 0.3f * noise(), 1600.0f) * shape * 0.8f;
    }
}

static void synth_charge(float *out, int n)
{
    // Atirador carregando: um apito que sobe durante toda a carga (o som
    // dura o mesmo que a carga, então o jogador "ouve" quando vai sair).
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float u = t / length;
        phase += sweep(380.0f, 2600.0f, t, length) / SAMPLE_RATE;
        float tremolo = 0.7f + 0.3f * sinf(2.0f * PI * (4.0f + 20.0f * u) * t);   // pulsa cada vez mais rápido
        out[i] = (0.7f * sine(phase) + 0.2f * square(phase)) * tremolo * (0.3f + 0.7f * u);
    }
}

static void synth_lance(float *out, int n)
{
    // Tiro do Atirador: estalo forte e um rasgo descendo.
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(2600.0f, 300.0f, t, 0.25f) / SAMPLE_RATE;
        float crack = (t < 0.012f) ? noise() : 0.0f;
        float tear = lowpass(&lp, noise(), sweep(6000.0f, 800.0f, t, 0.3f));
        out[i] = (0.5f * saw(phase) + crack + 0.8f * tear) * envelope(t, 0.001f, 9.0f);
    }
}

static void synth_deflect(float *out, int n)
{
    // Tiro parado pelo escudo: "tim" de vidro, agudo e curto.
    static const float PARTIALS[3] = { 2300.0f, 3450.0f, 5100.0f };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float v = 0.0f;
        for (int k = 0; k < 3; k++) v += sine(PARTIALS[k] * t) * expf(-t * (30.0f + 15.0f * (float)k));
        out[i] = 0.5f * v * envelope(t, 0.001f, 10.0f);
    }
}

static void synth_launch(float *out, int n)
{
    // Hangar abrindo: chiado pneumático e uma escada de bipes descendo.
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float hiss = (noise() - lowpass(&lp, noise(), 1500.0f)) * envelope(t, 0.01f, 6.0f);
        int step = (int)(t / 0.08f);
        float on = (step < 4 && fmodf(t, 0.08f) < 0.05f) ? 1.0f : 0.0f;
        phase += (900.0f - 140.0f * (float)step) / SAMPLE_RATE;
        out[i] = 0.7f * hiss + 0.35f * square(phase) * on;
    }
}

static void synth_ready(float *out, int n)
{
    static const float NOTES[2] = { 784.0f, 1174.7f };
    arpeggio(out, n, NOTES, 2, 0.07f, 9.0f);
}

static void synth_level_up(float *out, int n)
{
    // Subiu de nível: escala rápida subindo, brilhante.
    static const float NOTES[5] = { 523.3f, 659.3f, 784.0f, 1046.5f, 1568.0f };
    arpeggio(out, n, NOTES, 5, 0.06f, 5.0f);
}

static void synth_evolve(float *out, int n)
{
    // Evolução escolhida: um acorde cheio que cresce, com um brilho por cima.
    float length = (float)n / SAMPLE_RATE;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float chord = saw(261.6f * t) + 0.8f * saw(329.6f * t) + 0.7f * saw(392.0f * t) + 0.5f * sine(1046.5f * t);
        float shape = fminf(t / 0.08f, 1.0f) * fminf((length - t) / 0.4f, 1.0f);
        out[i] = lowpass(&lp, chord, sweep(600.0f, 4000.0f, t, length * 0.6f)) * shape * 0.5f;
    }
}

static void synth_reflect(float *out, int n)
{
    // Tiro devolvido: "ping" metálico subindo.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(900.0f, 2800.0f, t, 0.12f) / SAMPLE_RATE;
        out[i] = (0.7f * sine(phase) + 0.3f * sine(phase * 2.01f)) * envelope(t, 0.001f, 14.0f);
    }
}

static void synth_blast(float *out, int n) { explosion(out, n, 3800.0f, 5.0f, 90.0f, 0.02f); }

static void synth_crit(float *out, int n)
{
    // Crítico: estalo agudo e curto, por cima do acerto comum.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += 3200.0f / SAMPLE_RATE;
        float click = (t < 0.004f) ? noise() : 0.0f;
        out[i] = (0.6f * square(phase) + click) * envelope(t, 0.001f, 40.0f);
    }
}

static void synth_ally_shot(float *out, int n)
{
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(2200.0f, 900.0f, t, 0.06f) / SAMPLE_RATE;
        out[i] = sine(phase) * envelope(t, 0.001f, 45.0f);
    }
}

typedef struct {
    SynthFn fn;
    float   seconds;
    float   gain;      // volume de base
    int     voices;    // quantas cópias podem tocar ao mesmo tempo
} SoundDef;

static const SoundDef DEFS[SND_COUNT] = {
    [SND_SHOT]         = { synth_shot,         0.12f, 0.30f, 6 },
    [SND_ENEMY_SHOT]   = { synth_enemy_shot,   0.20f, 0.30f, 6 },
    [SND_HIT]          = { synth_hit,          0.08f, 0.45f, 4 },
    [SND_ARMOR]        = { synth_armor,        0.30f, 0.40f, 4 },
    [SND_WEAK]         = { synth_weak,         0.25f, 0.45f, 4 },
    [SND_KILL_SMALL]   = { synth_kill_small,   0.55f, 0.70f, 6 },
    [SND_KILL_BIG]     = { synth_kill_big,     1.80f, 1.00f, 2 },
    [SND_BOMB_ARM]     = { synth_bomb_arm,     0.42f, 0.55f, 3 },
    [SND_BOMB_DROP]    = { synth_bomb_drop,    1.10f, 0.45f, 3 },
    [SND_BOMB_DOWN]    = { synth_bomb_down,    0.60f, 0.80f, 3 },
    [SND_IMPACT_SMALL] = { synth_impact_small, 0.45f, 0.50f, 6 },
    [SND_IMPACT_BIG]   = { synth_impact_big,   2.00f, 1.00f, 3 },
    [SND_SHIELD]       = { synth_shield,       0.30f, 0.55f, 3 },
    [SND_HULL]         = { synth_hull,         0.40f, 0.70f, 3 },
    [SND_PLAYER_DOWN]  = { synth_player_down,  2.00f, 1.00f, 1 },
    [SND_HORN]         = { synth_horn,         1.60f, 0.80f, 1 },
    [SND_CHIME]        = { synth_chime,        1.00f, 0.60f, 1 },
    [SND_WIN]          = { synth_win,          2.40f, 0.70f, 1 },
    [SND_LOSE]         = { synth_lose,         2.50f, 0.80f, 1 },
    [SND_DASH]         = { synth_dash,         0.35f, 0.45f, 2 },
    [SND_BUMP]         = { synth_bump,         0.25f, 0.60f, 3 },
    [SND_WARP]         = { synth_warp,         0.90f, 0.40f, 3 },
    [SND_LOCK]         = { synth_lock,         0.24f, 0.55f, 3 },
    [SND_DETONATE]     = { synth_detonate,     1.10f, 1.00f, 2 },
    [SND_DIG]          = { synth_dig,          1.20f, 0.55f, 2 },
    [SND_DRAIN]        = { synth_drain,        1.00f, 0.45f, 2 },
    [SND_CHARGE]       = { synth_charge,       1.40f, 0.40f, 3 },   // a carga do Atirador (sniper.charge_time)
    [SND_LANCE]        = { synth_lance,        0.40f, 0.65f, 3 },
    [SND_DEFLECT]      = { synth_deflect,      0.22f, 0.35f, 4 },
    [SND_LAUNCH]       = { synth_launch,       0.60f, 0.55f, 2 },
    [SND_READY]        = { synth_ready,        0.40f, 0.50f, 1 },
    [SND_LEVEL_UP]     = { synth_level_up,     0.80f, 0.60f, 1 },
    [SND_EVOLVE]       = { synth_evolve,       1.00f, 0.60f, 1 },
    [SND_REFLECT]      = { synth_reflect,      0.30f, 0.50f, 3 },
    [SND_BLAST]        = { synth_blast,        0.70f, 0.80f, 3 },
    [SND_CRIT]         = { synth_crit,         0.12f, 0.40f, 3 },
    [SND_ALLY_SHOT]    = { synth_ally_shot,    0.10f, 0.12f, 6 },
};

static Sound make_sound(const SoundDef *def)
{
    int count = (int)(def->seconds * SAMPLE_RATE);
    if (count > MAX_SAMPLES) count = MAX_SAMPLES;
    memset(buffer, 0, sizeof(float) * (size_t)count);
    def->fn(buffer, count);

    // Normaliza pelo pico e converte para 16 bits.
    float peak = 1e-6f;
    for (int i = 0; i < count; i++) peak = fmaxf(peak, fabsf(buffer[i]));
    short *samples = MemAlloc((unsigned int)count * sizeof(short));
    for (int i = 0; i < count; i++) samples[i] = (short)(Clamp(buffer[i] / peak, -1.0f, 1.0f) * 30000.0f);

    Wave wave = { (unsigned int)count, SAMPLE_RATE, 16, 1, samples };
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

// ---------------------------------------------------------------------------
//  Tocar
// ---------------------------------------------------------------------------

static float random_unit(Audio *a)
{
    a->noise = a->noise * 1664525u + 1013904223u;
    return (float)(a->noise >> 8) / 16777216.0f;
}

// Toca um som. `pos` NULL = sem posição (centro, volume cheio). `floor` é o
// volume mínimo, para avisos que não podem passar despercebidos.
static void play(Audio *a, const Tuning *t, SoundId id, const Vector3 *pos, Camera3D camera, float volume, float floor)
{
    const SoundDef *def = &DEFS[id];
    float pan = 0.5f;
    if (pos != NULL) {
        Vector3 to = Vector3Subtract(*pos, camera.position);
        float distance = Vector3Length(to);
        volume *= fmaxf(1.0f / (1.0f + distance / t->audio_falloff), floor);
        if (distance > 1e-3f) {
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
            pan = 0.5f - 0.4f * Vector3DotProduct(Vector3Scale(to, 1.0f / distance), right);
        }
    }
    int slot = a->next_voice[id];
    a->next_voice[id] = (slot + 1) % def->voices;
    Sound s = a->voices[id][slot];
    SetSoundVolume(s, Clamp(volume * def->gain, 0.0f, 1.0f));
    SetSoundPitch(s, 1.0f + (random_unit(a) - 0.5f) * 2.0f * PITCH_SPREAD);
    SetSoundPan(s, pan);
    PlaySound(s);
}

void audio_init(Audio *a, const World *w, const Tuning *t, bool muted)
{
    memset(a, 0, sizeof *a);
    a->noise = 0x9e3779b9u;
    a->events_seen = w->events.count;
    a->impacts_seen = w->impact_count;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    for (int i = 0; i < SND_COUNT; i++) {
        a->sounds[i] = make_sound(&DEFS[i]);
        for (int v = 0; v < DEFS[i].voices; v++) a->voices[i][v] = LoadSoundAlias(a->sounds[i]);
    }
    a->ready = true;
    audio_apply_tuning(a, t, muted);
}

void audio_apply_tuning(Audio *a, const Tuning *t, bool muted)
{
    if (a->ready) SetMasterVolume(muted ? 0.0f : t->audio_volume);
}

void audio_update(Audio *a, const World *w, const Tuning *t, Camera3D camera)
{
    if (!a->ready) return;
    int played[SND_COUNT] = { 0 };

    // Crateras: o som depende do tamanho.
    if (w->impact_count < a->impacts_seen) a->impacts_seen = w->impact_count;
    for (uint32_t id = a->impacts_seen + 1; id <= w->impact_count; id++) {
        const Impact *impact = world_find_impact(w, id);
        if (impact == NULL || impact->quiet) continue;   // broca e feixe têm som próprio, no começo
        SoundId s = (impact->radius >= t->bomber_crater_radius * 0.6f) ? SND_IMPACT_BIG : SND_IMPACT_SMALL;
        if (played[s]++ < PER_FRAME) play(a, t, s, &impact->pos, camera, 1.0f, 0.15f);
    }
    a->impacts_seen = w->impact_count;

    if (w->events.count < a->events_seen) a->events_seen = w->events.count;
    for (uint32_t id = a->events_seen + 1; id <= w->events.count; id++) {
        const GameEvent *e = events_find(&w->events, id);
        if (e == NULL) continue;
        SoundId s = SND_COUNT;
        float volume = 1.0f, floor = 0.0f;
        bool positional = true;
        switch ((EventType)e->type) {
        case EV_SHOT:        s = SND_SHOT; positional = false; break;
        case EV_ENEMY_SHOT:  s = (e->sub == ENEMY_SNIPER) ? SND_LANCE : SND_ENEMY_SHOT; floor = 0.1f; break;
        case EV_HIT:         s = SND_HIT; break;
        case EV_ARMOR_HIT:   s = SND_ARMOR; break;
        case EV_WEAK_HIT:    s = SND_WEAK; break;
        case EV_KILL:
        case EV_CRASH:
            s = (e->sub == ENEMY_BOMBER || e->sub == ENEMY_CARRIER) ? SND_KILL_BIG : SND_KILL_SMALL;
            floor = 0.2f;
            break;
        case EV_LOCK:          s = SND_LOCK; floor = 0.5f; break;
        case EV_DETONATE:      s = SND_DETONATE; floor = 0.4f; break;
        case EV_DIG_START:     s = SND_DIG; floor = 0.35f; break;
        case EV_DRAIN_START:   s = SND_DRAIN; floor = 0.3f; break;
        case EV_SNIPER_CHARGE: s = SND_CHARGE; floor = 0.5f; break;   // mira na nave: tem de ser ouvido
        case EV_SHIELDED:      s = SND_DEFLECT; break;
        case EV_LAUNCH:        s = SND_LAUNCH; floor = 0.3f; break;
        case EV_CALM_SKIP:     s = SND_READY; positional = false; break;
        case EV_LEVEL_UP:      s = SND_LEVEL_UP; positional = false; break;
        case EV_EVOLVE:        s = SND_EVOLVE; positional = false; break;
        case EV_REFLECT:       s = SND_REFLECT; positional = false; break;
        case EV_BLAST:         s = SND_BLAST; floor = 0.3f; break;
        case EV_CRIT:          s = SND_CRIT; break;
        case EV_ALLY_SHOT:     s = SND_ALLY_SHOT; break;
        case EV_BOMB_ARMING: s = SND_BOMB_ARM; floor = 0.45f; break;
        case EV_BOMB_DROP:   s = SND_BOMB_DROP; floor = 0.3f; break;
        case EV_BOMB_DOWN:   s = SND_BOMB_DOWN; break;
        case EV_PLAYER_HIT:  s = (e->sub == 1) ? SND_SHIELD : SND_HULL; positional = false; break;
        case EV_PLAYER_DOWN: s = SND_PLAYER_DOWN; positional = false; break;
        case EV_WAVE_START:  s = SND_HORN; positional = false; break;
        case EV_WAVE_CLEAR:  s = SND_CHIME; positional = false; break;
        case EV_GAME_WON:    s = SND_WIN; positional = false; break;
        case EV_GAME_LOST:   s = SND_LOSE; positional = false; break;
        case EV_DASH:        s = SND_DASH; positional = false; break;
        case EV_BUMP:        s = SND_BUMP; positional = false; volume = Clamp(e->value / 20.0f, 0.2f, 1.0f); break;
        case EV_SPAWN:       s = SND_WARP; floor = 0.35f; break;
        default:             break;
        }
        if (s == SND_COUNT || played[s]++ >= PER_FRAME) continue;
        play(a, t, s, positional ? &e->pos : NULL, camera, volume, floor);
    }
    a->events_seen = w->events.count;
}

void audio_shutdown(Audio *a)
{
    if (!a->ready) return;
    for (int i = 0; i < SND_COUNT; i++) {
        for (int v = 0; v < DEFS[i].voices; v++) UnloadSoundAlias(a->voices[i][v]);
        UnloadSound(a->sounds[i]);
    }
    CloseAudioDevice();
    a->ready = false;
}
