/*
 * sd_check.cpp — the snare's Decay pot must work on the WHOLE voice.
 *
 * THE BUG THIS EXISTS FOR. Decay scaled only the two shells' Q. The noise
 * burst ran on a fixed 75 ms constant, so with Snappy up it outlasted the
 * shells and swamped the knob: the note measured 0.25 s to 0.30 s across the
 * entire throw, a range of 1.18x, against 3.7x with Snappy down. Reported
 * from the field as "decay does not take the snappy part" — which was exactly
 * right, and on the half of the voice you can actually hear, the pot did
 * nothing.
 *
 * Asserted here as a PROPERTY rather than a curve: whatever Snappy is set to,
 * the Decay pot has to change the length of the note by a real factor, and it
 * has to do it monotonically. A future change that reintroduces a fixed tail
 * anywhere in this voice fails this.
 *
 * Build: clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/sd_check \
 *            tools/sd_check.cpp src/dsp/sc808_engine.cpp
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

/* Seconds until the note falls below 1% of its own peak and stays there. */
static double note_len(int decayPot, int snappyPot)
{
    sc808_engine_t *e = sc808_create((float)SR);
    char b[16];
    snprintf(b, sizeof b, "%d", decayPot);  sc808_set_param(e, "sd_decay", b);
    snprintf(b, sizeof b, "%d", snappyPot); sc808_set_param(e, "sd_snappy", b);

    static const int N = (int)(SR * 3.0);
    static float buf[(int)(44100 * 3)];
    memset(buf, 0, sizeof buf);
    sc808_trigger(e, SC808_SD, 110);
    sc808_render(e, buf, N);
    sc808_destroy(e);

    double pk = 0.0;
    for(int i = 0; i < N; ++i) { const double a = fabs((double)buf[i]); if(a > pk) pk = a; }
    for(int i = N; i-- > 0; ) if(fabs((double)buf[i]) > pk * 0.01) return (double)i / SR;
    return 0.0;
}

int main()
{
    printf("circuit snare — Decay reaches the whole voice\n\n");

    static const int kSnappy[3] = { 0, 64, 127 };
    static const int kDecay[5]  = { 0, 32, 64, 96, 127 };

    for(int s = 0; s < 3; ++s)
    {
        double L[5];
        for(int i = 0; i < 5; ++i) L[i] = note_len(kDecay[i], kSnappy[s]);

        char d[192];
        snprintf(d, sizeof d, "snappy %3d: %.3f %.3f %.3f %.3f %.3f s",
                 kSnappy[s], L[0], L[1], L[2], L[3], L[4]);

        bool mono = true;
        for(int i = 1; i < 5; ++i) if(L[i] < L[i - 1] * 0.98) mono = false;
        check(mono, "Decay lengthens the note monotonically", d);

        char r[96];
        const double range = L[4] / (L[0] > 1e-9 ? L[0] : 1e-9);
        snprintf(r, sizeof r, "snappy %3d: %.2fx from end to end", kSnappy[s], range);
        /* 2.5x: the shells alone give 3.7x, and the fault shipped at 1.18x.
         * Anything under this means part of the voice has stopped listening
         * to the knob again. */
        check(range > 2.5, "and the pot has real range at every Snappy setting", r);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
