/*
 * sc808_fx.h — the two send buses and the bus glue.
 *
 * A small plate-ish reverb, a tempo-synced delay, and a one-knob compressor.
 * None of it is 808 circuitry and this file does not pretend otherwise: the
 * machine had a mixer and nothing else. Ported from 9W9 by way of 6W6
 * (GPL-3.0), including the three bugs already found and fixed there — they
 * are called out at each site so nobody "simplifies" them back in.
 *
 * Both buses highpass the send input so the kit's low end stays out of the
 * wet path, and both write their loops at 12 bits for the early-rack grain.
 *
 * SENDS DEFAULT TO ZERO, and that is load-bearing: with them down, both ticks
 * are fed exactly 0.0 from silent state and return exactly 0.0, so putting
 * them in the mix cannot change a single sample of the kit that was signed
 * off. tools/golden_check is what holds that promise.
 *
 * GPL-3.0.
 */
#ifndef SC808_FX_H
#define SC808_FX_H

#include <math.h>

#include "sc_ugens.h"

#define SC808_DLY_MAX  88200        /* 2 s at 44.1k: a dotted half at 90 BPM */
#define SC808_DLY_DIVS 13

/* Note divisions, shortest to longest, in beats. Must match the dly_time enum
 * option list in gen_params.py: 1/32 1/16T 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4
 * 1/2T 1/4. 1/2 1/2. */
static const float kSC808DlyBeats[SC808_DLY_DIVS] = {
    0.125f, 1.0f/6.0f, 0.25f, 1.0f/3.0f, 0.375f, 0.5f, 2.0f/3.0f,
    0.75f, 1.0f, 4.0f/3.0f, 1.5f, 2.0f, 3.0f
};

/* Freeverb's canonical comb and allpass lengths at 44.1 k. Not sample-rate
 * scaled — the module runs at one rate. */
static const int kSC808VerbCL[4] = { 1116, 1188, 1277, 1356 };
static const int kSC808VerbAL[2] = { 556, 441 };

struct sc808_verb_t {
    float decay, tone, hpf_hz, level;
    sc::Biquad hp;
    float comb[4][1356];
    int   cpos[4];
    float cdmp[4];                  /* per-comb damping state */
    float apb[2][556];
    int   apos[2];
};

struct sc808_dly_t {
    float fdbk, tone, hpf_hz, level;
    float time_ms;
    int   divi;                     /* note-division index, 0..12 */
    float bpm;                      /* fed by the host each block */
    sc::Biquad hp;
    float buf[SC808_DLY_MAX];
    int   w;
    float dcur;                     /* slewed delay, in samples */
    float lp;                       /* loop damping state       */
};

/* The send highpass, Q = 0.7071 — sc_ugens' Biquad takes the RECIPROCAL of Q,
 * so the argument is sqrt(2) and not its inverse. Getting that backwards
 * gives a resonant peak where a gentle corner belongs. */
static const double kSC808_SendRQ = 1.4142135623730951;

static inline void sc808_verb_init(sc808_verb_t *r, float sr)
{
    r->hp.setHiPass(r->hpf_hz, kSC808_SendRQ, sr);
    r->hp.reset();
    for(int i = 0; i < 4; ++i)
    {
        r->cpos[i] = 0; r->cdmp[i] = 0.0f;
        for(int k = 0; k < 1356; ++k) r->comb[i][k] = 0.0f;
    }
    for(int j = 0; j < 2; ++j)
    {
        r->apos[j] = 0;
        for(int k = 0; k < 556; ++k) r->apb[j][k] = 0.0f;
    }
}

static inline void sc808_dly_init(sc808_dly_t *d, float sr)
{
    d->hp.setHiPass(d->hpf_hz, kSC808_SendRQ, sr);
    d->hp.reset();
    d->w = 0; d->lp = 0.0f;
    for(int k = 0; k < SC808_DLY_MAX; ++k) d->buf[k] = 0.0f;
}

static inline float sc808_verb_tick(sc808_verb_t *r, const float _in)
{
    const float x = r->hp.process(_in);
    /* Tone: bright opens the loop's damping, dark closes it. */
    const float damp = 0.75f - r->tone * 0.55f;
    const float fb   = r->decay;
    float acc = 0.0f;
    for(int i = 0; i < 4; ++i)
    {
        float *b = r->comb[i];
        const int n = kSC808VerbCL[i];
        const float y = b[r->cpos[i]];
        acc += y;
        r->cdmp[i] = y + (r->cdmp[i] - y) * damp;
        float st = x + r->cdmp[i] * fb;
        /* BUG FIX (1 of 3), do not "simplify": truncate TOWARD ZERO. floorf
         * biases every pass by -0.5 LSB, and a DC-fed comb loop settles into
         * a -70 dB hum that never decays. */
        st = truncf(st * 2048.0f) * (1.0f / 2048.0f);
        b[r->cpos[i]] = st;
        if(++r->cpos[i] >= n) r->cpos[i] = 0;
    }
    float y = acc * 0.25f;
    for(int j = 0; j < 2; ++j)
    {
        float *b = r->apb[j];
        const int n = kSC808VerbAL[j];
        const float bo = b[r->apos[j]];
        b[r->apos[j]] = y + bo * 0.5f;
        y = bo - y * 0.5f;
        if(++r->apos[j] >= n) r->apos[j] = 0;
    }
    return y * r->level;
}

static inline float sc808_dly_tick(sc808_dly_t *d, const float _in, const float _sr)
{
    const float x = d->hp.process(_in);
    const float target = d->time_ms * 0.001f * _sr;
    /* Slewed, so turning Time warps the echo like the old units instead of
     * clicking. ~30 ms to settle. */
    d->dcur += (target - d->dcur) * 0.0008f;
    float rp = (float)d->w - d->dcur;
    while(rp < 0.0f) rp += (float)SC808_DLY_MAX;
    int i0 = (int)rp;
    const float fr = rp - (float)i0;
    /* BUG FIX (2 of 3): float spacing at 36000 is 1/256, so a read a hair
     * under the wrap point rounds UP to exactly SC808_DLY_MAX and indexes one
     * past the buffer — it leaked the write counter into the audio as
     * denormals. Clamp AFTER the cast. */
    if(i0 >= SC808_DLY_MAX) i0 -= SC808_DLY_MAX;
    const int   i1 = i0 + 1 >= SC808_DLY_MAX ? 0 : i0 + 1;
    const float y  = d->buf[i0] + (d->buf[i1] - d->buf[i0]) * fr;

    /* Feedback through a one-pole: every repeat gets darker, like tape and
     * the early digitals both did. 12-bit write for the grain. */
    const float tc = 0.06f + d->tone * 0.6f;
    d->lp += (y * d->fdbk - d->lp) * tc;
    /* BUG FIX (3 of 3): flush the feedback state or the tail never ends. */
    if(fabsf(d->lp) < 1e-20f) d->lp = 0.0f;
    float st = x + d->lp;
    st = truncf(st * 2048.0f) * (1.0f / 2048.0f);   /* toward zero: no DC */
    d->buf[d->w] = st;
    if(++d->w >= SC808_DLY_MAX) d->w = 0;
    return y * d->level;
}

/* beats -> ms, clamped to the line. Called when the division or the tempo
 * changes, never per sample. */
static inline void sc808_dly_retime(sc808_dly_t *d, float sr)
{
    int i = d->divi;
    if(i < 0) i = 0;
    if(i >= SC808_DLY_DIVS) i = SC808_DLY_DIVS - 1;
    const float bpm = d->bpm > 20.0f ? d->bpm : 120.0f;
    float ms = kSC808DlyBeats[i] * 60000.0f / bpm;
    /*
     * 9W9's margin, not 6W6's. 6W6 clamps to (MAX - 1) samples, which lets a
     * fully-slewed read land on the sample the writer is about to overwrite —
     * not out of bounds, the index clamps hold, but a read/write collision on
     * the longest divisions at slow tempos. 256 samples of daylight costs
     * 6 ms of maximum delay and cannot collide.
     */
    const float max_ms = (float)(SC808_DLY_MAX - 256) * 1000.0f / sr;
    if(ms > max_ms) ms = max_ms;
    d->time_ms = ms;
}

/*
 * One-knob bus glue, ported from 9W9's master_glue. NOT 808 circuitry, and
 * honest about it: at zero the caller does not enter this function at all, so
 * the stage is bit-identical to absent. The knob walks threshold, ratio and
 * auto-makeup together — +10 dB / 2:1 / no makeup at the bottom, -8 dB /
 * 5:1 / +8.8 dB at the top.
 *
 * Two calibration lessons came with it and are worth keeping visible:
 *
 *  - DETECT ON AN ENVELOPE, NOT THE SAMPLE. Detecting instantaneously made
 *    the stage track the waveform of a 50 Hz kick — a 20 ms period, far
 *    longer than the attack — so it locked onto the cycle peaks and crushed
 *    everything between them. RMS fell to a third while the peaks sailed past.
 *  - THRESHOLDS ARE ANCHORED TO THIS BUS, not to dBFS. The sum runs about
 *    +3 dBFS peak on a busy pattern BEFORE master volume scales it, so
 *    dBFS-style thresholds sit tens of dB under the signal and slam it.
 */
struct sc808_glue_t {
    float env_db;      /* smoothed gain reduction */
    float det;         /* rectified level follower */
};

static inline float sc808_glue_tick(sc808_glue_t *g, const float _in,
                                    const float _amount, const float _sr)
{
    const float a = _amount;
    const float thr = 10.0f - 18.0f * a;
    const float ratio = 2.0f + 3.0f * a;
    const float knee = 6.0f;

    const float mag = fabsf(_in);
    const float drel = expf(-1.0f / (0.035f * _sr));
    g->det = mag > g->det ? mag : g->det * drel;
    const float in_db = g->det > 1e-9f ? 20.0f * log10f(g->det) : -120.0f;
    const float over = in_db - thr;
    float gr = 0.0f;
    if(over >= knee * 0.5f)
        gr = -(over) * (1.0f - 1.0f / ratio);
    else if(over > -knee * 0.5f)
    {
        const float t = over + knee * 0.5f;
        gr = -(t * t) / (2.0f * knee) * (1.0f - 1.0f / ratio);
    }

    const float atk = expf(-1.0f / (0.003f * _sr));
    const float rel = expf(-1.0f / (0.120f * _sr));
    const float coef = gr < g->env_db ? atk : rel;
    g->env_db = gr + (g->env_db - gr) * coef;

    /* AutoGain fitted so loudness stays flat across the knob: linear makeup
     * left +1 dB at noon and -1.8 at full, because the reduction grows faster
     * than linearly as the threshold walks down. This is that loss inverted. */
    const float makeup = a * 2.2f + a * a * 6.6f;
    return _in * powf(10.0f, (g->env_db + makeup) / 20.0f);
}

#endif /* SC808_FX_H */
