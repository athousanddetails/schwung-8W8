/*
 * render.cpp — the kit, offline, as WAVs.
 *
 * Renders every lane on its own and then a pattern, so 8W8 can be heard and
 * A/B'd against 808 recordings without a Move in the loop. This is the same
 * engine the module runs, through the same pot mapping — not a shortcut path.
 *
 *   sc808_render <outdir> [--pattern-only]
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_engine.h"

static const double SR = 44100.0;

static void put32(FILE *f, const uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, const uint16_t v) { fwrite(&v, 2, 1, f); }

/* 16-bit, because that is what the Move outputs — hearing the render is
 * hearing the device, quantisation included. */
static void write_wav(const char *dir, const char *name,
                      const float *d, const int n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if(!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    const uint32_t bytes = (uint32_t)n * 2u;
    fwrite("RIFF", 1, 4, f); put32(f, 36u + bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16u);
    put16(f, 1); put16(f, 1);
    put32(f, (uint32_t)SR); put32(f, (uint32_t)SR * 2u);
    put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, bytes);
    for(int i = 0; i < n; ++i)
    {
        float v = d[i];
        if(v >  1.0f) v =  1.0f;
        if(v < -1.0f) v = -1.0f;
        put16(f, (uint16_t)(int16_t)(v * 32767.0f));
    }
    fclose(f);
}

static float g_buf[(int)(44100 * 9)];

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    const int pattern_only = argc > 2 && !strcmp(argv[2], "--pattern-only");

    if(!pattern_only)
    {
        const int frames = (int)(SR * 3);
        for(int v = 0; v < SC808_NUM_VOICES; ++v)
        {
            sc808_engine_t *e = sc808_create((float)SR);
            sc808_trigger(e, v, 100);
            sc808_render(e, g_buf, frames);
            sc808_destroy(e);

            double peak = 0.0;
            for(int i = 0; i < frames; ++i)
            { const double a = fabs((double)g_buf[i]); if(a > peak) peak = a; }
            printf("%-3s peak %6.3f\n", sc808_voice_id(v), peak);
            write_wav(dir, sc808_voice_id(v), g_buf, frames);
        }
    }

    /*
     * Two bars at 120 BPM. Not a showcase — a plain 808 pattern, because what
     * this render is for is hearing whether the kit sits together, and a
     * pattern that avoids the awkward combinations would not answer that.
     * Accents are velocity 127; everything else is 90, below the accent
     * threshold.
     */
    {
        const double bpm = 120.0;
        const double step = 60.0 / bpm / 4.0;              /* a 16th */
        const int    steps = 32;
        const int    frames = (int)(SR * (step * steps + 3.0));

        /* step -> voice, velocity. 16 steps per bar. */
        struct Hit { int step, voice, vel; };
        static const Hit hits[] = {
            /* bar 1 */
            { 0, SC808_BD, 127}, { 4, SC808_SD, 110}, { 6, SC808_BD,  90},
            { 8, SC808_BD, 100}, {12, SC808_SD, 110}, {14, SC808_CP, 110},
            { 0, SC808_CH,  90}, { 2, SC808_CH,  80}, { 4, SC808_CH,  90},
            { 6, SC808_CH,  80}, { 8, SC808_CH,  90}, {10, SC808_CH,  80},
            {12, SC808_CH,  90}, {14, SC808_OH, 100},
            { 3, SC808_RS,  90}, {11, SC808_CB,  90},
            /* bar 2 */
            {16, SC808_BD, 127}, {20, SC808_SD, 110}, {22, SC808_BD,  90},
            {24, SC808_BD, 100}, {28, SC808_SD, 110}, {30, SC808_CP, 110},
            {16, SC808_CH,  90}, {18, SC808_CH,  80}, {20, SC808_CH,  90},
            {22, SC808_CH,  80}, {24, SC808_CH,  90}, {26, SC808_CH,  80},
            {28, SC808_CY, 100},
            {25, SC808_LT,  95}, {26, SC808_MT,  95}, {27, SC808_HT,  95},
            {29, SC808_LC,  90}, {30, SC808_MC,  90}, {31, SC808_HC,  90},
            {19, SC808_MA,  85}, {23, SC808_MA,  85},
        };

        sc808_engine_t *e = sc808_create((float)SR);
        int done = 0;
        for(int s = 0; s < steps; ++s)
        {
            for(size_t k = 0; k < sizeof(hits) / sizeof(hits[0]); ++k)
                if(hits[k].step == s) sc808_trigger(e, hits[k].voice, hits[k].vel);

            const int n = (int)(SR * step);
            if(done + n > frames) break;
            sc808_render(e, g_buf + done, n);
            done += n;
        }
        if(done < frames) { sc808_render(e, g_buf + done, frames - done); done = frames; }
        sc808_destroy(e);

        double peak = 0.0;
        for(int i = 0; i < done; ++i)
        { const double a = fabs((double)g_buf[i]); if(a > peak) peak = a; }
        printf("pattern peak %6.3f (%+.1f dBFS)%s\n", peak,
               20.0 * log10(peak > 0 ? peak : 1e-12),
               peak > 1.0 ? "   *** CLIPPING ***" : "");
        write_wav(dir, "pattern", g_buf, done);
    }
    return 0;
}
