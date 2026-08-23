/*
 * nullref.cpp — render every Engine A voice at sc808's OWN defaults.
 *
 * This is the other half of the null test. test/nrt_render.scd renders the
 * SynthDefs in scsynth; this renders the same voices from sc808_voices.h with
 * the same arguments the SynthDefs declare, so the two sets of WAVs should
 * agree sample for sample.
 *
 * It deliberately does NOT go through sc808_engine: no pots, no per-voice
 * trim, no drive stage, no accent. Those are 8W8's additions and would make
 * a difference that means nothing. The point here is the transcription only.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_voices.h"

using namespace sc808;

/* Storage behind the harness-only flag declared in sc_ugens.h. See the note
 * there: SC constructs a synth per note and runs one sample through the whole
 * graph at construction time. Reproducing that here — and nowhere in the
 * engine — is what lets a persistent voice be compared with a per-note one. */
namespace sc { bool g_scPriming = false; }

static const double SR      = 44100.0;
static const double SECONDS = 3.0;

/* ---- minimal float32 mono WAV writer ---------------------------------- */

static void put32(FILE *f, const uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, const uint16_t v) { fwrite(&v, 2, 1, f); }

static int write_wav(const char *path, const float *data, const int frames)
{
    FILE *f = fopen(path, "wb");
    if(!f) { fprintf(stderr, "cannot write %s\n", path); return 0; }
    const uint32_t bytes = (uint32_t)frames * 4u;
    fwrite("RIFF", 1, 4, f); put32(f, 36u + bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16u);
    put16(f, 3);                       /* IEEE float */
    put16(f, 1);                       /* mono */
    put32(f, (uint32_t)SR);
    put32(f, (uint32_t)SR * 4u);
    put16(f, 4); put16(f, 32);
    fwrite("data", 1, 4, f); put32(f, bytes);
    fwrite(data, 4, (size_t)frames, f);
    fclose(f);
    return 1;
}

/* ---- render one voice ------------------------------------------------- */

static float g_buf[(int)(44100 * 3) + 8];

template <class V>
static void render(const char *dir, const char *name, V &v, const int frames)
{
    /* SC's construction sample: every UGen in the voice advances once with
     * its inputs at their initial values. Discarded, exactly as SC discards
     * it by overwriting out[0] on the first real block. */
    sc::g_scPriming = true;
    v.process();
    sc::g_scPriming = false;

    for(int i = 0; i < frames; ++i) g_buf[i] = v.process();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    write_wav(path, g_buf, frames);

    double peak = 0.0;
    for(int i = 0; i < frames; ++i)
    { const double a = fabs((double)g_buf[i]); if(a > peak) peak = a; }
    printf("%-14s peak %9.5f\n", name, peak);
}

/*
 * A second take of a noise voice, from the SAME voice object with its RNG
 * already advanced past the first take: identical code, identical parameters,
 * a different draw of noise.
 *
 * This is the control for the noise voices. They cannot null against scsynth
 * because their randomness comes from a different generator, so the question
 * "is 5 dB of band error a bug?" has no answer until you know what two
 * innocent draws of the same voice score. Whatever this control produces is
 * the metric's own floor; a real transcription error has to beat it to be
 * visible.
 */
template <class V>
static void control(const char *dir, const char *name, V &v, const int frames)
{
    if(!dir) return;
    sc::g_scPriming = true;  v.process();  sc::g_scPriming = false;
    for(int i = 0; i < frames; ++i) g_buf[i] = v.process();
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
    write_wav(path, g_buf, frames);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    /* Optional: a directory for the noise-voice control takes. */
    const char *ctrl = argc > 2 ? argv[2] : NULL;
    const int frames = (int)(SR * SECONDS);
    const double sr = SR;

    /* Every argument below is sc808's declared default for that SynthDef.
     * Anything that looks like a magic number is quoted from the .scd. */

    { BassDrum v; v.init(sr);
      v.trigger(midicps(34.0f), 0.11f, 2.0f, 2.0f);
      render(dir, "bassdrum", v, frames);
      printf("%-14s limiter latency %ld samples\n", "", v.latency()); }

    { Snare v; v.init(sr);
      /* note 65, detune -11, hpf 93, lpf 121, mix 0.7, decay 4.2, click 0.999 */
      v.trigger(midicps(65.0f), midicps(65.0f - 11.0f), 4.2f, 0.7f,
                midicps(93.0f), midicps(121.0f), 0.999f);
      render(dir, "snare", v, frames);
      v.trigger(midicps(65.0f), midicps(65.0f - 11.0f), 4.2f, 0.7f,
                midicps(93.0f), midicps(121.0f), 0.999f);
      control(ctrl, "snare", v, frames); }

    { Clap v; v.init(sr);
      /* hpf 71, lpf 84 (used as the BPF centre), click 0.5, decay 0.3 */
      v.trigger(midicps(71.0f), midicps(84.0f), 0.5f, 0.3f, 0.026f, 1.0f);
      render(dir, "clap", v, frames);
      v.trigger(midicps(71.0f), midicps(84.0f), 0.5f, 0.3f, 0.026f, 1.0f);
      control(ctrl, "clap", v, frames); }

    { Tom v; v.init(sr); v.trigger(kTomLo,   midicps(kTomLo.note),   kTomLo.decay);
      render(dir, "tomlo", v, frames); }
    { Tom v; v.init(sr); v.trigger(kTomMid,  midicps(kTomMid.note),  kTomMid.decay);
      render(dir, "tommid", v, frames); }
    { Tom v; v.init(sr); v.trigger(kTomHi,   midicps(kTomHi.note),   kTomHi.decay);
      render(dir, "tomhi", v, frames); }
    { Tom v; v.init(sr); v.trigger(kCongaLo, midicps(kCongaLo.note), kCongaLo.decay);
      render(dir, "congalo", v, frames); }
    { Tom v; v.init(sr); v.trigger(kCongaMid,midicps(kCongaMid.note),kCongaMid.decay);
      render(dir, "congamid", v, frames); }
    { Tom v; v.init(sr); v.trigger(kCongaHi, midicps(kCongaHi.note), kCongaHi.decay);
      render(dir, "congahi", v, frames); }

    { RimClave v; v.init(sr);
      /* note 92, detune -22, hpf 63, lpf 118, decay 0.07 */
      v.trigger(0, midicps(92.0f), 0.07f, midicps(63.0f), midicps(118.0f));
      render(dir, "rimshot", v, frames);
      v.trigger(0, midicps(92.0f), 0.07f, midicps(63.0f), midicps(118.0f));
      control(ctrl, "rimshot", v, frames); }

    { RimClave v; v.init(sr);
      /* claves are note 99 in sc808; the lane's Tune drives the rim at 92 and
       * the +7 semitone offset lives in the voice, so pass the rim's note. */
      v.trigger(1, midicps(92.0f), 0.10f, 0.0, 0.0);
      render(dir, "claves", v, frames); }

    { Maracas v; v.init(sr);
      v.trigger(midicps(113.0f), 0.027f, 0.07f);
      render(dir, "maracas", v, frames);
      v.trigger(midicps(113.0f), 0.027f, 0.07f);
      control(ctrl, "maracas", v, frames); }

    { Cowbell v; v.init(sr);
      /* sc808's notes resolve to 811.4 / 538.7 Hz, i.e. ratio 1.0 on the
       * metal bank's oscillators 5 and 6. */
      v.trigger(1.0, 9.5f, midicps(59.0f), midicps(109.0f));
      render(dir, "cowbell", v, frames); }

    { ClosedHat v; v.init(sr);
      v.trigger(1.0, 0.42f, midicps(121.25219487074914f),
                            midicps(121.05875888638981f));
      render(dir, "closed_hihat", v, frames); }

    { OpenHat v; v.init(sr);
      v.trigger(1.0, 0.5f, midicps(118.551f), midicps(107.213f));
      render(dir, "open_hihat", v, frames); }

    { Cymbal v; v.init(sr);
      v.trigger(1.0, 2.0f, 0.25f);
      render(dir, "cymbal", v, frames); }

    return 0;
}
