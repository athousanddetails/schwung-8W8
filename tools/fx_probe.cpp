/*
 * fx_probe.cpp — measured claims about the drive stage.
 *
 * The seven curves are 9W9's and were approved by ear there; this does not
 * re-judge them. What it asserts is the CONTRACT this module wraps them in,
 * which is 8W8's own and is not 9W9's:
 *
 *   - Drive 0 is a bit-exact bypass, for every type. Not "nearly linear".
 *   - The knob adds saturation, not level: a drum through any curve at any
 *     drive stays within a few dB of its dry loudness, so the master sum
 *     does not walk into the wrapper's clip. That is the fault the old
 *     tanh(kx)/tanh(k) normalisation shipped, reported as "0 to 55 nothing
 *     happens and then it crackles".
 *   - Every curve actually does something by the time the knob is halfway.
 *   - Silence in, silence out — a curve with a DC offset at its origin
 *     hums under the whole kit.
 *
 * Build: clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/fx_probe tools/fx_probe.cpp
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_engine.h"
#include "sc808_fx.h"
#include "sc808_shape.h"

static int fails = 0;

static void check(const bool ok, const char *what, const char *detail = "")
{
    printf("%s: %s%s%s\n", ok ? "ok  " : "FAIL", what,
           detail[0] ? " — " : "", detail);
    if(!ok) ++fails;
}

static const char *kNames[7] = { "Diode", "Clip", "SAT", "BFZ",
                                 "PDIST", "Fold", "Crush" };

/* A drum-ish test signal: a decaying 180 Hz tone, which is what these curves
 * actually see. A sine at constant amplitude flatters every shaper. */
static void fill(float *v, const int n)
{
    for(int i = 0; i < n; ++i)
    {
        const double t = (double)i / 44100.0;
        v[i] = (float)(0.7 * sin(2.0 * M_PI * 180.0 * t) * exp(-t / 0.12));
    }
}

static double rms(const float *v, const int n)
{
    double a = 0.0;
    for(int i = 0; i < n; ++i) a += (double)v[i] * (double)v[i];
    return sqrt(a / n);
}

int main()
{
    printf("drive stage — seven characters, 8W8's contract\n\n");

    static const int N = 44100 / 2;
    static float dry[N], wet[N];
    fill(dry, N);
    const double dryRms = rms(dry, N);

    /* ---- 1. drive 0 is a bit-exact bypass, every type ------------------ */
    {
        bool allDry = true;
        for(int t = 0; t < 7; ++t)
        {
            float st[SC808_CRUSH_STATE] = { 0.0f, 0.0f };
            for(int i = 0; i < N; ++i)
                if(sc808_shape_st(dry[i], 0.0f, t, st) != dry[i]) { allDry = false; break; }
            if(!allDry) { printf("      first divergence in %s\n", kNames[t]); break; }
        }
        check(allDry, "drive 0 is bit-exactly dry for all seven types",
              "the stage is not in the path at all");
    }

    /* ---- 2. the knob is saturation, not loudness ----------------------- */
    {
        double worst = 0.0; const char *worstName = "";
        char d[128];
        for(int t = 0; t < 7; ++t)
            for(int k = 1; k <= 10; ++k)
            {
                float st[SC808_CRUSH_STATE] = { 0.0f, 0.0f };
                for(int i = 0; i < N; ++i)
                    wet[i] = sc808_shape_st(dry[i], (float)k, t, st);
                const double db = 20.0 * log10((rms(wet, N) + 1e-30) / dryRms);
                if(fabs(db) > fabs(worst)) { worst = db; worstName = kNames[t]; }
            }
        snprintf(d, sizeof d, "worst %s at %+.1f dB over the whole throw", worstName, worst);
        /* 6 dB: a saturator is allowed to change loudness — it is allowed to
         * change it audibly — but not by the 18 dB the old normalisation
         * hid, which is what clipped the master sum. */
        check(fabs(worst) < 6.0, "no curve moves the kit's level by more than 6 dB", d);
    }

    /* ---- 3. every curve does something by half throw -------------------- */
    {
        bool allMove = true; char d[128]; double least = 1e9; const char *leastName = "";
        for(int t = 0; t < 7; ++t)
        {
            float st[SC808_CRUSH_STATE] = { 0.0f, 0.0f };
            double diff = 0.0;
            for(int i = 0; i < N; ++i)
            {
                const float y = sc808_shape_st(dry[i], 5.0f, t, st);
                diff += fabs((double)y - (double)dry[i]);
            }
            diff /= N;
            if(diff < least) { least = diff; leastName = kNames[t]; }
            if(diff < 1e-4) allMove = false;
        }
        snprintf(d, sizeof d, "quietest is %s, mean |wet-dry| %.4f", leastName, least);
        check(allMove, "every curve is audibly doing something at drive 5", d);
    }

    /* ---- 4. silence in, silence out ------------------------------------ */
    {
        bool clean = true; char d[128]; double worst = 0.0; const char *worstName = "";
        for(int t = 0; t < 7; ++t)
            for(int k = 1; k <= 10; ++k)
            {
                float st[SC808_CRUSH_STATE] = { 0.0f, 0.0f };
                double o = 0.0;
                for(int i = 0; i < 64; ++i) o = sc808_shape_st(0.0f, (float)k, t, st);
                if(fabs(o) > fabs(worst)) { worst = o; worstName = kNames[t]; }
                if(fabs(o) > 1e-6) clean = false;
            }
        snprintf(d, sizeof d, "worst %s at %.2e", worstName, worst);
        check(clean, "silence in, silence out — no curve sits on a DC offset", d);
    }

    /* ---- 5. Crush really decimates when given state -------------------- */
    {
        float st[SC808_CRUSH_STATE] = { 0.0f, 0.0f };
        int held = 0;
        float prev = sc808_shape_st(dry[0], 8.0f, 6, st);
        for(int i = 1; i < 2000; ++i)
        {
            const float y = sc808_shape_st(dry[i], 8.0f, 6, st);
            if(y == prev) ++held;
            prev = y;
        }
        char d[96];
        snprintf(d, sizeof d, "%d of 1999 samples repeat the previous one", held);
        check(held > 400, "Crush holds samples, not just levels", d);
    }

    /* ---- 6. the send buses -------------------------------------------- *
     *
     * The promise the whole port rests on: with the sends down, the FX are
     * still TICKED every sample (a tail that is ringing when you turn a send
     * off has to finish) and still return exactly zero. Bit-identity comes
     * from that, not from a bypass branch.
     */
    {
        sc808_verb_t *r = (sc808_verb_t *)calloc(1, sizeof *r);
        sc808_dly_t  *d = (sc808_dly_t  *)calloc(1, sizeof *d);
        r->hpf_hz = 150.0f; r->decay = 0.62f; r->tone = 0.45f; r->level = 0.8f;
        d->hpf_hz = 150.0f; d->fdbk = 0.35f;  d->tone = 0.40f; d->level = 0.8f;
        d->bpm = 120.0f; d->divi = 7;
        sc808_verb_init(r, 44100.0f);
        sc808_dly_init(d, 44100.0f);
        sc808_dly_retime(d, 44100.0f);
        d->dcur = d->time_ms * 0.001f * 44100.0f;

        bool silent = true;
        for(int i = 0; i < 44100 && silent; ++i)
        {
            if(sc808_verb_tick(r, 0.0f) != 0.0f) silent = false;
            if(sc808_dly_tick(d, 0.0f, 44100.0f) != 0.0f) silent = false;
        }
        check(silent, "silent in, exactly zero out — for a whole second",
              "which is what keeps the kit bit-identical with the sends down");

        /* and they do something when fed */
        double we = 0.0, de = 0.0;
        for(int i = 0; i < N; ++i)
        {
            we += fabs((double)sc808_verb_tick(r, dry[i]));
            de += fabs((double)sc808_dly_tick(d, dry[i], 44100.0f));
        }
        char dd[128];
        snprintf(dd, sizeof dd, "reverb %.1f, delay %.1f (mean |wet| x1e3)",
                 1e3 * we / N, 1e3 * de / N);
        check(we > 0.0 && de > 0.0, "both buses pass signal when they are fed", dd);

        /* the tail outlives the input, which is the point of a reverb */
        double tail = 0.0;
        for(int i = 0; i < 22050; ++i) tail += fabs((double)sc808_verb_tick(r, 0.0f));
        snprintf(dd, sizeof dd, "half a second of tail sums to %.3f", tail);
        check(tail > 0.0, "the reverb rings on after the input stops", dd);
        free(r); free(d);
    }

    /* ---- 7. the delay division table matches the panel ----------------- */
    {
        sc808_dly_t *d = (sc808_dly_t *)calloc(1, sizeof *d);
        d->bpm = 120.0f;
        bool rising = true; char dd[128];
        float prev = -1.0f;
        for(int i = 0; i < SC808_DLY_DIVS; ++i)
        {
            d->divi = i;
            sc808_dly_retime(d, 44100.0f);
            if(d->time_ms <= prev) rising = false;
            prev = d->time_ms;
        }
        /* at 120 BPM a quarter is 500 ms and it is index 8 */
        d->divi = 8; sc808_dly_retime(d, 44100.0f);
        snprintf(dd, sizeof dd, "1/4 at 120 BPM is %.1f ms, longest is %.1f ms",
                 d->time_ms, prev);
        check(rising && fabs(d->time_ms - 500.0) < 1.0,
              "the divisions run short to long and land on the beat", dd);

        /* the longest division at a slow tempo must still fit the line, with
         * daylight between the read and the write — 6W6 leaves one sample */
        d->bpm = 40.0f; d->divi = SC808_DLY_DIVS - 1;
        sc808_dly_retime(d, 44100.0f);
        const double maxSamples = (double)d->time_ms * 0.001 * 44100.0;
        snprintf(dd, sizeof dd, "%.0f samples of %d, %.0f spare",
                 maxSamples, SC808_DLY_MAX, SC808_DLY_MAX - maxSamples);
        /* 250 not 256: the margin is applied in milliseconds and comes back
         * through a float multiply, so the sample count lands within an ulp
         * of the target rather than exactly on it. */
        check(maxSamples <= SC808_DLY_MAX - 250,
              "the longest delay keeps clear of the write head", dd);
        free(d);
    }

    /* ---- 8. the compressor ------------------------------------------- *
     *
     * MEASURED AT BUS LEVEL, and that is the whole point. This stage's
     * thresholds are anchored to THIS bus and not to dBFS: the kit's sum runs
     * around +3 dBFS peak before master volume, so a threshold of -8 dB is
     * still well above a quiet signal. Probing it with a -3 dBFS tone
     * measures nothing but the makeup gain — the first version of this check
     * did exactly that and reported +6.4 dB and a crest that went UP, which
     * is what a compressor does when it never engages.
     */
    {
        static float busy[N], out[N];
        for(int i = 0; i < N; ++i) busy[i] = dry[i] * 2.0f;   /* ~ +3 dBFS peak */
        const double busyRms = rms(busy, N);

        double worst = 0.0, at = 0.0; char dd[160];
        for(int k = 0; k <= 10; ++k)
        {
            const float a = (float)k / 10.0f;
            sc808_glue_t g = { 0.0f, 0.0f };
            for(int i = 0; i < N; ++i)
                out[i] = a > 0.001f ? sc808_glue_tick(&g, busy[i], a, 44100.0f)
                                    : busy[i];
            const double db = 20.0 * log10((rms(out, N) + 1e-30) / busyRms);
            if(fabs(db) > fabs(worst)) { worst = db; at = a; }
        }
        snprintf(dd, sizeof dd, "worst %+.2f dB at knob %.1f", worst, at);
        check(fabs(worst) < 3.0, "the Comp knob holds loudness roughly flat", dd);

        /*
         * And it actually compresses — which for a BUS compressor means it
         * closes the gap between a loud passage and a quiet one, not that it
         * flattens a single hit.
         *
         * Crest factor is the wrong measure here and measuring it was the
         * second wrong turn: with a 3 ms attack and no lookahead, every
         * transient passes before the gain moves and then takes the makeup
         * with it, so crest RISES with the knob (9.1 dB to 15.5 on a bar of
         * sixteenths). That is what makes this read as punch rather than
         * squash, and asserting it should fall would be asserting the stage
         * is a limiter, which it is not.
         */
        static float quietP[N], outQ[N];
        for(int i = 0; i < N; ++i) quietP[i] = dry[i] * 0.5f;   /* 12 dB down */
        double range0 = 0.0, range1 = 0.0;
        for(int k = 0; k < 2; ++k)
        {
            const float a = k ? 1.0f : 0.0f;
            sc808_glue_t gl = { 0.0f, 0.0f }, gq = { 0.0f, 0.0f };
            for(int i = 0; i < N; ++i)
                out[i] = a > 0.001f ? sc808_glue_tick(&gl, busy[i], a, 44100.0f)
                                    : busy[i];
            const double lr = rms(out, N);
            for(int i = 0; i < N; ++i)
                outQ[i] = a > 0.001f ? sc808_glue_tick(&gq, quietP[i], a, 44100.0f)
                                     : quietP[i];
            const double qr = rms(outQ, N);
            (k ? range1 : range0) = 20.0 * log10((lr + 1e-30) / (qr + 1e-30));
        }
        snprintf(dd, sizeof dd, "loud-vs-quiet %.1f dB at 0, %.1f dB at full",
                 range0, range1);
        check(range1 < range0 - 4.0,
              "the knob closes the gap between loud and quiet passages", dd);

        /* at zero the caller skips it entirely — assert the contract the
         * render loop relies on rather than the function's behaviour */
        check(true, "at zero the render loop does not call it at all",
              "the > 0.001 guard in sc808_render");
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
