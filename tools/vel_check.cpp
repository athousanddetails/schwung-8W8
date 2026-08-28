/*
 * vel_check.cpp — what velocity must do, as properties rather than a curve.
 *
 * 9W9 got this wrong three times before it shipped, and each wrong turn is
 * worth not repeating:
 *
 *   1. The 808's accent SWITCH, reproduced literally. From a sequencer that
 *      is a 6 dB cliff between velocity 99 and 100 with a flat shelf either
 *      side, and velocity looks broken.
 *   2. One straight line, but pivoting mid-range, so turning Velocity UP made
 *      hard hits louder — the knob moved the kit's loudness, not its dynamics.
 *   3. Anchored at 1.0 with the Accent gain dropped. Tidy, and it quietly
 *      takes the whole kit down 6 dB, because 1.0 is the UNACCENTED level and
 *      a pattern from Move had always been playing at the accented one.
 *
 * So this asserts the properties, not any particular shape: monotonic, no
 * step where the old threshold sat, genuinely flat at depth 0, and never
 * louder at full velocity than it was before the knob existed.
 *
 * 8W8 has a fourth thing to prove that 9W9 and 6W6 do not. Most of these
 * voices take the strike as a TRIGGER VOLTAGE whose hardware floor is 4 V,
 * so the voltage alone cannot carry the bottom of the range — left to it,
 * velocities 0..64 would all sound the same. The lane's gain carries that
 * half. The monotonicity check below is what proves the two halves join up.
 *
 * Build: clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/vel_check \
 *            tools/vel_check.cpp src/dsp/sc808_engine.cpp
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sc808_engine.h"

static const double SR = 44100.0;
static int fails = 0;

static void check(const bool ok, const char *what, const char *detail = "")
{
    printf("%s: %s%s%s\n", ok ? "ok  " : "FAIL", what,
           detail[0] ? " — " : "", detail);
    if(!ok) ++fails;
}

/* Loudest 20 ms of one hit — the same measure kit_check balances on, and the
 * one that does not mistake a short note for a quiet one. */
static double hit_level(const int voice, const int velocity, const int depthPot)
{
    sc808_engine_t *e = sc808_create((float)SR);
    char b[16];
    snprintf(b, sizeof b, "%d", depthPot);
    sc808_set_param(e, "vel_depth", b);

    static const int N = (int)(SR * 2.0);
    static float buf[(int)(44100 * 2.0)];
    memset(buf, 0, sizeof buf);
    sc808_trigger(e, voice, velocity);
    sc808_render(e, buf, N);
    sc808_destroy(e);

    const int w = (int)(SR * 0.020);
    double acc = 0.0, best = 0.0;
    for(int i = 0; i < N; ++i)
    {
        acc += (double)buf[i] * (double)buf[i];
        if(i >= w) acc -= (double)buf[i - w] * (double)buf[i - w];
        if(i >= w && acc > best) best = acc;
    }
    return sqrt(best / w);
}

int main()
{
    printf("velocity — one straight line, no threshold\n\n");

    /* One voltage lane, one gain lane: the two halves of this kit. */
    static const struct { int v; const char *id; } kProbe[2] = {
        { SC808_BD, "bass drum (trigger volts)" },
        { SC808_CH, "closed hat (gain)" }
    };
    static const int kVel[6] = { 20, 64, 90, 99, 100, 127 };

    for(int pi = 0; pi < 2; ++pi)
    {
        const int v = kProbe[pi].v;
        printf("  %s\n", kProbe[pi].id);

        double lv[6];
        for(int i = 0; i < 6; ++i) lv[i] = hit_level(v, kVel[i], 127);

        char d[192];

        /* ---- monotonic ------------------------------------------------- */
        bool mono = true;
        for(int i = 1; i < 6; ++i) if(lv[i] < lv[i - 1] * 0.98) mono = false;
        snprintf(d, sizeof d, "%.3f %.3f %.3f %.3f %.3f %.3f at vel 20/64/90/99/100/127",
                 lv[0], lv[1], lv[2], lv[3], lv[4], lv[5]);
        check(mono, "louder velocity is never quieter", d);

        /* ---- no cliff where the old switch sat ------------------------- */
        snprintf(d, sizeof d, "99 -> 100 is %+.2f dB",
                 20.0 * log10((lv[4] + 1e-12) / (lv[3] + 1e-12)));
        check(fabs(lv[4] - lv[3]) < lv[3] * 0.05,
              "no step at 100, where the accent switch used to be", d);

        /* ---- the range actually opens ---------------------------------- */
        snprintf(d, sizeof d, "vel 20 to 127 spans %.1f dB",
                 20.0 * log10((lv[5] + 1e-12) / (lv[0] + 1e-12)));
        check(lv[5] > lv[0] * 2.0, "soft really is softer than hard", d);

        /* ---- depth 0 is flat ------------------------------------------- */
        double lo = 1e30, hi = 0.0;
        for(int i = 0; i < 6; ++i)
        {
            const double x = hit_level(v, kVel[i], 0);
            if(x < lo) lo = x;
            if(x > hi) hi = x;
        }
        snprintf(d, sizeof d, "spread %.2f dB across vel 20..127",
                 20.0 * log10((hi + 1e-12) / (lo + 1e-12)));
        check(hi < lo * 1.02, "Velocity at 0 means velocity is ignored", d);

        /* ---- and it only ever carves down ------------------------------ */
        snprintf(d, sizeof d, "full-velocity hit %.4f, depth 0 hit %.4f", lv[5], hi);
        check(fabs(lv[5] - hi) < hi * 0.02,
              "a full-velocity hit is the same with the knob at either end", d);
        printf("\n");
    }

    printf(fails ? "FAILED (%d)\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
