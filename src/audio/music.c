#include "audio/music.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define SR            ((float)MUSIC_SAMPLE_RATE)
#define TAU           6.28318530718f
#define BPM           112.0f
#define STEPS_PER_BAR 16          // semicolcheias por compasso
#define BARS          4           // compassos da progressão
#define FADE_SECONDS  1.6f        // tempo de uma camada entrar ou sair
#define GLIDE_SECONDS 0.12f       // o pad desliza de um acorde para o outro
#define DELAY_STEPS   3           // eco do arpejo: três semicolcheias
#define DELAY_MAX     20000       // amostras por canal (3 semicolcheias a 112 bpm = 17.700)
#define FEEDBACK      0.42f
#define MASTER        0.85f

enum { L_PAD, L_ARP, L_BASS, L_DRUMS, L_LEAD, L_TENSION, LAYERS };

// Quanto de cada camada soa em cada clima.
static const float MOOD_LAYERS[MOOD_COUNT][LAYERS] = {
    //               pad    arpejo baixo  bateria metais tensão
    [MOOD_SILENT]  = { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f },
    [MOOD_TITLE]   = { 0.9f,  0.75f, 0.0f,  0.0f,  0.0f,  0.0f },
    [MOOD_CALM]    = { 0.75f, 0.85f, 0.35f, 0.0f,  0.0f,  0.0f },
    [MOOD_BATTLE]  = { 0.6f,  0.3f,  0.9f,  0.85f, 0.0f,  0.0f },
    [MOOD_BOSS]    = { 0.5f,  0.0f,  1.0f,  1.0f,  0.85f, 0.0f },
    [MOOD_VICTORY] = { 0.9f,  1.0f,  0.4f,  0.0f,  0.0f,  0.0f },
    [MOOD_DEFEAT]  = { 0.75f, 0.0f,  0.0f,  0.0f,  0.0f,  0.0f },
};
#define TENSION_LEVEL 0.7f

// Notas MIDI: o acorde do pad (oitava 4) e a fundamental do baixo (oitava 1-2).
typedef struct {
    int pad[3];
    int root;
} Chord;

// Ré menor, i-VI-III-VII (Dm, Bb, F, C). A versão tensa termina em sol menor e
// lá maior, que pede a volta ao ré menor com mais urgência.
static const Chord PROGRESSION[2][BARS] = {
    { { { 62, 65, 69 }, 38 }, { { 62, 65, 70 }, 34 }, { { 60, 65, 69 }, 41 }, { { 60, 64, 67 }, 36 } },
    { { { 62, 65, 69 }, 38 }, { { 62, 65, 70 }, 34 }, { { 62, 67, 70 }, 43 }, { { 61, 64, 69 }, 33 } },
};

static const int ARP_PATTERN[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };   // notas do acorde, subindo e descendo
static const int BASS_PATTERN[8] = { 0, 0, 12, 0, 0, 0, 12, 7 };  // semitons acima da fundamental

// --- estado compartilhado entre as threads ----------------------------------
static atomic_int shared_mood = MOOD_SILENT;
static atomic_int shared_danger = 0;
static atomic_int shared_muffled = 0;

// --- estado da síntese (só a thread de áudio mexe) ----------------------------
typedef struct {
    float    step_len, step_pos;
    long     step;
    int      progression;
    bool     want_tense, boss_drums;
    float    gain[LAYERS];
    float    target[LAYERS];
    float    muffle, muffle_target;
    float    fade_k, glide_k;
    uint32_t noise;

    // decaimentos por amostra (envelopes multiplicativos)
    float d_arp, d_bass, d_bass_bright, d_kick, d_kick_pitch, d_click, d_snare, d_snare_tone, d_hat;
    float d_lead, d_lead_bright, d_tick;

    float pad_freq[3], pad_target[3], pad_phase[3][2], pad_lp[2][2], lfo;
    float arp_freq, arp_phase, arp_env;
    float bass_freq, bass_phase, bass_sub, bass_env, bass_bright, bass_lp[2];
    float kick_phase, kick_env, kick_pitch, kick_click;
    float snare_phase, snare_env, snare_tone, snare_lp;
    float hat_env, hat_level, hat_lp;
    float lead_freq[3], lead_phase[3], lead_env, lead_bright, lead_lp[2];
    float tick_phase, tick_env, tick_level;
    float delay[2][DELAY_MAX];
    int   delay_pos, delay_len;
    float master_lp[2];
} Synth;

static Synth s;

static float midi_hz(int note)
{
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
}

static float decay(float per_second)
{
    return expf(-per_second / SR);
}

// Coeficiente de um filtro passa-baixa de um polo para `cutoff` Hz.
static float lp_coef(float cutoff)
{
    return fminf(TAU * cutoff / SR, 1.0f);
}

static float noise(void)
{
    s.noise ^= s.noise << 13;
    s.noise ^= s.noise >> 17;
    s.noise ^= s.noise << 5;
    return (float)(s.noise >> 8) / 8388608.0f - 1.0f;
}

static float wrap(float phase)
{
    return phase - floorf(phase);
}

static float saw(float phase)
{
    return 2.0f * phase - 1.0f;
}

void music_reset(void)
{
    memset(&s, 0, sizeof s);
    s.step_len = SR * 60.0f / BPM / 4.0f;
    s.step_pos = s.step_len;   // o primeiro passo dispara na primeira amostra
    s.step = -1;
    s.noise = 0x2545f491u;
    s.fade_k = 1.0f - expf(-1.0f / (FADE_SECONDS * SR));
    s.glide_k = 1.0f - expf(-1.0f / (GLIDE_SECONDS * SR));
    s.d_arp = decay(5.0f);
    s.d_bass = decay(3.5f);
    s.d_bass_bright = decay(12.0f);
    s.d_kick = decay(9.0f);
    s.d_kick_pitch = decay(28.0f);
    s.d_click = decay(400.0f);
    s.d_snare = decay(16.0f);
    s.d_snare_tone = decay(24.0f);
    s.d_hat = decay(42.0f);
    s.d_lead = decay(4.0f);
    s.d_lead_bright = decay(9.0f);
    s.d_tick = decay(70.0f);
    s.delay_len = (int)(s.step_len * (float)DELAY_STEPS);
    if (s.delay_len > DELAY_MAX) s.delay_len = DELAY_MAX;
    for (int v = 0; v < 3; v++) {
        s.pad_freq[v] = s.pad_target[v] = midi_hz(PROGRESSION[0][0].pad[v]);
        s.lead_freq[v] = s.pad_freq[v];
    }
    s.arp_freq = s.bass_freq = midi_hz(PROGRESSION[0][0].root);
}

void music_set(MusicMood mood, bool danger, bool muffled)
{
    if ((int)mood < 0 || mood >= MOOD_COUNT) mood = MOOD_SILENT;
    atomic_store(&shared_mood, (int)mood);
    atomic_store(&shared_danger, danger ? 1 : 0);
    atomic_store(&shared_muffled, muffled ? 1 : 0);
}

// Uma semicolcheia: dispara as notas e as batidas dela.
static void on_step(void)
{
    s.step++;
    int beat = (int)(s.step % STEPS_PER_BAR);
    int bar = (int)((s.step / STEPS_PER_BAR) % BARS);
    if (beat == 0) s.progression = s.want_tense ? 1 : 0;   // a harmonia só muda na virada do compasso
    const Chord *c = &PROGRESSION[s.progression][bar];

    if (beat == 0) {
        for (int v = 0; v < 3; v++) s.pad_target[v] = midi_hz(c->pad[v]);
    }
    if (beat % 2 == 0) {
        int i = ARP_PATTERN[(beat / 2) % 8];
        s.arp_freq = midi_hz(c->pad[i % 3] + 12 * (i / 3) + 12);
        s.arp_env = 1.0f;
        s.bass_freq = midi_hz(c->root + BASS_PATTERN[beat / 2]);
        s.bass_env = 1.0f;
        s.bass_bright = 1.0f;
    }

    // Bateria: no boss, bumbo em todo tempo e chimbal em toda semicolcheia.
    bool fill = (bar == BARS - 1) && beat >= 13;   // virada no fim da progressão
    bool kick, hat, accent;
    if (s.boss_drums) {
        kick = (beat % 4 == 0) || beat == 14;
        hat = true;
        accent = (beat % 2 == 1);
    } else {
        kick = beat == 0 || beat == 8 || beat == 11;
        hat = (beat % 2 == 0);
        accent = (beat % 4 == 2);
    }
    if (kick) {
        s.kick_env = 1.0f;
        s.kick_pitch = 1.0f;
        s.kick_click = 1.0f;
        s.kick_phase = 0.0f;
    }
    if (beat == 4 || beat == 12 || fill) {
        s.snare_env = fill ? 0.65f : 1.0f;
        s.snare_tone = 1.0f;
    }
    if (hat) {
        s.hat_env = 1.0f;
        s.hat_level = accent ? 1.0f : 0.5f;
    }

    // Metais do boss: acordes curtos em ritmo sincopado.
    if (beat == 0 || beat == 3 || beat == 6 || beat == 10 || beat == 12) {
        s.lead_freq[0] = midi_hz(c->pad[0] - 12);
        s.lead_freq[1] = midi_hz(c->pad[2] - 12);
        s.lead_freq[2] = midi_hz(c->pad[0]);
        s.lead_env = (beat == 0) ? 1.0f : 0.8f;
        s.lead_bright = 1.0f;
    }

    s.tick_env = 1.0f;
    s.tick_level = (beat % 4 == 0) ? 1.0f : 0.45f;
}

void music_render(float *stereo, int frames)
{
    // O clima vale para o bloco inteiro.
    int mood = atomic_load(&shared_mood);
    bool danger = atomic_load(&shared_danger) != 0;
    bool muffled = atomic_load(&shared_muffled) != 0;
    if (mood < 0 || mood >= MOOD_COUNT) mood = MOOD_SILENT;
    memcpy(s.target, MOOD_LAYERS[mood], sizeof s.target);
    if (danger && (mood == MOOD_CALM || mood == MOOD_BATTLE || mood == MOOD_BOSS)) s.target[L_TENSION] = TENSION_LEVEL;
    s.want_tense = danger || mood == MOOD_DEFEAT;
    s.boss_drums = (mood == MOOD_BOSS);
    s.muffle_target = muffled ? 1.0f : 0.0f;

    float k_snare = lp_coef(1500.0f), k_hat = lp_coef(7000.0f);
    for (int i = 0; i < frames; i++) {
        s.step_pos += 1.0f;
        if (s.step_pos >= s.step_len) {
            s.step_pos -= s.step_len;
            on_step();
        }
        for (int l = 0; l < LAYERS; l++) s.gain[l] += (s.target[l] - s.gain[l]) * s.fade_k;
        s.muffle += (s.muffle_target - s.muffle) * s.fade_k * 4.0f;

        float left = 0.0f, right = 0.0f;

        // Pad: três vozes, cada uma com duas serras desafinadas (uma por lado),
        // num filtro que respira devagar.
        s.lfo = wrap(s.lfo + 0.07f / SR);
        float k_pad = lp_coef(950.0f + 380.0f * sinf(TAU * s.lfo));
        float pl = 0.0f, pr = 0.0f;
        for (int v = 0; v < 3; v++) {
            s.pad_freq[v] += (s.pad_target[v] - s.pad_freq[v]) * s.glide_k;
            s.pad_phase[v][0] = wrap(s.pad_phase[v][0] + s.pad_freq[v] * 1.0035f / SR);
            s.pad_phase[v][1] = wrap(s.pad_phase[v][1] + s.pad_freq[v] * 0.9965f / SR);
            pl += saw(s.pad_phase[v][0]);
            pr += saw(s.pad_phase[v][1]);
        }
        s.pad_lp[0][0] += (pl - s.pad_lp[0][0]) * k_pad;
        s.pad_lp[0][1] += (s.pad_lp[0][0] - s.pad_lp[0][1]) * k_pad;
        s.pad_lp[1][0] += (pr - s.pad_lp[1][0]) * k_pad;
        s.pad_lp[1][1] += (s.pad_lp[1][0] - s.pad_lp[1][1]) * k_pad;
        left += s.pad_lp[0][1] * 0.16f * s.gain[L_PAD];
        right += s.pad_lp[1][1] * 0.16f * s.gain[L_PAD];

        // Arpejo: um sininho (seno com harmônicos) que vai para o eco.
        s.arp_phase = wrap(s.arp_phase + s.arp_freq / SR);
        float sn = sinf(TAU * s.arp_phase), cs = cosf(TAU * s.arp_phase);
        float bell = sn + 0.3f * (2.0f * sn * cs) + 0.12f * (3.0f * sn - 4.0f * sn * sn * sn);
        float arp = bell * s.arp_env * 0.12f * s.gain[L_ARP];
        s.arp_env *= s.d_arp;
        left += arp;
        right += arp;

        // Baixo: serra com uma senoide uma oitava abaixo, filtro que fecha a cada nota.
        s.bass_phase = wrap(s.bass_phase + s.bass_freq / SR);
        s.bass_sub = wrap(s.bass_sub + s.bass_freq * 0.5f / SR);
        float raw = 0.7f * saw(s.bass_phase) + 0.6f * sinf(TAU * s.bass_sub);
        float k_bass = lp_coef(140.0f + 1100.0f * s.bass_bright);
        s.bass_lp[0] += (raw - s.bass_lp[0]) * k_bass;
        s.bass_lp[1] += (s.bass_lp[0] - s.bass_lp[1]) * k_bass;
        float bass = s.bass_lp[1] * s.bass_env * 0.42f * s.gain[L_BASS];
        s.bass_env *= s.d_bass;
        s.bass_bright *= s.d_bass_bright;
        left += bass;
        right += bass;

        // Bateria.
        s.kick_phase = wrap(s.kick_phase + (46.0f + 110.0f * s.kick_pitch) / SR);
        float kick = sinf(TAU * s.kick_phase) * s.kick_env + noise() * 0.3f * s.kick_click;
        s.kick_env *= s.d_kick;
        s.kick_pitch *= s.d_kick_pitch;
        s.kick_click *= s.d_click;

        float n = noise();
        s.snare_lp += (n - s.snare_lp) * k_snare;
        s.snare_phase = wrap(s.snare_phase + 185.0f / SR);
        float snare = (n - s.snare_lp) * s.snare_env * 0.7f + sinf(TAU * s.snare_phase) * s.snare_tone * 0.5f;
        s.snare_env *= s.d_snare;
        s.snare_tone *= s.d_snare_tone;

        float hn = noise();
        s.hat_lp += (hn - s.hat_lp) * k_hat;
        float hat = (hn - s.hat_lp) * s.hat_env * s.hat_level * 0.32f;
        s.hat_env *= s.d_hat;

        float drums = s.gain[L_DRUMS];
        left += (0.9f * kick + 0.5f * snare + 0.8f * hat) * drums;
        right += (0.9f * kick + 0.5f * snare + 1.0f * hat) * drums;

        // Metais: três serras abertas e fechadas pelo filtro, como um naipe.
        float brass = 0.0f;
        for (int v = 0; v < 3; v++) {
            s.lead_phase[v] = wrap(s.lead_phase[v] + s.lead_freq[v] / SR);
            brass += saw(s.lead_phase[v]);
        }
        float k_lead = lp_coef(300.0f + 2600.0f * s.lead_bright);
        s.lead_lp[0] += (brass - s.lead_lp[0]) * k_lead;
        s.lead_lp[1] += (s.lead_lp[0] - s.lead_lp[1]) * k_lead;
        float lead = s.lead_lp[1] * s.lead_env * 0.15f * s.gain[L_LEAD];
        s.lead_env *= s.d_lead;
        s.lead_bright *= s.d_lead_bright;
        left += lead;
        right += lead;

        // Tensão: um tique agudo em toda semicolcheia.
        s.tick_phase = wrap(s.tick_phase + 2093.0f / SR);
        float tick = sinf(TAU * s.tick_phase) * s.tick_env * s.tick_level * 0.07f * s.gain[L_TENSION];
        s.tick_env *= s.d_tick;
        left += tick * 0.6f;
        right += tick;

        // Eco pingue-pongue (arpejo e um pouco dos metais).
        float send = arp + lead * 0.3f;
        float dl = s.delay[0][s.delay_pos], dr = s.delay[1][s.delay_pos];
        s.delay[0][s.delay_pos] = send + dr * FEEDBACK;
        s.delay[1][s.delay_pos] = dl * FEEDBACK;
        s.delay_pos = (s.delay_pos + 1) % s.delay_len;
        left += dl * 0.5f;
        right += dr * 0.5f;

        // Pausa: abafa e abaixa. Por fim, um limitador suave.
        float k_master = lp_coef(16000.0f * (1.0f - s.muffle) + 450.0f * s.muffle);
        s.master_lp[0] += (left - s.master_lp[0]) * k_master;
        s.master_lp[1] += (right - s.master_lp[1]) * k_master;
        float level = MASTER * (1.0f - 0.45f * s.muffle);
        stereo[2 * i] = tanhf(s.master_lp[0] * level);
        stereo[2 * i + 1] = tanhf(s.master_lp[1] * level);
    }
}
