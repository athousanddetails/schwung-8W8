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
#include "sc808_engine.h"
#include "sc808_params.h"
#include "sc808_shape.h"

using namespace sc808;

#define SC808_STATE_VERSION 1

/* A choke is a 2 ms fade, not a hard stop — cutting a ringing open hat dead
 * puts a click on the front of the closed hat that follows it. */
static const float kChokeSeconds = 0.002f;

namespace {

const char *kVoiceIds[SC808_NUM_VOICES] = {
    "bd", "sd", "lt", "mt", "ht", "lc", "mc", "hc",
    "rs", "ma", "cp", "cb", "ch", "oh", "cy"
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
    40.0f,   /* lt */
    44.0f,   /* mt */
    52.0f,   /* ht */
    52.0f,   /* lc */
    57.0f,   /* mc */
    62.0f,   /* hc — see above */
    92.0f,   /* rs */
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
 * Fifteen of those summed is not a drum machine, it is a fault.
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
const float kVoiceTrim[SC808_NUM_VOICES] = {
    0.3122f,   /* bd — the reference: everything else is set against the kick */
    0.1461f,   /* sd */
    0.1041f,   /* lt */
    0.1061f,   /* mt */
    0.1111f,   /* ht */
    0.0921f,   /* lc */
    0.0921f,   /* mc */
    0.0921f,   /* hc */
    0.1651f,   /* rs — a click with a crest factor of 11 */
    0.0650f,   /* ma */
    0.6265f,   /* cp — the quietest voice in sc808 by a long way */
    0.1451f,   /* cb */
    0.0080f,   /* ch — raw peak near 17 before the drive stage catches it */
    0.1941f,   /* oh */
    0.1701f,   /* cy */
};

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
    int bd_attack, bd_tone, sd_snappy, sd_tone;
    int ma_attack, cp_spread, cp_room, cy_tone;
    /* Globals. */
    int e_master_dist, e_choke, e_note_map, e_rs_mode, e_bd_engine;
    int p_master_drive, p_volume, p_accent;

    unsigned mutes;

    BassDrum        bd;    /* the sc808 transcription */
    CircuitBassDrum bdc;   /* the bridged-T circuit   */
    Snare     sd;
    Tom       lt, mt, ht, lc, mc, hc;
    RimClave  rs;
    Maracas   ma;
    Clap      cp;
    Cowbell   cb;
    ClosedHat ch;
    OpenHat   oh;
    Cymbal    cy;
};

const char *sc808_voice_id(int voice)
{
    return (voice >= 0 && voice < SC808_NUM_VOICES) ? kVoiceIds[voice] : "";
}

sc808_engine_t *sc808_create(float sample_rate)
{
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
    e->sd_tone   = find_pot("sd_tone");
    e->ma_attack = find_pot("ma_attack");
    e->cp_spread = find_pot("cp_spread");
    e->cp_room   = find_pot("cp_room");
    e->cy_tone   = find_pot("cy_tone");
    e->p_master_drive = find_pot("master_drive");
    e->p_volume       = find_pot("volume");
    e->p_accent       = find_pot("accent");
    e->e_master_dist  = find_enum("master_dist");
    e->e_choke        = find_enum("hh_choke");
    e->e_note_map     = find_enum("note_map");
    e->e_rs_mode      = find_enum("rs_mode");
    e->e_bd_engine    = find_enum("bd_engine");

    const double sr = e->sample_rate;
    e->bd.init(sr);
    /* No lookahead limiter on the kick: 20 ms of latency and 23 dB of
     * squash, neither of which belongs in a drum machine. See BassDrum. */
    e->bd.setLimiter(false);
    e->bdc.init(sr);
    e->sd.init(sr);
    e->lt.init(sr); e->mt.init(sr); e->ht.init(sr);
    e->lc.init(sr); e->mc.init(sr); e->hc.init(sr);
    e->rs.init(sr); e->ma.init(sr); e->cp.init(sr); e->cb.init(sr);
    e->ch.init(sr); e->oh.init(sr); e->cy.init(sr);
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
static inline double lane_hz(sc808_engine_t *e, int v, float offset)
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
    if(voice == SC808_CH && choke >= 1) choke_voice(e, SC808_OH);
    if(voice == SC808_OH && choke == 2) choke_voice(e, SC808_CH);

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
            e->bdc.trigger(lane_hz(e, voice, tune),
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
            e->bd.trigger(lane_hz(e, voice, tune), e->potv[e->bd_attack],
                          decay, e->potv[e->bd_tone]);
        }
        break;
    case SC808_SD:
        /* detune -11 semitones on the second shell oscillator; the noise
         * highpass stays at sc808's 93 while Tone moves the lowpass. */
        e->sd.trigger(lane_hz(e, voice, tune),
                      lane_hz(e, voice, tune - 11.0f),
                      decay, e->potv[e->sd_snappy],
                      (double)midicps(93.0f),
                      (double)midicps(e->potv[e->sd_tone]),
                      0.999f);
        break;
    case SC808_LT: e->lt.trigger(kTomLo,    lane_hz(e, voice, tune), decay); break;
    case SC808_MT: e->mt.trigger(kTomMid,   lane_hz(e, voice, tune), decay); break;
    case SC808_HT: e->ht.trigger(kTomHi,    lane_hz(e, voice, tune), decay); break;
    case SC808_LC: e->lc.trigger(kCongaLo,  lane_hz(e, voice, tune), decay); break;
    case SC808_MC: e->mc.trigger(kCongaMid, lane_hz(e, voice, tune), decay); break;
    case SC808_HC: e->hc.trigger(kCongaHi,  lane_hz(e, voice, tune), decay); break;
    case SC808_RS:
        /* The rim shot's band filters follow Tune. sc808 pins them at 63 and
         * 118, which is fine when the note never moves and sounds broken once
         * it does — a rim shot tuned down an octave through a fixed 311 Hz
         * highpass is all click and no body. */
        e->rs.trigger(e->env[e->e_rs_mode], lane_hz(e, voice, tune), decay,
                      (double)midicps(63.0f + tune),
                      (double)midicps(118.0f + tune));
        break;
    case SC808_MA:
        e->ma.trigger(lane_hz(e, voice, tune), e->potv[e->ma_attack], decay);
        break;
    case SC808_CP:
        /* hpf note 71, bandpass note 84; Tune moves both together. */
        e->cp.trigger(lane_hz(e, voice, tune),
                      (double)midicps(84.0f + tune),
                      0.5f, decay, e->potv[e->cp_spread], e->potv[e->cp_room]);
        break;
    case SC808_CB:
        e->cb.trigger((double)tune, decay,
                      (double)midicps(59.0f), (double)midicps(109.0f));
        break;
    case SC808_CH:
        e->ch.trigger((double)tune, decay,
                      (double)midicps(121.25219487074914f),
                      (double)midicps(121.05875888638981f));
        break;
    case SC808_OH:
        e->oh.trigger((double)tune, decay,
                      (double)midicps(118.551f), (double)midicps(107.213f));
        break;
    case SC808_CY:
        e->cy.trigger((double)tune, decay, e->potv[e->cy_tone]);
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
        SC808_LANE(SC808_SD, sd);
        SC808_LANE(SC808_LT, lt);
        SC808_LANE(SC808_MT, mt);
        SC808_LANE(SC808_HT, ht);
        SC808_LANE(SC808_LC, lc);
        SC808_LANE(SC808_MC, mc);
        SC808_LANE(SC808_HC, hc);
        SC808_LANE(SC808_RS, rs);
        SC808_LANE(SC808_MA, ma);
        SC808_LANE(SC808_CP, cp);
        SC808_LANE(SC808_CB, cb);
        SC808_LANE(SC808_CH, ch);
        SC808_LANE(SC808_OH, oh);
        SC808_LANE(SC808_CY, cy);

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

    const char *mp = strstr(json, "\"mutes\"");
    if(mp) { mp = strchr(mp, ':'); if(mp) e->mutes = (unsigned)strtoul(mp + 1, NULL, 10)
                                                     & ((1u << SC808_NUM_VOICES) - 1u); }
}

/* Read-only view of the kit balance, for tools/kit_check. Not part of the
 * plugin surface. */
extern "C" const float *sc808_debug_trim(void) { return kVoiceTrim; }
