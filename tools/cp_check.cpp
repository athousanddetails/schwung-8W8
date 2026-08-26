/*
 * cp_check.cpp — assert what the circuit hand clap claims.
 *
 * The strongest test here is the ENVELOPE TABLE: the reference render's RMS
 * in 5 ms windows, embedded, and the voice must track it. That table is what
 * caught the versions that matched a scalar and missed the sound — a clap
 * whose "decay to 1%" agreed while the whole middle of the note was absent.
 *
 * The derived constants are asserted against their components: the main
 * decay is C144 x R365 = 38.5 ms and the floor is C143 x R362 = 330 ms —
 * two different RCs with two different jobs, conflating which is how the
 * first envelope went wrong.
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

        snprintf(d, sizeof d, "main %.1f ms, floor %.0f ms",
                 sc808::kCP_MainTau * 1000.0, sc808::kCP_FloorTau * 1000.0);
        check(fabs(sc808::kCP_MainTau - 0.0385) < 1e-6,
              "the main decay is C144 x R365", d);
        check(fabs(sc808::kCP_FloorTau - 0.330) < 1e-6,
              "the floor is C143 x R362", d);
    }

    /* ---- the envelope tracks the reference ----------------------------- *
     *
     * Roland's own render at the default kit, RMS in 5 ms windows,
     * normalised to its peak. Teeth at 0/10/20 ms, the main hit at 35, the
     * two-slope tail. Tolerance is generous — noise realisations differ —
     * but a missing tooth, a missing tail, or a peak in the wrong window
     * fails immediately.
     */
    {
        static const double REF[24] = { 42,14,42,19,49,42,47,100,79,69,57,45,
                                        36,27,23,20,16,16,10, 9, 7, 6, 8, 6 };
        sc808::ClapCircuit c;
        c.init(SR);
        c.trigger(1.0, 1.0f, 0.010f, 0.0f);
        std::vector<double> v;
        for(int i = 0; i < (int)(SR * 0.12); ++i) v.push_back(c.process());
        const int w = (int)(SR * 0.005);
        double e[24], mx = 0.0;
        for(int k = 0; k < 24; ++k)
        {
            double a = 0.0;
            for(int i = k * w; i < (k + 1) * w && i < (int)v.size(); ++i)
                a += v[i] * v[i];
            e[k] = sqrt(a / w);
            if(e[k] > mx) mx = e[k];
        }
        for(int k = 0; k < 24; ++k) e[k] = 100.0 * e[k] / (mx > 0 ? mx : 1);

        int peakAt = 0; double pv = 0;
        for(int k = 0; k < 24; ++k) if(e[k] > pv) { pv = e[k]; peakAt = k; }
        char d[128];
        /* the reference peaks in window 7; realisation noise moves ours
         * between 7 and 8. The claim is: AFTER the bursts, not among them. */
        snprintf(d, sizeof d, "peak window %d (ref 7)", peakAt);
        check(peakAt == 7 || peakAt == 8,
              "the loudest instant is the post-burst hit", d);

        check(e[0] > 25 && e[2] > 25 && e[4] > 20, "three teeth are present",
              "windows 0/2/4");
        check(e[1] < e[0] && e[3] < e[2], "with gaps between them", "");

        double err = 0;
        for(int k = 0; k < 24; ++k) err += (e[k] - REF[k]) * (e[k] - REF[k]);
        /* Threshold calibrated against the comparison's own noise: two
         * renders of THIS voice with different noise realisations differ by
         * 10.7 points rms in these windows, and the reference is itself one
         * realisation. 16 = that floor plus real headroom; a missing tooth
         * or tail blows far past it. */
        snprintf(d, sizeof d, "rms deviation %.1f points (stochastic floor ~11)", sqrt(err / 24));
        check(sqrt(err / 24) < 16.0, "the whole envelope tracks the reference", d);
    }

    /* ---- the Decay pot scales the note --------------------------------- */
    {
        auto len = [](float dscale){
            sc808::ClapCircuit c;
            c.init(SR);
            c.trigger(1.0, dscale, 0.010f, 0.0f);
            std::vector<float> v((size_t)(SR * 4), 0.0f);
            for(size_t i = 0; i < v.size(); ++i) v[i] = c.process();
            double pk = 0; for(float x : v) if(fabs(x) > pk) pk = fabs(x);
            for(size_t i = v.size(); i-- > 0; )
                if(fabs((double)v[i]) > pk * 0.01) return (double)i / SR;
            return 0.0; };
        const double a = len(0.5f), b = len(1.0f), c2 = len(2.0f);
        char d[128];
        snprintf(d, sizeof d, "0.5x %.2f s, 1x %.2f s, 2x %.2f s", a, b, c2);
        check(a < b && b < c2, "Decay orders the note", d);
    }

    /* ---- the note ends -------------------------------------------------- */
    {
        sc808::ClapCircuit c;
        c.init(SR);
        c.trigger(1.0, 2.8f, 0.010f, 0.0f);
        int n = 0;
        while(c.active() && n < (int)(SR * 30.0)) { c.process(); ++n; }
        char d[96];
        snprintf(d, sizeof d, "%.2f s at the longest setting", n / SR);
        check(!c.active(), "the voice releases itself", d);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
