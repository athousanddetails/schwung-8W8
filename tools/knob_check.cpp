/*
 * knob_check.cpp — every control must actually do something.
 *
 * THE BUG THIS EXISTS FOR. The maracas Attack pot changed nothing for
 * several releases. It fed the sc808 maracas; when the Engine switches came
 * off, that branch went and the circuit's trigger had never taken an attack.
 * The knob kept its slot, kept resolving, and reached no audio.
 *
 * Nothing in this suite noticed, and nothing could have: golden_check proves
 * the DEFAULTS have not moved, kit_check weighs the lanes, the editor tests
 * check that keys resolve and pages lay out. A dead knob passes all of them,
 * and so does reading the call site — that is how it survived a code review
 * on two separate sessions.
 *
 * So this measures. Set a control one way, render its lane, hash. Set it the
 * other way, render, hash. They must differ.
 *
 * CONTEXT IS THE WHOLE DIFFICULTY, and getting it wrong makes the probe lie
 * in the direction of alarm. A distortion TYPE does nothing while that
 * voice's Drive is 0 — and Drive defaults to 0, because the 808 had no drive
 * stage. A send bus's controls do nothing while no voice is feeding it.
 * vel_depth does nothing at velocity 127, by design, since it sets how far a
 * SOFT hit falls below the top. Each of those is a claim about how the module
 * is meant to work, so each context below says why it is there.
 *
 * The trap worth naming, hit by CW-78's version of this probe: seed the bus
 * context from the KICK and all eight bus controls read dead, because the
 * kick has no sends — it stays dry by design. Reaching for the one lane that
 * cannot feed a bus makes the send section look broken. The seed here is the
 * snare, deliberately.
 *
 * Build: clang++ -std=c++14 -O2 -Isrc/dsp -o build-native/knob_check \
 *            tools/knob_check.cpp src/dsp/sc808_engine.cpp
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sc808_engine.h"
#include "sc808_params.h"

static const double SR = 44100.0;
static const int    kFrames = (int)(SR * 2.0);
static int fails = 0, live = 0, excused = 0;

struct Setup { const char *key; const char *val; };

/* Render one lane with a list of params applied, and hash the result. */
static unsigned long long render(int voice, int velocity,
                                 const Setup *setup, int nsetup,
                                 int second)
{
    sc808_engine_t *e = sc808_create((float)SR);
    for(int i = 0; i < nsetup; ++i)
        sc808_set_param(e, setup[i].key, setup[i].val);

    static float buf[(int)(44100 * 2)];
    memset(buf, 0, sizeof buf);
    /* `second` lets a case trigger a second lane first — the hat choke needs
     * an open hat ringing before the closed hat can cut it. */
    if(second >= 0) { sc808_trigger(e, second, velocity); sc808_render(e, buf, 2048); }
    sc808_trigger(e, voice, velocity);
    sc808_render(e, buf, kFrames);
    sc808_destroy(e);

    unsigned long long h = 1469598103934665603ULL;
    for(int i = 0; i < kFrames; ++i)
    {
        unsigned int b; memcpy(&b, &buf[i], sizeof b);
        if(buf[i] == 0.0f) b = 0;
        for(int k = 0; k < 4; ++k) { h ^= (unsigned char)(b >> (k * 8)); h *= 1099511628211ULL; }
    }
    return h;
}

/*
 * One control, measured between two values in a context where it is supposed
 * to work. `why` names the context so a reader can judge whether the claim is
 * fair rather than taking the pass on trust.
 */
static void probe(const char *key, const char *a, const char *b,
                  int voice, int velocity, const char *why,
                  const Setup *ctx = 0, int nctx = 0, int second = -1)
{
    Setup s[8];
    int n = 0;
    for(int i = 0; i < nctx && n < 7; ++i) s[n++] = ctx[i];
    s[n] = (Setup){ key, a };
    const unsigned long long ha = render(voice, velocity, s, n + 1, second);
    s[n] = (Setup){ key, b };
    const unsigned long long hb = render(voice, velocity, s, n + 1, second);

    if(ha != hb) { ++live; return; }
    ++fails;
    printf("FAIL: %s does nothing (%s..%s on %s)%s%s\n", key, a, b,
           sc808_voice_id(voice), why[0] ? " — context: " : "", why);
}

static void excuse(const char *key, const char *why)
{
    ++excused;
    printf("      %-14s excused — %s\n", key, why);
}

static int lane_of(const char *key)
{
    for(int v = 0; v < SC808_NUM_VOICES; ++v)
    {
        const char *id = sc808_voice_id(v);
        const size_t n = strlen(id);
        if(!strncmp(key, id, n) && key[n] == '_') return v;
    }
    return -1;
}

int main()
{
    printf("every control must reach the audio\n\n");

    char buf[64];

    /* ---- per-voice controls ------------------------------------------- */
    for(int i = 0; i < SC808_NUM_POTS; ++i)
    {
        const char *k = g_sc808_pots[i].key;
        const int v = lane_of(k);
        if(v < 0) continue;                       /* handled below */

        /* A send moves nothing unless its bus returns something, but both
         * return levels default high, so the send itself is the variable. */
        probe(k, "0", "127", v, 110, "");
    }
    for(int i = 0; i < SC808_NUM_ENUMS; ++i)
    {
        const char *k = g_sc808_enums[i].key;
        const int v = lane_of(k);
        if(v < 0 || !strstr(k, "_dist_type")) continue;

        /* A distortion TYPE is inaudible while that voice's Drive is 0 — and
         * Drive defaults to 0, because the 808 had no drive stage. Measured
         * with the stage switched on, which is the only place the choice can
         * mean anything. */
        snprintf(buf, sizeof buf, "%s_drive", sc808_voice_id(v));
        const Setup ctx[1] = { { buf, "127" } };
        probe(k, "0", "6", v, 110, "its own Drive at full", ctx, 1);
    }

    /* ---- the send buses ------------------------------------------------ *
     * Seeded from the SNARE, not the kick: the kick has no sends at all, so
     * seeding from it would read the whole bus section as dead. */
    {
        const Setup rev[1] = { { "sd_rev", "127" } };
        const Setup dly[1] = { { "sd_dly", "127" } };
        probe("rev_decay", "0", "127", SC808_SD, 110, "snare feeding the bus", rev, 1);
        probe("rev_tone",  "0", "127", SC808_SD, 110, "snare feeding the bus", rev, 1);
        probe("rev_hpf",   "0", "127", SC808_SD, 110, "snare feeding the bus", rev, 1);
        probe("rev_level", "0", "127", SC808_SD, 110, "snare feeding the bus", rev, 1);
        probe("dly_fdbk",  "0", "127", SC808_SD, 110, "snare feeding the bus", dly, 1);
        probe("dly_tone",  "0", "127", SC808_SD, 110, "snare feeding the bus", dly, 1);
        probe("dly_hpf",   "0", "127", SC808_SD, 110, "snare feeding the bus", dly, 1);
        probe("dly_level", "0", "127", SC808_SD, 110, "snare feeding the bus", dly, 1);
        probe("dly_time",  "0", "12",  SC808_SD, 110, "snare feeding the bus", dly, 1);
    }

    /* ---- master ---------------------------------------------------------*/
    {
        /* Master distortion is Off at option 0 and inaudible at drive 0, so
         * the two have to be switched on for either to be measurable. */
        const Setup md[1] = { { "master_drive", "127" } };
        probe("master_dist", "1", "7", SC808_SD, 110, "master drive at full", md, 1);
        const Setup mt[1] = { { "master_dist", "1" } };
        probe("master_drive", "0", "127", SC808_SD, 110, "master dist switched on", mt, 1);

        probe("comp", "0", "127", SC808_SD, 110, "");
        probe("volume", "20", "127", SC808_SD, 110, "");

        /* Velocity DEPTH sets how far a SOFT hit falls below the top, so at
         * velocity 127 it is meant to do nothing. Measured at 40. */
        probe("vel_depth", "0", "127", SC808_SD, 40, "a soft hit, velocity 40");

        /* The choke needs something to cut: open hat first, then closed. */
        probe("hh_choke", "0", "2", SC808_CH, 110, "an open hat ringing",
              0, 0, SC808_OH);
    }

    /* ---- what cannot be measured here, and why ------------------------- */
    excuse("note_map", "remaps incoming NOTES to lanes; this probe triggers "
                       "lanes directly, so it is out of reach here. "
                       "src/tools/loadtest.c covers it through the MIDI path.");

    printf("\n%d live, %d excused, %d dead\n", live, excused, fails);
    printf(fails ? "FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
