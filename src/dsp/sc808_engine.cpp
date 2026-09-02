/*
 * sc808_engine.cpp — see sc808_engine.h.
 *
 * GPL-3.0. The voices under src/dsp/sc808_voices.h are a transcription of
 * sc808.scd (MIT); see THIRD_PARTY.md.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc808_voices.h"
#include "sc808_bd_circuit.h"
#include "sc808_cp_circuit.h"
#include "sc808_sd_circuit.h"
#include "sc808_tom_circuit.h"
#include "sc808_mt_circuit.h"
#include "sc808_rs_circuit.h"
#include "sc808_engine.h"
#include "sc808_params.h"
#include "sc808_fx.h"
#include "sc808_shape.h"

using namespace sc808;

/*
 * 2: the sixteen Engine switches and the Metal enum are gone — every lane is
 *    its circuit voice now — which renumbers the enum table. v1 blobs are
 *    loaded BY NAME against the table that shipped with them; see
 *    kV1EnumKeys and sc808_deserialize.
 * 3: the drive stage grew from four characters to 9W9's seven, which moved
 *    Fold 2 -> 5 and Crush 3 -> 6 inside every *_dist_type, and 3 -> 6 /
 *    4 -> 7 inside master_dist (its list is "Off" plus the same seven).
 *    Without the remap every old Crush patch comes back as something else,
 *    because deserialize clamps a selection to the last option rather than
 *    guessing.
 * 4: the Accent pot is gone — velocity subsumed it — which renumbers every
 *    pot after it. The pot table was untouched from v1 through v3, so
 *    kV1PotKeys is the order all three of them wrote and older blobs are
 *    placed by name against it.
 */
#define SC808_STATE_VERSION 4

/*
 * The gain a FULL-velocity hit reaches — the old Accent pot's default,
 * 1 + (42/127)*3, kept as a constant now the control is gone.
 *
 * The number matters. Accent was never "extra": it was the level a pattern
 * from Move actually played at, because Move sends velocity 100 and up. Drop
 * the pot and anchor the velocity line at 1.0 instead and the whole kit comes
 * back 6 dB quieter. Anchoring here keeps every existing pattern at exactly
 * the level it had.
 */
#define SC808_FULL_VELOCITY_GAIN 1.9921260f

/* A choke is a 2 ms fade, not a hard stop — cutting a ringing open hat dead
 * puts a click on the front of the closed hat that follows it. */
static const float kChokeSeconds = 0.002f;

namespace {

constexpr const char *kVoiceIds[SC808_NUM_VOICES] = {
    "bd", "sd", "lt", "mt", "ht", "lc", "mc", "hc",
    "rs", "cl", "ma", "cp", "cb", "ch", "oh", "cy"
};

/*
 * Base MIDI notes, taken from sc808's SynthDef defaults. Every Tune pot is a
 * SEMITONE OFFSET around these, so pot 64 is the sc808 sound whatever the
 * voice happens to be tuned to, and the null test's verified defaults are
 * what a fresh patch loads with.
 *
 * The metal voices (cowbell, both hats, cymbal) have no note — they are six
 * fixed Schmitt oscillators — so their Tune is a frequency RATIO instead and
 * their entry here is unused.
 *
 * ONE DELIBERATE DEPARTURE: sc808 gives congahi note 52, the same as congalo,
 * which is plainly a copy-paste slip since congamid sits at 57 between them.
 * 62 continues the progression. sc808_voices.h is untouched and the null test
 * still runs against 52 — this is the engine choosing a knob position, not a
 * change to the transcription.
 */
const float kBaseNote[SC808_NUM_VOICES] = {
    34.0f,   /* bd */
    65.0f,   /* sd */
    /*
     * The six tom/conga notes are MEASURED, not sc808's. Hardware samples
     * with note names in the filenames, fundamentals confirmed by
     * autocorrelation: toms F2 / C3 / G3, congas G3 / D4 / A4 — seven
     * semitones between neighbours, congas more than an octave above their
     * toms. sc808's declared notes (40/44/52 and 52/57/62) are both mistuned
     * and wrongly spaced against the real machine, which is why the lanes
     * neither sounded right nor overlapped. The sc808 engine is still
     * exactly the transcription — it just gets played at the right pitch.
     */
    /* Settled fundamentals of Roland's own model at the default preset —
     * the user's reference render (toms808.wav) matches within a cent. The
     * earlier F2/C3/G3 came from third-party hardware samples recorded at
     * other TUNING positions. */
    42.02f,  /* lt — 92.6 Hz */
    48.58f,  /* mt — 135.3 Hz */
    54.83f,  /* ht — 183.0 Hz */
    /* Congas: Roland's model with the kit switch flipped to the conga
     * side of each channel, measured settled — 206.1 / 302.1 / 404.6 Hz.
     * The 196/293.7/440 that stood here came from third-party samples at
     * unknown knob positions; the hi conga was a semitone and a half off. */
    55.88f,  /* lc — 206.1 Hz */
    62.49f,  /* mc — 302.1 Hz */
    67.55f,  /* hc — 404.6 Hz */
    /* MEASURED from rim808.wav: the tock sits at 1788 Hz and sc808 puts it
     * at note x 1.1, so the note is 91.62 — its declared 92 landed the rim
     * a third of a semitone sharp, which is the "needs better tuning on the
     * default" from the first field report. */
    91.62f,  /* rs */
    99.2f,   /* cl — MEASURED: Roland's own model pings at 2518 Hz, which is
              *      99.2; sc808's 99 (2489 Hz) sat 20 cents flat */
   113.0f,   /* ma — the highpass corner, this voice has no oscillator */
    71.0f,   /* cp — the highpass corner */
     0.0f, 0.0f, 0.0f, 0.0f    /* cb, ch, oh, cy: ratio-tuned */
};

/*
 * Kit balance.
 *
 * Left alone the voices arrive at wildly different levels, because each
 * SynthDef was written to be played on its own with its own `amp`: the closed
 * hat peaks near 17 while the hand clap peaks at 0.38, a spread of 33 dB.
 * Sixteen of those summed is not a drum machine, it is a fault.
 *
 * These trims put the kit in proportion at pot centre, so Level 64 means
 * "the balanced 808", not "unity gain". They set TWO things at once and
 * tools/kit_check fits both together:
 *
 *   balance   each lane's loudest 20 ms, against a written-down voicing
 *             table (kVoicing, in kit_check) rather than against each other.
 *             Equal loudness would be the easy defensible choice and it is
 *             wrong: a kit whose hi-hat is as loud as its kick is not a kit.
 *             Converged to within 0.5 dB.
 *   level     the absolute scale, set so a real downbeat — kick, snare, clap
 *             and closed hat together on an accent — lands at -1 dBFS. A
 *             solo accented kick sits at -10.5 dBFS, which is what leaves
 *             room for the other fourteen.
 *
 * Measured, not guessed: run tools/kit_check after any voice change and it
 * prints what to paste back here. Two earlier metrics are documented in that
 * file as failures worth not repeating — peak, and RMS over a fixed window.
 */
constexpr float kVoiceTrim[SC808_NUM_VOICES] = {
    0.2964f,   /* bd — the reference: everything else is set against the kick */
    0.1953f,   /* sd */
    0.4110f,   /* lt */
    0.4199f,   /* mt */
    0.3917f,   /* ht */
    0.3750f,   /* lc */
    0.3899f,   /* mc */
    0.3843f,   /* hc */
    0.2643f,   /* rs — a click with a crest factor of 11 */
    2.6005f,   /* cl */
    0.6142f,   /* ma */
    3.9640f,   /* cp — the quietest voice in sc808 by a long way */
    0.1219f,   /* cb */
    0.0021f,   /* ch — raw peak near 17 before the drive stage catches it */
    0.0402f,   /* oh */
    5.9505f,   /* cy */
};

/*
 * A short initialiser list on a sized array does not warn — C zero-fills the
 * tail — and a zero trim is a SILENT LANE. Adding the claves as a sixteenth
 * voice did exactly this: the table kept fifteen entries, the cymbal read
 * 0.0f, and it vanished. tools/kit_check caught it as a -inf, which is what
 * that check is for, but a compile error is cheaper than a render.
 */
constexpr bool trim_table_filled(const int i = 0)
{
    return i >= SC808_NUM_VOICES
         ? true
         : (kVoiceTrim[i] > 0.0f && trim_table_filled(i + 1));
}
static_assert(trim_table_filled(),
              "kVoiceTrim has a zero entry: a short initialiser list "
              "zero-fills the tail, and a zero trim silences that lane");

/* Same trap, same table shape. */
constexpr bool voice_ids_filled(const int i = 0)
{
    return i >= SC808_NUM_VOICES
         ? true
         : (kVoiceIds[i] != nullptr && kVoiceIds[i][0] != '\0'
            && voice_ids_filled(i + 1));
}
static_assert(voice_ids_filled(), "kVoiceIds is short of SC808_NUM_VOICES");

/* Per-voice pot/enum slots, resolved once at create time so the audio path
 * never searches by string. */
struct VoiceSlots {
    int tune, decay, drive, level;   /* every voice has these four */
    int dist;                        /* enum slot                  */
    /* Send amounts. -1 on the kick, which is dry by design — see SENDS in
     * gen_params.py. Resolved once here so the audio path never searches by
     * string. */
    int rev, dly;
};

struct VoiceRt {
    float hit_gain;      /* this hit's velocity gain            */
    float choke_gain;    /* 1.0 normally, ramps to 0 on a choke */
    float choke_step;    /* < 0 while choking, else 0           */
    /* Crush's sample-and-hold. Per lane, because one shared state would
     * put the closed hat's decimator steps on the kick. */
    float crush_st[SC808_CRUSH_STATE];
};

int find_pot(const char *key)
{
    for(int i = 0; i < SC808_NUM_POTS; ++i)
        if(!strcmp(g_sc808_pots[i].key, key)) return i;
    return -1;
}

int find_enum(const char *key)
{
    for(int i = 0; i < SC808_NUM_ENUMS; ++i)
        if(!strcmp(g_sc808_enums[i].key, key)) return i;
    return -1;
}

/* pot position 0..127 -> engineering value.
 * EXP: value = min * (max/min)^(pot/127). Fine control at the bottom of a
 * time or frequency range, where the ear actually is. */
float pot_value(int slot, int pot)
{
    const sc808_pot_t &p = g_sc808_pots[slot];
    const float t = (float)pot / 127.0f;
    if(p.curve == SC808_EXP && p.min > 0.0f)
        return p.min * powf(p.max / p.min, t);
    return p.min + (p.max - p.min) * t;
}

} /* namespace */

struct sc808_engine {
    float sample_rate;

    int   pot[SC808_NUM_POTS];       /* raw 0..127, the stored form   */
    float potv[SC808_NUM_POTS];      /* resolved, recomputed on write */
    int   env[SC808_NUM_ENUMS];      /* enum selections               */

    VoiceSlots slot[SC808_NUM_VOICES];
    VoiceRt    rt[SC808_NUM_VOICES];

    /* Voice-specific extras that only some lanes have. */
    int bd_attack, bd_tone, sd_snappy;
    int ma_attack;
    /* Globals. */
    int e_master_dist, e_choke, e_note_map;
    int p_master_drive, p_volume, p_vel_depth, p_comp;
    float crush_master[SC808_CRUSH_STATE];

    /* The two send buses and the bus glue. Heap-allocated with the engine —
     * the delay line alone is 352 KB, which is why sc808_create callocs
     * rather than putting an engine on a stack anywhere. */
    sc808_verb_t verb;
    sc808_dly_t  dly;
    sc808_glue_t glue;
    int p_rev_decay, p_rev_tone, p_rev_hpf, p_rev_level;
    int p_dly_fdbk, p_dly_tone, p_dly_hpf, p_dly_level;
    int e_dly_time;
    /* what the FX structs were last told, so sync only touches them on a
     * real change — resetting a biquad every block would tick the reverb
     * with a filter that never settles */
    float fx_rev_hpf, fx_dly_hpf;
    int   fx_divi;
    float fx_bpm;

    unsigned mutes;

    CircuitBassDrum bdc;   /* the bridged-T circuit   */
    CircuitSnare    sdc;   /* two bridged-T shells    */
    ClapCircuit     cpc;   /* burst, tail, 874 Hz MFB */
    /* Six copies of one channel, three struck as toms and three as
     * congas — which is what the switch on the real board does. */
    TomCircuit      ltc, mtc, htc, lcc, mcc, hcc;
    /* The metal circuit: ONE free-running bank feeding the cymbal, both
     * hats and the cowbell, exactly as the hardware wires half an HD14584.
     * The engine ticks the bank once per sample; see SchmittBank::tick. */
    SchmittBank     mbank;
    CymbalCircuit   cyc;
    HatCircuit      chc, ohc;
    CowbellCircuit  cbc;
    ClaveCircuit    clc;
    MaracasCircuit  mac;
    /*
     * The ONE sc808 voice still in the audio path. Two circuit rim shots
     * were built from the schematic and both lost to this algorithm on
     * hardware, so the transcription is the rim. Every other lane is the
     * circuit; sc808_voices.h keeps the rest of the transcription and the
     * null test still verifies all of it, straight from the header —
     * src/tools/nullref.cpp includes it without going through this engine.
     */
    RimClave  rs;
};

/*
 * Push the pot table's engineering values into the FX structs.
 *
 * Called at create and after every parameter write. The cheap fields are
 * copied unconditionally; the two that COST something — the send highpass
 * biquads and the delay's retime — are only touched when their input
 * actually moved. Rebuilding a biquad every block is not free, and resetting
 * one would tick the reverb with a filter that never settles.
 */
static void sc808_fx_sync(sc808_engine_t *e)
{
    e->verb.decay = e->potv[e->p_rev_decay];
    e->verb.tone  = e->potv[e->p_rev_tone];
    e->verb.level = e->potv[e->p_rev_level];
    e->dly.fdbk   = e->potv[e->p_dly_fdbk];
    e->dly.tone   = e->potv[e->p_dly_tone];
    e->dly.level  = e->potv[e->p_dly_level];

    const float rh = e->potv[e->p_rev_hpf];
    if(rh != e->fx_rev_hpf)
    {
        e->verb.hpf_hz = rh;
        e->verb.hp.setHiPass(rh, kSC808_SendRQ, e->sample_rate);
        e->fx_rev_hpf = rh;
    }
    const float dh = e->potv[e->p_dly_hpf];
    if(dh != e->fx_dly_hpf)
    {
        e->dly.hpf_hz = dh;
        e->dly.hp.setHiPass(dh, kSC808_SendRQ, e->sample_rate);
        e->fx_dly_hpf = dh;
    }

    const int divi = e->env[e->e_dly_time];
    if(divi != e->fx_divi || e->dly.bpm != e->fx_bpm)
    {
        e->dly.divi = divi;
        sc808_dly_retime(&e->dly, e->sample_rate);
        e->fx_divi = divi;
        e->fx_bpm  = e->dly.bpm;
    }
}

const char *sc808_voice_id(int voice)
{
    return (voice >= 0 && voice < SC808_NUM_VOICES) ? kVoiceIds[voice] : "";
}

sc808_engine_t *sc808_create(float sample_rate)
{
    /*
     * calloc, and that has a consequence worth knowing before you add a voice.
     *
     * NO CONSTRUCTOR RUNS. Every member of every voice object in here is
     * zero bytes, and C++ default member initialisers — `double dc_ = 0.045;`
     * and friends — are silently skipped. A class that relies on one to be
     * usable before its own init() is called will be quietly wrong rather
     * than obviously broken.
     *
     * It cost a day once already: the tom/conga channel called reset() on its
     * PulseShaper instead of init(), so the shaper kept the zeroed dc_ and
     * returned exactly zero for every input. The toms still sounded, because
     * their noise head drives the resonator on its own, and only the congas
     * went silent — which pointed the search at the congas, which were fine.
     *
     * So: every voice's init() must set everything it needs, explicitly.
     */
    sc808_engine_t *e = (sc808_engine_t *)calloc(1, sizeof(sc808_engine_t));
    if(!e) return NULL;
    e->sample_rate = sample_rate > 0.0f ? sample_rate : 44100.0f;

    for(int i = 0; i < SC808_NUM_POTS; ++i)
    {
        e->pot[i]  = g_sc808_pots[i].def;
        e->potv[i] = pot_value(i, e->pot[i]);
    }
    for(int i = 0; i < SC808_NUM_ENUMS; ++i)
        e->env[i] = g_sc808_enums[i].def;

    /* Resolve every key once. A miss here is a generator/engine mismatch and
     * the loadtest asserts on it rather than letting it degrade silently. */
    char key[64];
    for(int v = 0; v < SC808_NUM_VOICES; ++v)
    {
        const char *id = kVoiceIds[v];
        snprintf(key, sizeof(key), "%s_tune",      id); e->slot[v].tune  = find_pot(key);
        snprintf(key, sizeof(key), "%s_decay",     id); e->slot[v].decay = find_pot(key);
        snprintf(key, sizeof(key), "%s_drive",     id); e->slot[v].drive = find_pot(key);
        snprintf(key, sizeof(key), "%s_level",     id); e->slot[v].level = find_pot(key);
        snprintf(key, sizeof(key), "%s_dist_type", id); e->slot[v].dist  = find_enum(key);
        /* The kick declares no sends, so find_pot returns -1 and the render
         * loop skips it. That is the whole of "the kick is dry". */
        snprintf(key, sizeof(key), "%s_rev",       id); e->slot[v].rev   = find_pot(key);
        snprintf(key, sizeof(key), "%s_dly",       id); e->slot[v].dly   = find_pot(key);
        e->rt[v].hit_gain   = 1.0f;
        e->rt[v].choke_gain = 1.0f;
        e->rt[v].choke_step = 0.0f;
    }
    e->bd_attack = find_pot("bd_attack");
    e->bd_tone   = find_pot("bd_tone");
    e->sd_snappy = find_pot("sd_snappy");
    e->ma_attack = find_pot("ma_attack");
    e->p_master_drive = find_pot("master_drive");
    e->p_volume       = find_pot("volume");
    e->p_vel_depth    = find_pot("vel_depth");
    e->p_comp         = find_pot("comp");
    e->p_rev_decay    = find_pot("rev_decay");
    e->p_rev_tone     = find_pot("rev_tone");
    e->p_rev_hpf      = find_pot("rev_hpf");
    e->p_rev_level    = find_pot("rev_level");
    e->p_dly_fdbk     = find_pot("dly_fdbk");
    e->p_dly_tone     = find_pot("dly_tone");
    e->p_dly_hpf      = find_pot("dly_hpf");
    e->p_dly_level    = find_pot("dly_level");
    e->e_dly_time     = find_enum("dly_time");
    e->e_master_dist  = find_enum("master_dist");
    e->e_choke        = find_enum("hh_choke");
    e->e_note_map     = find_enum("note_map");

    const double sr = e->sample_rate;
    e->bdc.init(sr);
    e->sdc.init(sr);
    e->cpc.init(sr);
    e->ltc.init(sr, 0); e->mtc.init(sr, 0); e->htc.init(sr, 0);
    e->lcc.init(sr, 1); e->mcc.init(sr, 1); e->hcc.init(sr, 1);
    e->mbank.init(sr);
    e->cyc.init(sr, &e->mbank);
    e->chc.init(sr, &e->mbank, 0);
    e->ohc.init(sr, &e->mbank, 1);
    e->cbc.init(sr, &e->mbank);
    e->clc.init(sr);
    e->mac.init(sr);
    e->rs.init(sr);

    /* The FX read their engineering values from the pot table; sync pushes
     * them across and builds the send filters. The delay's read pointer is
     * seeded at its target so the first note does not sweep in from zero. */
    e->fx_bpm = 120.0f;
    e->dly.bpm = 120.0f;
    e->fx_rev_hpf = -1.0f; e->fx_dly_hpf = -1.0f; e->fx_divi = -1;
    sc808_fx_sync(e);
    sc808_verb_init(&e->verb, (float)sr);
    sc808_dly_init(&e->dly, (float)sr);
    e->dly.dcur = e->dly.time_ms * 0.001f * (float)sr;
    e->glue.env_db = 0.0f; e->glue.det = 0.0f;
    return e;
}

void sc808_destroy(sc808_engine_t *e) { free(e); }

void sc808_set_mutes(sc808_engine_t *e, unsigned mask)
{
    e->mutes = mask & ((1u << SC808_NUM_VOICES) - 1u);
    for(int v = 0; v < SC808_NUM_VOICES; ++v)
    {
        if(e->mutes & (1u << v))
        {
            /* Fade rather than cut, same reason as the choke. The lane keeps
             * rendering through the ramp; it drops out of the mix only once
             * the gain reaches zero. */
            if(e->rt[v].choke_gain > 0.0f && e->rt[v].choke_step == 0.0f)
                e->rt[v].choke_step = -1.0f / (kChokeSeconds * e->sample_rate);
        }
        else if(e->rt[v].choke_step < 0.0f)
        {
            /* Unmuted mid-fade: stop fading, but do not resurrect the tail —
             * the lane comes back on its next hit. */
            e->rt[v].choke_step = 0.0f;
        }
    }
}

unsigned sc808_get_mutes(const sc808_engine_t *e) { return e->mutes; }

static void choke_voice(sc808_engine_t *e, int v)
{
    if(e->rt[v].choke_gain > 0.0f && e->rt[v].choke_step == 0.0f)
        e->rt[v].choke_step = -1.0f / (kChokeSeconds * e->sample_rate);
}

/* Semitone offset -> Hz, around the lane's base note. */
static inline double lane_hz(int v, float offset)
{
    return (double)midicps(kBaseNote[v] + offset);
}

void sc808_trigger(sc808_engine_t *e, int voice, int velocity)
{
    if(voice < 0 || voice >= SC808_NUM_VOICES) return;
    if(velocity <= 0) return;                       /* note-off: one-shots */
    if(e->mutes & (1u << voice)) return;

    /*
     * VELOCITY. One straight line, no threshold: the full-velocity gain is
     * the top of the range and softer hits come down from it. Velocity on
     * Master is how far down — 0 means every hit plays at the top, which is
     * what this kit sounded like when Accent was a switch at 100.
     *
     * It reaches the voices TWO WAYS, and that is 8W8's own problem: most of
     * these circuits take the strike as a TRIGGER VOLTAGE, where a harder hit
     * is a different sound and not a louder one, while the hats and cymbal
     * take it as a gain (a hotter envelope into their diode gate stretches
     * the note 30%, which every reference render says does not happen).
     *
     * The voltage lanes cannot carry the whole range on their own: the
     * hardware's floor is 4 V, so below the unaccented level the voltage has
     * nowhere left to go and velocities 0..64 would all sound identical. So
     * the line is split at the unaccented point — above it the voltage rises
     * to the accented 7.3 V, below it the voltage sits at its floor and the
     * lane's gain carries the rest. Both halves are continuous and both
     * reference points land exactly where they always did.
     */
    const int   vi    = velocity > 127 ? 127 : velocity;
    const float vgain = SC808_FULL_VELOCITY_GAIN
                      * (1.0f - e->potv[e->p_vel_depth]
                                * (1.0f - (float)vi * (1.0f / 127.0f)));
    /* the strike, in the volts the hardware's VR3 delivers */
    float volts = 4.0f + 10.0f * ((vgain > 1.0f ? vgain : 1.0f) - 1.0f) / 3.0f;
    if(volts < 4.0f)  volts = 4.0f;
    if(volts > 14.0f) volts = 14.0f;
    /* what a voltage lane still has to do with gain, once the volts bottom */
    const float softGain = vgain < 1.0f ? vgain : 1.0f;

    e->rt[voice].hit_gain   = vgain;
    e->rt[voice].choke_gain = 1.0f;
    e->rt[voice].choke_step = 0.0f;

    const VoiceSlots &s = e->slot[voice];
    const float decay = e->potv[s.decay];
    const float tune  = e->potv[s.tune];

    /* The 808 shares one metal source between the closed and open hat, so
     * closed cutting open is the hardware wiring. Here it is a switch:
     * Off / CH > OH / Mutual. The cymbal is deliberately outside the group —
     * on the hardware it shares the same oscillators, but nobody wants a
     * hi-hat swallowing their crash. */
    const int choke = e->env[e->e_choke];
    if(voice == SC808_CH && choke >= 1) { choke_voice(e, SC808_OH); e->ohc.choke(); }
    if(voice == SC808_OH && choke == 2) { choke_voice(e, SC808_CH); e->chc.choke(); }

    switch(voice)
    {
    case SC808_BD:
    {
        /*
         * The circuit kick. Its three pots are read as raw POT POSITIONS
         * rather than through the pot table's engineering mapping — Decay
         * is LOOP GAIN in 0..1, not seconds, and mapping 0.1..8 s onto a
         * loop gain would be meaningless.
         *
         * Velocity is a TRIGGER VOLTAGE here, 4 V to 14 V as the
         * hardware's VR3 delivers, because on this circuit a harder hit is
         * a different sound and not a louder one — the trigger drives a
         * diode.
         */
        e->bdc.trigger(lane_hz(voice, tune),
                       (float)e->pot[e->slot[voice].decay] / 127.0f,
                       (float)e->pot[e->bd_tone]   / 127.0f,
                       (float)e->pot[e->bd_attack] / 127.0f,
                       volts);
        /* The hit is already in the trigger voltage; the lane's gain only
         * carries what is left below the voltage floor. */
        e->rt[voice].hit_gain = softGain;
        break;
    }
    case SC808_SD:
    {
        /* The circuit snare, read as raw POT POSITIONS: Snappy is a divider
         * on the trigger rather than a mix, and Tune is a ratio on both
         * shells' component-derived frequencies. */
        /* Shell balance fixed at centre — the Tone pot is gone ("Tune is
         * enough"), and centre is where its default sat. */
        e->sdc.trigger(powf(2.0f, tune / 12.0f),
                       (float)e->pot[e->slot[voice].decay] / 127.0f,
                       0.5f,
                       (float)e->pot[e->sd_snappy] / 127.0f,
                       volts);
        e->rt[voice].hit_gain = softGain;   /* the rest is in the trigger volts */
        break;
    }
    /*
     * The six tom / conga lanes — one channel of the voicing board, built
     * six times, three struck as toms and three as congas, which is what
     * the switch on the real board does.
     *
     * Velocity is a TRIGGER VOLTAGE rather than a gain: the strike is what
     * changes, so the lane's own gain only carries the range below the
     * voltage floor.
     */
    case SC808_LT: case SC808_MT: case SC808_HT:
    case SC808_LC: case SC808_MC: case SC808_HC:
    {
        const int i = voice - SC808_LT;
        TomCircuit *const tc[6] = { &e->ltc, &e->mtc, &e->htc,
                                    &e->lcc, &e->mcc, &e->hcc };
        /* Decay is RING TIME IN SECONDS — the circuit solves the loop gain
         * that produces it at the current pitch. */
        tc[i]->trigger(lane_hz(voice, tune), decay,
                       volts);
        e->rt[voice].hit_gain = softGain;   /* the rest is in the trigger volts */
        break;
    }
    case SC808_RS:
        /*
         * ONE VOICE, no switch: the sc808 rim, which beat two circuit
         * builds on hardware. Decay arrives as SECONDS of audible ring and
         * is converted here — sc808's envelope runs at curve -42, so the
         * part above 1% of peak is ln(100)/42 = 11% of the declared
         * duration.
         *
         * The band filters follow Tune. sc808 pins them at 63 and 118,
         * which is fine while the note never moves and sounds broken once
         * it does.
         */
        e->rs.trigger(0, lane_hz(voice, tune), decay * 9.12f,
                      (double)midicps(63.0f + tune),
                      (double)midicps(118.0f + tune));
        break;
    case SC808_CL:
    {
        e->clc.trigger(powf(2.0f, tune / 12.0f),
                       (float)e->pot[e->slot[voice].decay] / 127.0f,
                       volts);
        e->rt[voice].hit_gain = softGain;
        break;
    }
    case SC808_MA:
    {
        e->mac.trigger(powf(2.0f, tune / 12.0f), decay,
                       (float)e->pot[e->ma_attack] / 127.0f, volts);
        e->rt[voice].hit_gain = softGain;
        break;
    }
    case SC808_CP:
        /* Burst spacing and tail mix are the hardware's, fixed — the panel
         * has Tune and Decay, like the machine had Level alone. Decay
         * scales the two derived tails together; the burst spacing stays
         * the hardware's 10 ms. */
        e->cpc.trigger(powf(2.0f, tune / 12.0f), decay, 0.010f, 0.0f);
        break;
    case SC808_CB:
    {
        e->mbank.setRatio((double)tune);
        e->cbc.setRatio((double)tune);
        e->cbc.trigger(decay, volts);
        e->rt[voice].hit_gain = softGain;
        break;
    }
    case SC808_CH:
        /*
         * Velocity stays a GAIN on the hats, not a trigger voltage. A
         * hotter envelope into the diode gate makes the note LONGER and puts
         * a beat-wobble in the tail — measured 30% stretch at the top — and
         * every reference render keeps its length regardless of level. The
         * kick's trigger-volts treatment is right for the kick; here it made
         * the pads sound unlike the renders.
         */
        e->mbank.setRatio((double)tune);
        e->chc.trigger(decay, 4.0f);   /* the envelope that matches the references */
        break;
    case SC808_OH:
        /* velocity as gain, same reasoning as the closed hat */
        e->mbank.setRatio((double)tune);
        e->ohc.trigger(decay, 4.0f);
        break;
    case SC808_CY:
        /* velocity as gain, like the hats; no Tone — the crash balance is
         * the reference's, fixed in the voice */
        e->mbank.setRatio((double)tune);
        e->cyc.setRatio((double)tune);
        e->cyc.trigger(decay, 4.0f);
        break;
    default: break;
    }
}

/* One sample from one lane, through its own drive stage. */
static inline float voice_sample(sc808_engine *e, int v, float raw)
{
    VoiceRt &r = e->rt[v];
    if(r.choke_step < 0.0f)
    {
        r.choke_gain += r.choke_step;
        if(r.choke_gain <= 0.0f) { r.choke_gain = 0.0f; r.choke_step = 0.0f; }
    }
    if(r.choke_gain <= 0.0f) return 0.0f;

    const VoiceSlots &s = e->slot[v];
    /* Trim goes BEFORE the drive stage, not after.
     *
     * The lanes arrive 33 dB apart, so a trim applied after the shaper would
     * leave Drive meaning something different on every pad — the closed hat
     * slammed into the clipper and the hand clap barely touching it at the
     * same knob position. Trimming first makes Drive 64 mean the same thing
     * kit-wide, and it lets the diode stage do what a diode stage is for:
     * catching the spiky voices. The rim shot has a crest factor of 11, and
     * post-drive trimming would have had it peaking at 6.5 to sit level. */
    const float trimmed = raw * kVoiceTrim[v];
    const float shaped = sc808_shape_st(trimmed, e->potv[s.drive],
                                        e->env[s.dist], r.crush_st);
    return shaped * e->potv[s.level] * r.hit_gain * r.choke_gain;
}

/*
 * One lane into the dry mix and the two send buses.
 *
 * The sends are POST-FADER — taken from the sample after Drive, Level,
 * velocity and the choke — because what you hear is what you should send. A
 * pre-fader send would keep feeding the reverb from a lane you had just
 * turned down, which is not what a send knob means on any desk.
 */
static inline void sc808_add(sc808_engine *e, const int v, const float raw,
                             float *mix, float *send_r, float *send_d)
{
    const float sv = voice_sample(e, v, raw);
    *mix += sv;
    const VoiceSlots &s = e->slot[v];
    if(s.rev >= 0) *send_r += sv * e->potv[s.rev];
    if(s.dly >= 0) *send_d += sv * e->potv[s.dly];
}

/* A silent lane must not cost a process() call — the cymbal alone runs
 * eighteen biquads. Guarding on the gate gain also means a muted lane keeps
 * rendering until its fade completes, then disappears. */
#define SC808_LANE(vid, obj) \
    do { if(e->rt[vid].choke_gain > 0.0f && e->obj.active()) \
             sc808_add(e, vid, e->obj.process(), &mix, &send_r, &send_d); \
       } while(0)


void sc808_render(sc808_engine_t *e, float *out, int frames)
{
    const int   mdist  = e->env[e->e_master_dist];
    const float mdrive = e->potv[e->p_master_drive];
    const float vol    = e->potv[e->p_volume];
    const float comp   = e->potv[e->p_comp];

    for(int i = 0; i < frames; ++i)
    {
        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;
        SC808_LANE(SC808_BD, bdc);
        SC808_LANE(SC808_SD, sdc);
        SC808_LANE(SC808_LT, ltc);
        SC808_LANE(SC808_MT, mtc);
        SC808_LANE(SC808_HT, htc);
        SC808_LANE(SC808_LC, lcc);
        SC808_LANE(SC808_MC, mcc);
        SC808_LANE(SC808_HC, hcc);
        SC808_LANE(SC808_RS, rs);           /* the sc808 rim, see the trigger */
        SC808_LANE(SC808_CL, clc);
        SC808_LANE(SC808_MA, mac);
        SC808_LANE(SC808_CP, cpc);
        /*
         * The shared bank ticks ONCE per sample, always — free-running is
         * only true if the oscillators advance while nothing is sounding,
         * and a single tick keeps all four circuit metal voices reading the
         * same bus, as the hardware's one HD14584 does.
         */
        const double mbus = e->mbank.tick();
        SC808_LANE(SC808_CB, cbc);
        /* The three lanes that read the shared bus. */
#define SC808_METAL(vid, circ) \
        do { if(e->rt[vid].choke_gain > 0.0f && e->circ.active()) \
                 sc808_add(e, vid, e->circ.process(mbus), \
                           &mix, &send_r, &send_d); } while(0)
        SC808_METAL(SC808_CH, chc);
        SC808_METAL(SC808_OH, ohc);
        SC808_METAL(SC808_CY, cyc);
#undef SC808_METAL

        /*
         * The wet returns join the bus BEFORE the master stages, so master
         * distortion and the glue work on the whole picture rather than on a
         * dry kit with the FX bolted on afterwards.
         *
         * Both are ticked unconditionally, never branched around on a zero
         * input: a send turned down while a tail is still ringing has to let
         * that tail finish. Bit-identity with the sends at zero does not come
         * from a bypass — it comes from a silent-state tick fed exactly 0.0
         * returning exactly 0.0, which golden_check is what proves.
         */
        mix += sc808_verb_tick(&e->verb, send_r);
        mix += sc808_dly_tick(&e->dly, send_d, e->sample_rate);

        /* Master stage. Option 0 is Off, so the kit can be left alone. */
        if(mdist > 0) mix = sc808_shape_st(mix, mdrive, mdist - 1, e->crush_master);
        /* Glue after the distortion, before the volume — and skipped entirely
         * at zero, which is the default, so it cannot colour a kit nobody
         * asked it to touch. */
        if(comp > 0.001f) mix = sc808_glue_tick(&e->glue, mix, comp, e->sample_rate);
        mix *= vol;

        if(!(mix > -8.0f && mix < 8.0f)) mix = 0.0f;   /* also catches NaN */
        out[i] = mix;
    }
}

/* ---- parameters ------------------------------------------------------- */

int sc808_set_param(sc808_engine_t *e, const char *key, const char *val)
{
    /* Tempo for the synced delay. A raw key: it lives on no page and in no
     * pot table, because it is the host's business and not the player's. */
    if(!strcmp(key, "dly_bpm"))
    {
        const float bpm = (float)atof(val);
        if(bpm > 20.0f) { e->dly.bpm = bpm; sc808_fx_sync(e); }
        return 1;
    }

    /* "default" resets a control to its sc808 default. Those defaults are not
     * pot centre — they are the arguments the SynthDefs declare — so a UI
     * that wants a reset gesture must not guess 64; it asks. */
    const int reset = !strcmp(val, "default");
    const int slot = find_pot(key);
    if(slot >= 0)
    {
        int p = reset ? g_sc808_pots[slot].def : (int)(atof(val) + 0.5f);
        if(p < 0) p = 0;
        if(p > 127) p = 127;                 /* clamp, never wrap */
        e->pot[slot]  = p;
        e->potv[slot] = pot_value(slot, p);
        sc808_fx_sync(e);
        return 1;
    }
    const int es = find_enum(key);
    if(es >= 0)
    {
        int v = reset ? g_sc808_enums[es].def : (int)(atof(val) + 0.5f);
        if(v < 0) v = 0;
        if(v >= g_sc808_enums[es].count) v = g_sc808_enums[es].count - 1;
        e->env[es] = v;
        sc808_fx_sync(e);
        return 1;
    }
    return 0;
}

int sc808_get_param(sc808_engine_t *e, const char *key, char *buf, int len)
{
    const int slot = find_pot(key);
    if(slot >= 0) return snprintf(buf, len, "%d", e->pot[slot]);
    const int es = find_enum(key);
    if(es >= 0)   return snprintf(buf, len, "%d", e->env[es]);
    return -1;
}

int sc808_serialize(const sc808_engine_t *e, char *buf, int len)
{
    int n = snprintf(buf, len, "{\"v\":%d,\"pots\":[", SC808_STATE_VERSION);
    for(int i = 0; i < SC808_NUM_POTS && n < len; ++i)
        n += snprintf(buf + n, len - n, i ? ",%d" : "%d", e->pot[i]);
    if(n < len) n += snprintf(buf + n, len - n, "],\"enums\":[");
    for(int i = 0; i < SC808_NUM_ENUMS && n < len; ++i)
        n += snprintf(buf + n, len - n, i ? ",%d" : "%d", e->env[i]);
    if(n < len) n += snprintf(buf + n, len - n, "],\"mutes\":%u}", e->mutes);
    return n;
}

/* ---- state migration: the v1 storage order, frozen -------------------- *
 *
 * The pot and enum tables are POSITIONAL storage for the patch blob, so a
 * control removed from the middle renumbers everything after it: a v1 blob
 * replayed against the v2 table would land bd_engine's value in
 * bd_dist_type, and so on down the kit. Sixteen Engine switches and the
 * Metal enum left at once, which is the largest such move this module will
 * ever make.
 *
 * So v1 blobs are placed BY NAME against the order that shipped with them,
 * and keys that no longer exist have nowhere to land and are dropped. From
 * v2 on the tables are positional again and APPEND-ONLY.
 */
static const char *const kV1PotKeys[71] = {
    "bd_tune", "bd_attack", "bd_decay", "bd_tone",
    "bd_drive", "bd_level", "sd_tune", "sd_decay",
    "sd_snappy", "sd_drive", "sd_level", "lt_tune",
    "lt_decay", "lt_drive", "lt_level", "mt_tune",
    "mt_decay", "mt_drive", "mt_level", "ht_tune",
    "ht_decay", "ht_drive", "ht_level", "lc_tune",
    "lc_decay", "lc_drive", "lc_level", "mc_tune",
    "mc_decay", "mc_drive", "mc_level", "hc_tune",
    "hc_decay", "hc_drive", "hc_level", "rs_tune",
    "rs_decay", "rs_drive", "rs_level", "cl_tune",
    "cl_decay", "cl_drive", "cl_level", "ma_tune",
    "ma_attack", "ma_decay", "ma_drive", "ma_level",
    "cp_tune", "cp_decay", "cp_drive", "cp_level",
    "cb_tune", "cb_decay", "cb_drive", "cb_level",
    "ch_tune", "ch_decay", "ch_drive", "ch_level",
    "oh_tune", "oh_decay", "oh_drive", "oh_level",
    "cy_tune", "cy_decay", "cy_drive", "cy_level",
    "master_drive", "volume", "accent",
};

static const char *const kV1EnumKeys[35] = {
    "bd_engine", "bd_dist_type", "sd_engine", "sd_dist_type",
    "lt_engine", "lt_dist_type", "mt_engine", "mt_dist_type",
    "ht_engine", "ht_dist_type", "lc_engine", "lc_dist_type",
    "mc_engine", "mc_dist_type", "hc_engine", "hc_dist_type",
    "rs_dist_type", "cl_engine", "cl_dist_type", "ma_engine",
    "ma_dist_type", "cp_engine", "cp_dist_type", "cb_engine",
    "cb_dist_type", "ch_engine", "ch_dist_type", "hh_choke",
    "oh_engine", "oh_dist_type", "cy_engine", "cy_dist_type",
    "master_dist", "note_map", "metal_run",
};

/* Reads the arrays positionally. A blob shorter than the current table is a
 * patch saved before a control was appended — the missing tail keeps its
 * default rather than reading garbage. */
static const char *scan_ints(const char *p, int *dst, int max, int *got)
{
    *got = 0;
    if(!p) return NULL;
    p = strchr(p, '[');
    if(!p) return NULL;
    ++p;
    while(*p && *p != ']' && *got < max)
    {
        while(*p == ' ' || *p == ',') ++p;
        if(*p == ']' || !*p) break;
        char *end = NULL;
        dst[(*got)++] = (int)strtol(p, &end, 10);
        if(end == p) break;      /* not a number: stop, do not spin */
        p = end;
    }
    const char *end = strchr(p, ']');
    return end ? end + 1 : NULL;
}

void sc808_deserialize(sc808_engine_t *e, const char *json)
{
    if(!json || !*json) return;

    /* Blobs carry their version; anything without one is a v1 blob from
     * before the field existed. */
    int version = 1;
    {
        const char *vp = strstr(json, "\"v\"");
        if(vp) { vp = strchr(vp, ':'); if(vp) version = (int)strtol(vp + 1, NULL, 10); }
    }
    /* Pots: the table did not move until v4, so one frozen order serves
     * every blob written before it. Enums: the table moved at v2. */
    const bool potsByName  = version < 4;
    const bool enumsByName = version < 2;
    /* Old menu position -> new, for the four-type drive stage. Index is the
     * stored value; the master's list carries a leading "Off" so it shifts
     * by one. */
    static const int kDistV2toV3[4]   = { 0, 1, 5, 6 };
    static const int kMDistV2toV3[5]  = { 0, 1, 2, 6, 7 };
    const bool remapDist = version < 3;

    const char *p = strstr(json, "\"pots\"");
    enum { kScratch = (SC808_NUM_POTS > SC808_NUM_ENUMS ? SC808_NUM_POTS
                                                        : SC808_NUM_ENUMS) };
    const int kV1Pots  = (int)(sizeof kV1PotKeys  / sizeof kV1PotKeys[0]);
    const int kV1Enums = (int)(sizeof kV1EnumKeys / sizeof kV1EnumKeys[0]);
    int vals[kScratch > kV1Pots ? (kScratch > kV1Enums ? kScratch : kV1Enums)
                                : (kV1Pots > kV1Enums ? kV1Pots : kV1Enums)];
    int got = 0;

    p = scan_ints(p, vals, potsByName ? kV1Pots : SC808_NUM_POTS, &got);
    for(int i = 0; i < got; ++i)
    {
        const int v = vals[i] < 0 ? 0 : (vals[i] > 127 ? 127 : vals[i]);
        /* Older blob: the value belongs to whatever key sat at this index
         * then, which may now live somewhere else or nowhere at all —
         * "accent" is exactly that, and it simply has nowhere to land. */
        const int slot = potsByName ? (i < kV1Pots ? find_pot(kV1PotKeys[i]) : -1) : i;
        if(slot < 0 || slot >= SC808_NUM_POTS) continue;
        e->pot[slot]  = v;
        e->potv[slot] = pot_value(slot, v);
    }

    const char *q = strstr(json, "\"enums\"");
    scan_ints(q, vals, enumsByName ? kV1Enums : SC808_NUM_ENUMS, &got);
    for(int i = 0; i < got; ++i)
    {
        const int slot = enumsByName ? (i < kV1Enums ? find_enum(kV1EnumKeys[i]) : -1) : i;
        if(slot < 0 || slot >= SC808_NUM_ENUMS) continue;
        int v = vals[i] < 0 ? 0 : vals[i];
        if(remapDist)
        {
            const char *k = g_sc808_enums[slot].key;
            const size_t kl = strlen(k);
            if(kl > 10 && !strcmp(k + kl - 10, "_dist_type"))
            { if(v < 4) v = kDistV2toV3[v]; }
            else if(!strcmp(k, "master_dist"))
            { if(v < 5) v = kMDistV2toV3[v]; }
        }
        if(v >= g_sc808_enums[slot].count) v = g_sc808_enums[slot].count - 1;
        e->env[slot] = v;
    }
    sc808_fx_sync(e);

    const char *mp = strstr(json, "\"mutes\"");
    if(mp) { mp = strchr(mp, ':'); if(mp) e->mutes = (unsigned)strtoul(mp + 1, NULL, 10)
                                                     & ((1u << SC808_NUM_VOICES) - 1u); }
}

/* Read-only view of the kit balance, for tools/kit_check. Not part of the
 * plugin surface. */
extern "C" const float *sc808_debug_trim(void) { return kVoiceTrim; }
