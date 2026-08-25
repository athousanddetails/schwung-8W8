/*
 * tom_check.cpp — assert what the circuit tom / conga channel claims.
 *
 * Same job as tools/bd_check and tools/cp_check. The claims worth testing are
 * not "it makes a sound" but the two the voice exists for:
 *
 *   A TOM AND A CONGA AT THE SAME PITCH MUST NOT BE THE SAME SOUND. That was
 *   the report — "Hi Tom and Conga are very similar" — and in sc808 they are
 *   literally the same graph with a different decay. Here the tom has a noise
 *   head and the conga does not, so a spectral and a transient measure both
 *   have to separate them, at matched pitch, with nothing else differing.
 *
 *   THE TWO ENGINES MUST AGREE ABOUT LEVEL. Each lane has one trim, shared,
 *   so if the circuit tom is louder than the sc808 tom then switching the
 *   Engine switch changes the mix and the fitted kit balance is wrong for one
 *   of them.
 *
 * Build:  clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/tom_check tools/tom_check.cpp
 *
 * GPL-3.0.
 */
#include "sc808_tom_circuit.h"
#include "sc808_voices.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

static const double SR = 44100.0;
static int fails = 0;

static void check(const bool ok, const char *what, const char *detail = "")
{
    printf("%s: %s%s%s\n", ok ? "ok  " : "FAIL", what,
           detail[0] ? " — " : "", detail);
    if(!ok) ++fails;
}

static double midicps(const double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }

static std::vector<float> circuitHit(const int mode, const double hz,
                                     const float decay01, const float accentV = 8.0f,
                                     const double seconds = 4.0)
{
    sc808::TomCircuit t;
    t.init(SR, mode);
    t.trigger(hz, decay01, accentV);
    std::vector<float> v((size_t)(SR * seconds), 0.0f);
    for(size_t i = 0; i < v.size(); ++i) v[i] = t.process();
    return v;
}

static std::vector<float> sc808Hit(const sc808::TomSpec &spec, const double hz,
                                   const float decaySec, const double seconds = 4.0)
{
    sc808::Tom t;
    t.init(SR);
    t.trigger(spec, hz, decaySec);
    std::vector<float> v((size_t)(SR * seconds), 0.0f);
    for(size_t i = 0; i < v.size(); ++i) v[i] = t.process();
    return v;
}

static double peakOf(const std::vector<float> &v)
{
    double pk = 0.0;
    for(float x : v) if(fabs((double)x) > pk) pk = fabs((double)x);
    return pk;
}

/* Energy above `hz`, as a fraction of total — the noise head lives up there
 * and a pure ringing shell does not. */
static double highFraction(const std::vector<float> &v, const double hz)
{
    double hi = 0.0, all = 0.0;
    for(double f = 60.0; f < 12000.0; f *= 1.06)
    {
        const double w = 2.0 * M_PI * f / SR;
        const double cw = 2.0 * cos(w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for(float x : v) { s0 = (double)x + cw * s1 - s2; s2 = s1; s1 = s0; }
        double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
        if(p < 0.0) p = 0.0;
        all += p;
        if(f > hz) hi += p;
    }
    return all > 0.0 ? hi / all : 0.0;
}

static double decayTime(const std::vector<float> &v, const double frac)
{
    double pk = 0.0;
    for(float x : v) if(fabs((double)x) > pk) pk = fabs((double)x);
    const double thr = pk * frac;
    for(size_t i = v.size(); i-- > 0; )
        if(fabs((double)v[i]) > thr) return (double)(i + 1) / SR;
    return 0.0;
}

int main()
{
    printf("circuit tom / conga — TR-808 voicing board\n\n");

    /* ---- the one derived number ---------------------------------------- */
    {
        const double q = sc808::bridgedTQ(sc808::kTOM_R1, sc808::kTOM_R2);
        char d[96];
        snprintf(d, sizeof d, "R218/R219 = 2.2M/4.7k gives Q %.3f", q);
        check(fabs(q - sc808::kTOM_Q) < 1e-9, "Q comes from the component values", d);
    }

    /* ---- THE COMPLAINT: tom and conga must differ at the same pitch ---- *
     *
     * Everything matched except the switch — same frequency, same decay, same
     * accent. Whatever separates them has to be the circuit, not the tuning.
     */
    {
        const double hz = midicps(52.0);
        const std::vector<float> tom = circuitHit(0, hz, 0.65f);
        const std::vector<float> cga = circuitHit(1, hz, 0.65f);

        const double ht = highFraction(tom, 900.0);
        const double hc = highFraction(cga, 900.0);
        char d[160];
        snprintf(d, sizeof d, "above 900 Hz: tom %.1f%%, conga %.1f%%",
                 ht * 100.0, hc * 100.0);
        check(ht > hc * 3.0, "the tom has a noise head the conga has not", d);

        /*
         * And the tom's attack is sharper against its own body. Measured as a
         * RATIO within each voice, because the two are separately level-
         * matched to their sc808 counterparts — comparing raw energy between
         * them measures the fitted output scales, not the circuit.
         */
        double et = 0.0, ec = 0.0, tt = 0.0, tc = 0.0;
        const size_t n = (size_t)(0.006 * SR);
        for(size_t i = 0; i < tom.size(); ++i)
        {
            if(i < n) { et += tom[i] * tom[i]; ec += cga[i] * cga[i]; }
            tt += tom[i] * tom[i]; tc += cga[i] * cga[i];
        }
        const double ft = et / (tt + 1e-30), fc = ec / (tc + 1e-30);
        snprintf(d, sizeof d, "first 6 ms holds %.1f%% of the tom, %.1f%% of the conga",
                 ft * 100.0, fc * 100.0);
        check(ft > fc * 1.3, "the tom's strike is sharper than the conga's", d);

        /* the two are not the same waveform in any useful sense */
        double num = 0.0, dt = 0.0, dc = 0.0;
        for(size_t i = 0; i < tom.size(); ++i)
        { num += tom[i] * cga[i]; dt += tom[i] * tom[i]; dc += cga[i] * cga[i]; }
        const double corr = fabs(num) / sqrt((dt + 1e-30) * (dc + 1e-30));
        snprintf(d, sizeof d, "correlation %.3f", corr);
        check(corr < 0.9, "tom and conga are not the same signal", d);
    }

    /* ---- Decay is loop gain, and it works ------------------------------ */
    {
        const double hz = midicps(52.0);
        const double a = decayTime(circuitHit(0, hz, 0.0f), 0.01);
        const double b = decayTime(circuitHit(0, hz, 0.5f), 0.01);
        const double c = decayTime(circuitHit(0, hz, 1.0f), 0.01);
        char d[160];
        snprintf(d, sizeof d, "%.2f s / %.2f s / %.2f s", a, b, c);
        check(a < b && b < c, "Decay lengthens the ring", d);
        check(c < 4.0, "and the longest setting still ends", d);
    }

    /* ---- lower drums ring longer, as a resonator must ------------------ */
    {
        const double lo = decayTime(circuitHit(0, midicps(40.0), 0.65f), 0.01);
        const double hi = decayTime(circuitHit(0, midicps(52.0), 0.65f), 0.01);
        char d[128];
        snprintf(d, sizeof d, "low tom %.2f s, hi tom %.2f s", lo, hi);
        check(lo > hi, "the low tom rings longer than the hi tom at one Decay", d);
    }

    /* ---- the two engines agree about level ----------------------------- *
     *
     * Same lane, same trim, so a switch between engines must not move the
     * mix. Compared at each lane's own default note and default Decay.
     */
    {
        struct Lane { const char *id; const sc808::TomSpec *spec; double note;
                      float decaySec; int mode; };
        const Lane lanes[] = {
            { "lt", &sc808::kTomLo,   40.0, 20.0f, 0 },
            { "mt", &sc808::kTomMid,  44.0, 16.0f, 0 },
            { "ht", &sc808::kTomHi,   52.0, 11.0f, 0 },
            { "lc", &sc808::kCongaLo, 52.0, 18.0f, 1 },
            { "mc", &sc808::kCongaMid,57.0, 18.0f, 1 },
            { "hc", &sc808::kCongaHi, 62.0, 18.0f, 1 },
        };
        /* the default Decay pot position, from gen_params' 1..40 EXP range */
        double worst = 0.0;
        const char *worstId = "";
        for(const Lane &L : lanes)
        {
            const double hz = midicps(L.note);
            const float pot = (float)(log((double)L.decaySec / 1.0) / log(40.0 / 1.0));
            const double pa = peakOf(sc808Hit(*L.spec, hz, L.decaySec));
            const double pb = peakOf(circuitHit(L.mode, hz, pot));
            const double db = 20.0 * log10((pb + 1e-30) / (pa + 1e-30));
            printf("     %s  sc808 %.4f   circuit %.4f   %+.1f dB\n",
                   L.id, pa, pb, db);
            if(fabs(db) > fabs(worst)) { worst = db; worstId = L.id; }
        }
        char d[96];
        snprintf(d, sizeof d, "worst %s at %+.1f dB", worstId, worst);
        check(fabs(worst) < 3.0, "the two engines land within 3 dB on every lane", d);
    }

    /* ---- accent is a trigger voltage, so it changes the sound ---------- */
    {
        const double hz = midicps(52.0);
        const std::vector<float> soft = circuitHit(0, hz, 0.65f, 4.0f);
        const std::vector<float> hard = circuitHit(0, hz, 0.65f, 14.0f);
        const double ps = peakOf(soft), ph = peakOf(hard);
        char d[128];
        snprintf(d, sizeof d, "%.1f dB louder", 20.0 * log10(ph / ps));
        check(ph > ps * 1.5, "a harder trigger is louder", d);

        /*
         * And not ONLY louder — the same claim bd_check makes about the kick,
         * for the same reason: accent is a trigger VOLTAGE into a diode, so a
         * harder hit comes back a different shape and not just a bigger one.
         *
         * This assertion is here because its absence was a symptom. While the
         * pulse shaper was being left uninitialised it returned a fixed
         * fraction of its input, the diode never saw anything, and the two
         * accents correlated at 1.000000 — a pure gain. That reading was the
         * bug reporting itself and it was nearly written off as "this voice
         * just works that way". Correlation below 1 is now load-bearing.
         */
        double num = 0.0, ds = 0.0, dh = 0.0;
        for(size_t i = 0; i < soft.size(); ++i)
        { num += soft[i] * hard[i]; ds += soft[i] * soft[i]; dh += hard[i] * hard[i]; }
        const double corr = fabs(num) / sqrt((ds + 1e-30) * (dh + 1e-30));
        snprintf(d, sizeof d, "waveform correlation %.6f", corr);
        check(corr < 0.999, "and it is not only louder — the diode shapes it", d);
    }

    printf(fails ? "\nFAILED (%d)\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
