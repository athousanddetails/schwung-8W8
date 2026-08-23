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

extern "C" const float *sc808_debug_trim(void);

static const double SR      = 44100.0;
static const int    FRAMES  = (int)(44100 * 3);
static const int    WINDOW  = (int)(44100 * 0.020);   /* 20 ms */

static float g_buf[FRAMES];

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
 * The loudest thing a real pattern does, and what the kit's absolute level is
 * set from: a downbeat with kick, snare, clap and a closed hat landing
 * together on an accent.
 *
 * Target -3 dBFS, not -1. A single simultaneous hit is not the whole story —
 * a real pattern also has tails from the previous bar under it, and fitting
 * this scenario to -1 dBFS left src/tools/render.cpp's two-bar pattern
 * clipping at +0.7. The extra 2 dB is that overlap.
 *
 * "All fifteen at once" is easy to measure and irrelevant — no pattern does
 * it — and solo-voice peaks say nothing about a mix. Fitting the balance
 * without also fitting this is how you end up with a kit whose every lane is
 * beautifully in proportion and which clips on the first bar.
 */
#define HEADROOM_TARGET 0.708

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
        if(v != REF_VOICE && fabs(err) > worst) worst = fabs(err);
    }
    printf("worst lane off its target by %.2f dB\n", worst);

    /* The single scale that would put the downbeat at its target. The fitter
     * multiplies every suggested trim by this, so balance and absolute level
     * converge together instead of fighting each other. */
    {
        static const int downbeat[8] =
            { SC808_BD, SC808_SD, SC808_CP, SC808_CH, -1 };
        const double p = scenario_peak(downbeat);
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
