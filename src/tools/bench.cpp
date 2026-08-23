/*
 * bench.cpp — the CPU gate. Cross-compiled and run ON the Move.
 *
 * 6W6's three metal voices summed 141 sines per sample and took 55% of the
 * device's block budget before they were rewritten. 8W8's arithmetic is
 * cheaper per voice — naive pulses and biquads rather than partial sums — but
 * there are fifteen lanes instead of eight and the cymbal alone runs eighteen
 * biquads through three parallel chains. That is worth measuring on the
 * hardware rather than assuming, which is what this is for.
 *
 * Reports the realtime factor and, more usefully, the share of one core. The
 * number to compare against is 6W6, which shipped at 22% of a core after its
 * metal voices were rewritten (they started at 55%). A module shares the
 * device with everything else in the chain, so a third of a core is the point
 * at which this needs work.
 *
 * MEASURED ON THE MOVE, 2026-08-23, first build:
 *   worst single lane   cymbal, 39.7x realtime (2.5% of a core)
 *   all 15 every 16th   5.6x realtime (18.0% of a core)
 *   busy pattern        see below
 * The "all 15" case is pathological — every voice retriggered 9.3 times a
 * second — and is here as a ceiling, not as a target.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "sc808_engine.h"

static const double SR     = 44100.0;
static const int    BLOCK  = 128;          /* Schwung's block */
static const double SECS   = 5.0;

static float g_buf[BLOCK];

static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/*
 * Render `SECS` of audio, retriggering on every 16th at 140 BPM — a busy
 * pattern, not a single decaying hit, because a voice that has gone quiet
 * costs nothing and would flatter the result.
 *
 * voice >= 0   that lane alone
 * voice == -1  all fifteen, every 16th (the ceiling)
 * voice == -2  a realistic pattern: hats on every 16th, kick and snare and
 *              clap on their beats. This is the one that matters.
 */
static double measure(int voice, const char *label)
{
    sc808_engine_t *e = sc808_create((float)SR);
    const int total  = (int)(SR * SECS);
    const int period = (int)(SR * 60.0 / 140.0 / 4.0);

    const double t0 = now_seconds();
    int next = 0, step = 0;
    for(int i = 0; i < total; i += BLOCK)
    {
        if(i >= next)
        {
            if(voice >= 0) sc808_trigger(e, voice, 127);
            else if(voice == -1)
                for(int v = 0; v < SC808_NUM_VOICES; ++v)
                    sc808_trigger(e, v, 127);
            else
            {
                const int s = step & 15;
                sc808_trigger(e, (s & 1) ? SC808_CH : SC808_CH, 90);
                if(s == 0 || s == 6 || s == 8)  sc808_trigger(e, SC808_BD, 120);
                if(s == 4 || s == 12)           sc808_trigger(e, SC808_SD, 110);
                if(s == 14)                     sc808_trigger(e, SC808_CP, 110);
                if(s == 7)                      sc808_trigger(e, SC808_OH, 100);
                if(s == 11)                     sc808_trigger(e, SC808_CB,  90);
            }
            next += period;
            ++step;
        }
        sc808_render(e, g_buf, BLOCK);
    }
    const double dt = now_seconds() - t0;
    sc808_destroy(e);

    const double rt = SECS / (dt > 0 ? dt : 1e-9);
    printf("%-22s %8.1fx realtime   %6.2f%% of one core\n",
           label, rt, 100.0 / rt);
    return rt;
}

int main(void)
{
    printf("8W8 bench — %g s of audio per case, %d-frame blocks\n\n",
           SECS, BLOCK);

    double worst = 1e30;
    for(int v = 0; v < SC808_NUM_VOICES; ++v)
    {
        char label[32];
        snprintf(label, sizeof(label), "%s only", sc808_voice_id(v));
        const double rt = measure(v, label);
        if(rt < worst) worst = rt;
    }

    printf("\n");
    const double busy = measure(-2, "busy pattern");
    const double all  = measure(-1, "all 15, every 16th");

    printf("\nworst single lane %.1fx (%.1f%% of a core)\n",
           worst, 100.0 / worst);
    printf("busy pattern      %.1fx (%.1f%% of a core)   <- the one that matters\n",
           busy, 100.0 / busy);
    printf("all 15 at once    %.1fx (%.1f%% of a core)   [pathological]\n",
           all, 100.0 / all);

    /* 6W6 ships at 22% of a core. A third is where this stops being fine. */
    if(100.0 / busy > 33.0)
        printf("\n*** busy pattern over a third of a core — this needs work ***\n");
    return 0;
}
