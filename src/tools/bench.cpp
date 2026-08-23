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
 * Reports the realtime factor: how many times faster than realtime each case
 * renders. A factor of 1.0 means the Move is exactly saturated, so anything
 * under about 20x for the full kit is a problem — the module shares the
 * device with everything else in the chain.
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

/* Render `SECS` of audio, retriggering `voice` (or all of them, if < 0) every
 * 16th note at 140 BPM — a busy pattern, not a single decaying hit, because a
 * voice that has gone quiet costs nothing and would flatter the result. */
static double measure(int voice, const char *label)
{
    sc808_engine_t *e = sc808_create((float)SR);
    const int total  = (int)(SR * SECS);
    const int period = (int)(SR * 60.0 / 140.0 / 4.0);

    const double t0 = now_seconds();
    int next = 0;
    for(int i = 0; i < total; i += BLOCK)
    {
        if(i >= next)
        {
            if(voice >= 0) sc808_trigger(e, voice, 127);
            else for(int v = 0; v < SC808_NUM_VOICES; ++v)
                     sc808_trigger(e, v, 127);
            next += period;
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
    const double all = measure(-1, "all 15, every 16th");
    printf("\nworst single lane %.1fx, full kit %.1fx realtime\n", worst, all);
    if(all < 20.0)
        printf("*** full kit under 20x — this needs work before it ships ***\n");
    return 0;
}
