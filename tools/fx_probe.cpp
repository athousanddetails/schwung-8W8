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
#include <string.h>

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

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
