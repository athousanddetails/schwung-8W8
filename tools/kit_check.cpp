/*
 * kit_check.cpp — measure the kit balance, and re-derive kVoiceTrim.
 *
 * Each sc808 SynthDef was written to be played on its own with its own `amp`,
 * so the voices arrive at wildly different levels: the closed hat peaks near
 * 17 while the hand clap peaks at 0.38. Summing fifteen of those is not a
 * drum machine. sc808_engine.cpp carries a per-lane trim table to put them in
 * proportion, and this is what produces it.
 *
 * Balance is measured as the LOUDEST 20 ms of the note: a sliding-window RMS,
 * maximum taken. Two wrong metrics were tried first and both are instructive.
 *
 * Peak is wrong: the closed hat's raw peak is 17, a single-sample filter
 * transient at the note onset, and trimming the lane by 1/17 on the strength
 * of it makes the hat inaudible.
 *
 * RMS over a fixed window is also wrong, and more subtly: a 70 ms rim shot
 * measured over 150 ms is half silence, so it reads 3 dB quiet purely because
 * it is SHORT. Fitting to that asked for a trim of 16.7x on a voice with a
 * crest factor of 11, which drove it so far into the diode stage that it came
 * out a square wave. Note length is not loudness.
 *
 * A sliding 20 ms window is immune to both: it finds the impact wherever it
 * is and does not care what follows it.
 *
 * Run it, paste the suggested trims into kVoiceTrim, run it again — the
 * second run should report every lane at its target.
 *
 *   ./build-native/kit_check
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_engine.h"
#include "../src/tools/demo_pattern.h"

extern "C" const float *sc808_debug_trim(void);

static const double SR      = 44100.0;
static const int    FRAMES  = (int)(44100 * 3);
static const int    WINDOW  = (int)(44100 * 0.020);   /* 20 ms */

static float g_buf[FRAMES];

/* The demo pattern is two bars plus a tail — about 7 s, more than the 3 s
 * single-voice buffer above. sc808_render_demo returns 0 rather than
 * overrunning, which is safe and silent: an undersized buffer here made the
 * pattern measure as -240 dBFS and the level fit quietly do nothing at all. */
#define PATTERN_FRAMES ((int)(44100 * 10))
static float g_pattern[PATTERN_FRAMES];

/* The lane every other lane is balanced against. A drum machine is built
 * around its kick; everything else is set relative to it. */
#define REF_VOICE SC808_BD

/*
 * Where each lane should sit relative to the kick, in dB.
 *
 * This is the one table in the project that is taste rather than measurement,
 * and it is written down rather than smuggled into the trim numbers so that
 * disagreeing with it is a one-line edit. Balancing every lane to EQUAL
 * loudness is the defensible-sounding choice and it is wrong: a kit whose
 * hi-hat is as loud as its kick is not a kit anybody would use.
 *
 * These are roughly where an 808's own faders end up on a normal pattern.
 * The Level pots start at 64 and go both ways from here.
 */
static const double kVoicing[SC808_NUM_VOICES] = {
      0.0,   /* bd — the reference */
     -2.0,   /* sd */
     -4.0,   /* lt */
     -4.0,   /* mt */
     -4.0,   /* ht */
     -5.0,   /* lc */
     -5.0,   /* mc */
     -5.0,   /* hc */
     -6.0,   /* rs */
    -10.0,   /* ma */
     -3.0,   /* cp — the clap carries a backbeat, it needs to be heard */
     -7.0,   /* cb */
    -10.0,   /* ch */
     -8.0,   /* oh */
     -9.0,   /* cy */
};

/*
 * What the kit's absolute level is fitted to: the two-bar demo pattern from
 * demo_pattern.h, targeted at -1 dBFS.
 *
 * Two cheaper proxies were tried first and both were wrong. A four-voice
 * downbeat left the pattern CLIPPING at +0.7 dBFS, because a real pattern
 * also carries the previous bar's tails underneath it. A six-voice accented
 * hit went the other way and left the pattern 8.6 dB quieter than it needed
 * to be, because no pattern ever fires six accented voices on one sixteenth.
 *
 * The scenarios below are still measured and printed, and one of them will
 * be over full scale: `busy` fires six accented voices on a single sixteenth
 * and sits about 6 dB above the pattern, so fitting the pattern to -1 dBFS
 * means that hit clips. That is a deliberate trade and the same one 6W6
 * makes — it fits a dense pattern to -1.1 dBFS. Fitting `busy` under the
 * ceiling instead costs 7 dB on everything anyone actually plays, to protect
 * a hit that is rare and that the Volume pot already answers.
 *
 * "All fifteen at once" is easy to measure and irrelevant — no pattern does
 * it — and solo-voice peaks say nothing about a mix. Fitting the balance
 * without also fitting this is how you end up with a kit whose every lane is
 * beautifully in proportion and which clips on the first bar.
 */
#define HEADROOM_TARGET 0.891

/*
 * Where each lane's energy should sit, in Hz.
 *
 * This table exists because of a bug that reached the user's ears. bd_tune
 * was declared as an absolute MIDI note while the engine treats every Tune
 * pot as a semitone OFFSET, so the kick rendered at midicps(34+34) = 415 Hz
 * and the snare shell at 9.4 kHz. Everything still built, every test passed,
 * every pad sounded — the loadtest only asks whether a lane makes a noise —
 * and the kit was simply wrong.
 *
 * These bands are deliberately WIDE. The point is not to pin a voice's
 * timbre; it is to catch a drum that has moved to the wrong end of the
 * spectrum, which is the failure that actually happened and the one no other
 * check in this project would notice.
 */
struct Band { double lo, hi; const char *what; };
static const Band kBand[SC808_NUM_VOICES] = {
    {   35.0,    90.0, "kick fundamental"          },
    {  140.0,  3000.0, "snare shell plus wires"    },
    {   60.0,   200.0, "low tom"                   },
    {   70.0,   260.0, "mid tom"                   },
    {  100.0,   400.0, "hi tom"                    },
    {   90.0,   340.0, "low conga"                 },
    {  120.0,   450.0, "mid conga"                 },
    {  150.0,   600.0, "hi conga"                  },
    {  300.0,  4000.0, "rim shot"                  },
    { 3000.0, 16000.0, "maracas"                   },
    {  300.0,  3000.0, "hand clap"                 },
    {  400.0,  3000.0, "cowbell"                   },
    { 4000.0, 16000.0, "closed hat"                },
    { 3000.0, 16000.0, "open hat"                  },
    { 2000.0, 12000.0, "cymbal"                    },
};

struct Measure { double peak, impact_rms, drive_in_peak; };

static Measure render_one(int voice)
{
    sc808_engine_t *e = sc808_create((float)SR);

    /* Take the master stage out of the measurement: unity volume, no master
     * distortion. The per-voice Drive stage stays in, because it is on by
     * default and it is part of what the lane actually sounds like. */
    sc808_set_param(e, "volume", "127");
    sc808_set_param(e, "master_dist", "0");

    /* Velocity 80: below SC808_ACCENT_VELOCITY, so no accent multiplier. */
    sc808_trigger(e, voice, 80);
    sc808_render(e, g_buf, FRAMES);
    sc808_destroy(e);

    Measure m = { 0.0, 0.0, 0.0 };
    for(int i = 0; i < FRAMES; ++i)
    {
        const double a = fabs((double)g_buf[i]);
        if(a > m.peak) m.peak = a;
    }

    /* Loudest 20 ms, by a running sum rather than a rescan per position. */
    double acc = 0.0, best = 0.0;
    for(int i = 0; i < FRAMES; ++i)
    {
        acc += (double)g_buf[i] * (double)g_buf[i];
        if(i >= WINDOW)
            acc -= (double)g_buf[i - WINDOW] * (double)g_buf[i - WINDOW];
        if(i >= WINDOW && acc > best) best = acc;
    }
    m.impact_rms = sqrt(best / WINDOW);
    return m;
}

static double scenario_peak(const int *voices)
{
    sc808_engine_t *e = sc808_create((float)SR);
    for(int i = 0; i < 8 && voices[i] >= 0; ++i)
        sc808_trigger(e, voices[i], 127);          /* accented */
    sc808_render(e, g_buf, FRAMES);
    double peak = 0.0;
    for(int i = 0; i < FRAMES; ++i)
    { const double a = fabs((double)g_buf[i]); if(a > peak) peak = a; }
    sc808_destroy(e);
    return peak;
}

/*
 * Spectral centroid of a lane's first 200 ms, by a coarse filterbank rather
 * than an FFT — this file has no FFT and does not need one for a check whose
 * bands are an octave wide. One-pole bandpass energy per 1/3-octave bin.
 */
static double centroid(const int n)
{
    double num = 0.0, den = 0.0;
    const int win = (int)(SR * 0.2) < n ? (int)(SR * 0.2) : n;
    for(double f = 30.0; f < 18000.0; f *= 1.2599)      /* 1/3 octave */
    {
        /* two-pole resonator, Q ~ 4 */
        const double w = 2.0 * M_PI * f / SR;
        const double r = 1.0 - w / 8.0;
        const double a1 = 2.0 * r * cos(w), a2 = -r * r;
        double y1 = 0.0, y2 = 0.0, e = 0.0;
        for(int i = 0; i < win; ++i)
        {
            const double y = (double)g_buf[i] + a1 * y1 + a2 * y2;
            y2 = y1; y1 = y;
            e += y * y;
        }
        e *= (1.0 - r) * (1.0 - r);      /* normalise the resonator's gain */
        num += e * f;
        den += e;
    }
    return den > 0.0 ? num / den : 0.0;
}

int main(void)
{
    const float *trim = sc808_debug_trim();

    Measure m[SC808_NUM_VOICES];
    for(int v = 0; v < SC808_NUM_VOICES; ++v) m[v] = render_one(v);

    const double ref = m[REF_VOICE].impact_rms;

    printf("lane   peak    loud-20ms    rel dB  target   err   trim now   suggested\n");
    printf("--------------------------------------------------------------------------\n");
    double worst = 0.0;
    for(int v = 0; v < SC808_NUM_VOICES; ++v)
    {
        const double rel = 20.0 * log10(m[v].impact_rms / (ref > 0 ? ref : 1e-12));
        const double err = rel - kVoicing[v];
        /* The trim that would put this lane where kVoicing wants it, given
         * whatever trim it is already carrying. */
        const double want = m[v].impact_rms > 1e-9
                          ? (double)trim[v] * pow(10.0, kVoicing[v] / 20.0)
                            * ref / m[v].impact_rms
                          : 1.0;
        printf("%-4s %7.3f  %10.5f  %7.2f  %6.1f %6.2f  %8.3f  %10.3f\n",
               sc808_voice_id(v), m[v].peak, m[v].impact_rms, rel,
               kVoicing[v], err, trim[v], want);
        /* A line the fitter can read without parsing the table. The table is
         * for people and its columns move; this does not. */
        printf("trim_suggest %s %.6f\n", sc808_voice_id(v), want);
        if(v != REF_VOICE && fabs(err) > worst) worst = fabs(err);
    }
    printf("worst lane off its target by %.2f dB\n", worst);

    /* ---- every voice in a plausible band ---- */
    printf("\nspectral placement\n");
    {
        int wrong = 0;
        for(int v = 0; v < SC808_NUM_VOICES; ++v)
        {
            sc808_engine_t *e = sc808_create((float)SR);
            sc808_set_param(e, "volume", "127");
            sc808_set_param(e, "master_dist", "0");
            sc808_trigger(e, v, 80);
            sc808_render(e, g_buf, FRAMES);
            sc808_destroy(e);
            const double c = centroid(FRAMES);
            const int ok = c >= kBand[v].lo && c <= kBand[v].hi;
            if(!ok) ++wrong;
            printf("  %-4s %s centroid %8.1f Hz   expected %6.0f-%6.0f  (%s)\n",
                   sc808_voice_id(v), ok ? "ok  " : "FAIL", c,
                   kBand[v].lo, kBand[v].hi, kBand[v].what);
        }
        printf(wrong ? "  *** %d lane(s) in the wrong part of the spectrum ***\n"
                     : "  all %d lanes land where they should\n",
               wrong ? wrong : SC808_NUM_VOICES);
        if(wrong) return 1;
    }

    /* The single scale that would put the downbeat at its target. The fitter
     * multiplies every suggested trim by this, so balance and absolute level
     * converge together instead of fighting each other. */
    {
        sc808_engine_t *pe = sc808_create((float)SR);
        const int done = sc808_render_demo(pe, g_pattern, PATTERN_FRAMES, SR, 2.0);
        sc808_destroy(pe);
        if(done == 0)
        {
            printf("*** pattern did not render — buffer too small ***\n");
            return 1;
        }
        double p = 0.0;
        for(int i = 0; i < done; ++i)
        { const double a = fabs((double)g_pattern[i]); if(a > p) p = a; }
        printf("pattern peak %.3f (%+.1f dBFS)\n", p,
               20.0 * log10(p > 0 ? p : 1e-12));
        printf("headroom_scale %.6f\n",
               p > 1e-9 ? HEADROOM_TARGET / p : 1.0);
    }

    /* ---- headroom ----
     *
     * "All fifteen at once" is the number that is easy to measure and the one
     * that does not matter: no pattern ever does that. What sets the default
     * volume is the loudest thing a REAL pattern does, which is a downbeat —
     * kick, snare, clap and a hat landing together on an accent.
     */
    struct Scenario { const char *name; int voices[8]; };
    static const Scenario scen_defs[] = {
        { "kick alone, accented",        { SC808_BD, -1 } },
        { "downbeat: BD+SD+CP+CH",       { SC808_BD, SC808_SD, SC808_CP, SC808_CH, -1 } },
        { "busy: +OH+CB+RS",             { SC808_BD, SC808_SD, SC808_CP, SC808_OH,
                                           SC808_CB, SC808_RS, -1 } },
    };

    printf("---------------------------------------------------------------\n");
    for(size_t k = 0; k < sizeof(scen_defs) / sizeof(scen_defs[0]); ++k)
        printf("%-28s peak %.3f (%+.1f dBFS)\n", scen_defs[k].name,
               scenario_peak(scen_defs[k].voices),
               20.0 * log10(scenario_peak(scen_defs[k].voices) + 1e-12));

    /* The absolute worst case, for reference only. */
    sc808_engine_t *e = sc808_create((float)SR);
    for(int v = 0; v < SC808_NUM_VOICES; ++v) sc808_trigger(e, v, 127);
    sc808_render(e, g_buf, FRAMES);
    double peak = 0.0;
    for(int i = 0; i < FRAMES; ++i)
    { const double a = fabs((double)g_buf[i]); if(a > peak) peak = a; }
    sc808_destroy(e);
    printf("%-28s peak %.3f (%+.1f dBFS)   [never happens]\n",
           "all 15 at once", peak, 20.0 * log10(peak > 0 ? peak : 1e-12));
    return 0;
}
