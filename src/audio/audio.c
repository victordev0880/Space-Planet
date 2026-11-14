#include "raylib.h"

#include "audio/audio.h"

#include <math.h>
#include <string.h>

#include "core/vmath.h"

#define SAMPLE_RATE  44100
#define MAX_SAMPLES  (SAMPLE_RATE * 36 / 10)   // o som mais longo tem 3,6 segundos
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

// --- Boss -------------------------------------------------------------------

static void synth_boss_enter(float *out, int n)
{
    // Chegada do boss: um ronco grave que sobe, com uma sirene de dois tons
    // por cima. Algo enorme está vindo.
    LowPass lp = { 0 }, siren_lp = { 0 };
    float phase = 0.0f, siren = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(38.0f, 62.0f, t, length) / SAMPLE_RATE;
        float drone = saw(phase) + 0.7f * saw(phase * 1.498f) + 0.5f * square(phase * 0.5f);
        float rumble = lowpass(&lp, drone + 0.4f * noise(), 420.0f);
        siren += ((fmodf(t, 0.9f) < 0.45f) ? 466.2f : 349.2f) / SAMPLE_RATE;
        float alarm = lowpass(&siren_lp, square(siren), 1800.0f) * ((t > 0.4f) ? 0.22f : 0.0f);
        float shape = fminf(t / 0.6f, 1.0f) * fminf((length - t) / 0.8f, 1.0f);
        out[i] = (rumble * 0.9f + alarm) * shape;
    }
}

static void synth_boss_charge(float *out, int n)
{
    // O canhão começa a carregar: zumbido elétrico subindo, com estalos.
    LowPass lp = { 0 };
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float u = t / length;
        phase += sweep(55.0f, 330.0f, t, length) / SAMPLE_RATE;
        float hum = saw(phase) + 0.5f * saw(phase * 2.003f);
        float crackle = (noise() > 0.995f - 0.01f * u) ? noise() : 0.0f;
        float body = lowpass(&lp, hum, 900.0f + 2400.0f * u);
        float shape = fminf(t / 0.3f, 1.0f) * fminf((length - t) / 0.5f, 1.0f);
        out[i] = (0.8f * body + 0.6f * crackle) * shape * (0.4f + 0.6f * u);
    }
}

static void synth_boss_fire(float *out, int n)
{
    // O laser cortando: rugido grave e chiado de rocha derretendo, o tempo todo.
    LowPass lp = { 0 }, hiss_lp = { 0 };
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float wobble = 1.0f + 0.04f * sinf(2.0f * PI * 9.0f * t);
        phase += 68.0f * wobble / SAMPLE_RATE;
        float roar = lowpass(&lp, saw(phase) + 0.8f * saw(phase * 1.01f) + 0.6f * noise(), 700.0f);
        float hiss = noise() - lowpass(&hiss_lp, noise(), 2500.0f);
        float blast = (t < 0.05f) ? noise() * (1.0f - t / 0.05f) : 0.0f;
        float shape = fminf(t / 0.02f, 1.0f) * fminf((length - t) / 0.6f, 1.0f);
        out[i] = (1.2f * roar + 0.35f * hiss + blast) * shape;
    }
}

static void synth_boss_capacitor(float *out, int n)
{
    // Capacitor estourado: estalo elétrico e uma descarga descendo.
    explosion(out, n, 5200.0f, 7.0f, 140.0f, 0.03f);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(2400.0f, 200.0f, t, 0.35f) / SAMPLE_RATE;
        out[i] += 0.5f * square(phase) * envelope(t, 0.001f, 7.0f);
    }
}

static void synth_boss_stagger(float *out, int n)
{
    // Atordoado: as máquinas desligando, com engasgos.
    LowPass lp = { 0 };
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(420.0f, 32.0f, t, length * 0.8f) / SAMPLE_RATE;
        float sputter = (fmodf(t, 0.11f) < 0.07f || t < 0.3f) ? 1.0f : 0.25f;
        float v = lowpass(&lp, saw(phase) + 0.5f * square(phase * 0.5f), 1400.0f);
        out[i] = v * sputter * envelope(t, 0.005f, 1.6f);
    }
}

static void synth_boss_phase(float *out, int n)
{
    // Mudança de fase: um urro metálico, mais grave que a trompa da horda.
    LowPass lp = { 0 };
    float phase = 0.0f;
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float bend = sweep(70.0f, 52.0f, t, length);
        phase += bend / SAMPLE_RATE;
        float v = saw(phase) + 0.8f * saw(phase * 1.26f) + 0.6f * saw(phase * 1.5f) + 0.3f * noise();
        float growl = 0.75f + 0.25f * sinf(2.0f * PI * 13.0f * t);
        float shape = fminf(t / 0.1f, 1.0f) * fminf((length - t) / 0.5f, 1.0f);
        out[i] = lowpass(&lp, v, 1100.0f) * growl * shape;
    }
}

static void synth_boss_down(float *out, int n)
{
    // O boss explodindo: a maior explosão do jogo, com um grave que demora a sumir.
    explosion(out, n, 2600.0f, 1.1f, 44.0f, 0.03f);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(52.0f, 26.0f, t, 2.5f) / SAMPLE_RATE;
        out[i] += 0.9f * sine(phase) * envelope(t, 0.01f, 1.2f);
    }
}

static void synth_reward(float *out, int n)
{
    // Núcleo adquirido: um acorde que sobe, brilhando, com um arpejo em cima.
    static const float NOTES[6] = { 392.0f, 523.3f, 659.3f, 784.0f, 1046.5f, 1568.0f };
    arpeggio(out, n, NOTES, 6, 0.09f, 1.6f);
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float pad = saw(196.0f * t) + 0.7f * saw(293.7f * t) + 0.5f * saw(392.0f * t);
        float shape = fminf(t / 0.4f, 1.0f) * fminf((length - t) / 0.8f, 1.0f);
        out[i] += 0.35f * lowpass(&lp, pad, sweep(400.0f, 3000.0f, t, length * 0.7f)) * shape;
    }
}

static void synth_ability(float *out, int n)
{
    // O raio do núcleo: estalo e um rasgo grave que desce, bem encorpado.
    LowPass lp = { 0 }, tear_lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(900.0f, 60.0f, t, 0.5f) / SAMPLE_RATE;
        float crack = (t < 0.02f) ? noise() : 0.0f;
        float body = lowpass(&lp, saw(phase) + 0.6f * saw(phase * 1.5f), 2000.0f);
        float tear = lowpass(&tear_lp, noise(), sweep(7000.0f, 500.0f, t, 0.6f));
        out[i] = (body + 1.4f * crack + 0.7f * tear) * envelope(t, 0.001f, 3.5f);
    }
}

// --- Rochas e conserto --------------------------------------------------------

static void synth_meteor(float *out, int n)
{
    // Meteoro chegando: um rugido de vento que cresce e passa, com estalos.
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float u = t / length;
        float rush = lowpass(&lp, noise(), sweep(300.0f, 1400.0f, t, length * 0.6f));
        float crackle = (noise() > 0.996f) ? noise() * 0.6f : 0.0f;
        float shape = sinf(PI * u) * sinf(PI * u);
        out[i] = (1.6f * rush + crackle) * shape;
    }
}

static void synth_fragment(float *out, int n)
{
    // Fragmento arrancado: estalo de rocha e um assobio subindo.
    explosion(out, n, 2600.0f, 8.0f, 110.0f, 0.03f);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(300.0f, 900.0f, t, 0.5f) / SAMPLE_RATE;
        out[i] += 0.25f * sine(phase) * envelope(t, 0.05f, 4.0f);
    }
}

static void synth_tractor_grab(float *out, int n)
{
    // Raio trator pegando: um "vuuum" subindo e um sino de trava no fim.
    float phase = 0.0f, bell = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(140.0f, 620.0f, t, 0.35f) / SAMPLE_RATE;
        float wobble = 1.0f + 0.3f * sinf(2.0f * PI * 18.0f * t);
        float hum = (sine(phase) + 0.4f * sine(phase * 2.0f)) * wobble * envelope(t, 0.01f, 3.0f);
        float lock = 0.0f;
        if (t > 0.3f) {
            bell += 1568.0f / SAMPLE_RATE;
            lock = 0.5f * sine(bell) * envelope(t - 0.3f, 0.002f, 9.0f);
        }
        out[i] = 0.7f * hum + lock;
    }
}

static void synth_tractor_miss(float *out, int n)
{
    // Nada ao alcance: dois bipes graves.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float on = (fmodf(t, 0.11f) < 0.07f) ? 1.0f : 0.0f;
        phase += 260.0f / SAMPLE_RATE;
        out[i] = 0.5f * square(phase) * on * envelope(t, 0.002f, 6.0f);
    }
}

static void synth_tractor_release(float *out, int n)
{
    // Soltou: o zumbido descendo, com um clique.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(620.0f, 150.0f, t, 0.3f) / SAMPLE_RATE;
        float click = (t < 0.006f) ? noise() : 0.0f;
        out[i] = (0.7f * sine(phase) + 0.25f * sine(phase * 2.0f)) * envelope(t, 0.002f, 7.0f) + click;
    }
}

static void synth_tractor_break(float *out, int n)
{
    // Arrebentou: um estalo seco e o zumbido caindo de vez.
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float snap = (noise() - lowpass(&lp, noise(), 1200.0f)) * envelope(t, 0.001f, 30.0f);
        phase += sweep(800.0f, 80.0f, t, 0.4f) / SAMPLE_RATE;
        out[i] = 1.2f * snap + 0.5f * saw(phase) * envelope(t, 0.002f, 6.0f);
    }
}

static void synth_deposit(float *out, int n)
{
    // A rocha batendo no chão: um baque surdo e terra assentando.
    explosion(out, n, 600.0f, 4.0f, 55.0f, 0.004f);
}

static void synth_repair(float *out, int n)
{
    // Conserto pronto: um acorde maior que cresce, brilhando, como uma cura.
    static const float NOTES[4] = { 392.0f, 493.9f, 587.3f, 784.0f };
    arpeggio(out, n, NOTES, 4, 0.07f, 2.4f);
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float pad = sine(196.0f * t) + 0.6f * sine(293.7f * t) + 0.4f * sine(392.0f * t);
        float shape = fminf(t / 0.25f, 1.0f) * fminf((length - t) / 0.6f, 1.0f);
        out[i] += 0.4f * lowpass(&lp, pad, 2000.0f) * shape;
    }
}

// --- Mineração e defesas -------------------------------------------------------

static void synth_rock_shot(float *out, int n)
{
    // Tiro na rocha: um "toc" seco de pedra.
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(420.0f, 180.0f, t, 0.08f) / SAMPLE_RATE;
        float knock = lowpass(&lp, noise(), 1800.0f) * 1.5f;
        out[i] = (knock + 0.6f * sine(phase)) * envelope(t, 0.001f, 30.0f);
    }
}

static void synth_ore(float *out, int n)
{
    // Cristal coletado: um "plim" agudo e curto.
    static const float NOTES[2] = { 1318.5f, 1975.5f };
    arpeggio(out, n, NOTES, 2, 0.035f, 18.0f);
}

static void synth_buy(float *out, int n)
{
    // Defesa comprada: três notas subindo e um zumbido de máquina ligando.
    static const float NOTES[3] = { 659.3f, 880.0f, 1174.7f };
    arpeggio(out, n, NOTES, 3, 0.06f, 6.0f);
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(90.0f, 180.0f, t, 0.4f) / SAMPLE_RATE;
        out[i] += 0.3f * saw(phase) * envelope(t, 0.05f, 5.0f);
    }
}

static void synth_deny(float *out, int n)
{
    // Não deu para comprar: um zumbido grave e curto.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += 140.0f / SAMPLE_RATE;
        out[i] = 0.5f * square(phase) * envelope(t, 0.003f, 9.0f);
    }
}

static void synth_intercept(float *out, int n)
{
    // Satélite derrubando um tiro: um zap elétrico descendo e um estalo.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(3200.0f, 700.0f, t, 0.12f) / SAMPLE_RATE;
        float crack = (t > 0.06f && t < 0.075f) ? noise() : 0.0f;
        out[i] = (0.6f * square(phase) * envelope(t, 0.001f, 20.0f)) + crack;
    }
}

static void synth_shield_block(float *out, int n)
{
    // Escudo segurando um golpe: um "vuum" grave e abafado, com brilho em cima.
    LowPass lp = { 0 };
    float phase = 0.0f, high = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(160.0f, 70.0f, t, 0.3f) / SAMPLE_RATE;
        high += 1760.0f / SAMPLE_RATE;
        float body = lowpass(&lp, sine(phase) + 0.4f * noise(), 600.0f);
        out[i] = (1.2f * body + 0.25f * sine(high) * expf(-t * 18.0f)) * envelope(t, 0.004f, 6.0f);
    }
}

static void synth_shield_down(float *out, int n)
{
    // Escudo acabando: vidro estilhaçando e o zumbido caindo.
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float shatter = (noise() - lowpass(&lp, noise(), 3000.0f)) * envelope(t, 0.001f, 7.0f);
        phase += sweep(900.0f, 120.0f, t, 0.6f) / SAMPLE_RATE;
        out[i] = 0.9f * shatter + 0.35f * sine(phase) * envelope(t, 0.01f, 3.0f);
    }
}

// --- Interface -------------------------------------------------------------------

static void synth_ui_move(float *out, int n)
{
    // Trocar de item: um "tic" curto e macio.
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(1500.0f, 1100.0f, t, 0.04f) / SAMPLE_RATE;
        out[i] = sine(phase) * envelope(t, 0.001f, 70.0f);
    }
}

static void synth_ui_confirm(float *out, int n)
{
    static const float NOTES[2] = { 880.0f, 1318.5f };
    arpeggio(out, n, NOTES, 2, 0.05f, 16.0f);
}

static void synth_ui_back(float *out, int n)
{
    static const float NOTES[2] = { 987.8f, 659.3f };
    arpeggio(out, n, NOTES, 2, 0.05f, 18.0f);
}

static void synth_ui_start(float *out, int n)
{
    // Começar a partida: um sopro subindo e um acorde que se abre.
    static const float NOTES[4] = { 220.0f, 329.6f, 440.0f, 659.3f };
    arpeggio(out, n, NOTES, 4, 0.07f, 2.2f);
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float rush = lowpass(&lp, noise(), sweep(300.0f, 5000.0f, t, 0.7f)) * envelope(t, 0.4f, 3.0f);
        out[i] += 1.4f * rush;
    }
}

// --- Colmeia Lunar (sons orgânicos) ----------------------------------------------

static void synth_hive_inflate(float *out, int n)
{
    // Bolsa inchando: um borbulhar que acelera e um gemido grave subindo.
    LowPass lp = { 0 }, lp2 = { 0 };
    float phase = 0.0f, length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float rate = 6.0f + 26.0f * u * u;                       // bolhas por segundo
        float bubble = powf(fabsf(sinf(PI * rate * t)), 12.0f);
        float wet = lowpass(&lp, noise(), 500.0f + 1500.0f * bubble) * bubble * 2.5f;
        phase += sweep(70.0f, 210.0f, t, length) / SAMPLE_RATE;
        float groan = lowpass(&lp2, saw(phase), 400.0f + 500.0f * u);
        out[i] = (wet + 0.6f * groan) * fminf(u * 3.0f, 1.0f) * (0.4f + 0.6f * u);
    }
}

static void synth_hive_pop(float *out, int n)
{
    // Bolsa estourando: um "plof" molhado.
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float splat = lowpass(&lp, noise(), sweep(2600.0f, 180.0f, t, 0.3f)) * 2.0f;
        phase += sweep(160.0f, 55.0f, t, 0.25f) / SAMPLE_RATE;
        out[i] = (splat * envelope(t, 0.002f, 9.0f) + 0.9f * sine(phase) * envelope(t, 0.002f, 12.0f));
    }
}

static void synth_hive_birth(float *out, int n)
{
    // Drones saindo da bolsa: chilreios agudos e desencontrados.
    memset(out, 0, sizeof(float) * (size_t)n);
    for (int k = 0; k < 7; k++) {
        int start = (int)((float)k * 0.07f * SAMPLE_RATE);
        float f0 = 1700.0f + 180.0f * (float)(k % 4), f1 = f0 * 1.5f;
        float phase = 0.0f;
        for (int i = start; i < n; i++) {
            float t = (float)(i - start) / SAMPLE_RATE;
            if (t > 0.12f) break;
            phase += sweep(f0, f1, t, 0.08f) / SAMPLE_RATE;
            out[i] += 0.35f * square(phase) * envelope(t, 0.003f, 30.0f);
        }
    }
}

static void synth_hive_thruster(float *out, int n)
{
    // Propulsor arrebentando: explosão grave e um chiado de gás escapando.
    explosion(out, n, 1600.0f, 3.2f, 52.0f, 0.012f);
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = noise();
        out[i] += 0.5f * (x - lowpass(&lp, x, 3000.0f)) * envelope(t, 0.05f, 2.5f);
    }
}

static void synth_hive_push(float *out, int n)
{
    // A Lua sendo empurrada: um rangido grave e enorme, que cresce e some.
    LowPass lp = { 0 };
    float phase = 0.0f, length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float vibrato = 1.0f + 0.04f * sinf(2.0f * PI * 3.3f * t);
        phase += 44.0f * vibrato / SAMPLE_RATE;
        float body = lowpass(&lp, saw(phase) + 0.5f * saw(phase * 1.49f) + 0.3f * noise(), 260.0f);
        out[i] = body * sinf(PI * u) * 1.4f;
    }
}

static void synth_hive_spit(float *out, int n)
{
    // O coração prepara o cuspe: um chiado ácido crescendo.
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float x = noise();
        float hiss = x - lowpass(&lp, x, 1800.0f + 3000.0f * u);
        out[i] = hiss * u * u * (0.8f + 0.2f * sinf(2.0f * PI * 40.0f * t));
    }
}

static void synth_spore(float *out, int n)
{
    // Esporo explodindo: estouro molhado e brilhante.
    explosion(out, n, 3000.0f, 5.0f, 85.0f, 0.02f);
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        out[i] += 0.8f * lowpass(&lp, noise(), sweep(3000.0f, 300.0f, t, 0.2f)) * envelope(t, 0.001f, 14.0f);
    }
}

static void synth_brood(float *out, int n)
{
    // A Ninhada saindo: um enxame zumbindo que passa.
    static const float VOICES[4] = { 180.0f, 211.0f, 247.0f, 263.0f };
    float phase[4] = { 0 };
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float v = 0.0f;
        for (int k = 0; k < 4; k++) {
            phase[k] += VOICES[k] * (1.0f + 0.3f * u) / SAMPLE_RATE;
            v += saw(phase[k]);
        }
        float buzz = 0.6f + 0.4f * sinf(2.0f * PI * 55.0f * t);
        out[i] = lowpass(&lp, v * buzz, 2400.0f) * sinf(PI * u) * 0.6f;
    }
}

// --- Verme do Núcleo ---------------------------------------------------------------

static void synth_worm_rumble(float *out, int n)
{
    // O chão rachando: um ronco grave crescendo, com estalos de pedra.
    LowPass lp = { 0 };
    float phase = 0.0f, length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        phase += (32.0f + 18.0f * u) / SAMPLE_RATE;
        float rumble = lowpass(&lp, noise(), 160.0f + 120.0f * u) * 3.0f + 0.5f * saw(phase);
        float crack = (noise() > 1.0f - 0.004f * (1.0f + 6.0f * u)) ? noise() : 0.0f;
        out[i] = (rumble * u + 0.7f * crack) * fminf(u * 4.0f, 1.0f);
    }
}

static void synth_worm_breach(float *out, int n)
{
    // A erupção: rocha explodindo e um rugido rasgado por cima.
    explosion(out, n, 1800.0f, 2.2f, 45.0f, 0.02f);
    LowPass lp = { 0 };
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float vibrato = 1.0f + 0.06f * sinf(2.0f * PI * 9.0f * t);
        phase += sweep(140.0f, 70.0f, t, 1.5f) * vibrato / SAMPLE_RATE;
        float roar = lowpass(&lp, saw(phase) + 0.6f * saw(phase * 1.51f) + 0.5f * noise(), 900.0f);
        out[i] += 0.8f * roar * envelope(t, 0.08f, 1.6f);
    }
}

static void synth_worm_dive(float *out, int n)
{
    // O mergulho: um baque surdo e terra caindo.
    explosion(out, n, 900.0f, 4.0f, 50.0f, 0.03f);
}

static void synth_worm_core(float *out, int n)
{
    // A mordida no núcleo: um rangido metálico grave e um zumbido de alarme.
    LowPass lp = { 0 };
    float phase = 0.0f, alarm = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(90.0f, 40.0f, t, 1.0f) / SAMPLE_RATE;
        float crunch = lowpass(&lp, noise(), 700.0f) * 2.0f + square(phase);
        alarm += ((fmodf(t, 0.3f) < 0.15f) ? 660.0f : 520.0f) / SAMPLE_RATE;
        out[i] = crunch * envelope(t, 0.005f, 3.0f) + 0.3f * square(alarm) * envelope(t, 0.01f, 1.5f);
    }
}

static void synth_worm_nodule(float *out, int n)
{
    // Nódulo arrebentando: estouro em brasa e um chiado de magma.
    explosion(out, n, 2600.0f, 4.5f, 70.0f, 0.03f);
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = noise();
        out[i] += 0.5f * (x - lowpass(&lp, x, 2500.0f)) * envelope(t, 0.02f, 3.0f);
    }
}

static void synth_tunnel(float *out, int n)
{
    // Atravessando o planeta: um sopro que mergulha e volta do outro lado.
    LowPass lp = { 0 };
    float length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float cutoff = (u < 0.4f) ? sweep(4000.0f, 120.0f, t, length * 0.4f) : sweep(120.0f, 3000.0f, t - length * 0.4f, length * 0.6f);
        out[i] = lowpass(&lp, noise(), cutoff) * 2.2f * sinf(PI * u);
    }
}

// --- Devorador ---------------------------------------------------------------------

// Rugido: serras graves desafinadas, com um rosnado (modulação rápida) e ar.
static void roar(float *out, int n, float from, float to, float growl)
{
    LowPass lp = { 0 };
    float phase = 0.0f, length = (float)n / SAMPLE_RATE;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE, u = t / length;
        float rasp = 1.0f + growl * sinf(2.0f * PI * 31.0f * t);
        phase += sweep(from, to, t, length) * rasp / SAMPLE_RATE;
        float v = saw(phase) + 0.7f * saw(phase * 1.007f) + 0.5f * saw(phase * 0.5f) + 0.4f * noise();
        out[i] = lowpass(&lp, v, 700.0f + 900.0f * sinf(PI * u)) * sinf(PI * fminf(u * 1.4f, 1.0f));
    }
}

static void synth_dev_aim(float *out, int n)
{
    // A mira: um rugido que sobe e um alarme por cima.
    roar(out, n, 55.0f, 95.0f, 0.12f);
    float alarm = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        alarm += ((fmodf(t, 0.4f) < 0.2f) ? 740.0f : 590.0f) / SAMPLE_RATE;
        out[i] += 0.25f * square(alarm) * fminf(t * 2.0f, 1.0f);
    }
}

static void synth_dev_bite(float *out, int n)
{
    // A mordida: o planeta estalando entre os dentes.
    explosion(out, n, 1400.0f, 1.8f, 40.0f, 0.04f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float crack = (noise() > 0.985f - 0.02f * expf(-t * 3.0f)) ? noise() : 0.0f;
        out[i] += 0.6f * crack;
    }
}

static void synth_dev_screech(float *out, int n)
{
    // Errou (ou foi arrancado): um guincho de dor, agudo e rasgado.
    roar(out, n, 420.0f, 180.0f, 0.25f);
}

static void synth_dev_broken(float *out, int n)
{
    // Dentes quebrados no escudo: estilhaços e o guincho.
    roar(out, n, 380.0f, 150.0f, 0.3f);
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        float x = noise();
        out[i] += 1.2f * (x - lowpass(&lp, x, 3500.0f)) * envelope(t, 0.001f, 5.0f);
    }
}

static void synth_dev_gulp(float *out, int n)
{
    // Engoliu: um "glup" grave descendo.
    float phase = 0.0f;
    LowPass lp = { 0 };
    for (int i = 0; i < n; i++) {
        float t = (float)i / SAMPLE_RATE;
        phase += sweep(220.0f, 60.0f, t, 0.35f) / SAMPLE_RATE;
        out[i] = lowpass(&lp, sine(phase) + 0.4f * noise(), 600.0f) * envelope(t, 0.01f, 6.0f) * 1.5f;
    }
}

static void synth_dev_roar(float *out, int n)
{
    // Chegada e banquete: o rugido grande.
    roar(out, n, 70.0f, 40.0f, 0.18f);
}

static void synth_dev_teeth(float *out, int n)
{
    // Rajada de dentes: cuspidas secas em sequência.
    memset(out, 0, sizeof(float) * (size_t)n);
    LowPass lp = { 0 };
    for (int k = 0; k < 5; k++) {
        int start = (int)((float)k * 0.05f * SAMPLE_RATE);
        for (int i = start; i < n; i++) {
            float t = (float)(i - start) / SAMPLE_RATE;
            if (t > 0.15f) break;
            out[i] += lowpass(&lp, noise(), 2200.0f) * 1.6f * envelope(t, 0.001f, 30.0f);
        }
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
    [SND_BOSS_ENTER]     = { synth_boss_enter,     3.60f, 0.90f, 1 },
    [SND_BOSS_CHARGE]    = { synth_boss_charge,    2.80f, 0.70f, 1 },
    [SND_BOSS_FIRE]      = { synth_boss_fire,      3.00f, 0.90f, 1 },   // dura o mesmo que breaker.fire_time
    [SND_BOSS_CAPACITOR] = { synth_boss_capacitor, 0.70f, 0.80f, 3 },
    [SND_BOSS_STAGGER]   = { synth_boss_stagger,   1.80f, 0.80f, 1 },
    [SND_BOSS_PHASE]     = { synth_boss_phase,     1.80f, 0.85f, 1 },
    [SND_BOSS_DOWN]      = { synth_boss_down,      3.60f, 1.00f, 1 },
    [SND_REWARD]         = { synth_reward,         2.60f, 0.65f, 1 },
    [SND_ABILITY]        = { synth_ability,        1.00f, 0.85f, 2 },
    [SND_METEOR]          = { synth_meteor,          2.20f, 0.60f, 2 },
    [SND_FRAGMENT]        = { synth_fragment,        0.60f, 0.45f, 3 },
    [SND_TRACTOR_GRAB]    = { synth_tractor_grab,    0.70f, 0.60f, 1 },
    [SND_TRACTOR_MISS]    = { synth_tractor_miss,    0.22f, 0.40f, 1 },
    [SND_TRACTOR_RELEASE] = { synth_tractor_release, 0.40f, 0.50f, 1 },
    [SND_TRACTOR_BREAK]   = { synth_tractor_break,   0.50f, 0.60f, 1 },
    [SND_DEPOSIT]         = { synth_deposit,         0.90f, 0.80f, 3 },
    [SND_REPAIR]          = { synth_repair,          1.50f, 0.55f, 2 },
    [SND_ROCK_SHOT]       = { synth_rock_shot,       0.12f, 0.30f, 4 },
    [SND_ORE]             = { synth_ore,             0.20f, 0.35f, 3 },
    [SND_BUY]             = { synth_buy,             0.60f, 0.60f, 1 },
    [SND_DENY]            = { synth_deny,            0.25f, 0.45f, 1 },
    [SND_INTERCEPT]       = { synth_intercept,       0.20f, 0.45f, 3 },
    [SND_SHIELD_BLOCK]    = { synth_shield_block,    0.60f, 0.65f, 3 },
    [SND_SHIELD_DOWN]     = { synth_shield_down,     0.90f, 0.70f, 1 },
    [SND_UI_MOVE]         = { synth_ui_move,         0.06f, 0.22f, 3 },
    [SND_UI_CONFIRM]      = { synth_ui_confirm,      0.25f, 0.40f, 2 },
    [SND_UI_BACK]         = { synth_ui_back,         0.25f, 0.35f, 2 },
    [SND_UI_START]        = { synth_ui_start,        1.60f, 0.60f, 1 },
    [SND_HIVE_INFLATE]    = { synth_hive_inflate,    2.60f, 0.70f, 2 },   // dura o mesmo que hive.inflate_time
    [SND_HIVE_POP]        = { synth_hive_pop,        0.50f, 0.80f, 3 },
    [SND_HIVE_BIRTH]      = { synth_hive_birth,      0.70f, 0.55f, 2 },
    [SND_HIVE_THRUSTER]   = { synth_hive_thruster,   1.60f, 0.95f, 2 },
    [SND_HIVE_PUSH]       = { synth_hive_push,       2.80f, 0.90f, 1 },
    [SND_HIVE_SPIT]       = { synth_hive_spit,       0.90f, 0.55f, 1 },   // hive.spit_warn
    [SND_SPORE]           = { synth_spore,           0.80f, 0.85f, 2 },
    [SND_BROOD]           = { synth_brood,           0.90f, 0.60f, 1 },
    [SND_WORM_RUMBLE]     = { synth_worm_rumble,     1.80f, 0.90f, 1 },   // worm.rumble_time
    [SND_WORM_BREACH]     = { synth_worm_breach,     2.20f, 1.00f, 1 },
    [SND_WORM_DIVE]       = { synth_worm_dive,       1.00f, 0.80f, 2 },
    [SND_WORM_CORE]       = { synth_worm_core,       1.40f, 0.90f, 1 },
    [SND_WORM_NODULE]     = { synth_worm_nodule,     0.90f, 0.85f, 2 },
    [SND_TUNNEL]          = { synth_tunnel,          0.90f, 0.70f, 1 },
    [SND_DEV_AIM]         = { synth_dev_aim,         2.00f, 0.90f, 1 },
    [SND_DEV_BITE]        = { synth_dev_bite,        2.20f, 1.00f, 1 },
    [SND_DEV_SCREECH]     = { synth_dev_screech,     1.20f, 0.85f, 1 },
    [SND_DEV_BROKEN]      = { synth_dev_broken,      1.40f, 0.95f, 1 },
    [SND_DEV_GULP]        = { synth_dev_gulp,        0.50f, 0.70f, 2 },
    [SND_DEV_ROAR]        = { synth_dev_roar,        2.40f, 0.90f, 1 },
    [SND_DEV_TEETH]       = { synth_dev_teeth,       0.45f, 0.55f, 2 },
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
    SetSoundVolume(s, Clamp(volume * def->gain * a->sfx, 0.0f, 1.0f));
    SetSoundPitch(s, 1.0f + (random_unit(a) - 0.5f) * 2.0f * PITCH_SPREAD);
    SetSoundPan(s, pan);
    PlaySound(s);
}

#define MUSIC_BUFFER 2048    // quadros por meio-buffer do stream (~46 ms)
#define MUSIC_GAIN   0.55f   // a música fica embaixo dos efeitos

// Chamada pela thread de áudio do raylib sempre que o stream pede mais amostras.
static void music_callback(void *out, unsigned int frames)
{
    music_render((float *)out, (int)frames);
}

void audio_init(Audio *a, const World *w, const Tuning *t, bool muted)
{
    memset(a, 0, sizeof *a);
    a->noise = 0x9e3779b9u;
    a->volume = 1.0f;
    a->sfx = 1.0f;
    a->music = 1.0f;
    a->events_seen = w->events.count;
    a->impacts_seen = w->impact_count;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    for (int i = 0; i < SND_COUNT; i++) {
        a->sounds[i] = make_sound(&DEFS[i]);
        for (int v = 0; v < DEFS[i].voices; v++) a->voices[i][v] = LoadSoundAlias(a->sounds[i]);
    }
    a->ready = true;

    // A música só existe com som: no autoplay (mudo) ela nem começa, para não
    // gastar processador nas medições.
    if (!muted) {
        music_reset();
        SetAudioStreamBufferSizeDefault(MUSIC_BUFFER);
        a->music_stream = LoadAudioStream(MUSIC_SAMPLE_RATE, 32, 2);
        if (IsAudioStreamValid(a->music_stream)) {
            SetAudioStreamCallback(a->music_stream, music_callback);
            PlayAudioStream(a->music_stream);
            a->music_on = true;
        }
    }
    audio_apply_tuning(a, t, muted);
}

void audio_apply_tuning(Audio *a, const Tuning *t, bool muted)
{
    a->muted = muted;
    if (a->ready) SetMasterVolume(muted ? 0.0f : t->audio_volume * a->volume);
}

void audio_set_levels(Audio *a, const Tuning *t, float volume, float sfx, float music)
{
    a->volume = Clamp(volume, 0.0f, 1.0f);
    a->sfx = Clamp(sfx, 0.0f, 1.0f);
    a->music = Clamp(music, 0.0f, 1.0f);
    if (a->music_on) SetAudioStreamVolume(a->music_stream, a->music * MUSIC_GAIN);
    audio_apply_tuning(a, t, a->muted);
}

void audio_music(Audio *a, MusicMood mood, bool danger, bool muffled)
{
    if (a->music_on) music_set(mood, danger, muffled);
}

void audio_play_ui(Audio *a, const Tuning *t, SoundId id)
{
    if (!a->ready || id < SND_UI_MOVE || id >= SND_COUNT) return;
    Camera3D none = { 0 };
    play(a, t, id, NULL, none, 1.0f, 1.0f);
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
        // O boss é enorme e importante: tem de ser ouvido de qualquer lugar.
        case EV_BOSS_ENTER:     s = SND_BOSS_ENTER; positional = false; break;
        case EV_BOSS_CHARGE:    s = SND_BOSS_CHARGE; floor = 0.6f; break;
        case EV_BOSS_FIRE:      s = SND_BOSS_FIRE; floor = 0.7f; break;
        case EV_BOSS_CAPACITOR: s = SND_BOSS_CAPACITOR; floor = 0.5f; break;
        case EV_BOSS_STAGGER:   s = SND_BOSS_STAGGER; floor = 0.6f; break;
        case EV_BOSS_PHASE:     s = SND_BOSS_PHASE; floor = 0.6f; break;
        case EV_BOSS_BLAST:     s = SND_BLAST; floor = 0.4f; break;
        case EV_BOSS_DOWN:      s = SND_BOSS_DOWN; positional = false; break;
        case EV_REWARD:         s = SND_REWARD; positional = false; break;
        case EV_ABILITY:        s = SND_ABILITY; positional = false; break;
        case EV_METEOR:          s = SND_METEOR; floor = 0.45f; break;   // tem de ser notado
        case EV_FRAGMENT:        s = SND_FRAGMENT; floor = 0.1f; break;
        case EV_TRACTOR_GRAB:    s = SND_TRACTOR_GRAB; positional = false; break;
        case EV_TRACTOR_MISS:    s = SND_TRACTOR_MISS; positional = false; break;
        case EV_TRACTOR_RELEASE: s = SND_TRACTOR_RELEASE; positional = false; break;
        case EV_TRACTOR_BREAK:   s = SND_TRACTOR_BREAK; positional = false; break;
        case EV_DEPOSIT:         s = SND_DEPOSIT; floor = 0.3f; break;
        case EV_REPAIR:          s = SND_REPAIR; floor = 0.4f; break;
        case EV_ROCK_HIT:        s = SND_BUMP; positional = false; volume = Clamp(e->value / 20.0f, 0.3f, 1.0f); break;
        case EV_ROCK_SHOT:       s = SND_ROCK_SHOT; break;
        case EV_ROCK_MINED:      s = SND_FRAGMENT; floor = 0.3f; break;
        case EV_ORE:             s = SND_ORE; positional = false; break;
        case EV_BUY:             s = SND_BUY; positional = false; break;
        case EV_BUY_FAIL:        s = SND_DENY; positional = false; break;
        case EV_TURRET_SHOT:     s = SND_ALLY_SHOT; break;
        case EV_INTERCEPT:       s = SND_INTERCEPT; floor = 0.15f; break;
        case EV_SHIELD_BLOCK:    s = SND_SHIELD_BLOCK; floor = 0.25f; break;
        case EV_SHIELD_DOWN:     s = SND_SHIELD_DOWN; floor = 0.4f; break;
        // Colmeia Lunar: os avisos têm de ser ouvidos de longe (a luta é na Lua).
        case EV_HIVE_INFLATE:    s = SND_HIVE_INFLATE; floor = 0.45f; break;
        case EV_HIVE_POP:        s = SND_HIVE_POP; floor = 0.35f; break;
        case EV_HIVE_BIRTH:      s = SND_HIVE_BIRTH; floor = 0.35f; break;
        case EV_HIVE_THRUSTER:   s = SND_HIVE_THRUSTER; floor = 0.5f; break;
        case EV_HIVE_PUSH:       s = (e->value > 0.5f) ? SND_HIVE_PUSH : SND_CHIME; positional = false; break;
        case EV_HIVE_SPIT_WARN:  s = SND_HIVE_SPIT; floor = 0.5f; break;
        case EV_SPORE_BLAST:     s = SND_SPORE; floor = 0.4f; break;
        case EV_BROOD:           s = SND_BROOD; positional = false; break;
        // Verme do Núcleo: o aviso de saída tem de ser ouvido de longe.
        case EV_WORM_RUMBLE:     s = SND_WORM_RUMBLE; floor = 0.55f; break;
        case EV_WORM_BREACH:     s = SND_WORM_BREACH; floor = 0.5f; break;
        case EV_WORM_DIVE:       s = SND_WORM_DIVE; floor = 0.35f; break;
        case EV_WORM_CORE:       s = SND_WORM_CORE; positional = false; break;
        case EV_WORM_NODULE:     s = SND_WORM_NODULE; floor = 0.5f; break;
        case EV_TUNNEL:          s = SND_TUNNEL; positional = false; break;
        // Devorador.
        case EV_DEV_AIM:         s = SND_DEV_AIM; positional = false; break;
        case EV_DEV_BITE:        s = SND_DEV_BITE; floor = 0.6f; break;
        case EV_DEV_MISS:
        case EV_DEV_TORN:        s = SND_DEV_SCREECH; floor = 0.6f; break;
        case EV_DEV_BROKEN:      s = SND_DEV_BROKEN; floor = 0.6f; break;
        case EV_DEV_SWALLOW:     s = SND_DEV_GULP; floor = 0.4f; break;
        case EV_DEV_LATCH:       s = SND_DEV_ROAR; positional = false; break;
        case EV_DEV_TEETH:       s = SND_DEV_TEETH; floor = 0.3f; break;
        // Sinergias.
        case EV_SUICIDE:         s = SND_LAUNCH; positional = false; volume = 0.6f; break;
        case EV_RESTORE_BEAM:    s = SND_REPAIR; floor = 0.4f; break;
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
    if (a->music_on) {
        StopAudioStream(a->music_stream);
        UnloadAudioStream(a->music_stream);
        a->music_on = false;
    }
    for (int i = 0; i < SND_COUNT; i++) {
        for (int v = 0; v < DEFS[i].voices; v++) UnloadSoundAlias(a->voices[i][v]);
        UnloadSound(a->sounds[i]);
    }
    CloseAudioDevice();
    a->ready = false;
}
