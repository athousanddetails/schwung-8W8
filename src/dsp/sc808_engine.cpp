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
#include "sc808_shape.h"

using namespace sc808;

#define SC808_STATE_VERSION 1

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
    0.2851f,   /* bd — the reference: everything else is set against the kick */
    0.1879f,   /* sd */
    0.3954f,   /* lt */
    0.4039f,   /* mt */
    0.3767f,   /* ht */
    0.3607f,   /* lc */
    0.3751f,   /* mc */
    0.3697f,   /* hc */
    0.2542f,   /* rs — a click with a crest factor of 11 */
    2.5014f,   /* cl */
    0.5908f,   /* ma */
    3.8128f,   /* cp — the quietest voice in sc808 by a long way */
    0.1163f,   /* cb */
    0.0014f,   /* ch — raw peak near 17 before the drive stage catches it */
    0.0023f,   /* oh */
    1.0217f,   /* cy */
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
};

struct VoiceRt {
    float hit_gain;      /* this hit's accent scale             */
    float choke_gain;    /* 1.0 normally, ramps to 0 on a choke */
    float choke_step;    /* < 0 while choking, else 0           */
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
    int ma_attack, cy_tone;
    /* Globals. */
    int e_master_dist, e_choke, e_note_map, e_bd_engine, e_metal_run, e_sd_engine;
    int e_cp_engine;
    /* lt mt ht lc mc hc, then cl ma — the lanes SC808_TOM_LANE serves.
     * The rim shot left this list when its switch went. */
    int e_tom_engine[8];
    int e_cl_engine, e_ma_engine, e_cb_engine;
    int e_ch_engine, e_oh_engine, e_cy_engine;
    int p_master_drive, p_volume, p_accent;

    unsigned mutes;

    BassDrum        bd;    /* the sc808 transcription */
    CircuitBassDrum bdc;   /* the bridged-T circuit   */
    Snare           sd;    /* the sc808 transcription */
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
    Tom       lt, mt, ht, lc, mc, hc;
    RimClave  rs;
    RimClave  cl;          /* the same circuit, mode fixed to Clave */
    Maracas   ma;
    Clap      cp;
    Cowbell   cb;
    ClosedHat ch;
    OpenHat   oh;
    Cymbal    cy;
};

/* Push the metal_run enum down into the three voices that care. Called at
 * create and whenever the enum is written, so idle() below never acts on a
 * stale flag. */
static void apply_metal_run(sc808_engine_t *e)
{
    const bool freeRun = e->env[e->e_metal_run] == 0;
    e->ch.setFreeRun(freeRun);
    e->oh.setFreeRun(freeRun);
    e->cy.setFreeRun(freeRun);
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
        e->rt[v].hit_gain   = 1.0f;
        e->rt[v].choke_gain = 1.0f;
        e->rt[v].choke_step = 0.0f;
    }
    e->bd_attack = find_pot("bd_attack");
    e->bd_tone   = find_pot("bd_tone");
    e->sd_snappy = find_pot("sd_snappy");
    e->ma_attack = find_pot("ma_attack");
    e->cy_tone   = find_pot("cy_tone");
    e->p_master_drive = find_pot("master_drive");
    e->p_volume       = find_pot("volume");
    e->p_accent       = find_pot("accent");
    e->e_master_dist  = find_enum("master_dist");
    e->e_choke        = find_enum("hh_choke");
    e->e_note_map     = find_enum("note_map");
    e->e_bd_engine    = find_enum("bd_engine");
    e->e_metal_run    = find_enum("metal_run");
    e->e_sd_engine    = find_enum("sd_engine");
    e->e_cp_engine    = find_enum("cp_engine");
    {
        static const char *tomIds[6] = { "lt", "mt", "ht", "lc", "mc", "hc" };
        for(int i = 0; i < 6; ++i)
        {
            char k[32];
            snprintf(k, sizeof k, "%s_engine", tomIds[i]);
            e->e_tom_engine[i] = find_enum(k);
        }
    }
    e->e_cl_engine = e->e_tom_engine[6] = find_enum("cl_engine");
    e->e_ma_engine = e->e_tom_engine[7] = find_enum("ma_engine");
    e->e_cb_engine = find_enum("cb_engine");
    e->e_ch_engine = find_enum("ch_engine");
    e->e_oh_engine = find_enum("oh_engine");
    e->e_cy_engine = find_enum("cy_engine");

    const double sr = e->sample_rate;
    e->bd.init(sr);
    /* No lookahead limiter on the kick: 20 ms of latency and 23 dB of
     * squash, neither of which belongs in a drum machine. See BassDrum. */
    e->bd.setLimiter(false);
    e->bdc.init(sr);
    e->sd.init(sr);
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
    e->lt.init(sr); e->mt.init(sr); e->ht.init(sr);
    e->lc.init(sr); e->mc.init(sr); e->hc.init(sr);
    e->rs.init(sr); e->cl.init(sr); e->ma.init(sr); e->cp.init(sr); e->cb.init(sr);
    e->ch.init(sr); e->oh.init(sr); e->cy.init(sr);
    apply_metal_run(e);
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

    const float accent = velocity >= SC808_ACCENT_VELOCITY
                       ? e->potv[e->p_accent] : 1.0f;
    e->rt[voice].hit_gain   = accent;
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
        if(e->env[e->e_bd_engine] == 0)
        {
            /*
             * The circuit kick. Its three shared pots mean something else
             * here, so they are read as raw POT POSITIONS rather than through
             * the sc808 mapping — Decay is loop gain in 0..1, not seconds,
             * and mapping 0.1..8 s onto a loop gain would be meaningless.
             * One knob, two engines, each reading the position its own way.
             *
             * Accent is a TRIGGER VOLTAGE here, 4 V to 14 V as the hardware's
             * VR3 delivers, because on this circuit accent is not a gain: the
             * trigger drives a diode and a harder hit is a different sound,
             * not a louder one.
             */
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->bdc.trigger(lane_hz(voice, tune),
                           (float)e->pot[e->slot[voice].decay] / 127.0f,
                           (float)e->pot[e->bd_tone]   / 127.0f,
                           (float)e->pot[e->bd_attack] / 127.0f,
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            /* The accent is already in the trigger voltage; do not also
             * multiply the lane by it. */
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            e->bd.trigger(lane_hz(voice, tune), e->potv[e->bd_attack],
                          decay, e->potv[e->bd_tone]);
        }
        break;
    case SC808_SD:
        if(e->env[e->e_sd_engine] == 0)
        {
            /* The circuit snare. Its shared pots mean different things here,
             * so they are read as raw POT POSITIONS: Tone is the balance
             * between two bridged-T shells rather than a filter corner, and
             * Snappy is a divider on the trigger rather than a mix. Tune is
             * a ratio on both shells' component-derived frequencies. */
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            /* Shell balance fixed at centre — the Tone pot is gone ("Tune
             * is enough"), and centre is where its default sat. */
            e->sdc.trigger(powf(2.0f, tune / 12.0f),
                           (float)e->pot[e->slot[voice].decay] / 127.0f,
                           0.5f,
                           (float)e->pot[e->sd_snappy] / 127.0f,
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;   /* accent is in the trigger volts */
        }
        else
        {
            /* detune -11 semitones on the second shell oscillator; noise
             * highpass at sc808's 93, lowpass fixed at its old default 121
             * now that the Tone pot is gone. */
            e->sd.trigger(lane_hz(voice, tune),
                          lane_hz(voice, tune - 11.0f),
                          decay, e->potv[e->sd_snappy],
                          (double)midicps(93.0f),
                          (double)midicps(121.0f),
                          0.999f);
        }
        break;
    /*
     * The six tom / conga lanes, each with two engines.
     *
     * On the circuit side Decay is LOOP GAIN, so it is read as a raw pot
     * position exactly as the kick and snare read theirs, and accent is a
     * TRIGGER VOLTAGE rather than a gain — the strike is what changes, so the
     * lane must not also be multiplied by the accent afterwards.
     */
    case SC808_LT: case SC808_MT: case SC808_HT:
    case SC808_LC: case SC808_MC: case SC808_HC:
    {
        const int i = voice - SC808_LT;
        if(e->env[e->e_tom_engine[i]] == 0)
        {
            TomCircuit *const tc[6] = { &e->ltc, &e->mtc, &e->htc,
                                        &e->lcc, &e->mcc, &e->hcc };
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            /* Decay is RING TIME IN SECONDS — the circuit solves the loop
             * gain that produces it at the current pitch. */
            tc[i]->trigger(lane_hz(voice, tune), decay,
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;   /* accent is in the trigger volts */
        }
        else
        {
            static const TomSpec *const spec[6] = { &kTomLo,   &kTomMid,  &kTomHi,
                                                    &kCongaLo, &kCongaMid, &kCongaHi };
            Tom *const t[6] = { &e->lt, &e->mt, &e->ht, &e->lc, &e->mc, &e->hc };
            /*
             * One knob, both engines, one meaning: seconds of audible ring.
             * sc808's envelope runs at curve -250, where the audible part is
             * the first ln(100)/250 = 1.8% of the declared duration — so its
             * "20 seconds" was always about 0.37 s of tom. 54 converts real
             * seconds into the seconds that envelope wants.
             */
            t[i]->trigger(*spec[i], lane_hz(voice, tune), decay * 54.0f);
        }
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
        if(e->env[e->e_cl_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->clc.trigger(powf(2.0f, tune / 12.0f),
                           (float)e->pot[e->slot[voice].decay] / 127.0f,
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            e->cl.trigger(1, lane_hz(voice, tune),
                          0.01f + ((float)e->pot[e->slot[voice].decay] / 127.0f) * 0.24f,
                          0.0, 0.0);
        }
        break;
    case SC808_MA:
        if(e->env[e->e_ma_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->mac.trigger(powf(2.0f, tune / 12.0f), decay,
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            /* sc808's decay envelope reads ~1.8x long against the meter */
            e->ma.trigger(lane_hz(voice, tune), e->potv[e->ma_attack],
                          decay * 1.8f);
        }
        break;
    case SC808_CP:
        if(e->env[e->e_cp_engine] == 0)
        {
            /* Burst spacing and tail mix are the hardware's, fixed — the
             * panel has Tune and Decay, like the machine had Level alone. */
            /* Decay scales the two derived tails together; the burst
             * spacing stays the hardware's 10 ms. */
            e->cpc.trigger(powf(2.0f, tune / 12.0f), decay, 0.010f, 0.0f);
        }
        else
        {
            /* hpf note 71, bandpass note 84; Tune moves both together.
             * Spread fixed at sc808's own 26 ms, rev at its default 1. */
            e->cp.trigger(lane_hz(voice, tune),
                          (double)midicps(84.0f + tune),
                          0.5f, decay, 0.026f, 1.0f);
        }
        break;
    case SC808_CB:
        if(e->env[e->e_cb_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->mbank.setRatio((double)tune);
            e->cbc.setRatio((double)tune);
            e->cbc.trigger(decay, av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            /* seconds -> sc808's envPerc argument: its curve makes the
             * audible part about 1/29th of the declared duration */
            e->cb.trigger((double)tune, decay * 29.0f,
                          (double)midicps(59.0f), (double)midicps(109.0f));
        }
        break;
    case SC808_CH:
        if(e->env[e->e_ch_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->mbank.setRatio((double)tune);
            e->chc.trigger(decay, av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            e->ch.trigger((double)tune, decay * 5.9f,
                          (double)midicps(121.25219487074914f),
                          (double)midicps(121.05875888638981f));
        }
        break;
    case SC808_OH:
        if(e->env[e->e_oh_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->mbank.setRatio((double)tune);
            e->ohc.trigger(decay, av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            e->oh.trigger((double)tune, decay * 0.925f,
                          (double)midicps(118.551f), (double)midicps(107.213f));
        }
        break;
    case SC808_CY:
        if(e->env[e->e_cy_engine] == 0)
        {
            const float av = 4.0f + 10.0f * (accent - 1.0f) / 3.0f;
            e->mbank.setRatio((double)tune);
            e->cyc.trigger(decay, e->potv[e->cy_tone],
                           av < 4.0f ? 4.0f : (av > 14.0f ? 14.0f : av));
            e->rt[voice].hit_gain = 1.0f;
        }
        else
        {
            e->cy.trigger((double)tune, decay * 1.453f, e->potv[e->cy_tone]);
        }
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
    const float shaped = sc808_shape(trimmed, e->potv[s.drive], e->env[s.dist]);
    return shaped * e->potv[s.level] * r.hit_gain * r.choke_gain;
}

/* A silent lane must not cost a process() call — the cymbal alone runs
 * eighteen biquads. Guarding on the gate gain also means a muted lane keeps
 * rendering until its fade completes, then disappears. */
#define SC808_LANE(vid, obj) \
    do { if(e->rt[vid].choke_gain > 0.0f && e->obj.active()) \
             mix += voice_sample(e, vid, e->obj.process()); } while(0)

/* Same, for a lane with two engines behind a switch. */
#define SC808_TOM_LANE(vid, idx, circ, sc) \
    do { if(e->rt[vid].choke_gain > 0.0f) { \
             if(e->env[e->e_tom_engine[idx]] == 0) { \
                 if(e->circ.active()) mix += voice_sample(e, vid, e->circ.process()); \
             } else if(e->sc.active()) { \
                 mix += voice_sample(e, vid, e->sc.process()); \
             } } } while(0)

void sc808_render(sc808_engine_t *e, float *out, int frames)
{
    const int   mdist  = e->env[e->e_master_dist];
    const float mdrive = e->potv[e->p_master_drive];
    const float vol    = e->potv[e->p_volume];

    for(int i = 0; i < frames; ++i)
    {
        float mix = 0.0f;
        if(e->rt[SC808_BD].choke_gain > 0.0f)
        {
            if(e->env[e->e_bd_engine] == 0)
            { if(e->bdc.active()) mix += voice_sample(e, SC808_BD, e->bdc.process()); }
            else
            { if(e->bd.active())  mix += voice_sample(e, SC808_BD, e->bd.process()); }
        }
        if(e->rt[SC808_SD].choke_gain > 0.0f)
        {
            if(e->env[e->e_sd_engine] == 0)
            { if(e->sdc.active()) mix += voice_sample(e, SC808_SD, e->sdc.process()); }
            else
            { if(e->sd.active())  mix += voice_sample(e, SC808_SD, e->sd.process()); }
        }
        SC808_TOM_LANE(SC808_LT, 0, ltc, lt);
        SC808_TOM_LANE(SC808_MT, 1, mtc, mt);
        SC808_TOM_LANE(SC808_HT, 2, htc, ht);
        SC808_TOM_LANE(SC808_LC, 3, lcc, lc);
        SC808_TOM_LANE(SC808_MC, 4, mcc, mc);
        SC808_TOM_LANE(SC808_HC, 5, hcc, hc);
        SC808_LANE(SC808_RS, rs);           /* one voice, see the trigger */
        SC808_TOM_LANE(SC808_CL, 6, clc, cl);
        SC808_TOM_LANE(SC808_MA, 7, mac, ma);
        if(e->rt[SC808_CP].choke_gain > 0.0f)
        {
            if(e->env[e->e_cp_engine] == 0)
            { if(e->cpc.active()) mix += voice_sample(e, SC808_CP, e->cpc.process()); }
            else
            { if(e->cp.active())  mix += voice_sample(e, SC808_CP, e->cp.process()); }
        }
        /*
         * The shared bank ticks ONCE per sample, always — free-running is
         * only true if the oscillators advance while nothing is sounding,
         * and a single tick keeps all four circuit metal voices reading the
         * same bus, as the hardware's one HD14584 does.
         */
        const double mbus = e->mbank.tick();
        if(e->rt[SC808_CB].choke_gain > 0.0f)
        {
            if(e->env[e->e_cb_engine] == 0)
            { if(e->cbc.active()) mix += voice_sample(e, SC808_CB, e->cbc.process()); }
            else if(e->cb.active())
            { mix += voice_sample(e, SC808_CB, e->cb.process()); }
        }
        /*
         * The metal lanes, with their oscillator banks free-running.
         *
         * A lane that is not sounding still has to advance its bank, or
         * "free-running" only means "free-running while you can hear it" and
         * every hit lands on the same phase after all — which is the whole
         * thing this is here to avoid. idle() is six naive pulse oscillators
         * and no filters, so a silent lane costs almost nothing.
         */
#define SC808_METAL(vid, idx_enum, circ, sc) \
        do { if(e->env[idx_enum] == 0) { \
                 e->sc.idle(); \
                 if(e->rt[vid].choke_gain > 0.0f && e->circ.active()) \
                     mix += voice_sample(e, vid, e->circ.process(mbus)); \
             } else if(e->rt[vid].choke_gain > 0.0f && e->sc.active()) { \
                 mix += voice_sample(e, vid, e->sc.process()); \
             } else e->sc.idle(); } while(0)
        SC808_METAL(SC808_CH, e->e_ch_engine, chc, ch);
        SC808_METAL(SC808_OH, e->e_oh_engine, ohc, oh);
        SC808_METAL(SC808_CY, e->e_cy_engine, cyc, cy);
#undef SC808_METAL

        /* Master stage. Option 0 is Off, so the kit can be left alone. */
        if(mdist > 0) mix = sc808_shape(mix, mdrive, mdist - 1);
        mix *= vol;

        if(!(mix > -8.0f && mix < 8.0f)) mix = 0.0f;   /* also catches NaN */
        out[i] = mix;
    }
}

/* ---- parameters ------------------------------------------------------- */

int sc808_set_param(sc808_engine_t *e, const char *key, const char *val)
{
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
        return 1;
    }
    const int es = find_enum(key);
    if(es >= 0)
    {
        int v = reset ? g_sc808_enums[es].def : (int)(atof(val) + 0.5f);
        if(v < 0) v = 0;
        if(v >= g_sc808_enums[es].count) v = g_sc808_enums[es].count - 1;
        e->env[es] = v;
        if(es == e->e_metal_run) apply_metal_run(e);
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
    const char *p = strstr(json, "\"pots\"");
    int vals[SC808_NUM_POTS > SC808_NUM_ENUMS ? SC808_NUM_POTS : SC808_NUM_ENUMS];
    int got = 0;

    p = scan_ints(p, vals, SC808_NUM_POTS, &got);
    for(int i = 0; i < got; ++i)
    {
        int v = vals[i] < 0 ? 0 : (vals[i] > 127 ? 127 : vals[i]);
        e->pot[i]  = v;
        e->potv[i] = pot_value(i, v);
    }

    const char *q = strstr(json, "\"enums\"");
    scan_ints(q, vals, SC808_NUM_ENUMS, &got);
    for(int i = 0; i < got; ++i)
    {
        int v = vals[i] < 0 ? 0 : vals[i];
        if(v >= g_sc808_enums[i].count) v = g_sc808_enums[i].count - 1;
        e->env[i] = v;
    }
    apply_metal_run(e);

    const char *mp = strstr(json, "\"mutes\"");
    if(mp) { mp = strchr(mp, ':'); if(mp) e->mutes = (unsigned)strtoul(mp + 1, NULL, 10)
                                                     & ((1u << SC808_NUM_VOICES) - 1u); }
}

/* Read-only view of the kit balance, for tools/kit_check. Not part of the
 * plugin surface. */
extern "C" const float *sc808_debug_trim(void) { return kVoiceTrim; }
