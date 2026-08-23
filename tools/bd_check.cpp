/*
 * bd_check.cpp — hold the circuit bass drum to the paper's own claims.
 *
 * The rest of the kit has a null test: render it in scsynth, render it here,
 * subtract. The circuit kick cannot have one, because there is nothing to
 * null against — it is a model built from a published analysis rather than a
 * transcription of a program. So it gets this instead: every behaviour Werner,
 * Abel and Smith assert about the real circuit, asserted here as a test.
 *
 * These are not smoke tests. Each one fails if the model quietly stops being
 * a bridged-T in a feedback loop and becomes an envelope with a sine on it —
 * which is exactly the failure mode this voice exists to avoid, and which
 * would still sound like a kick drum.
 *
 *   ./build-native/bd_check
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_bd_circuit.h"

using namespace sc808;

static const double SR = 44100.0;
static const int    N  = (int)(44100 * 4);
static float g_buf[N];

static int g_fail = 0;
static void ok(const int cond, const char *what, const char *detail)
{
    printf("  %s  %s%s%s\n", cond ? "ok  " : "FAIL", what,
           detail ? " — " : "", detail ? detail : "");
    if(!cond) ++g_fail;
}

/* ---- measurement helpers ---------------------------------------------- */

/* Peak of a sliding 30 ms window — long enough to span a cycle at 50 Hz, so
 * it measures the envelope and not the waveform. */
static double envAt(const int i, const int n)
{
    const int w = (int)(SR * 0.03);
    double m = 0.0;
    for(int j = i; j < i + w && j < n; ++j)
    { const double a = fabs((double)g_buf[j]); if(a > m) m = a; }
    return m;
}

/* Time in ms from the peak to a given decay, or -1 if it never gets there. */
static double decayMs(const int n, const double db)
{
    double pk = 0.0; int pki = 0;
    for(int i = 0; i < n - (int)(SR * 0.03); i += 64)
    { const double e = envAt(i, n); if(e > pk) { pk = e; pki = i; } }
    const double target = pk * pow(10.0, -db / 20.0);
    for(int i = pki; i < n - (int)(SR * 0.03); i += 64)
        if(envAt(i, n) < target) return (double)i / SR * 1000.0;
    return -1.0;
}

/* Frequency of the k-th half period, in Hz. */
static double halfPeriodHz(const int k, const int n)
{
    int seen = -1, last = -1;
    for(int i = 1; i < n; ++i)
        if((g_buf[i - 1] < 0.0f) != (g_buf[i] < 0.0f))
        {
            if(last >= 0 && ++seen == k) return SR / (2.0 * (i - last));
            last = i;
        }
    return 0.0;
}

/* Fundamental in a window, by FFT-free autocorrelation over a plausible band. */
static double fundamental(const int from, const int len)
{
    double best = 0.0; int bestLag = 0;
    const int lo = (int)(SR / 200.0), hi = (int)(SR / 30.0);
    for(int lag = lo; lag <= hi; ++lag)
    {
        double s = 0.0;
        for(int i = from; i < from + len - lag; ++i)
            s += (double)g_buf[i] * (double)g_buf[i + lag];
        if(s > best) { best = s; bestLag = lag; }
    }
    return bestLag ? SR / bestLag : 0.0;
}

static void render(CircuitBassDrum &v, const int n)
{
    for(int i = 0; i < n; ++i) g_buf[i] = v.process();
}

int main(void)
{
    printf("8W8 circuit bass drum — checked against Werner, Abel and Smith,\n"
           "\"A Physically-Informed, Circuit-Bendable, Digital Model of the\n"
           " Roland TR-808 Bass Drum Circuit\", DAFx-14.\n\n");

    /* ---- 1. Decay is loop gain, and it spans a real range ----
     * "The length of an 808 bass drum note is user-controllable via VR6" and
     * the feedback buffer's family of responses is drawn for k in
     * [0.001, 1.0]. If Decay ever stops being loop gain and becomes an
     * envelope time, this still passes — but the NEXT test will not. */
    printf("decay as loop gain\n");
    {
        double prev = -1.0; int mono = 1;
        char detail[128] = "";
        for(int i = 0; i < 5; ++i)
        {
            const float k = 0.1f + 0.225f * i;
            CircuitBassDrum v; v.init(SR);
            v.trigger(kBD_F0_NOMINAL, k, 0.5f, 1.0f, 8.0f);
            render(v, N);
            const double d = decayMs(N, 40.0);
            if(i == 0) snprintf(detail, sizeof(detail), "k=0.1 -> %.0f ms", d);
            if(i == 4) snprintf(detail + strlen(detail), sizeof(detail) - strlen(detail),
                                ", k=1.0 -> %.0f ms", d);
            if(d < prev) mono = 0;
            prev = d;
        }
        ok(mono, "longer decay pot gives a longer note, monotonically", detail);
        ok(prev > 700.0, "the pot reaches a real 808 length (>700 ms at full)", detail);
    }

    /* ---- 2. The note is longer than the network's own ringing ----
     * The bridged-T alone has Q 2.36 at 49.5 Hz, which rings for about
     * 100 ms. Anything past that comes from the loop and nowhere else. If
     * someone replaces the loop with an amplitude envelope, this test is the
     * one that notices: the envelope would still produce a long note, but
     * turning the decay to zero would no longer collapse it to the
     * network's own 100 ms. */
    printf("\nthe loop, not an envelope\n");
    {
        CircuitBassDrum a; a.init(SR);
        a.trigger(kBD_F0_NOMINAL, 0.0f, 0.5f, 1.0f, 8.0f);
        render(a, N);
        const double bare = decayMs(N, 40.0);

        CircuitBassDrum b; b.init(SR);
        b.trigger(kBD_F0_NOMINAL, 1.0f, 0.5f, 1.0f, 8.0f);
        render(b, N);
        const double full = decayMs(N, 40.0);

        char d[128];
        snprintf(d, sizeof(d), "loop open %.0f ms, loop closed %.0f ms (%.1fx)",
                 bare, full, full / (bare > 0 ? bare : 1));
        ok(bare > 40.0 && bare < 250.0,
           "with the loop open the network rings for about its own Q", d);
        ok(full > bare * 4.0, "closing the loop multiplies that several times", d);
    }

    /* ---- 3. The pitch sighs ----
     * "This sigh is actually a consequence of leakage through R161." The
     * paper's Figure 11 shows the instantaneous frequency starting near 57 Hz
     * and settling around 49 Hz over the first 300 ms. Downward, and it must
     * actually arrive. */
    printf("\npitch sigh\n");
    {
        CircuitBassDrum v; v.init(SR);
        v.trigger(kBD_F0_NOMINAL, 0.9f, 0.5f, 0.0f, 8.0f);   /* attack off:
                                                              * isolate the
                                                              * sigh from the
                                                              * attack jump */
        render(v, N);
        const int w = (int)(SR * 0.25);
        const double early = fundamental((int)(SR * 0.02), w);
        const double late  = fundamental((int)(SR * 1.20), w);
        char d[128];
        snprintf(d, sizeof(d), "%.1f Hz -> %.1f Hz (paper: 57 -> 49)", early, late);
        ok(early > late + 1.0, "the pitch falls through the note", d);
        ok(early - late < 15.0, "and it is a sigh, not a sweep", d);
        ok(fabs(late - kBD_F0_NOMINAL) < 4.0,
           "it settles at the network's resting frequency", d);
    }

    /* ---- 4. The attack is a frequency jump ----
     * "the transfer functions describing the bridged-T networks behaviour
     * under changing component values... raising both the Q and the center
     * frequency" — and the shift is "more than an octave". The paper is
     * explicit that this is NOT heard as a pitch, because it is over inside
     * one period, but that it is what makes the attack punchy. */
    printf("\nattack-time frequency shift\n");
    {
        CircuitBassDrum off; off.init(SR);
        off.trigger(kBD_F0_NOMINAL, 0.8f, 0.5f, 0.0f, 8.0f);
        render(off, N);
        const double flat = halfPeriodHz(0, N);

        CircuitBassDrum on; on.init(SR);
        on.trigger(kBD_F0_NOMINAL, 0.8f, 0.5f, 1.0f, 8.0f);
        render(on, N);
        const double lifted = halfPeriodHz(0, N);
        const double settled = halfPeriodHz(8, N);

        char d[160];
        snprintf(d, sizeof(d), "first half-period %.0f Hz vs %.0f Hz flat, "
                 "settling to %.0f Hz", lifted, flat, settled);
        ok(lifted > flat * 1.9, "full attack lifts the first period by an octave+", d);
        ok(settled < lifted * 0.7, "and it is gone within a few cycles", d);
    }

    /* ---- 5. No machine-gun effect ----
     * The paper's Figure 12 is titled exactly that. "Each note is slightly
     * different, as in a real 808, since the remaining filter states may
     * interfere constructively or destructively with the response to a new
     * trigger." A voice that resets its state on trigger produces identical
     * notes; that is the effect, and it is what the whole no-reset policy in
     * CircuitBassDrum::trigger() exists to avoid. */
    printf("\nno machine-gun on fast repeats\n");
    {
        CircuitBassDrum v; v.init(SR);
        const int period = (int)(SR * 0.125);            /* 16ths at 120 bpm */
        double peak[8];
        for(int h = 0; h < 8; ++h)
        {
            v.trigger(kBD_F0_NOMINAL, 0.9f, 0.5f, 1.0f, 8.0f);
            for(int i = 0; i < period; ++i) g_buf[i] = v.process();
            double p = 0.0;
            for(int i = 0; i < period; ++i)
            { const double a = fabs((double)g_buf[i]); if(a > p) p = a; }
            peak[h] = p;
        }
        double lo = peak[1], hi = peak[1];
        for(int h = 2; h < 8; ++h)
        { if(peak[h] < lo) lo = peak[h]; if(peak[h] > hi) hi = peak[h]; }
        char d[128];
        snprintf(d, sizeof(d), "peaks %.3f..%.3f over 7 repeats, spread %.1f%%",
                 lo, hi, 100.0 * (hi - lo) / (hi > 0 ? hi : 1));
        ok(hi - lo > hi * 0.005, "repeated hits are not identical copies", d);
        ok(hi - lo < hi * 0.60, "but they are still the same drum", d);
        ok(hi < 4.0, "and the loop does not run away when retriggered", d);
    }

    /* ---- 6. Accent is a trigger VOLTAGE, not a gain ----
     * "A timing signal and accent signal are produced by the CPU, and combined
     * into a common trigger signal Vct, whose ON voltage is set by VR3." That
     * voltage drives the pulse shaper's diode, which is not linear in it — so
     * a harder hit is a different sound and not only a louder one. */
    printf("\naccent\n");
    {
        double pk[2], cent[2];
        for(int i = 0; i < 2; ++i)
        {
            CircuitBassDrum v; v.init(SR);
            v.trigger(kBD_F0_NOMINAL, 0.8f, 0.5f, 1.0f, i ? 14.0f : 4.0f);
            render(v, N);
            double p = 0.0, num = 0.0, den = 0.0;
            for(int j = 0; j < (int)(SR * 0.05); ++j)
            {
                const double a = fabs((double)g_buf[j]);
                if(a > p) p = a;
            }
            /* crude brightness: mean |first difference| over mean |signal|,
             * which rises with harmonic content */
            for(int j = 1; j < (int)(SR * 0.05); ++j)
            { num += fabs((double)g_buf[j] - (double)g_buf[j - 1]);
              den += fabs((double)g_buf[j]); }
            pk[i] = p; cent[i] = den > 0 ? num / den : 0.0;
        }
        char d[160];
        snprintf(d, sizeof(d), "4 V peak %.3f bright %.4f, 14 V peak %.3f bright %.4f",
                 pk[0], cent[0], pk[1], cent[1]);
        ok(pk[1] > pk[0] * 1.2, "a harder trigger is louder", d);
        ok(fabs(cent[1] - cent[0]) > cent[0] * 0.01,
           "and it is not only louder — the harmonic content moves too", d);
    }

    /* ---- 7. Stability, and level across the tuning range ----
     *
     * A feedback loop whose gain approaches unity is exactly where a model
     * goes unstable, and the decay pot's job is to take it there. The test is
     * DIVERGENCE, not an absolute ceiling: a resonator hammered at 16ths
     * legitimately builds, and what matters is whether it plateaus. An
     * earlier version of this check asserted a fixed threshold and failed on
     * a loop that was perfectly well behaved at 4.1 and holding.
     *
     * The tuning sweep is here because it is what caught the real bug: the
     * bridged-T's forward gain goes as 1/Reff and Reff as 1/f0^2, so before
     * tuneTrim_ existed the top of the Tune pot ran 26 dB hot, hit the safety
     * limiter and reset the voice on every single hit — which showed up as a
     * suspiciously IDENTICAL peak on all forty repeats.
     */
    printf("\nstability and tuning\n");
    {
        int nonfinite = 0, diverged = 0;
        double loLevel = 1e30, hiLevel = 0.0;
        char d[160] = "";
        for(int t = 0; t < 5; ++t)
        {
            /* the full range of the Tune pot: base note 34 +/- 12 semitones */
            const double f0 = 29.1 * pow(116.5 / 29.1, t / 4.0);
            CircuitBassDrum v; v.init(SR);
            double first = 0.0, last = 0.0, worst = 0.0;
            for(int h = 0; h < 40; ++h)
            {
                v.trigger(f0, 1.0f, 1.0f, 1.0f, 14.0f);
                double p = 0.0;
                for(int i = 0; i < (int)(SR * 0.05); ++i)
                {
                    const float y = v.process();
                    if(!(y > -1e6f && y < 1e6f)) ++nonfinite;
                    else { const double a = fabs((double)y); if(a > p) p = a; }
                }
                if(h == 0) first = p;
                if(h >= 30) { last = p; if(p > worst) worst = p; }
            }
            /* Plateaued: the last third is not still climbing away from the
             * first hit by more than an order of magnitude. */
            if(last > first * 12.0) ++diverged;
            if(first < loLevel) loLevel = first;
            if(first > hiLevel) hiLevel = first;
            (void)worst;
        }
        snprintf(d, sizeof(d), "single-hit peak %.2f..%.2f across the Tune range "
                 "(%.1f dB)", loLevel, hiLevel,
                 20.0 * log10(hiLevel / (loLevel > 0 ? loLevel : 1e-9)));
        ok(nonfinite == 0, "40 retriggers at full decay stay finite", NULL);
        ok(diverged == 0, "and plateau rather than diverging", NULL);
        ok(hiLevel < loLevel * 4.0,
           "the Tune pot does not swing the level more than 12 dB", d);
    }

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
