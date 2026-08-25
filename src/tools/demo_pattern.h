/*
 * demo_pattern.h — two bars of 8W8, as data.
 *
 * Shared by src/tools/render.cpp, which writes it out so the kit can be
 * heard, and tools/kit_check.cpp, which fits the kit's absolute level to it.
 *
 * Those two wanting the same pattern is not a convenience. The level has to
 * be fitted to what a pattern ACTUALLY peaks at, and every cheaper proxy for
 * that has been wrong in turn: a four-voice downbeat left this pattern
 * clipping at +0.7 dBFS, and a six-voice accented hit — which no pattern ever
 * produces — left it 8.6 dB quieter than it needed to be. Fit the thing you
 * are going to listen to.
 *
 * Deliberately plain: a normal 808 pattern, not a showcase. A pattern that
 * avoided the awkward simultaneous hits would not answer the question.
 *
 * GPL-3.0.
 */
#ifndef SC808_DEMO_PATTERN_H
#define SC808_DEMO_PATTERN_H

#include "sc808_engine.h"

typedef struct { int step, voice, vel; } sc808_hit_t;

/* 32 sixteenths — two bars at 120 BPM. Accents are velocity 127; everything
 * else sits below SC808_ACCENT_VELOCITY. */
static const sc808_hit_t kDemoHits[] = {
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

#define SC808_DEMO_STEPS 32
#define SC808_DEMO_BPM   120.0
#define SC808_DEMO_HITS  ((int)(sizeof(kDemoHits) / sizeof(kDemoHits[0])))

/*
 * Render the pattern into `out`, plus `tailSeconds` of ring-out. Returns the
 * number of frames written, or 0 if the buffer is too small.
 */
static inline int sc808_render_demo(sc808_engine_t *e, float *out,
                                    const int capacity, const double sampleRate,
                                    const double tailSeconds)
{
    const double step = 60.0 / SC808_DEMO_BPM / 4.0;
    const int n = (int)(sampleRate * step);
    const int total = n * SC808_DEMO_STEPS + (int)(sampleRate * tailSeconds);
    if(total > capacity) return 0;

    int done = 0;
    for(int s = 0; s < SC808_DEMO_STEPS; ++s)
    {
        for(int k = 0; k < SC808_DEMO_HITS; ++k)
            if(kDemoHits[k].step == s)
                sc808_trigger(e, kDemoHits[k].voice, kDemoHits[k].vel);
        sc808_render(e, out + done, n);
        done += n;
    }
    if(total > done) { sc808_render(e, out + done, total - done); done = total; }
    return done;
}

#endif /* SC808_DEMO_PATTERN_H */
