/*
 * cp_check.cpp — assert what the circuit hand clap claims about itself.
 *
 * The same job tools/bd_check does for the kick. A circuit voice has nothing
 * to null against, so instead every behaviour its header claims is written
 * down here as a test, and the ones that matter most are the two that were
 * reported broken in sc808's clap:
 *
 *   Decay must change the length of the audible tail.
 *   Spread must change the spacing of the burst, and only that.
 *
 * Those two are the whole reason this engine exists, so they are not
 * "probably fine" — they are asserted.
 *
 * Build:  clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/cp_check tools/cp_check.cpp
 *
 * GPL-3.0.
 */
#include "sc808_cp_circuit.h"

#include <cmath>
#include <cstdio>
#include <vector>

static const double SR = 44100.0;
static int fails = 0;

static void check(const bool ok, const char *what, const char *detail = "")
{
    printf("%s: %s%s%s\n", ok ? "ok  " : "FAIL", what,
           detail[0] ? " — " : "", detail);
    if(!ok) ++fails;
}

/* Render one hit, plus enough tail for the longest decay to finish. */
static std::vector<float> hit(const float decay, const float spread,
                              const float room, const double tune = 1.0,
                              const double seconds = 6.0)
{
    sc808::ClapCircuit c;
    c.init(SR);
    c.trigger(tune, decay, spread, room);
    std::vector<float> v((size_t)(SR * seconds), 0.0f);
    for(size_t i = 0; i < v.size(); ++i) v[i] = c.process();
    return v;
}

/* Envelope follower, so burst peaks can be found without chasing zero
 * crossings of the noise. */
static std::vector<double> envelope(const std::vector<float> &v, const double tau)
{
    std::vector<double> e(v.size(), 0.0);
    const double a = exp(-1.0 / (tau * SR));
    double y = 0.0;
    for(size_t i = 0; i < v.size(); ++i)
    {
        const double x = fabs((double)v[i]);
        y = x > y ? x : (y * a + x * (1.0 - a));
        e[i] = y;
    }
    return e;
}

/* Seconds until the envelope drops below `frac` of its peak and stays there. */
static double decayTime(const std::vector<float> &v, const double frac)
{
    const std::vector<double> e = envelope(v, 0.003);
    double pk = 0.0;
    for(double x : e) if(x > pk) pk = x;
    const double thr = pk * frac;
    for(size_t i = e.size(); i-- > 0; )
        if(e[i] > thr) return (double)(i + 1) / SR;
    return 0.0;
}

static double peakOf(const std::vector<float> &v)
{
    double pk = 0.0;
    for(float x : v) if(fabs((double)x) > pk) pk = fabs((double)x);
    return pk;
}

/* Spectral centroid via Goertzel over a log-spaced bank — enough to place a
 * broad bandpass, and no FFT dependency. */
static double centroid(const std::vector<float> &v)
{
    double num = 0.0, den = 0.0;
    for(double f = 100.0; f < 12000.0; f *= 1.03)
    {
        const double w = 2.0 * M_PI * f / SR;
        const double cw = 2.0 * cos(w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for(float x : v) { s0 = (double)x + cw * s1 - s2; s2 = s1; s1 = s0; }
        const double mag = s1 * s1 + s2 * s2 - cw * s1 * s2;
        const double p = mag > 0.0 ? mag : 0.0;
        num += f * p; den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

int main()
{
    printf("circuit hand clap — TR-808 voicing board\n\n");

    /* ---- the derived numbers are what the header says they are --------- */
    {
        const double f = sc808::mfbBandpassFreq(sc808::kCP_R342, sc808::kCP_R334,
                                                sc808::kCP_C128);
        const double q = sc808::mfbBandpassQ(sc808::kCP_R342, sc808::kCP_R334);
        char d[128];
        snprintf(d, sizeof d, "%.1f Hz, Q %.3f", f, q);
        check(fabs(f - 874.4) < 1.0 && fabs(q - 1.291) < 0.01,
              "R342/R334/C128 give the documented bandpass", d);

        snprintf(d, sizeof d, "%.0f ms", sc808::kCP_TailTau * 1000.0);
        check(fabs(sc808::kCP_TailTau - 0.33) < 1e-9,
              "R362 x C143 is the 330 ms tail", d);
    }

    /* ---- Decay actually changes the tail ------------------------------- *
     *
     * The reported fault, as a test. In sc808 the audible tail is a
     * hard-coded six seconds and this measurement came out flat.
     */
    {
        const double shortT = decayTime(hit(0.08f, 0.010f, 1.0f), 0.01);
        const double midT   = decayTime(hit(0.33f, 0.010f, 1.0f), 0.01);
        const double longT  = decayTime(hit(1.20f, 0.010f, 1.0f), 0.01);
        char d[160];
        snprintf(d, sizeof d, "0.08 s -> %.2f s, 0.33 s -> %.2f s, 1.2 s -> %.2f s",
                 shortT, midT, longT);
        check(shortT < midT && midT < longT, "Decay lengthens the tail", d);
        /* and by a musically obvious amount, not a hair */
        check(longT > shortT * 4.0, "across the pot that is a big change", d);
    }

    /* ---- the tail really is exponential with the pot's time constant --- */
    {
        const std::vector<float> v = hit(0.33f, 0.010f, 1.0f);
        const std::vector<double> e = envelope(v, 0.003);
        /* measure well clear of the burst, which ends by 3 x spread */
        const size_t a = (size_t)(0.10 * SR), b = (size_t)(0.60 * SR);
        const double tau = (double)(b - a) / SR / log(e[a] / e[b]);
        char d[96];
        snprintf(d, sizeof d, "measured %.0f ms, asked for 330 ms", tau * 1000.0);
        check(fabs(tau - 0.33) / 0.33 < 0.12, "the tail decays at the pot's tau", d);
    }

    /* ---- Spread moves the burst, and does not become a flam ------------ */
    {
        /* With Room at zero only the burst is left, so its structure is
         * measurable without the tail filling in the gaps. */
        for(double sp : { 0.005, 0.010, 0.020 })
        {
            const std::vector<float> v = hit(0.33f, (float)sp, 0.0f, 1.0, 1.0);
            const std::vector<double> e = envelope(v, 0.0008);
            /* count local maxima above a third of peak — the pulses */
            double pk = 0.0;
            for(double x : e) if(x > pk) pk = x;
            int peaks = 0;
            size_t last = 0;
            for(size_t i = 1; i + 1 < e.size(); ++i)
                if(e[i] > pk / 3.0 && e[i] >= e[i-1] && e[i] > e[i+1]
                   && (peaks == 0 || i - last > (size_t)(sp * SR * 0.5)))
                { ++peaks; last = i; }
            char d[96];
            snprintf(d, sizeof d, "spread %.0f ms -> %d pulses", sp * 1000.0, peaks);
            check(peaks == sc808::kCP_PULSES, "the burst is three pulses", d);
        }

        /* the LAST pulse lands at 2 x spread, so the burst scales with it */
        const std::vector<float> a = hit(0.33f, 0.005f, 0.0f, 1.0, 1.0);
        const std::vector<float> b = hit(0.33f, 0.020f, 0.0f, 1.0, 1.0);
        const double ta = decayTime(a, 0.05), tb = decayTime(b, 0.05);
        char d[128];
        snprintf(d, sizeof d, "5 ms -> %.0f ms burst, 20 ms -> %.0f ms burst",
                 ta * 1000.0, tb * 1000.0);
        check(tb > ta * 2.0, "Spread scales the burst's length", d);
    }

    /* ---- Spread must NOT change the level or the colour ---------------- *
     *
     * The complaint about sc808's Spread was that it changed the character of
     * the voice rather than its timing. Here it may only move the pulses.
     */
    {
        const double p1 = peakOf(hit(0.33f, 0.006f, 1.0f));
        const double p2 = peakOf(hit(0.33f, 0.024f, 1.0f));
        char d[96];
        snprintf(d, sizeof d, "%.3f vs %.3f", p1, p2);
        check(fabs(20.0 * log10(p1 / p2)) < 1.5,
              "Spread leaves the level alone", d);
    }

    /* ---- the voice sits where the bandpass puts it --------------------- */
    {
        const double c = centroid(hit(0.33f, 0.010f, 1.0f));
        char d[96];
        snprintf(d, sizeof d, "%.0f Hz", c);
        check(c > 600.0 && c < 1600.0, "energy sits around the 874 Hz bandpass", d);

        /* Tune moves it */
        const double lo = centroid(hit(0.33f, 0.010f, 1.0f, 0.5));
        const double hi = centroid(hit(0.33f, 0.010f, 1.0f, 2.0));
        snprintf(d, sizeof d, "%.0f Hz -> %.0f Hz", lo, hi);
        check(hi > lo * 1.8, "Tune moves the bandpass", d);
    }

    /* ---- Room balances tail against burst ------------------------------ */
    {
        const double dry = decayTime(hit(0.33f, 0.010f, 0.0f), 0.01);
        const double wet = decayTime(hit(0.33f, 0.010f, 2.0f), 0.01);
        char d[96];
        snprintf(d, sizeof d, "room 0 -> %.2f s, room 2 -> %.2f s", dry, wet);
        check(wet > dry * 3.0, "Room brings in the tail", d);
    }

    /* ---- the note ends ------------------------------------------------- */
    {
        sc808::ClapCircuit c;
        c.init(SR);
        c.trigger(1.0, 1.5f, 0.010f, 2.0f);            /* the longest setting */
        int n = 0;
        while(c.active() && n < (int)(SR * 30.0)) { c.process(); ++n; }
        char d[96];
        snprintf(d, sizeof d, "%.2f s at the longest decay", n / SR);
        check(!c.active(), "the voice releases itself", d);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
