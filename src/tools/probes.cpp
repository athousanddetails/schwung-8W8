/*
 * probes.cpp — the C++ half of the primitive-level null test.
 *
 * One probe per UGen, matching test/probes.scd line for line. When a whole
 * voice fails to null, this says which primitive to look at instead of
 * leaving a six-filter chain to be bisected by hand.
 *
 * Every probe renders one CONSTRUCTION SAMPLE first and throws it away — see
 * the note on SC's per-note construction step in sc_ugens.h. SC builds a
 * synth per note and every UGen Ctor runs one sample through the graph before
 * the first real block, so a comparison that skips it is comparing two
 * different things. The engine itself never does this: the flag and its
 * branch are compiled out unless SC808_NULLTEST is defined, which only these
 * test tools do.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "sc_ugens.h"

using namespace sc;

/* Storage behind the harness-only flag declared in sc_ugens.h. */
namespace sc { bool g_scPriming = false; }

static const double SR     = 44100.0;
static const int    FRAMES = (int)(44100 * 0.5);

static float g_buf[FRAMES + 8];

static void put32(FILE *f, const uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, const uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav(const char *dir, const char *name, const float *d, const int n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if(!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    const uint32_t bytes = (uint32_t)n * 4u;
    fwrite("RIFF", 1, 4, f); put32(f, 36u + bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16u);
    put16(f, 3); put16(f, 1);
    put32(f, (uint32_t)SR); put32(f, (uint32_t)SR * 4u);
    put16(f, 4); put16(f, 32);
    fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(d, 4, (size_t)n, f);
    fclose(f);
}

/* One probe: SC's construction sample, discarded, then the real ones. */
template <class Gen>
static void probe(const char *dir, const char *name, Gen gen)
{
    g_scPriming = true;
    gen();
    g_scPriming = false;
    for(int i = 0; i < FRAMES; ++i) g_buf[i] = gen();
    write_wav(dir, name, g_buf, FRAMES);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";

    /* ---- oscillators ---- */
    {   LFPulse o; o.reset(0.0);
        probe(dir, "pulse", [&] { return o.process(203.52, 0.5, SR); }); }

    {   LFPulse o; o.reset(0.0);
        probe(dir, "pulse_duty", [&] { return o.process(512.8, 0.8, SR); }); }

    {   LFTri o; o.reset(1.0);
        probe(dir, "tri", [&] { return o.process(1827.4, SR); }); }

    {   SinOsc o; o.set(440.0, SR); o.reset(kPi * 0.5);
        probe(dir, "sine", [&] { return o.process(); }); }

    {   static const double f[6] = { 203.52, 366.31, 301.77, 518.19, 811.16, 538.75 };
        LFPulse o[6];
        for(int k = 0; k < 6; ++k) o[k].reset(0.0);
        probe(dir, "bank", [&] {
            float s = 0.0f;
            for(int k = 0; k < 6; ++k) s += o[k].process(f[k], 0.5, SR);
            return s; }); }

    /* ---- envelopes, as they reach the audio path ---- */
    {   Env e; envPerc(e, 0.005f, 0.42f, 1.0f, -30.0f, SR);
        probe(dir, "env_perc", [&] { return e.next(); }); }

    {   Env e;
        const EnvSeg s[2] = { { 1.0f, 0.0f, -7.0f }, { 0.0f, 0.3f, -7.0f } };
        e.set(0.11f, s, 2, SR);
        probe(dir, "env_jump", [&] { return e.next(); }); }

    {   Env e;
        const EnvSeg s[2] = { { 1.0f, 0.027f, -250.0f }, { 0.0f, 0.07f, -250.0f } };
        e.set(0.3f, s, 2, SR);
        probe(dir, "env_lin", [&] { return e.next(); }); }

    /* ---- a control-rate frequency driving an oscillator ----
     * The one place where the construction sample is audible rather than a
     * one-sample curiosity: it advances the phase at the envelope's INITIAL
     * frequency (470 Hz) while the first real block runs at the value after
     * one control step (334 Hz). Everything else about a swept sine can be
     * right and it will still fail by 30 dB without this. */
    {   Env e;
        const EnvSeg sg[2] = { { 63.5f, 0.05f, -14.0f }, { 47.0f, 0.6f, -14.0f } };
        e.set(470.0f, sg, 2, SR);
        SinOsc o; o.reset(kPi * 0.5);
        probe(dir, "sweep_sin", [&] { return o.process((double)e.nextHeld(), SR); }); }

    {   Env e;
        const EnvSeg sg[2] = { { 63.5f, 0.05f, -14.0f }, { 47.0f, 0.6f, -14.0f } };
        e.set(470.0f, sg, 2, SR);
        LFTri o; o.reset(kPi * 0.5);
        probe(dir, "sweep_tri", [&] { return o.process((double)e.nextHeld(), SR); }); }

    /* ---- filters, excited by a deterministic source ---- */
#define PROBE_FILTER(NAME, DECL, SETUP)                                       \
    {   LFPulse o; o.reset(0.0); DECL; SETUP;                                 \
        probe(dir, NAME, [&] { return f.process(o.process(203.52, 0.5, SR)); }); }

    PROBE_FILTER("lpf", LPF f, f.set(3000.0, SR))
    PROBE_FILTER("hpf", HPF f, f.set(8998.0, SR))
    PROBE_FILTER("bpf", BPF f, f.set(8898.0, 1.0, SR))

    PROBE_FILTER("bpeakeq",   Biquad f, f.setPeakEQ(9700.0, 0.8, 0.7, SR))
    PROBE_FILTER("bbandpass", Biquad f, f.setBandPass(8900.0, 0.8, SR))
    PROBE_FILTER("bhipass",   Biquad f, f.setHiPass(9000.0, 0.3, SR))
    PROBE_FILTER("blowshelf", Biquad f, f.setLowShelf(990.0, 2.0, -3.0, SR))
    PROBE_FILTER("bhishelf",  Biquad f, f.setHiShelf(9400.0, 1.0, 5.0, SR))
    PROBE_FILTER("bhipass4",  HiPass4 f, f.set(8100.0, 0.7, SR))
#undef PROBE_FILTER

    /* ---- limiter ---- */
    {   SinOsc o; o.set(220.0, SR); o.reset(kPi * 0.5);
        Limiter l; l.set(0.01f, SR);
        probe(dir, "limiter", [&] { return l.process(o.process() * 3.0f, 0.5f); }); }

    printf("probes written to %s\n", dir);
    return 0;
}
