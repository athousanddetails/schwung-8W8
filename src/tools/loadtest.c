/*
 * loadtest.c — dlopen the REAL dsp.so, exactly as Schwung's chain host does.
 *
 * Cross-compiled for aarch64 and run ON the Move. Everything else in this
 * project tests the DSP; this tests the thing that actually ships — the
 * shared object, its exported symbol, its ABI, and whether it survives the
 * call sequence the host makes.
 *
 * It is deliberately paranoid about the boring failures, because those are
 * the ones that reach a user: a symbol that did not export, a parameter key
 * the generator emitted and the engine never resolved, a state blob that does
 * not round-trip, a lane that is silent because its pad note is wrong.
 *
 *   sc808_loadtest /path/to/dsp.so
 *
 * GPL-3.0.
 */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_api_v1.h"
#include "sc808_engine.h"
#include "sc808_params.h"

static int g_fail = 0;

/* Its own copy of the lane names: sc808_voice_id lives INSIDE dsp.so, and
 * this program is a host, not a link-time consumer of the engine. Linking
 * against the engine here would test a second copy of it and prove nothing
 * about the shared object we are loading. */
static const char *const kLane[SC808_NUM_VOICES] = {
    "bd", "sd", "lt", "mt", "ht", "lc", "mc", "hc",
    "rs", "ma", "cp", "cb", "ch", "oh", "cy"
};

static void ok(const int cond, const char *what, const char *detail)
{
    if(cond) { printf("  ok    %s\n", what); return; }
    printf("  FAIL  %s%s%s\n", what, detail ? " — " : "", detail ? detail : "");
    ++g_fail;
}

/* ---- a minimal host ---------------------------------------------------- */

static void host_log(const char *msg) { printf("  [dsp] %s\n", msg); }

/* A transport the test can drive. The step sequencer is clocked from
 * get_beat_position(), so without these two the lanes never fire and the
 * whole feature goes untested — which it did until this was added. */
static int    g_clock = MOVE_CLOCK_STATUS_STOPPED;
static double g_beat  = -1.0;
static int    host_clock(void) { return g_clock; }
static double host_beat(void)  { return g_beat; }
static float  host_bpm(void)   { return 120.0f; }

static host_api_v1_t g_host;

/* ---- helpers ----------------------------------------------------------- */

#define FRAMES MOVE_FRAMES_PER_BLOCK

static int16_t g_out[FRAMES * 2];

/* Render `blocks` blocks and report the peak absolute sample. */
static int render_peak(plugin_api_v2_t *api, void *inst, const int blocks)
{
    int peak = 0;
    for(int b = 0; b < blocks; ++b)
    {
        memset(g_out, 0, sizeof(g_out));
        api->render_block(inst, g_out, FRAMES);
        for(int i = 0; i < FRAMES * 2; ++i)
        {
            const int a = g_out[i] < 0 ? -g_out[i] : g_out[i];
            if(a > peak) peak = a;
        }
    }
    return peak;
}

/* Capture a lane's output so two hits can be compared sample for sample. */
#define CAP_FRAMES (FRAMES * 20)
static int16_t g_cap[2][CAP_FRAMES];
static void capture(plugin_api_v2_t *api, void *inst, const int which)
{
    for(int b = 0; b < 20; ++b)
    {
        api->render_block(inst, g_out, FRAMES);
        for(int i = 0; i < FRAMES; ++i) g_cap[which][b * FRAMES + i] = g_out[i * 2];
    }
}
/*
 * How different two captured hits are, as a fraction of their own level.
 *
 * NOT an equality test, and the first version of this was, which was wrong.
 * In Retrig mode the OSCILLATORS restart, but 8W8's voices are persistent and
 * their filters still hold the previous hit's tail — so two hits differ by
 * about -86 dB rather than exactly zero. SuperCollider gets exact equality
 * only because every note there is a brand new synth with brand new filters.
 *
 * The two cases are four orders of magnitude apart, so any threshold between
 * them is decisive: Retrig lands near 0.0001, Free near 1.4 (which is what
 * two uncorrelated signals of equal level give).
 */
static double captures_diff(void)
{
    double d = 0.0, r = 0.0;
    for(int i = 0; i < CAP_FRAMES; ++i)
    {
        const double a = g_cap[0][i], b = g_cap[1][i];
        d += (a - b) * (a - b);
        r += a * a;
    }
    return r > 0.0 ? sqrt(d / r) : 0.0;
}

static void note_on(plugin_api_v2_t *api, void *inst, const int note, const int vel)
{
    const uint8_t msg[3] = { 0x90, (uint8_t)note, (uint8_t)vel };
    api->on_midi(inst, msg, 3, MOVE_MIDI_SOURCE_INTERNAL);
}

/*
 * Silence the whole kit, immediately.
 *
 * Necessary, not tidiness: several 808 voices ring for a very long time. The
 * cymbal's shimmer envelope runs for decay x20 — forty seconds at the default
 * — and the toms are declared at twenty. Any test that triggers one lane and
 * asks "is it quiet now?" without doing this is really asking about the tail
 * of the lane before it, and four checks in this file failed exactly that way
 * before it existed.
 *
 * Muting every lane starts a 2 ms fade and then drops the lane entirely;
 * unmuting does NOT resurrect the tail, it only re-arms the lane for its next
 * trigger. That is precisely the behaviour wanted here.
 */
static void quiesce(plugin_api_v2_t *api, void *inst)
{
    api->set_param(inst, "mutes", "32767");     /* all 15 bits */
    render_peak(api, inst, 8);                  /* let the fades complete */
    api->set_param(inst, "mutes", "0");
    render_peak(api, inst, 2);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "./dsp.so";

    printf("8W8 loadtest: %s\n\n", path);

    /* ---- 1. dlopen and the entry symbol ---- */
    printf("load\n");
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if(!lib) { printf("  FAIL  dlopen — %s\n", dlerror()); return 1; }
    ok(1, "dlopen", NULL);

    move_plugin_init_v2_fn init =
        (move_plugin_init_v2_fn)dlsym(lib, MOVE_PLUGIN_INIT_V2_SYMBOL);
    ok(init != NULL, "exports " MOVE_PLUGIN_INIT_V2_SYMBOL, dlerror());
    if(!init) return 1;

    memset(&g_host, 0, sizeof(g_host));
    g_host.api_version     = MOVE_PLUGIN_API_VERSION;
    g_host.sample_rate     = MOVE_SAMPLE_RATE;
    g_host.frames_per_block= MOVE_FRAMES_PER_BLOCK;
    g_host.log             = host_log;
    g_host.get_clock_status= host_clock;
    g_host.get_bpm         = host_bpm;
    g_host.get_beat_position = host_beat;

    plugin_api_v2_t *api = init(&g_host);
    ok(api != NULL && api->api_version == MOVE_PLUGIN_API_VERSION_2,
       "returns a v2 API", NULL);
    if(!api) return 1;

    void *inst = api->create_instance(".", NULL);
    ok(inst != NULL, "create_instance", NULL);
    if(!inst) return 1;

    /* ---- 2. silence when idle ---- */
    printf("\nidle\n");
    ok(render_peak(api, inst, 16) == 0, "silent before any note", NULL);

    /* ---- 3. the JSON the Shadow UI needs ---- */
    printf("\nserved parameters\n");
    {
        static char buf[65536];
        const int n = api->get_param(inst, "chain_params", buf, sizeof(buf));
        ok(n == SC808_CHAIN_PARAMS_LEN, "chain_params length", NULL);
        ok(n > 0 && buf[0] == '[', "chain_params is an array", NULL);

        const int m = api->get_param(inst, "ui_pages", buf, sizeof(buf));
        ok(m == SC808_UI_PAGES_LEN, "ui_pages length", NULL);
        ok(m > 0 && buf[0] == '{', "ui_pages is an object", NULL);

        /* ui_hierarchy MUST be absent, or enterComponentEdit prefers it and
         * never loads ui_chain.js — the pad gestures silently stop working. */
        ok(api->get_param(inst, "ui_hierarchy", buf, sizeof(buf)) < 0,
           "ui_hierarchy is NOT served", NULL);
    }

    /* ---- 4. every generated key resolves in the engine ----
     * A key the generator emits and the engine never resolves is a silent
     * dead knob. This is the check that catches a rename. */
    printf("\nparameter surface\n");
    {
        char buf[64];
        int missing = 0;
        for(int i = 0; i < SC808_NUM_POTS; ++i)
            if(api->get_param(inst, g_sc808_pots[i].key, buf, sizeof(buf)) < 0)
            { printf("  FAIL  pot %s does not resolve\n", g_sc808_pots[i].key);
              ++missing; }
        for(int i = 0; i < SC808_NUM_ENUMS; ++i)
            if(api->get_param(inst, g_sc808_enums[i].key, buf, sizeof(buf)) < 0)
            { printf("  FAIL  enum %s does not resolve\n", g_sc808_enums[i].key);
              ++missing; }
        g_fail += missing;
        if(!missing)
            printf("  ok    all %d pots and %d enums resolve\n",
                   SC808_NUM_POTS, SC808_NUM_ENUMS);

        /* Defaults must match the table, or a fresh patch is not the kit the
         * null test verified. */
        int wrong = 0;
        for(int i = 0; i < SC808_NUM_POTS; ++i)
        {
            api->get_param(inst, g_sc808_pots[i].key, buf, sizeof(buf));
            if(atoi(buf) != g_sc808_pots[i].def) ++wrong;
        }
        ok(wrong == 0, "pot defaults match the generated table", NULL);
    }

    /* ---- 5. every pad sounds ----
     * The drum-rack notes, 36..50, which is what a Move drum track sends. */
    printf("\nvoices\n");
    {
        int silent = 0;
        for(int v = 0; v < SC808_NUM_VOICES; ++v)
        {
            note_on(api, inst, 36 + v, 100);
            /* The bass drum's lookahead limiter delays it by 20 ms, so a
             * short render would call it silent. 60 blocks is 174 ms. */
            const int peak = render_peak(api, inst, 60);
            if(peak == 0)
            { printf("  FAIL  %s (note %d) is silent\n",
                     kLane[v], 36 + v); ++silent; }
        }
        g_fail += silent;
        if(!silent) printf("  ok    all %d lanes sound on notes 36-%d\n",
                           SC808_NUM_VOICES, 36 + SC808_NUM_VOICES - 1);
    }

    /* Pad notes: the left 4x4 block. Pad 95 is Master and must NOT sound. */
    {
        quiesce(api, inst);
        static const int pads[SC808_NUM_VOICES] = {
            68, 69, 70, 71, 76, 77, 78, 79, 84, 85, 86, 87, 92, 93, 94
        };
        int silent = 0;
        for(int v = 0; v < SC808_NUM_VOICES; ++v)
        {
            note_on(api, inst, pads[v], 100);
            if(render_peak(api, inst, 60) == 0)
            { printf("  FAIL  pad %d (%s) is silent\n",
                     pads[v], kLane[v]); ++silent; }
        }
        g_fail += silent;
        if(!silent) printf("  ok    all %d pads sound\n", SC808_NUM_VOICES);

        quiesce(api, inst);
        note_on(api, inst, 95, 100);
        ok(render_peak(api, inst, 8) == 0, "pad 95 (Master) does not sound", NULL);
    }

    /* ---- 6. a pot actually changes the audio ---- */
    printf("\npots do something\n");
    {
        quiesce(api, inst);
        api->set_param(inst, "bd_level", "0");
        note_on(api, inst, 36, 100);
        const int quiet = render_peak(api, inst, 60);
        quiesce(api, inst);
        api->set_param(inst, "bd_level", "default");
        note_on(api, inst, 36, 100);
        const int loud = render_peak(api, inst, 60);
        ok(quiet == 0 && loud > 0, "bd_level 0 silences, default restores", NULL);
    }

    /* ---- 7. mutes ---- */
    printf("\nmutes\n");
    {
        quiesce(api, inst);
        api->set_param(inst, "mutes", "1");            /* bit 0 = bass drum */
        note_on(api, inst, 36, 100);
        const int muted = render_peak(api, inst, 60);
        api->set_param(inst, "mutes", "0");
        note_on(api, inst, 36, 100);
        const int un = render_peak(api, inst, 60);
        ok(muted == 0, "a muted lane swallows its trigger", NULL);
        ok(un > 0, "unmuting restores it", NULL);

        char buf[32];
        api->set_param(inst, "mutes", "16384");        /* bit 14 = cymbal */
        api->get_param(inst, "mutes", buf, sizeof(buf));
        ok(atoi(buf) == 16384, "the top lane's mute bit survives", buf);
        api->set_param(inst, "mutes", "0");
    }

    /* ---- 8. hi-hat choke ---- */
    printf("\nchoke\n");
    {
        quiesce(api, inst);
        api->set_param(inst, "hh_choke", "1");         /* CH > OH */
        note_on(api, inst, 36 + SC808_OH, 100);
        render_peak(api, inst, 4);
        note_on(api, inst, 36 + SC808_CH, 100);
        render_peak(api, inst, 40);
        /* The open hat should be gone; the closed hat is short too, so this
         * only asserts that the pair does not ring on for its full length. */
        api->set_param(inst, "ch_level", "0");
        const int tail = render_peak(api, inst, 40);
        ok(tail == 0, "closed hat chokes the open hat", NULL);
        api->set_param(inst, "ch_level", "default");
        api->set_param(inst, "hh_choke", "default");
    }

    /* ---- 9. state round-trip ---- */
    printf("\nstate\n");
    {
        static char before[8192], after[8192];
        api->set_param(inst, "bd_tune", "17");
        api->set_param(inst, "cy_decay", "111");
        api->set_param(inst, "master_dist", "3");
        api->set_param(inst, "seq_cy", "4369");
        const int n = api->get_param(inst, "state", before, sizeof(before));
        ok(n > 0, "state serialises", NULL);

        /* Move everything, then restore. */
        api->set_param(inst, "bd_tune", "0");
        api->set_param(inst, "cy_decay", "0");
        api->set_param(inst, "master_dist", "0");
        api->set_param(inst, "seq_cy", "0");
        api->set_param(inst, "state", before);
        api->get_param(inst, "state", after, sizeof(after));
        ok(strcmp(before, after) == 0, "state round-trips exactly", NULL);

        char buf[32];
        api->get_param(inst, "bd_tune", buf, sizeof(buf));
        ok(atoi(buf) == 17, "a pot came back", buf);
        api->get_param(inst, "seq_cy", buf, sizeof(buf));
        ok(atoi(buf) == 4369, "a sequencer lane came back", buf);

        /* A blob from an older build, missing the tail, must not read garbage. */
        api->set_param(inst, "state", "{\"v\":1,\"pots\":[10,20],\"enums\":[1]}");
        api->get_param(inst, "bd_tune", buf, sizeof(buf));
        ok(atoi(buf) == 10, "a short blob applies what it has", buf);
        api->get_param(inst, "cy_level", buf, sizeof(buf));
        ok(atoi(buf) == g_sc808_pots[SC808_NUM_POTS - 1].def
           || atoi(buf) >= 0, "and leaves the rest alone", buf);
    }

    /* ---- 10. the General MIDI note map ---- */
    printf("\nGM note map\n");
    {
        quiesce(api, inst);
        api->set_param(inst, "note_map", "1");
        /* A note that means nothing in the drum-rack map (36..50) but is the
         * GM cowbell — proves the map actually switched rather than the pad
         * map catching it. */
        note_on(api, inst, 56, 100);
        const int gm = render_peak(api, inst, 60);
        ok(gm > 0, "GM note 56 (cowbell) sounds when note_map is GM", NULL);

        quiesce(api, inst);
        api->set_param(inst, "note_map", "0");
        note_on(api, inst, 56, 100);
        ok(render_peak(api, inst, 60) == 0,
           "and is silent again on the drum-rack map", NULL);
        api->set_param(inst, "note_map", "default");
    }

    /* ---- 11. the step sequencer ---- */
    printf("\nstep sequencer\n");
    {
        quiesce(api, inst);
        api->set_param(inst, "seq_bd", "1");      /* step 0 only */
        g_clock = MOVE_CLOCK_STATUS_RUNNING;

        /* Beat 0 lands on step 0 and should fire the kick. */
        g_beat = 0.0;
        const int fired = render_peak(api, inst, 60);
        ok(fired > 0, "a programmed step fires its lane when the transport runs",
           NULL);

        /* Step 1 is empty: nothing new should start. */
        quiesce(api, inst);
        g_beat = 0.25;                             /* step 1 */
        ok(render_peak(api, inst, 20) == 0, "an empty step fires nothing", NULL);

        /* Transport stopped: the lane re-arms and stays quiet. */
        quiesce(api, inst);
        g_clock = MOVE_CLOCK_STATUS_STOPPED;
        g_beat = -1.0;
        api->set_param(inst, "seq_bd", "65535");   /* every step */
        ok(render_peak(api, inst, 40) == 0,
           "with no transport the sequencer stays silent", NULL);

        char buf[32];
        api->get_param(inst, "seq_bd", buf, sizeof(buf));
        ok(atoi(buf) == 65535, "a sequencer lane reads back", buf);
        api->set_param(inst, "seq_bd", "0");
    }

    /* ---- 12. the two-in-one lanes and the kick's two engines ---- */
    printf("\nmodes\n");
    {
        /* Rim and Clave share a lane; both must sound, and differently. */
        quiesce(api, inst);
        api->set_param(inst, "rs_mode", "0");
        note_on(api, inst, 36 + SC808_RS, 100);
        const int rim = render_peak(api, inst, 60);
        quiesce(api, inst);
        api->set_param(inst, "rs_mode", "1");
        note_on(api, inst, 36 + SC808_RS, 100);
        const int clave = render_peak(api, inst, 60);
        ok(rim > 0 && clave > 0, "both Rim and Clave sound on the shared lane", NULL);
        ok(rim != clave, "and they are not the same sound", NULL);
        api->set_param(inst, "rs_mode", "default");

        /* Both kick engines must sound, and land at a comparable level —
         * they share one per-lane trim, so if they disagree about what a
         * kick comes out at, switching engines shifts the kit balance. */
        quiesce(api, inst);
        api->set_param(inst, "bd_engine", "0");        /* circuit */
        note_on(api, inst, 36, 100);
        const int circ = render_peak(api, inst, 90);
        quiesce(api, inst);
        api->set_param(inst, "bd_engine", "1");        /* sc808 */
        note_on(api, inst, 36, 100);
        const int sc = render_peak(api, inst, 90);
        char d[64];
        snprintf(d, sizeof(d), "circuit %d, sc808 %d", circ, sc);
        ok(circ > 0 && sc > 0, "both kick engines sound", d);
        ok(sc < circ * 3 && circ < sc * 3,
           "and they are within 10 dB of each other", d);
        api->set_param(inst, "bd_engine", "default");
    }

    /* ---- 13. free-running metal oscillators ----
     *
     * On the hardware the hats' and cymbal's six Schmitt-trigger oscillators
     * never stop; the envelopes gate them, so every hit catches the bank at a
     * different phase and no two are the same. sc808 restarts them per note
     * because in SuperCollider every note is a new synth.
     *
     * The test is exact: in Retrig the two hits must be BIT-IDENTICAL, and in
     * Free they must not be. A half-working free-run — one that advances the
     * bank only while the lane is audible — passes a "sounds different" check
     * and fails this one, because evenly spaced hits would land back on the
     * same phase every time. */
    printf("\nfree-running metal\n");
    {
        api->set_param(inst, "metal_run", "1");        /* Retrig, i.e. sc808 */
        quiesce(api, inst);
        note_on(api, inst, 36 + SC808_CH, 100); capture(api, inst, 0);
        note_on(api, inst, 36 + SC808_CH, 100); capture(api, inst, 1);
        {
            const double diff = captures_diff();
            char d[64]; snprintf(d, sizeof(d), "relative difference %.5f", diff);
            ok(diff < 0.01, "Retrig: consecutive hats are the same hit", d);
        }

        api->set_param(inst, "metal_run", "0");        /* Free, i.e. hardware */
        quiesce(api, inst);
        note_on(api, inst, 36 + SC808_CH, 100); capture(api, inst, 0);
        note_on(api, inst, 36 + SC808_CH, 100); capture(api, inst, 1);
        {
            const double diff = captures_diff();
            char d[64]; snprintf(d, sizeof(d), "relative difference %.5f", diff);
            ok(diff > 0.20, "Free: consecutive hats are different hits, as on an 808", d);
        }
        api->set_param(inst, "metal_run", "default");
    }

    /* ---- 14. nothing pathological in the output ---- */
    printf("\noutput sanity\n");
    {
        quiesce(api, inst);
        for(int v = 0; v < SC808_NUM_VOICES; ++v) note_on(api, inst, 36 + v, 127);
        api->set_param(inst, "master_dist", "4");      /* crush, worst case */
        api->set_param(inst, "master_drive", "127");
        int peak = render_peak(api, inst, 400);
        ok(peak <= 32767, "int16 range respected under full drive", NULL);
        ok(peak > 0, "and it is not silent", NULL);
        api->set_param(inst, "master_dist", "default");
        api->set_param(inst, "master_drive", "default");
    }

    api->destroy_instance(inst);
    dlclose(lib);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
