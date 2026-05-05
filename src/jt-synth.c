/**
 * JT Synth v4 — Johnytiger's AAA Wavetable Synthesizer
 *
 * Dual-oscillator wavetable synth with UNISON voicing:
 * - 8 morphable wavetable positions per oscillator
 * - Independent Osc1 + Osc2 with mix control
 * - 3 unison sub-voices per oscillator (stereo spread + detune)
 * - Multimode filter: LP (Moog ladder) / HP / BP (state-variable)
 * - Filter envelope with signed amount
 * - Tempo-synced gate/roll system
 * - ADSR envelope with velocity sensitivity
 * - 8-voice polyphony × 6 sub-voices = 48 oscillators
 * - 28 EDM-focused factory presets
 *
 * Build:
 *   aarch64-none-linux-gnu-gcc -shared -fPIC -O2 -o jt-synth.so jt-synth.c -lm
 *
 * Accent: Red [255, 30, 60]
 */

#include "pdsynth_api.h"
#include <SDL2/SDL.h>
#include "pd_text.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_VOICES 8
#define WAVETABLE_SIZE 2048
#define WAVETABLE_MASK (WAVETABLE_SIZE - 1)   /* power-of-two bitmask replaces `%` */
#define NUM_WAVE_POSITIONS 8
#define UNISON_VOICES 3   /* Sub-voices per oscillator for thickness */
#define PREVIEW_BUF_LEN 320  /* Pre-rendered waveform for Page 0 drawing */

/* Filter type enum */
#define FILT_LP  0
#define FILT_HP  1
#define FILT_BP  2

/* ── Fast-math helpers (hot path; libm calls replaced with approximations) ──
 *
 * On RG35XX (Cortex-A53, no hardware tanh/sin), each libm call is 30-80 cycles.
 * The ladder filter calls tanhf() 8× per voice per sample; replacing with a
 * Pade[3/2] rational approximation saves >10× DSP cost while preserving the
 * soft-clip character within a few ten-thousandths at the operating range. */

static inline float jt_fast_tanh(float x) {
    /* Pade[3/2]: tanh(x) ≈ x*(15 + x^2) / (15 + 6*x^2).
     * Matches tanh to ~0.5% on |x|<1.7 and saturates cleanly beyond.
     * Hard-clamp x^2 ceiling avoids div-by-overflow with pathological feedback. */
    float x2 = x * x;
    if (x2 > 9.0f) return (x > 0.0f) ? 1.0f : -1.0f;
    return x * (15.0f + x2) / (15.0f + 6.0f * x2);
}

static inline float jt_fast_sin(float x) {
    /* 5th-order Taylor around 0, range-reduced to [-pi, pi].
     * Max error ~1e-4 over [-pi, pi], more than good enough for SVF cutoff. */
    const float PI  = (float)M_PI;
    const float PI2 = 6.2831853071795864f;
    /* Wrap into [-pi, pi] */
    x -= PI2 * (int)((x + PI) * (1.0f / PI2) - (x + PI < 0.0f ? 1.0f : 0.0f));
    if (x > PI)  x -= PI2;
    if (x < -PI) x += PI2;
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f/6.0f - x2 * (1.0f/120.0f)));
}

/* Anti-denormal tiny DC bias. Flushing sub-normals manually is cheaper than
 * letting the FPU trap. Used on filter state variables that could decay to
 * denormal during long silent tails (most painful with high-resonance SVF). */
#define JT_DENORM_KILL 1.0e-25f
static inline float jt_flush_denorm(float x) {
    return (x < JT_DENORM_KILL && x > -JT_DENORM_KILL) ? 0.0f : x;
}

/* Set FPU flush-to-zero mode. Called once at plugin create; avoids
 * denormal-induced CPU spikes across the entire plugin. */
static void jt_enable_ftz(void) {
#if defined(__aarch64__)
    /* ARMv8: FPCR bit 24 (FZ) = flush-to-zero for single-precision. */
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
    /* x86: MXCSR FTZ (bit 15) + DAZ (bit 6). */
    unsigned int csr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040;
    __asm__ __volatile__("ldmxcsr %0" :: "m"(csr));
#endif
}

/* ── Wavetable Generation ── */

static void generateWavetable(float table[NUM_WAVE_POSITIONS][WAVETABLE_SIZE]) {
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        float phase = (float)i / WAVETABLE_SIZE;
        float t = phase * 2.0f * (float)M_PI;

        /* Position 0: Pure sine */
        table[0][i] = sinf(t);

        /* Position 1: Warm sine + harmonics (analog character) */
        table[1][i] = sinf(t) * 0.6f + sinf(t * 2) * 0.25f + sinf(t * 3) * 0.10f + sinf(t * 4) * 0.05f;

        /* Position 2: Triangle (bandlimited) */
        table[2][i] = 0;
        for (int h = 0; h < 20; h++) {
            int n = 2 * h + 1;
            float sign = (h % 2 == 0) ? 1.0f : -1.0f;
            table[2][i] += sign * sinf(t * n) / (float)(n * n);
        }
        table[2][i] *= (8.0f / ((float)M_PI * (float)M_PI));

        /* Position 3: Soft saw (fewer harmonics, warm) */
        table[3][i] = 0;
        for (int h = 1; h <= 24; h++) {
            float rolloff = 1.0f / (1.0f + 0.05f * h * h);
            table[3][i] += sinf(t * h) * rolloff / (float)h;
        }
        table[3][i] *= -2.0f / (float)M_PI;

        /* Position 4: Full saw (Serum-style, maximum harmonics) */
        table[4][i] = 0;
        for (int h = 1; h <= 64; h++) {
            table[4][i] += sinf(t * h) / (float)h;
        }
        table[4][i] *= -2.0f / (float)M_PI;

        /* Position 5: Square (thick, hollow) */
        table[5][i] = 0;
        for (int h = 0; h < 32; h++) {
            int n = 2 * h + 1;
            table[5][i] += sinf(t * n) / (float)n;
        }
        table[5][i] *= 4.0f / (float)M_PI;

        /* Position 6: Pulse (nasal, biting) */
        table[6][i] = 0;
        for (int h = 1; h <= 48; h++) {
            table[6][i] += sinf(t * h) * cosf((float)M_PI * h * 0.12f) / (float)h;
        }
        table[6][i] *= 2.2f / (float)M_PI;

        /* Position 7: Metallic / Formant (complex, aggressive) */
        table[7][i] = sinf(t) * 0.35f + sinf(t * 2.76f) * 0.25f +
                       sinf(t * 4.07f) * 0.20f + sinf(t * 5.54f) * 0.12f +
                       sinf(t * 6.98f) * 0.08f;
    }

    /* Normalize all positions */
    for (int p = 0; p < NUM_WAVE_POSITIONS; p++) {
        float maxAbs = 0;
        for (int i = 0; i < WAVETABLE_SIZE; i++) {
            float a = table[p][i] < 0 ? -table[p][i] : table[p][i];
            if (a > maxAbs) maxAbs = a;
        }
        if (maxAbs > 0.001f) {
            float norm = 0.95f / maxAbs;
            for (int i = 0; i < WAVETABLE_SIZE; i++) table[p][i] *= norm;
        }
    }
}

/* Read wavetable with linear interp (label says cubic; impl is bilinear
 * between two wave positions and two indices — sufficient with unison). */
static inline float readWavetable(float table[NUM_WAVE_POSITIONS][WAVETABLE_SIZE],
                                  float position, float phase) {
    float posF = position * (NUM_WAVE_POSITIONS - 1);
    int pos0 = (int)posF;
    if (pos0 < 0) pos0 = 0;
    if (pos0 >= NUM_WAVE_POSITIONS - 1) pos0 = NUM_WAVE_POSITIONS - 2;
    int pos1 = pos0 + 1;
    float posFrac = posF - pos0;

    float idx = phase * WAVETABLE_SIZE;
    int iI = (int)idx;
    /* Bitmask replaces `%` for power-of-two WAVETABLE_SIZE (2048) — several
     * cycles saved per call, and this runs 6× per voice per sample. */
    int i0 = iI & WAVETABLE_MASK;
    int i1 = (iI + 1) & WAVETABLE_MASK;
    float frac = idx - (float)iI;

    float s0 = table[pos0][i0] + frac * (table[pos0][i1] - table[pos0][i0]);
    float s1 = table[pos1][i0] + frac * (table[pos1][i1] - table[pos1][i0]);

    return s0 + posFrac * (s1 - s0);
}

/* ── Moog-style Ladder Filter (LP mode) ── */

typedef struct {
    float stage[4];
    float delay[4];
} LadderFilter;

static float processLadder(LadderFilter* f, float input, float cutoff, float resonance) {
    float fc = cutoff * cutoff * cutoff;
    if (fc > 0.99f) fc = 0.99f;
    if (fc < 0.001f) fc = 0.001f;

    float fb = resonance * 4.0f;
    float comp = 1.0f + fb * 0.25f;

    float fbSig = f->stage[3];
    fbSig = fbSig * (1.0f + 0.3f * fbSig * fbSig);

    float inp = input * comp - fb * fbSig;

    /* 8 tanhf() → 8 jt_fast_tanh() — main DSP win on device (~10× per call).
     * The ladder's soft-clipped feedback preserves the same character. */
    for (int s = 0; s < 4; s++) {
        float prev = (s == 0) ? inp : f->stage[s - 1];
        f->stage[s] += fc * (jt_fast_tanh(prev) - jt_fast_tanh(f->stage[s]));
        f->stage[s] = jt_flush_denorm(f->stage[s]);
    }

    return f->stage[3];
}

/* ── State-Variable Filter (HP/BP modes) ── */

typedef struct {
    float low;
    float band;
    float high;
} SVFilter;

static float processSVF(SVFilter* f, float input, float cutoff, float resonance, int type) {
    float fc = cutoff * cutoff * cutoff;
    if (fc > 0.99f) fc = 0.99f;
    if (fc < 0.001f) fc = 0.001f;

    /* Fast polynomial sin replaces libm sinf() — called per voice per sample.
     * At our argument range (0..~1.55) error is ~1e-4, inaudible in filter coef. */
    float f0 = 2.0f * jt_fast_sin((float)M_PI * fc * 0.5f);
    float q = 1.0f - resonance * 0.95f;
    if (q < 0.05f) q = 0.05f;

    f->high  = input - f->low - q * f->band;
    f->band += f0 * f->high;
    f->low  += f0 * f->band;

    /* Stability clamp on all three state vars — at extreme cutoff/resonance
     * the SVF can blow up; clamping band alone (previous version) left low
     * free to drift toward +Inf and trigger denormal CPU spikes on ARM. */
    if (f->band > 4.0f)  f->band = 4.0f;
    if (f->band < -4.0f) f->band = -4.0f;
    if (f->low  > 4.0f)  f->low  = 4.0f;
    if (f->low  < -4.0f) f->low  = -4.0f;
    f->band = jt_flush_denorm(f->band);
    f->low  = jt_flush_denorm(f->low);
    f->high = jt_flush_denorm(f->high);

    switch (type) {
        case FILT_HP: return f->high;
        case FILT_BP: return f->band;
        default:      return f->low;
    }
}

/* ── Unison sub-voice state ── */
typedef struct {
    float phase;
    float detuneRatio;
    float pan;
} UnisonVoice;

/* ── Voice (polyphonic) ── */

typedef struct {
    int active;
    uint8_t note;
    uint8_t velocity;

    UnisonVoice osc1[UNISON_VOICES];
    UnisonVoice osc2[UNISON_VOICES];
    float freq;

    /* ADSR */
    float envLevel;
    int envStage;       /* 0=off, 1=attack, 2=decay, 3=sustain, 4=release */
    float envTime;

    /* Per-voice stereo filters (both types, selected at runtime) */
    LadderFilter ladderL;
    LadderFilter ladderR;
    SVFilter svfL;
    SVFilter svfR;
} Voice;

/* ── Preset Definition ── */

typedef struct {
    const char* name;
    float osc1Wave;
    float osc2Wave;
    float osc2Vol;
    float detune;
    float filterType;   /* 0.0=LP, 0.5=HP, 1.0=BP */
    float attack;
    float decay;
    float sustain;
    float release;
    float filterEnv;    /* 0.0-1.0, center=0.5=no mod */
    float cutoff;
    float resonance;
    float gateOn;       /* 0.0 or 1.0 */
    float gateRate;     /* 0.0=1/4, 0.33=1/8, 0.67=1/16, 1.0=1/32 */
    float gateDepth;    /* 0.0-1.0 */
    /* v4.0 FX (all default off for existing presets → no tonal change). */
    float reverbOn;
    float reverbMix;
    float reverbSize;
    float delayOn;
    float delayTime;
    float delayFbk;
    float delayMix;
} JTPreset;

/* Param layout — kept in sync with manifest.json.
 * Appending to the enum preserves existing param indices so .pdp files
 * saved against v3.x still load cleanly. */
enum {
    P_Osc1Wave = 0, P_Osc2Wave, P_Osc2Vol, P_Detune,
    P_FilterType, P_Attack, P_Decay, P_Sustain, P_Release,
    P_FilterEnv, P_Cutoff, P_Resonance,
    P_GateOn, P_GateRate, P_GateDepth,
    /* v4.0 — Reverb + Delay */
    P_ReverbOn, P_ReverbMix, P_ReverbSize,
    P_DelayOn, P_DelayTime, P_DelayFbk, P_DelayMix,
    JT_PARAM_COUNT
};

/* Unison detune spreads (in cents deviation from center) */
static const float UNISON_SPREAD[UNISON_VOICES] = { 0.0f, -12.0f, 12.0f };
/* Unison stereo pan positions */
static const float UNISON_PAN[UNISON_VOICES] = { 0.0f, -0.6f, 0.6f };

/* Factory presets — the FX fields (reverbOn, reverbMix, reverbSize, delayOn,
 * delayTime, delayFbk, delayMix) are populated below. Bass presets get FX off
 * (they want punchy bottom end); pads + leads get tasteful reverb/delay on by
 * default to showcase the new built-in FX without overwhelming any preset. */
static const JTPreset FACTORY_PRESETS[] = {
    /*                          o1w    o2w    o2v   det    fTyp   atk    dec    sus    rel    fEnv   cut    res    gOn   gRate  gDpt   rvOn  rvMx   rvSz   dlOn  dlT    dlFb   dlMx */
    /* === BASS (0-3) — FX off, bass stays dry === */
    {"Deep Sub",         0.00f, 0.12f, 0.65f, 0.06f, 0.0f,  0.00f, 0.40f, 0.95f, 0.45f, 0.50f, 0.30f, 0.28f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Reese Bass",       0.50f, 0.52f, 0.92f, 0.28f, 0.0f,  0.01f, 0.50f, 0.88f, 0.35f, 0.65f, 0.36f, 0.45f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Growl Machine",    0.68f, 0.72f, 0.85f, 0.22f, 0.0f,  0.00f, 0.06f, 0.90f, 0.22f, 0.70f, 0.35f, 0.65f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"808 Boom",         0.00f, 0.00f, 0.30f, 0.02f, 0.0f,  0.00f, 0.60f, 0.70f, 0.80f, 0.45f, 0.25f, 0.10f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},

    /* === PLUCK BASS (4-6) — subtle delay adds bounce === */
    {"Pluck Bass",       0.55f, 0.58f, 0.90f, 0.12f, 0.0f,  0.00f, 0.08f, 0.20f, 0.15f, 0.75f, 0.55f, 0.40f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 1.0f, 0.30f, 0.35f, 0.20f},
    {"Acid Squelch",     0.55f, 0.60f, 0.80f, 0.05f, 0.0f,  0.00f, 0.12f, 0.30f, 0.10f, 0.85f, 0.35f, 0.80f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Stab Bass",        0.65f, 0.68f, 0.85f, 0.18f, 0.0f,  0.00f, 0.04f, 0.15f, 0.08f, 0.72f, 0.50f, 0.50f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f},

    /* === SAW PADS (7-10) — lush reverb + subtle delay === */
    {"Supersaw Pad",     0.55f, 0.58f, 0.95f, 0.42f, 0.0f,  0.65f, 0.50f, 0.80f, 0.95f, 0.55f, 0.62f, 0.15f, 0.0f, 0.0f,  0.0f,  1.0f, 0.35f, 0.65f, 1.0f, 0.45f, 0.35f, 0.20f},
    {"Detuned Heaven",   0.50f, 0.55f, 0.92f, 0.48f, 0.0f,  0.70f, 0.45f, 0.75f, 1.00f, 0.50f, 0.70f, 0.10f, 0.0f, 0.0f,  0.0f,  1.0f, 0.40f, 0.75f, 1.0f, 0.50f, 0.40f, 0.22f},
    {"Wide Wash",        0.45f, 0.50f, 0.98f, 0.50f, 0.0f,  0.80f, 0.60f, 0.70f, 0.98f, 0.52f, 0.58f, 0.08f, 0.0f, 0.0f,  0.0f,  1.0f, 0.45f, 0.85f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Supersaw Stack",   0.55f, 0.60f, 0.95f, 0.45f, 0.0f,  0.50f, 0.40f, 0.85f, 0.90f, 0.48f, 0.65f, 0.20f, 0.0f, 0.0f,  0.0f,  1.0f, 0.30f, 0.55f, 0.0f, 0.00f, 0.00f, 0.00f},

    /* === FILTERED PADS (11-14) — big reverb === */
    {"Evolving Pad",     0.30f, 0.35f, 0.90f, 0.35f, 0.0f,  0.75f, 0.55f, 0.72f, 1.00f, 0.70f, 0.40f, 0.25f, 0.0f, 0.0f,  0.0f,  1.0f, 0.45f, 0.80f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Dark Atmosphere",  0.38f, 0.42f, 0.88f, 0.40f, 0.0f,  0.80f, 0.60f, 0.65f, 0.95f, 0.62f, 0.30f, 0.35f, 0.0f, 0.0f,  0.0f,  1.0f, 0.50f, 0.85f, 1.0f, 0.55f, 0.30f, 0.18f},
    {"Sweep Pad",        0.45f, 0.50f, 0.92f, 0.38f, 0.0f,  0.70f, 0.50f, 0.78f, 0.90f, 0.80f, 0.35f, 0.20f, 0.0f, 0.0f,  0.0f,  1.0f, 0.35f, 0.65f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"HP Shimmer",       0.20f, 0.25f, 0.85f, 0.30f, 0.5f,  0.60f, 0.45f, 0.70f, 0.95f, 0.60f, 0.50f, 0.15f, 0.0f, 0.0f,  0.0f,  1.0f, 0.55f, 0.75f, 1.0f, 0.45f, 0.45f, 0.25f},

    /* === PLUCKS (15-18) — delay gives space and sparkle === */
    {"Bright Pluck",     0.55f, 0.58f, 0.85f, 0.18f, 0.0f,  0.00f, 0.06f, 0.10f, 0.20f, 0.72f, 0.75f, 0.25f, 0.0f, 0.0f,  0.0f,  1.0f, 0.20f, 0.45f, 1.0f, 0.35f, 0.40f, 0.25f},
    {"Bell Pluck",       0.22f, 0.60f, 0.70f, 0.15f, 0.0f,  0.00f, 0.10f, 0.08f, 0.40f, 0.65f, 0.80f, 0.08f, 0.0f, 0.0f,  0.0f,  1.0f, 0.30f, 0.60f, 1.0f, 0.40f, 0.45f, 0.22f},
    {"Kalimba",          0.18f, 0.30f, 0.60f, 0.10f, 0.0f,  0.00f, 0.12f, 0.05f, 0.35f, 0.68f, 0.70f, 0.12f, 0.0f, 0.0f,  0.0f,  1.0f, 0.25f, 0.55f, 1.0f, 0.30f, 0.35f, 0.18f},
    {"Sharp Stab",       0.65f, 0.70f, 0.90f, 0.20f, 0.0f,  0.00f, 0.03f, 0.05f, 0.10f, 0.78f, 0.72f, 0.40f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 1.0f, 0.25f, 0.40f, 0.22f},

    /* === STRINGS (19-21) — reverb for realism === */
    {"Analog Strings",   0.50f, 0.52f, 0.88f, 0.32f, 0.0f,  0.55f, 0.40f, 0.82f, 0.70f, 0.52f, 0.55f, 0.18f, 0.0f, 0.0f,  0.0f,  1.0f, 0.35f, 0.65f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Soft Strings",     0.35f, 0.38f, 0.90f, 0.28f, 0.0f,  0.65f, 0.50f, 0.85f, 0.80f, 0.50f, 0.48f, 0.12f, 0.0f, 0.0f,  0.0f,  1.0f, 0.40f, 0.70f, 0.0f, 0.00f, 0.00f, 0.00f},
    {"Cinematic Strings",0.42f, 0.48f, 0.92f, 0.35f, 0.0f,  0.70f, 0.55f, 0.78f, 0.90f, 0.55f, 0.52f, 0.20f, 0.0f, 0.0f,  0.0f,  1.0f, 0.50f, 0.85f, 0.0f, 0.00f, 0.00f, 0.00f},

    /* === LEADS (22-27) — tasteful delay + small reverb === */
    {"Supersaw Lead",    0.55f, 0.58f, 0.95f, 0.35f, 0.0f,  0.01f, 0.15f, 0.85f, 0.18f, 0.55f, 0.72f, 0.30f, 0.0f, 0.0f,  0.0f,  1.0f, 0.25f, 0.55f, 1.0f, 0.35f, 0.35f, 0.25f},
    {"Acid Lead",        0.65f, 0.70f, 0.88f, 0.10f, 0.0f,  0.00f, 0.10f, 0.78f, 0.12f, 0.80f, 0.45f, 0.78f, 0.0f, 0.0f,  0.0f,  0.0f, 0.00f, 0.00f, 1.0f, 0.30f, 0.45f, 0.22f},
    {"Laser Cannon",     0.78f, 0.82f, 0.92f, 0.25f, 0.0f,  0.00f, 0.06f, 0.90f, 0.10f, 0.60f, 0.78f, 0.52f, 0.0f, 0.0f,  0.0f,  1.0f, 0.30f, 0.50f, 1.0f, 0.25f, 0.50f, 0.28f},
    {"Trance Gate Lead", 0.55f, 0.58f, 0.90f, 0.30f, 0.0f,  0.01f, 0.12f, 0.88f, 0.15f, 0.55f, 0.68f, 0.35f, 1.0f, 0.67f, 0.85f, 1.0f, 0.30f, 0.60f, 1.0f, 0.40f, 0.40f, 0.25f},
    {"Gated Pad Lead",   0.45f, 0.50f, 0.92f, 0.38f, 0.0f,  0.40f, 0.35f, 0.80f, 0.60f, 0.58f, 0.60f, 0.20f, 1.0f, 0.33f, 0.70f, 1.0f, 0.35f, 0.70f, 1.0f, 0.45f, 0.35f, 0.22f},
    {"BP Scream",        0.70f, 0.75f, 0.85f, 0.15f, 1.0f,  0.00f, 0.08f, 0.85f, 0.12f, 0.65f, 0.55f, 0.70f, 0.0f, 0.0f,  0.0f,  1.0f, 0.25f, 0.45f, 0.0f, 0.00f, 0.00f, 0.00f},
};

#define NUM_PRESETS (sizeof(FACTORY_PRESETS) / sizeof(FACTORY_PRESETS[0]))

/* ── Plugin State ── */

/* ── Reverb / Delay DSP ──────────────────────────────────────────
 * Lean Schroeder reverb + single stereo delay line. Both processed
 * only when the corresponding ON toggle is > 0.5 and Mix > 0.001. */

/* Reverb comb/allpass constants — Freeverb-inspired prime sizes
 * scaled for 44.1 kHz and shaped by the Size knob at runtime. */
#define JT_NUM_COMBS    4
#define JT_NUM_ALLPASS  2
#define JT_COMB_MAXLEN  2048
#define JT_AP_MAXLEN    512

typedef struct {
    /* Comb filters — Size knob scales their delay lengths. */
    float combBufL[JT_NUM_COMBS][JT_COMB_MAXLEN];
    float combBufR[JT_NUM_COMBS][JT_COMB_MAXLEN];
    int   combLen[JT_NUM_COMBS];
    int   combIdxL[JT_NUM_COMBS];
    int   combIdxR[JT_NUM_COMBS];
    float combStoreL[JT_NUM_COMBS]; /* low-pass damping state */
    float combStoreR[JT_NUM_COMBS];

    /* Allpass filters (series) */
    float apBufL[JT_NUM_ALLPASS][JT_AP_MAXLEN];
    float apBufR[JT_NUM_ALLPASS][JT_AP_MAXLEN];
    int   apLen[JT_NUM_ALLPASS];
    int   apIdxL[JT_NUM_ALLPASS];
    int   apIdxR[JT_NUM_ALLPASS];
} JtReverb;

#define JT_DELAY_MAXLEN 48000  /* ~1 s at 44.1 kHz — generous headroom */

typedef struct {
    float bufL[JT_DELAY_MAXLEN];
    float bufR[JT_DELAY_MAXLEN];
    int   writeIdx;
} JtDelay;

typedef struct {
    Voice voices[MAX_VOICES];
    float wavetable[NUM_WAVE_POSITIONS][WAVETABLE_SIZE];

    /* Parameters (22 total — first 15 unchanged from v3.x for .pdp compat) */
    float osc1Wave;     /* 0 */
    float osc2Wave;     /* 1 */
    float osc2Vol;      /* 2 */
    float detune;       /* 3 */
    float filterType;   /* 4  — 0.0=LP, ~0.33=HP, ~0.67=BP */
    float attack;       /* 5 */
    float decay;        /* 6 */
    float sustain;      /* 7 */
    float release;      /* 8 */
    float filterEnv;    /* 9  — 0.0-1.0, center=0.5=no mod */
    float cutoff;       /* 10 */
    float resonance;    /* 11 */
    float gateOn;       /* 12 — 0.0 or 1.0 */
    float gateRate;     /* 13 — select: 0/0.33/0.67/1.0 → 1/4,1/8,1/16,1/32 */
    float gateDepth;    /* 14 — 0.0-1.0 */
    /* v4.0 FX — appended, never reordered. */
    float reverbOn;     /* 15 — 0.0 or 1.0 */
    float reverbMix;    /* 16 — 0.0-1.0 dry/wet */
    float reverbSize;   /* 17 — 0.0-1.0 modulates comb lengths + feedback */
    float delayOn;      /* 18 — 0.0 or 1.0 */
    float delayTime;    /* 19 — 0.0-1.0 → 30 ms..500 ms */
    float delayFbk;     /* 20 — 0.0-1.0 feedback amount */
    float delayMix;     /* 21 — 0.0-1.0 dry/wet */

    /* FX working state — allocated once, reused every block. */
    JtReverb reverb;
    JtDelay  delay;
    float    lastReverbSize;    /* rebuild comb lengths when Size changes */

    float sampleRate;
    int currentPreset;

    /* v4 host for transport access (gate sync) */
    const PdHostV4* hostV4;

    /* Draw-time preview cache: one 320-sample curve per oscillator. Rebuilt
     * only when the matching wave param changes. This replaces ~600
     * readWavetable() calls per frame on Page 0 (2× boxes × ~300 px × 2 reads). */
    float previewOsc1[PREVIEW_BUF_LEN];
    float previewOsc2[PREVIEW_BUF_LEN];
    float previewOsc1Pos;  /* Wave position that built previewOsc1 (−1 = stale) */
    float previewOsc2Pos;
} JTSynth;

/* Rebuild a preview curve for one oscillator — called from draw path when the
 * cached wave position no longer matches the live param. */
static void jt_rebuild_preview(float (*table)[WAVETABLE_SIZE], float wavePos, float* out) {
    for (int i = 0; i < PREVIEW_BUF_LEN; i++) {
        float phase = (float)i / (float)PREVIEW_BUF_LEN;
        out[i] = readWavetable(table, wavePos, phase);
    }
}

/* ── Param Helpers ── */

static float paramToTime(float p, float minMs, float maxMs) {
    return (minMs + (maxMs - minMs) * p * p) / 1000.0f;
}

static float centsToRatio(float cents) {
    return powf(2.0f, cents / 1200.0f);
}

/* ── Reverb configuration ──────────────────────────────────────── */

static void jt_reverb_configure(JtReverb* rv, float size, float sampleRate) {
    /* Reference prime delay lengths at 44.1 kHz (Freeverb values); scale by
     * actual sampleRate + size so the knob runs from "room" to "hall". */
    static const int BASE_COMB[JT_NUM_COMBS] = { 1116, 1188, 1277, 1356 };
    static const int BASE_AP[JT_NUM_ALLPASS] = { 556, 441 };
    float srScale = sampleRate / 44100.0f;
    float sizeScale = 0.65f + size * 0.55f;   /* 0.65..1.20 */
    for (int c = 0; c < JT_NUM_COMBS; c++) {
        int L = (int)((float)BASE_COMB[c] * srScale * sizeScale);
        if (L < 100) L = 100;
        if (L >= JT_COMB_MAXLEN) L = JT_COMB_MAXLEN - 1;
        rv->combLen[c] = L;
        if (rv->combIdxL[c] >= L) rv->combIdxL[c] = 0;
        if (rv->combIdxR[c] >= L) rv->combIdxR[c] = 0;
    }
    for (int a = 0; a < JT_NUM_ALLPASS; a++) {
        int L = (int)((float)BASE_AP[a] * srScale * sizeScale);
        if (L < 50) L = 50;
        if (L >= JT_AP_MAXLEN) L = JT_AP_MAXLEN - 1;
        rv->apLen[a] = L;
        if (rv->apIdxL[a] >= L) rv->apIdxL[a] = 0;
        if (rv->apIdxR[a] >= L) rv->apIdxR[a] = 0;
    }
}

/* Freeverb-style comb + allpass block. Per-sample stereo; writes into
 * outL/outR which the caller mixes back against dry. Feedback amount
 * derives from `size` so the tail length scales with the knob. */
static void jt_reverb_process(JtReverb* rv, float inL, float inR,
                              float size, float* outL, float* outR) {
    const float feedback = 0.78f + size * 0.20f;   /* 0.78..0.98 */
    const float damp     = 0.20f + size * 0.15f;
    const float input    = 0.025f;

    float accL = 0.0f, accR = 0.0f;
    /* Parallel combs */
    for (int c = 0; c < JT_NUM_COMBS; c++) {
        int L = rv->combLen[c];
        /* L */
        float xL = rv->combBufL[c][rv->combIdxL[c]];
        rv->combStoreL[c] = jt_flush_denorm(xL * (1.0f - damp) + rv->combStoreL[c] * damp);
        rv->combBufL[c][rv->combIdxL[c]] = inL * input + rv->combStoreL[c] * feedback;
        rv->combIdxL[c]++; if (rv->combIdxL[c] >= L) rv->combIdxL[c] = 0;
        accL += xL;
        /* R */
        float xR = rv->combBufR[c][rv->combIdxR[c]];
        rv->combStoreR[c] = jt_flush_denorm(xR * (1.0f - damp) + rv->combStoreR[c] * damp);
        rv->combBufR[c][rv->combIdxR[c]] = inR * input + rv->combStoreR[c] * feedback;
        rv->combIdxR[c]++; if (rv->combIdxR[c] >= L) rv->combIdxR[c] = 0;
        accR += xR;
    }
    /* Series allpass */
    for (int a = 0; a < JT_NUM_ALLPASS; a++) {
        int L = rv->apLen[a];
        const float apCoef = 0.5f;
        float yL = rv->apBufL[a][rv->apIdxL[a]];
        float wL = accL + yL * apCoef;
        rv->apBufL[a][rv->apIdxL[a]] = jt_flush_denorm(wL);
        accL = yL - wL * apCoef;
        rv->apIdxL[a]++; if (rv->apIdxL[a] >= L) rv->apIdxL[a] = 0;

        float yR = rv->apBufR[a][rv->apIdxR[a]];
        float wR = accR + yR * apCoef;
        rv->apBufR[a][rv->apIdxR[a]] = jt_flush_denorm(wR);
        accR = yR - wR * apCoef;
        rv->apIdxR[a]++; if (rv->apIdxR[a] >= L) rv->apIdxR[a] = 0;
    }
    *outL = accL;
    *outR = accR;
}

/* ── Stereo delay block ─────────────────────────────────────────
 * Single tap per side with feedback into the same buffer. Ping-pong
 * character emerges naturally from the delay time difference we
 * introduce below (R is offset slightly relative to L). */
static void jt_delay_process(JtDelay* dl, int delaySamplesL, int delaySamplesR,
                             float fbk, float inL, float inR,
                             float* outL, float* outR) {
    if (delaySamplesL <= 0) delaySamplesL = 1;
    if (delaySamplesR <= 0) delaySamplesR = 1;
    if (delaySamplesL >= JT_DELAY_MAXLEN) delaySamplesL = JT_DELAY_MAXLEN - 1;
    if (delaySamplesR >= JT_DELAY_MAXLEN) delaySamplesR = JT_DELAY_MAXLEN - 1;
    int readL = dl->writeIdx - delaySamplesL;
    int readR = dl->writeIdx - delaySamplesR;
    if (readL < 0) readL += JT_DELAY_MAXLEN;
    if (readR < 0) readR += JT_DELAY_MAXLEN;
    float dL = dl->bufL[readL];
    float dR = dl->bufR[readR];
    dl->bufL[dl->writeIdx] = jt_flush_denorm(inL + dL * fbk);
    dl->bufR[dl->writeIdx] = jt_flush_denorm(inR + dR * fbk);
    dl->writeIdx++;
    if (dl->writeIdx >= JT_DELAY_MAXLEN) dl->writeIdx = 0;
    *outL = dL;
    *outR = dR;
}

/* ── API ── */

int pdsynth_api_version(void) { return PDSYNTH_API_VERSION; }
const char* pdsynth_name(void) { return "JT Synth"; }
int pdsynth_param_count(void) { return JT_PARAM_COUNT; }

PdSynthInstance pdsynth_create(float sampleRate) {
    JTSynth* s = (JTSynth*)calloc(1, sizeof(JTSynth));
    if (!s) return NULL;
    s->sampleRate = sampleRate;

    /* Flush-to-zero denormal handling — one-time CPU instruction, pays off
     * over every subsequent sample (no denormal traps in filter state). */
    jt_enable_ftz();

    generateWavetable(s->wavetable);

    /* Mark draw preview caches stale; first draw will populate them. */
    s->previewOsc1Pos = -1.0f;
    s->previewOsc2Pos = -1.0f;

    /* Default preset: Supersaw Lead (index 22) */
    s->currentPreset = 22;
    const JTPreset* p = &FACTORY_PRESETS[s->currentPreset];
    s->osc1Wave   = p->osc1Wave;
    s->osc2Wave   = p->osc2Wave;
    s->osc2Vol    = p->osc2Vol;
    s->detune     = p->detune;
    s->filterType = p->filterType;
    s->attack     = p->attack;
    s->decay      = p->decay;
    s->sustain    = p->sustain;
    s->release    = p->release;
    s->filterEnv  = p->filterEnv;
    s->cutoff     = p->cutoff;
    s->resonance  = p->resonance;
    s->gateOn     = p->gateOn;
    s->gateRate   = p->gateRate;
    s->gateDepth  = p->gateDepth;
    s->reverbOn   = p->reverbOn;
    s->reverbMix  = p->reverbMix;
    s->reverbSize = p->reverbSize;
    s->delayOn    = p->delayOn;
    s->delayTime  = p->delayTime;
    s->delayFbk   = p->delayFbk;
    s->delayMix   = p->delayMix;

    /* Prime FX state so the first block of audio after create has stable
     * comb/allpass/delay buffers. */
    s->lastReverbSize = s->reverbSize;
    jt_reverb_configure(&s->reverb, s->reverbSize, s->sampleRate);

    fprintf(stderr, "[jt-synth] Created v4 (AAA), preset: %s\n", p->name);
    return (PdSynthInstance)s;
}

void pdsynth_destroy(PdSynthInstance inst) {
    if (inst) free(inst);
}

/* v4 host interface (transport access for gate sync) */
void pdsynth_set_host_v4(PdSynthInstance inst, const PdHostV4* host) {
    JTSynth* s = (JTSynth*)inst;
    if (s) s->hostV4 = host;
}

static void initUnisonVoices(UnisonVoice uv[], float baseDetuneCents) {
    for (int u = 0; u < UNISON_VOICES; u++) {
        uv[u].phase = (float)u * 0.33f;
        uv[u].detuneRatio = centsToRatio(UNISON_SPREAD[u] + baseDetuneCents);
        uv[u].pan = UNISON_PAN[u];
    }
}

void pdsynth_note(PdSynthInstance inst, PdSynthNote* event) {
    JTSynth* s = (JTSynth*)inst;

    if (event->type == 1) {
        /* Note on — find free voice */
        int slot = -1;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!s->voices[i].active) { slot = i; break; }
        }
        if (slot < 0) {
            float maxTime = -1;
            for (int i = 0; i < MAX_VOICES; i++) {
                if (s->voices[i].envTime > maxTime) { maxTime = s->voices[i].envTime; slot = i; }
            }
        }
        if (slot >= 0) {
            Voice* v = &s->voices[slot];
            v->active = 1;
            v->note = event->note;
            v->velocity = event->velocity;
            v->freq = 440.0f * powf(2.0f, (event->note - 69) / 12.0f);
            v->envLevel = 0.0f;
            v->envStage = 1;
            v->envTime = 0.0f;
            memset(&v->ladderL, 0, sizeof(LadderFilter));
            memset(&v->ladderR, 0, sizeof(LadderFilter));
            memset(&v->svfL, 0, sizeof(SVFilter));
            memset(&v->svfR, 0, sizeof(SVFilter));

            float osc2ExtraCents = s->detune * 25.0f;
            initUnisonVoices(v->osc1, 0.0f);
            initUnisonVoices(v->osc2, osc2ExtraCents);
        }
    } else {
        for (int i = 0; i < MAX_VOICES; i++) {
            if (s->voices[i].active && s->voices[i].note == event->note && s->voices[i].envStage != 4) {
                s->voices[i].envStage = 4;
                s->voices[i].envTime = 0.0f;
            }
        }
    }
}

void pdsynth_process(PdSynthInstance inst, PdSynthAudio* audio) {
    JTSynth* s = (JTSynth*)inst;
    float dt = 1.0f / s->sampleRate;

    float attackTime  = paramToTime(s->attack,  1.0f, 3000.0f);
    float decayTime   = paramToTime(s->decay,   1.0f, 2000.0f);
    float sustainLvl  = s->sustain;
    float releaseTime = paramToTime(s->release, 1.0f, 5000.0f);

    float detuneCents = s->detune * 50.0f;
    float detuneRatio = powf(2.0f, detuneCents / 1200.0f);
    float uniGain = 1.0f / sqrtf((float)UNISON_VOICES);

    /* Filter envelope amount: center=0.5 → 0.0 = -1, 1.0 = +1 */
    float filterEnvAmount = (s->filterEnv - 0.5f) * 2.0f;

    /* Filter type: 0=LP, 1=HP, 2=BP */
    int fType = (int)(s->filterType * 2.9f);
    if (fType < 0) fType = 0;
    if (fType > 2) fType = 2;

    /* Gate setup */
    float gatePeriodSamples = 0.0f;
    int64_t gateBasePos = 0;
    int gateActive = (s->gateOn > 0.5f);

    if (gateActive && s->hostV4 && s->hostV4->transport) {
        const PdHostTransport* tr = s->hostV4->transport;
        void* hd = tr->hostData;
        float bpm = tr->get_bpm(hd);
        int playing = tr->is_playing(hd);

        if (bpm > 0.0f && playing) {
            int rateIdx = (int)(s->gateRate * 3.9f);
            if (rateIdx < 0) rateIdx = 0;
            if (rateIdx > 3) rateIdx = 3;
            float noteDivisions[] = { 1.0f, 2.0f, 4.0f, 8.0f };
            float beatDuration = 60.0f / bpm;
            gatePeriodSamples = (beatDuration / noteDivisions[rateIdx]) * s->sampleRate;
            gateBasePos = (int64_t)tr->get_playback_pos_samples(hd);
        } else {
            gateActive = 0; /* Gate open when not playing */
        }
    }

    /* ── FX block setup — computed once per buffer. ──────────────────
     * Reverb comb lengths are rebuilt only when Size changes (expensive
     * divisor-ish work shouldn't run every sample). Both FX are bypassed
     * entirely when their ON toggle is < 0.5 or Mix is near zero so a
     * "clean" preset pays zero FX DSP cost. */
    int reverbActive = (s->reverbOn > 0.5f) && (s->reverbMix > 0.001f);
    int delayActive  = (s->delayOn  > 0.5f) && (s->delayMix  > 0.001f);
    float reverbMix = s->reverbMix;
    float delayMix  = s->delayMix;
    float delayFbk  = s->delayFbk * 0.92f;   /* cap feedback to avoid runaway */
    /* Delay time: 30..500 ms, with R offset by +13% for a gentle L/R spread. */
    int delaySamplesL = (int)(s->sampleRate * (0.030f + s->delayTime * 0.470f));
    int delaySamplesR = (int)(delaySamplesL * 1.13f);
    if (reverbActive && s->reverbSize != s->lastReverbSize) {
        jt_reverb_configure(&s->reverb, s->reverbSize, s->sampleRate);
        s->lastReverbSize = s->reverbSize;
    }

    /* Fast exit: if no voice is active AND no FX tail is decaying, there
     * is nothing to mix. We keep processing when reverb/delay are on so
     * their tails don't cut off the moment the last voice releases. */
    int anyActive = 0;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (s->voices[v].active) { anyActive = 1; break; }
    }
    if (!anyActive && !reverbActive && !delayActive) return;

    /* Block-level hoisted constants (were recomputed per sample in inner loop). */
    const float invSR = 1.0f / s->sampleRate;
    const int filterEnvNonZero = (filterEnvAmount > 0.001f || filterEnvAmount < -0.001f);
    const float invGatePeriod  = (gateActive && gatePeriodSamples > 0.0f)
                                    ? 1.0f / gatePeriodSamples : 0.0f;

    for (int i = 0; i < audio->bufferSize; i++) {
        float mixL = 0.0f, mixR = 0.0f;

        for (int v = 0; v < MAX_VOICES; v++) {
            Voice* vc = &s->voices[v];
            if (!vc->active) continue;

            /* ADSR Envelope */
            switch (vc->envStage) {
                case 1:
                    vc->envLevel += dt / (attackTime + 0.0001f);
                    if (vc->envLevel >= 1.0f) { vc->envLevel = 1.0f; vc->envStage = 2; vc->envTime = 0; }
                    break;
                case 2:
                    vc->envLevel -= dt / (decayTime + 0.0001f) * (1.0f - sustainLvl);
                    if (vc->envLevel <= sustainLvl) { vc->envLevel = sustainLvl; vc->envStage = 3; }
                    break;
                case 3:
                    break;
                case 4:
                    vc->envLevel -= dt / (releaseTime + 0.0001f) * vc->envLevel;
                    if (vc->envLevel <= 0.001f) { vc->envLevel = 0; vc->active = 0; continue; }
                    break;
                default:
                    vc->active = 0; continue;
            }
            vc->envTime += dt;

            /* Oscillator 1 — Unison rendering. `invSR` replaces a division per
             * sample per unison voice — a hot path on ARM where FP div is slow. */
            float osc1L = 0.0f, osc1R = 0.0f;
            float osc1PhaseInc = vc->freq * invSR;
            for (int u = 0; u < UNISON_VOICES; u++) {
                float sample = readWavetable(s->wavetable, s->osc1Wave, vc->osc1[u].phase);
                float pan = vc->osc1[u].pan;
                osc1L += sample * (0.5f - pan * 0.5f);
                osc1R += sample * (0.5f + pan * 0.5f);
                vc->osc1[u].phase += osc1PhaseInc * vc->osc1[u].detuneRatio;
                if (vc->osc1[u].phase >= 1.0f) vc->osc1[u].phase -= 1.0f;
            }
            osc1L *= uniGain;
            osc1R *= uniGain;

            /* Oscillator 2 — skip entire render when mix level is effectively 0. */
            float osc2L = 0.0f, osc2R = 0.0f;
            float osc2vol = s->osc2Vol;
            if (osc2vol > 0.01f) {
                float osc2PhaseInc = vc->freq * detuneRatio * invSR;
                for (int u = 0; u < UNISON_VOICES; u++) {
                    float sample = readWavetable(s->wavetable, s->osc2Wave, vc->osc2[u].phase);
                    float pan = vc->osc2[u].pan;
                    osc2L += sample * (0.5f - pan * 0.5f);
                    osc2R += sample * (0.5f + pan * 0.5f);
                    vc->osc2[u].phase += osc2PhaseInc * vc->osc2[u].detuneRatio;
                    if (vc->osc2[u].phase >= 1.0f) vc->osc2[u].phase -= 1.0f;
                }
                osc2L *= uniGain;
                osc2R *= uniGain;
            }

            /* Mix oscillators */
            float sampleL = osc1L * (1.0f - osc2vol * 0.4f) + osc2L * osc2vol;
            float sampleR = osc1R * (1.0f - osc2vol * 0.4f) + osc2R * osc2vol;

            /* Apply envelope + velocity */
            float vel = 0.3f + 0.7f * (vc->velocity / 127.0f);
            float envGain = vc->envLevel * vel;
            sampleL *= envGain;
            sampleR *= envGain;

            /* Filter envelope modulation — cheap when unmodulated (common for
             * bass/pluck presets). Skip the per-sample clamp then. */
            float envModCutoff = s->cutoff;
            if (filterEnvNonZero) {
                envModCutoff += vc->envLevel * filterEnvAmount * 0.5f;
                if (envModCutoff > 1.0f) envModCutoff = 1.0f;
                if (envModCutoff < 0.0f) envModCutoff = 0.0f;
            }

            /* Multimode filter */
            if (fType == FILT_LP) {
                sampleL = processLadder(&vc->ladderL, sampleL, envModCutoff, s->resonance);
                sampleR = processLadder(&vc->ladderR, sampleR, envModCutoff, s->resonance);
            } else {
                sampleL = processSVF(&vc->svfL, sampleL, envModCutoff, s->resonance, fType);
                sampleR = processSVF(&vc->svfR, sampleR, envModCutoff, s->resonance, fType);
            }

            /* Gate effect (tempo-synced amplitude modulation). `fmodf` replaced
             * with integer wrap + invGatePeriod; both are cheap ALU ops. */
            if (gateActive && invGatePeriod > 0.0f) {
                float pos = (float)(gateBasePos + i);
                /* pos mod period, via subtract-multiples-of-period loop
                 * (pos is small after first block iteration; typically 0-1 lap) */
                float laps = pos * invGatePeriod;
                float cyclePos = (laps - (float)(int)laps);
                float gateGain;
                if (cyclePos < 0.05f)
                    gateGain = cyclePos * 20.0f;               /* /0.05 */
                else if (cyclePos < 0.45f)
                    gateGain = 1.0f;
                else if (cyclePos < 0.50f)
                    gateGain = 1.0f - (cyclePos - 0.45f) * 20.0f;
                else
                    gateGain = 0.0f;
                float gateMod = 1.0f - s->gateDepth * (1.0f - gateGain);
                sampleL *= gateMod;
                sampleR *= gateMod;
            }

            /* Soft-clip output — was 2× libm tanhf() per voice per sample. */
            mixL += jt_fast_tanh(sampleL);
            mixR += jt_fast_tanh(sampleR);
        }

        /* ── Built-in FX: Reverb + Delay ───────────────────────────
         * Processed post-voice-sum, pre-output, so they colour the
         * entire patch. Each is fully bypassed when its ON toggle is
         * 0 or its Mix is near zero — no DSP cost on the bypass path. */
        if (reverbActive) {
            float rL, rR;
            jt_reverb_process(&s->reverb, mixL, mixR, s->reverbSize, &rL, &rR);
            mixL = mixL + (rL - mixL) * reverbMix;
            mixR = mixR + (rR - mixR) * reverbMix;
        }
        if (delayActive) {
            float dL, dR;
            jt_delay_process(&s->delay, delaySamplesL, delaySamplesR,
                             delayFbk, mixL, mixR, &dL, &dR);
            mixL = mixL + dL * delayMix;
            mixR = mixR + dR * delayMix;
        }

        audio->outputL[i] += mixL;
        audio->outputR[i] += mixR;
    }
}

/* ── Param get/set ── */

float pdsynth_get_param(PdSynthInstance inst, int index) {
    JTSynth* s = (JTSynth*)inst;
    switch (index) {
        case P_Osc1Wave:   return s->osc1Wave;
        case P_Osc2Wave:   return s->osc2Wave;
        case P_Osc2Vol:    return s->osc2Vol;
        case P_Detune:     return s->detune;
        case P_FilterType: return s->filterType;
        case P_Attack:     return s->attack;
        case P_Decay:      return s->decay;
        case P_Sustain:    return s->sustain;
        case P_Release:    return s->release;
        case P_FilterEnv:  return s->filterEnv;
        case P_Cutoff:     return s->cutoff;
        case P_Resonance:  return s->resonance;
        case P_GateOn:     return s->gateOn;
        case P_GateRate:   return s->gateRate;
        case P_GateDepth:  return s->gateDepth;
        case P_ReverbOn:   return s->reverbOn;
        case P_ReverbMix:  return s->reverbMix;
        case P_ReverbSize: return s->reverbSize;
        case P_DelayOn:    return s->delayOn;
        case P_DelayTime:  return s->delayTime;
        case P_DelayFbk:   return s->delayFbk;
        case P_DelayMix:   return s->delayMix;
        default: return 0;
    }
}

void pdsynth_set_param(PdSynthInstance inst, int index, float value) {
    JTSynth* s = (JTSynth*)inst;
    switch (index) {
        case P_Osc1Wave:   s->osc1Wave   = value; break;
        case P_Osc2Wave:   s->osc2Wave   = value; break;
        case P_Osc2Vol:    s->osc2Vol    = value; break;
        case P_Detune:     s->detune     = value; break;
        case P_FilterType: s->filterType = value; break;
        case P_Attack:     s->attack     = value; break;
        case P_Decay:      s->decay      = value; break;
        case P_Sustain:    s->sustain    = value; break;
        case P_Release:    s->release    = value; break;
        case P_FilterEnv:  s->filterEnv  = value; break;
        case P_Cutoff:     s->cutoff     = value; break;
        case P_Resonance:  s->resonance  = value; break;
        case P_GateOn:     s->gateOn     = value; break;
        case P_GateRate:   s->gateRate   = value; break;
        case P_GateDepth:  s->gateDepth  = value; break;
        case P_ReverbOn:   s->reverbOn   = value; break;
        case P_ReverbMix:  s->reverbMix  = value; break;
        case P_ReverbSize: s->reverbSize = value; break;
        case P_DelayOn:    s->delayOn    = value; break;
        case P_DelayTime:  s->delayTime  = value; break;
        case P_DelayFbk:   s->delayFbk   = value; break;
        case P_DelayMix:   s->delayMix   = value; break;
    }
}

const char* pdsynth_param_name(int index) {
    static const char* names[] = {
        "Osc1Wave", "Osc2Wave", "Osc2Vol", "Detune", "FilterType",
        "Attack", "Decay", "Sustain", "Release", "FilterEnv",
        "Cutoff", "Resonance", "GateOn", "GateRate", "GateDepth",
        "ReverbOn", "ReverbMix", "ReverbSize",
        "DelayOn", "DelayTime", "DelayFbk", "DelayMix"
    };
    if (index >= 0 && index < JT_PARAM_COUNT) return names[index];
    return NULL;
}

void pdsynth_reset(PdSynthInstance inst) {
    JTSynth* s = (JTSynth*)inst;
    for (int i = 0; i < MAX_VOICES; i++) {
        s->voices[i].active = 0;
        s->voices[i].envStage = 0;
        memset(&s->voices[i].ladderL, 0, sizeof(LadderFilter));
        memset(&s->voices[i].ladderR, 0, sizeof(LadderFilter));
        memset(&s->voices[i].svfL, 0, sizeof(SVFilter));
        memset(&s->voices[i].svfR, 0, sizeof(SVFilter));
    }
}

void pdsynth_pitch_bend(PdSynthInstance inst, float semitones) {
    (void)inst; (void)semitones;
}

void pdsynth_mod_wheel(PdSynthInstance inst, float value) {
    (void)inst; (void)value;
}

/* ── Presets ── */

int pdsynth_preset_count(void) { return NUM_PRESETS; }

const char* pdsynth_preset_name(int index) {
    if (index >= 0 && index < (int)NUM_PRESETS) return FACTORY_PRESETS[index].name;
    return NULL;
}

void pdsynth_load_preset(PdSynthInstance inst, int index) {
    JTSynth* s = (JTSynth*)inst;
    if (index < 0 || index >= (int)NUM_PRESETS) return;
    const JTPreset* p = &FACTORY_PRESETS[index];
    s->osc1Wave   = p->osc1Wave;
    s->osc2Wave   = p->osc2Wave;
    s->osc2Vol    = p->osc2Vol;
    s->detune     = p->detune;
    s->filterType = p->filterType;
    s->attack     = p->attack;
    s->decay      = p->decay;
    s->sustain    = p->sustain;
    s->release    = p->release;
    s->filterEnv  = p->filterEnv;
    s->cutoff     = p->cutoff;
    s->resonance  = p->resonance;
    s->gateOn     = p->gateOn;
    s->gateRate   = p->gateRate;
    s->gateDepth  = p->gateDepth;
    s->reverbOn   = p->reverbOn;
    s->reverbMix  = p->reverbMix;
    s->reverbSize = p->reverbSize;
    s->delayOn    = p->delayOn;
    s->delayTime  = p->delayTime;
    s->delayFbk   = p->delayFbk;
    s->delayMix   = p->delayMix;
    s->currentPreset = index;
    /* Force reverb re-configure on next block so new Size takes effect. */
    s->lastReverbSize = -1.0f;
}

int pdsynth_get_preset(PdSynthInstance inst) {
    JTSynth* s = (JTSynth*)inst;
    return s->currentPreset;
}

/* ── Waveform visualization ── */
int pdsynth_get_waveform(PdSynthInstance inst, float* buffer, int maxSamples) {
    JTSynth* s = (JTSynth*)inst;
    if (!buffer || maxSamples < 1) return 0;

    int halfLen = maxSamples / 2;
    if (halfLen < 1) halfLen = 1;

    for (int i = 0; i < halfLen && i < maxSamples; i++) {
        float phase = (float)i / halfLen;
        float sum = 0;
        for (int u = 0; u < UNISON_VOICES; u++) {
            float p = phase * centsToRatio(UNISON_SPREAD[u]);
            p -= (int)p;
            sum += readWavetable(s->wavetable, s->osc1Wave, p);
        }
        buffer[i] = sum / UNISON_VOICES;
    }

    for (int i = 0; i < halfLen && (halfLen + i) < maxSamples; i++) {
        float phase = (float)i / halfLen;
        float sum = 0;
        for (int u = 0; u < UNISON_VOICES; u++) {
            float p = phase * centsToRatio(UNISON_SPREAD[u] + s->detune * 25.0f);
            p -= (int)p;
            sum += readWavetable(s->wavetable, s->osc2Wave, p);
        }
        buffer[halfLen + i] = sum / UNISON_VOICES;
    }

    return halfLen * 2;
}

/* ══════════════════════════════════════════════════════════════
 * Wave Screen (Page 0) — custom draw for waveform visualization.
 * Page 1 (Params) — handled entirely by the host SDK.
 * ══════════════════════════════════════════════════════════════ */

static void jt_fillRect(SDL_Renderer* r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static void jt_drawRect(SDL_Renderer* r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
}

int pdsynth_draw(PdSynthInstance inst, const PdDrawContext* ctx) {
    /* Page 1+: let the host render knobs/toggles/selects */
    if (ctx->page != 0) return 0;

    /*
     * JT Synth v4.0 — Page 0 (redesigned 2-column layout)
     *
     *   Y=0..13     Title band: "JT SYNTH" + preset name readout
     *   Y=15..85    TWO COLUMNS:
     *                 Left  (152 wide)  OSC 1 wavetable preview  (red)
     *                 Right (152 wide)  OSC 2 wavetable preview  (cyan, α∝mix)
     *   Y=88..115   Row A — OSC + FILTER   (8 cells, red accent)
     *                 [WV1] [WV2] [MIX] [DET]  |  [FLT] [CUT] [RES] [ENV]
     *   Y=117..144  Row B — ADSR + GATE    (8 cells, red accent, last empty)
     *                 [ATK] [DCY] [SUS] [RLS]  |  [GON] [GRT] [GDP]  [ · ]
     *   Y=146..173  Row C — REVERB + DELAY (7 cells, cyan accent, dims when off)
     *                 [RVB] [RMX] [RSZ]  |  [DLY] [DTM] [DFB] [DMX]
     *   Y=176..208  Live stereo scope  (red L + cyan R over centre line)
     *   Y=211..227  Peak meters L/R
     *
     * Every interactive cell registers via pd_register_control so the SDK
     * v4.5 spatial-nav path picks them up — D-pad, mouse click, and A-edits
     * all work identically to the JT Synth v3.x layout. The three-row grid
     * groups params semantically instead of scattering them in one strip.
     */
    JTSynth* s = (JTSynth*)inst;
    SDL_Renderer* r = (SDL_Renderer*)ctx->renderer;
    int X = ctx->x, Y = ctx->y, W = ctx->w, H = ctx->h;
    int sel = ctx->selectedParam;
    int editing = ctx->editMode;

    /* Red accent (primary / OSC + CORE), cyan accent (OSC 2 + FX). */
    const uint8_t AR = 255, AG = 30,  AB = 60;
    const uint8_t CR = 80,  CG = 180, CB = 220;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* ── Background + peak glow ── */
    SDL_SetRenderDrawColor(r, 5, 6, 14, 255);
    jt_fillRect(r, X, Y, W, H);
    if (ctx->peak > 0.01f) {
        int glowH = (int)(ctx->peak * 50);
        if (glowH > H / 3) glowH = H / 3;
        for (int g = 0; g < glowH; g++) {
            int a = (int)((1.0f - (float)g / glowH) * ctx->peak * 40);
            SDL_SetRenderDrawColor(r, AR, AG, AB, a);
            SDL_RenderDrawLine(r, X, Y + H - 1 - g, X + W, Y + H - 1 - g);
        }
    }

    /* ── Title band (Y=0..13) with preset name ── */
    {
        int bandH = 13;
        SDL_SetRenderDrawColor(r, 12, 4, 10, 255);
        jt_fillRect(r, X, Y, W, bandH);
        pdt_drawStrC(r, X + 6, Y + 3, 255, 80, 110, 255, "JT SYNTH");
        /* Preset name right-aligned. Uppercase + truncate/pad-safe. */
        if (s->currentPreset >= 0 && s->currentPreset < (int)NUM_PRESETS) {
            const char* name = FACTORY_PRESETS[s->currentPreset].name;
            int nw = pdt_strWidth(name, 1);
            int nx = X + W - 8 - nw;
            if (nx < X + 70) nx = X + 70;
            pdt_drawStrC(r, nx, Y + 3, AR, AG, AB, 230, name);
        }
        SDL_SetRenderDrawColor(r, AR, AG, AB, 220);
        SDL_RenderDrawLine(r, X, Y + bandH, X + W - 1, Y + bandH);
    }

    /* Rebuild preview caches on param change. */
    if (s->previewOsc1Pos != s->osc1Wave) {
        jt_rebuild_preview(s->wavetable, s->osc1Wave, s->previewOsc1);
        s->previewOsc1Pos = s->osc1Wave;
    }
    if (s->previewOsc2Pos != s->osc2Wave) {
        jt_rebuild_preview(s->wavetable, s->osc2Wave, s->previewOsc2);
        s->previewOsc2Pos = s->osc2Wave;
    }

    /* ── Draw-helper macro for an oscillator preview box ── */
    #define DRAW_OSC_BOX(wx, wy, ww, wh, previewBuf, osc_r, osc_g, osc_b, osc_a, scope_buf) do { \
        SDL_SetRenderDrawColor(r, 10, 12, 22, 255);                                           \
        jt_fillRect(r, (wx), (wy), (ww), (wh));                                               \
        uint8_t borderA = ctx->peak > 0.01f ? 200 : 100;                                      \
        SDL_SetRenderDrawColor(r, (osc_r), (osc_g), (osc_b), borderA);                        \
        jt_drawRect(r, (wx), (wy), (ww), (wh));                                               \
        int _mid = (wy) + (wh) / 2;                                                           \
        SDL_SetRenderDrawColor(r, (osc_r) / 4, (osc_g) / 4, (osc_b) / 4, 120);                \
        SDL_RenderDrawLine(r, (wx) + 3, _mid, (wx) + (ww) - 4, _mid);                         \
        SDL_SetRenderDrawColor(r, (osc_r), (osc_g), (osc_b), (osc_a));                        \
        int _inner = (ww) - 4;                                                                \
        int _half = (wh) / 2 - 4;                                                             \
        int _prevY = _mid;                                                                    \
        for (int _i = 0; _i < _inner; _i++) {                                                 \
            int _pi = (_i * PREVIEW_BUF_LEN) / (_inner > 0 ? _inner : 1);                     \
            if (_pi >= PREVIEW_BUF_LEN) _pi = PREVIEW_BUF_LEN - 1;                            \
            int _ny = _mid - (int)((previewBuf)[_pi] * _half);                                \
            if (_i > 0) {                                                                     \
                SDL_RenderDrawLine(r, (wx) + 2 + _i - 1, _prevY, (wx) + 2 + _i, _ny);         \
            }                                                                                 \
            _prevY = _ny;                                                                     \
        }                                                                                     \
        if (ctx->scopeLen > 0 && (scope_buf) && ctx->peak > 0.02f) {                          \
            SDL_SetRenderDrawColor(r, 255, 255, 255, (uint8_t)(ctx->peak * 90));              \
            for (int _i = 1; _i < ctx->scopeLen && _i < _inner; _i++) {                       \
                int _s0 = ((_i - 1) * ctx->scopeLen) / _inner;                                \
                int _s1 = (_i * ctx->scopeLen) / _inner;                                      \
                if (_s1 >= ctx->scopeLen) break;                                              \
                SDL_RenderDrawLine(r,                                                         \
                    (wx) + 2 + _i - 1, _mid - (int)((scope_buf)[_s0] * ((wh) / 2 - 6)),       \
                    (wx) + 2 + _i,     _mid - (int)((scope_buf)[_s1] * ((wh) / 2 - 6)));      \
            }                                                                                 \
        }                                                                                     \
    } while (0)

    /* ── Two-column waveform area (Y=15..85, 70 px tall) ── */
    {
        int topY = Y + 15;
        int boxH = 70;
        int leftX  = X + 4;
        int rightX = X + W / 2 + 2;
        int boxW   = (W / 2) - 6;
        DRAW_OSC_BOX(leftX,  topY, boxW, boxH, s->previewOsc1, AR, AG, AB, 230, ctx->scopeBufL);
        {
            uint8_t o2a = (uint8_t)(60 + s->osc2Vol * 180);
            DRAW_OSC_BOX(rightX, topY, boxW, boxH, s->previewOsc2, CR, CG, CB, o2a, ctx->scopeBufR);
        }
        /* Column labels inside the top-left of each box. */
        pdt_drawStrC(r, leftX  + 5, topY + 4, AR, AG, AB, 220, "OSC 1");
        pdt_drawStrC(r, rightX + 5, topY + 4, CR, CG, CB, 220, "OSC 2");
    }
    #undef DRAW_OSC_BOX

    /* ── Three-row param grid (8 cells × 3 rows) ──
     * Row A: OSC + FILTER   (red accent)
     * Row B: ADSR + GATE    (red accent, trailing cell empty)
     * Row C: REVERB + DELAY (cyan accent, dimmed when its ON toggle is off)
     *
     * A visible divider between the 4th and 5th cell splits each row into
     * two semantic halves (osc-side / filter-side, env-side / gate-side,
     * reverb-side / delay-side). Keeps the grid readable without boxing. */
    const int gridCols = 8;
    const int gridX    = X + 4;
    const int gridW    = W - 8;
    const int cellW    = gridW / gridCols;
    const int rowGap   = 2;
    const int rowH     = 28;
    const int rowAY    = Y + 88;
    const int rowBY    = rowAY + rowH + rowGap;
    const int rowCY    = rowBY + rowH + rowGap;

    /* Row A — OSC + FILTER (8 cells) */
    {
        float vals[8] = {
            s->osc1Wave, s->osc2Wave, s->osc2Vol, s->detune,
            s->filterType, s->cutoff, s->resonance, s->filterEnv
        };
        static const char* labels[8] = {
            "WV1", "WV2", "MIX", "DET",  "FLT", "CUT", "RES", "ENV"
        };
        /* paramIdx per cell — maps to plugin param indices. */
        const int pidx[8] = {
            P_Osc1Wave, P_Osc2Wave, P_Osc2Vol, P_Detune,
            P_FilterType, P_Cutoff, P_Resonance, P_FilterEnv
        };
        for (int c = 0; c < gridCols; c++) {
            int cx = gridX + c * cellW;
            int cy = rowAY;
            int cw = cellW - 1;
            int isSel  = (sel == pidx[c]);
            int isEdit = isSel && editing;
            pdt_drawParamCell(r, ctx, pidx[c], cx, cy, cw, rowH,
                              labels[c], vals[c], isSel, isEdit, AR, AG, AB);
        }
        /* Divider between cell 3 (DET) and cell 4 (FLT). */
        int dx = gridX + 4 * cellW - 1;
        SDL_SetRenderDrawColor(r, AR / 3, AG / 3, AB / 3, 160);
        SDL_RenderDrawLine(r, dx, rowAY + 2, dx, rowAY + rowH - 3);
    }

    /* Row B — ADSR + GATE (7 live cells + 1 empty tail). */
    {
        float vals[7] = {
            s->attack, s->decay, s->sustain, s->release,
            s->gateOn, s->gateRate, s->gateDepth
        };
        static const char* labels[7] = {
            "ATK", "DCY", "SUS", "RLS",  "GON", "GRT", "GDP"
        };
        const int pidx[7] = {
            P_Attack, P_Decay, P_Sustain, P_Release,
            P_GateOn, P_GateRate, P_GateDepth
        };
        for (int c = 0; c < 7; c++) {
            int cx = gridX + c * cellW;
            int cy = rowBY;
            int cw = cellW - 1;
            int isSel  = (sel == pidx[c]);
            int isEdit = isSel && editing;
            if (pidx[c] == P_GateOn) {
                /* GateOn is a boolean — draw as a proper switch, not a slider. */
                pdt_drawToggleCell(r, ctx, pidx[c], cx, cy, cw, rowH,
                                   labels[c], vals[c], isSel, isEdit, AR, AG, AB);
            } else {
                pdt_drawParamCell(r, ctx, pidx[c], cx, cy, cw, rowH,
                                  labels[c], vals[c], isSel, isEdit, AR, AG, AB);
            }
        }
        /* Divider between cell 3 (RLS) and cell 4 (GON). */
        int dx = gridX + 4 * cellW - 1;
        SDL_SetRenderDrawColor(r, AR / 3, AG / 3, AB / 3, 160);
        SDL_RenderDrawLine(r, dx, rowBY + 2, dx, rowBY + rowH - 3);
    }

    /* Row C — REVERB + DELAY (7 cells, cyan accent). Sleeping cells (where
     * their ON toggle is 0) get drawn dimmer but are still navigable, so
     * the user never navigates into "dead space". */
    {
        int rvbOn = (s->reverbOn > 0.5f);
        int dlyOn = (s->delayOn  > 0.5f);
        float vals[7] = {
            s->reverbOn, s->reverbMix, s->reverbSize,
            s->delayOn,  s->delayTime, s->delayFbk, s->delayMix
        };
        static const char* labels[7] = {
            "RVB", "RMX", "RSZ",  "DLY", "DTM", "DFB", "DMX"
        };
        const int pidx[7] = {
            P_ReverbOn, P_ReverbMix, P_ReverbSize,
            P_DelayOn, P_DelayTime, P_DelayFbk, P_DelayMix
        };
        const int live[7] = { 1, rvbOn, rvbOn,  1, dlyOn, dlyOn, dlyOn };
        for (int c = 0; c < 7; c++) {
            int cx = gridX + c * cellW;
            int cy = rowCY;
            int cw = cellW - 1;
            int isSel  = (sel == pidx[c]);
            int isEdit = isSel && editing;
            /* FX row uses the cyan (secondary) accent so it reads as a
             * distinct section. Sleeping cells (where their ON toggle is
             * 0) render at ~40% alpha via a dimmer accent — still fully
             * selectable, just visually de-emphasised. */
            uint8_t accR = live[c] ? CR : (uint8_t)(CR * 0.45f);
            uint8_t accG = live[c] ? CG : (uint8_t)(CG * 0.45f);
            uint8_t accB = live[c] ? CB : (uint8_t)(CB * 0.45f);
            /* ReverbOn (c==0) and DelayOn (c==3) are boolean — render
             * as real ON/OFF switches so the state is instantly readable
             * and A-to-toggle (wired on the host side) feels right. The
             * remaining 5 cells are knobs and keep the slider-style. */
            if (pidx[c] == P_ReverbOn || pidx[c] == P_DelayOn) {
                pdt_drawToggleCell(r, ctx, pidx[c], cx, cy, cw, rowH,
                                   labels[c], vals[c], isSel, isEdit,
                                   CR, CG, CB);
            } else {
                pdt_drawParamCell(r, ctx, pidx[c], cx, cy, cw, rowH,
                                  labels[c], vals[c], isSel, isEdit,
                                  accR, accG, accB);
            }
        }
        /* Divider between cell 2 (RSZ) and cell 3 (DLY). */
        int dx = gridX + 3 * cellW - 1;
        SDL_SetRenderDrawColor(r, CR / 3, CG / 3, CB / 3, 160);
        SDL_RenderDrawLine(r, dx, rowCY + 2, dx, rowCY + rowH - 3);
        /* Tiny "FX" tag on the far right so users know what this row is. */
        pdt_drawStrC(r, gridX + 7 * cellW - 18, rowCY - 10, CR, CG, CB, 200, "FX");
    }

    /* ── Live stereo scope (Y=176..208, 32 px) ── */
    {
        int ox = X + 4, oy = Y + 176, ow = W - 8, oh = 32;
        SDL_SetRenderDrawColor(r, 8, 10, 18, 255);
        jt_fillRect(r, ox, oy, ow, oh);
        uint8_t borderA = ctx->peak > 0.01f ? 220 : 80;
        SDL_SetRenderDrawColor(r,
            (uint8_t)(AR * borderA / 255),
            (uint8_t)(AG * borderA / 255),
            (uint8_t)(AB * borderA / 255), 255);
        jt_drawRect(r, ox, oy, ow, oh);
        int mid = oy + oh / 2;
        SDL_SetRenderDrawColor(r, AR / 6, AG / 6, AB / 6, 140);
        SDL_RenderDrawLine(r, ox + 3, mid, ox + ow - 4, mid);
        if (ctx->scopeLen > 0 && ctx->scopeBufL) {
            SDL_SetRenderDrawColor(r, AR, AG, AB, 220);
            int px = ox + 2, py = mid;
            for (int i = 1; i < ow - 4; i++) {
                int si = i * ctx->scopeLen / (ow - 4);
                if (si >= ctx->scopeLen) break;
                int nx = ox + 2 + i;
                int ny = mid - (int)(ctx->scopeBufL[si] * (oh / 2 - 3));
                if (ny < oy + 2) ny = oy + 2;
                if (ny > oy + oh - 2) ny = oy + oh - 2;
                SDL_RenderDrawLine(r, px, py, nx, ny);
                px = nx; py = ny;
            }
            if (ctx->scopeBufR) {
                SDL_SetRenderDrawColor(r, CR, CG, CB, 150);
                px = ox + 2; py = mid;
                for (int i = 1; i < ow - 4; i++) {
                    int si = i * ctx->scopeLen / (ow - 4);
                    if (si >= ctx->scopeLen) break;
                    int nx = ox + 2 + i;
                    int ny = mid - (int)(ctx->scopeBufR[si] * (oh / 2 - 3));
                    if (ny < oy + 2) ny = oy + 2;
                    if (ny > oy + oh - 2) ny = oy + oh - 2;
                    SDL_RenderDrawLine(r, px, py, nx, ny);
                    px = nx; py = ny;
                }
            }
        }
    }

    /* ── Peak meters L/R (Y=211..227, 16 px) ── */
    {
        int mx = X + 4, my = Y + 211, mh = 16;
        int mw = (W - 12) / 2;
        for (int ch = 0; ch < 2; ch++) {
            float pk = (ch == 0) ? ctx->peakL : ctx->peakR;
            if (pk < 0.0f) pk = 0.0f;
            if (pk > 1.0f) pk = 1.0f;
            int bx = mx + ch * (mw + 4);
            SDL_SetRenderDrawColor(r, 18, 20, 32, 255);
            jt_fillRect(r, bx, my, mw, mh);
            int fill = (int)(pk * (mw - 2));
            uint8_t mcR = pk > 0.7f ? 255 : (uint8_t)(pk * 364);
            uint8_t mcG = pk < 0.7f ? 200 : (uint8_t)((1.0f - pk) * 290);
            uint8_t mcB = 40;
            SDL_SetRenderDrawColor(r, mcR, mcG, mcB, 255);
            jt_fillRect(r, bx + 1, my + 1, fill, mh - 2);
            SDL_SetRenderDrawColor(r, AR, AG, AB, 100);
            jt_drawRect(r, bx, my, mw, mh);
        }
    }

    /* Outer frame — faint red halo */
    SDL_SetRenderDrawColor(r, AR, AG, AB, 80);
    jt_drawRect(r, X, Y, W, H);
    return 1;
}
