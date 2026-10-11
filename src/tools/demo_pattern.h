/*
 * demo_pattern.h — two bars of 8W8, as data.
 *
 * Shared by src/tools/render.cpp, which writes it out so the kit can be
 * heard, and tools/kit_check.cpp, which fits the kit's absolute level to it.
 *
 * Those two wanting the same pattern is not a convenience. The level has to
 * be fitted to what a pattern ACTUALLY peaks at, and every cheaper proxy for
 * that has been wrong in turn: a four-voice downbeat left this pattern
 * clipping at +0.7 dBFS, and a six-voice full-velocity hit — which no pattern ever
 * produces — left it 8.6 dB quieter than it needed to be. Fit the thing you
 * are going to listen to.
 *
 * Deliberately plain: a normal 808 pattern, not a showcase. A pattern that
 * avoided the awkward simultaneous hits would not answer the question.
 *
 * GPL-3.0.
 */
#ifndef W8_DEMO_PATTERN_H
#define W8_DEMO_PATTERN_H

#include "w8_engine.h"

typedef struct { int step, voice, vel; } w8_hit_t;

/* 32 sixteenths — two bars at 120 BPM. Velocity is a straight line with no
 * threshold, so these are simply loud hits and quieter ones; 127 is the top
 * of the range. */
static const w8_hit_t kDemoHits[] = {
    /* bar 1 */
    { 0, W8_BD, 127}, { 4, W8_SD, 110}, { 6, W8_BD,  90},
    { 8, W8_BD, 100}, {12, W8_SD, 110}, {14, W8_CP, 110},
    { 0, W8_CH,  90}, { 2, W8_CH,  80}, { 4, W8_CH,  90},
    { 6, W8_CH,  80}, { 8, W8_CH,  90}, {10, W8_CH,  80},
    {12, W8_CH,  90}, {14, W8_OH, 100},
    { 3, W8_RS,  90}, {11, W8_CB,  90}, { 7, W8_CL,  85},
    /* bar 2 */
    {16, W8_BD, 127}, {20, W8_SD, 110}, {22, W8_BD,  90},
    {24, W8_BD, 100}, {28, W8_SD, 110}, {30, W8_CP, 110},
    {16, W8_CH,  90}, {18, W8_CH,  80}, {20, W8_CH,  90},
    {22, W8_CH,  80}, {24, W8_CH,  90}, {26, W8_CH,  80},
    {28, W8_CY, 100},
    {25, W8_LT,  95}, {26, W8_MT,  95}, {27, W8_HT,  95},
    {29, W8_LC,  90}, {30, W8_MC,  90}, {31, W8_HC,  90},
    {19, W8_MA,  85}, {23, W8_MA,  85},
};

#define W8_DEMO_STEPS 32
#define W8_DEMO_BPM   120.0
#define W8_DEMO_HITS  ((int)(sizeof(kDemoHits) / sizeof(kDemoHits[0])))

/*
 * Render the pattern into `out`, plus `tailSeconds` of ring-out. Returns the
 * number of frames written, or 0 if the buffer is too small.
 */
static inline int w8_render_demo(w8_engine_t *e, float *out,
                                    const int capacity, const double sampleRate,
                                    const double tailSeconds)
{
    const double step = 60.0 / W8_DEMO_BPM / 4.0;
    const int n = (int)(sampleRate * step);
    const int total = n * W8_DEMO_STEPS + (int)(sampleRate * tailSeconds);
    if(total > capacity) return 0;

    int done = 0;
    for(int s = 0; s < W8_DEMO_STEPS; ++s)
    {
        for(int k = 0; k < W8_DEMO_HITS; ++k)
            if(kDemoHits[k].step == s)
                w8_trigger(e, kDemoHits[k].voice, kDemoHits[k].vel);
        w8_render(e, out + done, n);
        done += n;
    }
    if(total > done) { w8_render(e, out + done, total - done); done = total; }
    return done;
}

#endif /* W8_DEMO_PATTERN_H */
