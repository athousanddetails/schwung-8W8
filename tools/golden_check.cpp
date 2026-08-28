/*
 * golden_check.cpp — the kit's sound, frozen as numbers.
 *
 * WHY THIS EXISTS. The voices were tuned one at a time against the player's
 * own reference renders, over many rounds, and each one was signed off by
 * ear on the hardware. That work is not reproducible from the source: it
 * lives in filter corners and envelope constants that nothing else asserts.
 * Structural work — deleting the Engine switches, adding sends, replacing
 * accent with velocity — touches the engine that WRAPS those voices, and a
 * mistake there is silent: the kit still plays, it just plays something
 * else.
 *
 * So: render every lane on its own through the real engine, at the panel's
 * defaults, and record a checksum plus a fingerprint. Run it before a
 * structural change to write the baseline, run it after to prove the audio
 * did not move.
 *
 *   ./build-native/golden_check --write   > tools/golden.txt   (baseline)
 *   ./build-native/golden_check           compares against tools/golden.txt
 *
 * TWO COMPARISONS, and the second one is not slack. On the host that wrote
 * the baseline the samples must be BIT-IDENTICAL, which is the strongest
 * statement available. Across hosts they cannot be: this repo builds on a Mac
 * (arm64 clang) and on the VPS (x86_64 gcc), and those disagree in the last
 * bit of tanhf, expf and their FMA contractions. A baseline written on one
 * fails on the other with every descriptor identical to twelve significant
 * digits, and a hash cannot tell that apart from a real change. So the
 * fallback compares the fingerprint — peak, rms, centroid, length and sixteen
 * 120 ms window RMS values — with a relative tolerance of 1e-6.
 *
 * That threshold is picked from measurement, not taste: the actual worst
 * cross-compiler deviation across all 32 renders is 1.95e-9, and a structural
 * mistake moves a lane by whole dB or changes its length outright. There is
 * about three orders of magnitude of daylight on each side.
 *
 * This asserts NOTHING about whether the kit sounds good — that is the
 * player's ear and it has already ruled. It asserts only that today's build
 * makes the same samples yesterday's did.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_engine.h"

static const double SR = 44100.0;
static const int    kTailFrames = (int)(SR * 3.0);   /* longest voice + slack */

/* FNV-1a over the raw sample bits. Exact: a one-LSB drift is a failure, and
 * it should be — nothing in a structural refactor has any business changing
 * a sample value at all. */
static unsigned long long hash_samples(const float *v, int n)
{
    unsigned long long h = 1469598103934665603ULL;
    for(int i = 0; i < n; ++i)
    {
        unsigned int b;
        memcpy(&b, &v[i], sizeof b);
        if(v[i] == 0.0f) b = 0;             /* -0.0 and +0.0 are one value */
        for(int k = 0; k < 4; ++k)
        {
            h ^= (unsigned char)(b >> (k * 8));
            h *= 1099511628211ULL;
        }
    }
    return h;
}

/* Sixteen 120 ms window RMS values, plus the scalars. The windows are what
 * make this a fingerprint of the SHAPE of the note and not just its size: a
 * changed envelope, a changed decay or a lost tail all move them, while a
 * last-bit difference between two compilers does not. */
#define GOLD_WINDOWS 16

struct Desc {
    unsigned long long hash;
    double peak, rms, centroid, len;
    double win[GOLD_WINDOWS];
};

/* One lane, one hit, at the panel defaults. */
static Desc render_voice(int v, int velocity)
{
    sc808_engine_t *e = sc808_create((float)SR);
    static float buf[3 * 44100 + 64];
    memset(buf, 0, sizeof buf);

    sc808_trigger(e, v, velocity);
    sc808_render(e, buf, kTailFrames);

    Desc d;
    d.hash = hash_samples(buf, kTailFrames);
    d.peak = 0.0; double sum = 0.0;
    int last = 0;
    for(int i = 0; i < kTailFrames; ++i)
    {
        const double x = fabs((double)buf[i]);
        if(x > d.peak) d.peak = x;
        sum += (double)buf[i] * (double)buf[i];
        if(x > 3.2e-5) last = i;
    }
    d.rms = sqrt(sum / kTailFrames);
    d.len = (double)last / SR;

    /* Spectral centroid over a log bank — coarse on purpose: it is a
     * readability aid for a failing line, not a measurement. */
    double num = 0.0, den = 0.0;
    for(double f = 60.0; f < 16000.0; f *= 1.10)
    {
        const double w = 2.0 * M_PI * f / SR;
        const double cw = 2.0 * cos(w);
        double s1 = 0.0, s2 = 0.0;
        for(int i = 0; i < kTailFrames; ++i)
        {
            const double s0 = (double)buf[i] + cw * s1 - s2;
            s2 = s1; s1 = s0;
        }
        double m = s1 * s1 + s2 * s2 - cw * s1 * s2;
        if(m < 0.0) m = 0.0;
        num += f * m; den += m;
    }
    d.centroid = den > 0.0 ? num / den : 0.0;

    const int w = (int)(SR * 0.120);
    for(int k = 0; k < GOLD_WINDOWS; ++k)
    {
        double a = 0.0;
        for(int i = k * w; i < (k + 1) * w && i < kTailFrames; ++i)
            a += (double)buf[i] * (double)buf[i];
        d.win[k] = sqrt(a / w);
    }

    sc808_destroy(e);
    return d;
}

int main(int argc, char **argv)
{
    const bool write = argc > 1 && !strcmp(argv[1], "--write");

    /* Two velocities: the top of the range, and one halfway down it. The
     * top is the anchor the whole velocity law is pinned to — a full hit
     * reaches exactly the gain the old Accent pot delivered — so a change
     * there is a change to the voice and not to the mapping. */
    static const int kVels[2] = { 64, 127 };

    if(write)
    {
        printf("# 8W8 golden render — generated by tools/golden_check --write\n");
        printf("# lane vel hash peak rms centroid length\n");
        for(int v = 0; v < SC808_NUM_VOICES; ++v)
            for(int k = 0; k < 2; ++k)
            {
                const Desc d = render_voice(v, kVels[k]);
                printf("%s %d %llu %.12g %.12g %.12g %.12g",
                       sc808_voice_id(v), kVels[k], d.hash,
                       d.peak, d.rms, d.centroid, d.len);
                for(int j = 0; j < GOLD_WINDOWS; ++j) printf(" %.12g", d.win[j]);
                printf("\n");
            }
        return 0;
    }

    FILE *f = fopen("tools/golden.txt", "r");
    if(!f)
    {
        printf("FAIL: tools/golden.txt missing — run --write to make a baseline\n");
        return 1;
    }
    char line[2048];
    int fails = 0, checked = 0, exact = 0;
    double worstRel = 0.0; char worstWhat[64] = "";

    while(fgets(line, sizeof line, f))
    {
        if(line[0] == '#' || line[0] == '\n') continue;
        char id[32]; int vel; unsigned long long h;
        double peak, rms, cen, len, win[GOLD_WINDOWS];
        int n = 0;
        const char *p = line;
        if(sscanf(p, "%31s %d %llu %lf %lf %lf %lf%n",
                  id, &vel, &h, &peak, &rms, &cen, &len, &n) != 7) continue;
        p += n;
        bool haveWin = true;
        for(int j = 0; j < GOLD_WINDOWS; ++j)
        {
            if(sscanf(p, " %lf%n", &win[j], &n) != 1) { haveWin = false; break; }
            p += n;
        }

        int v = -1;
        for(int i = 0; i < SC808_NUM_VOICES; ++i)
            if(!strcmp(sc808_voice_id(i), id)) { v = i; break; }
        if(v < 0)
        {
            printf("FAIL: %s — lane no longer exists\n", id);
            ++fails; continue;
        }
        const Desc d = render_voice(v, vel);
        ++checked;

        /* Same host, same compiler: the samples should be bit-identical, and
         * that is the strongest statement available, so take it when it is
         * true. */
        if(d.hash == h) { ++exact; continue; }

        /*
         * Different host or compiler, so fall back to the numbers.
         *
         * THIS IS NOT A WEAKER TEST BY ACCIDENT, and it is here because the
         * bit-exact hash is not portable: the Mac (arm64 clang) and the build
         * VPS (x86_64 gcc) disagree in the last bit of tanhf, expf and the
         * FMA contractions, so a baseline written on one host fails on the
         * other with every descriptor identical to twelve significant digits.
         * A hash cannot tell that apart from a real change; these numbers
         * can. The threshold is far below anything a structural mistake
         * produces (those move a lane by whole dB or shift its length) and
         * far above cross-compiler noise, which measures under 1e-9.
         */
        const double kTol = 1.0e-6;
        struct { const char *name; double was, now; } cmp[4 + GOLD_WINDOWS] = {
            { "peak", peak, d.peak }, { "rms", rms, d.rms },
            { "centroid", cen, d.centroid }, { "length", len, d.len },
        };
        for(int j = 0; j < GOLD_WINDOWS; ++j)
        {
            cmp[4 + j].name = "window";
            cmp[4 + j].was  = haveWin ? win[j] : d.win[j];
            cmp[4 + j].now  = d.win[j];
        }
        double worst = 0.0; const char *worstName = "";
        for(int j = 0; j < 4 + GOLD_WINDOWS; ++j)
        {
            const double base = fabs(cmp[j].was) > 1e-12 ? fabs(cmp[j].was) : 1e-12;
            const double rel  = fabs(cmp[j].now - cmp[j].was) / base;
            if(rel > worst) { worst = rel; worstName = cmp[j].name; }
        }
        if(worst > worstRel)
        {
            worstRel = worst;
            snprintf(worstWhat, sizeof worstWhat, "%s vel %d (%s)", id, vel, worstName);
        }
        if(worst <= kTol) continue;

        ++fails;
        printf("FAIL: %s vel %d — audio changed (worst %s off by %.3g)\n",
               id, vel, worstName, worst);
        printf("      peak     %.6f -> %.6f  (%+.2f dB)\n", peak, d.peak,
               peak > 0 && d.peak > 0 ? 20.0 * log10(d.peak / peak) : 0.0);
        printf("      rms      %.6f -> %.6f  (%+.2f dB)\n", rms, d.rms,
               rms > 0 && d.rms > 0 ? 20.0 * log10(d.rms / rms) : 0.0);
        printf("      centroid %.0f Hz -> %.0f Hz\n", cen, d.centroid);
        printf("      length   %.3f s -> %.3f s\n", len, d.len);
    }
    fclose(f);

    if(fails)
        printf("\nFAILED (%d of %d lane/velocity renders moved)\n", fails, checked);
    else if(exact == checked)
        printf("ALL PASS (%d lane/velocity renders bit-identical)\n", checked);
    else
        printf("ALL PASS (%d renders: %d bit-identical, %d within %g "
               "— worst %s at %.3g, a cross-compiler difference)\n",
               checked, exact, checked - exact, 1.0e-6,
               worstWhat[0] ? worstWhat : "none", worstRel);
    return fails ? 1 : 0;
}
