/*
 * sc808_voices.h — Engine A: sc808.scd, voice by voice, in C++.
 *
 * Each class below is one SynthDef from src/vendor/sc808/sc808.scd, kept in
 * the same order and using the same variable names as the original so the two
 * can be read side by side. Where sc808 does something surprising, the
 * comment says what and why it was NOT "fixed" — this is a transcription, and
 * a transcription that improves things does not null against SuperCollider.
 *
 * The knobs are ours. sc808 exposes MIDI-note and second arguments; 8W8
 * exposes 0-127 pots and maps them onto those arguments in sc808_engine.cpp.
 * Every trigger() below takes engineering values, already mapped.
 *
 * One structural difference from SC, and it is deliberate: SC creates a new
 * synth per note, so every note starts with fresh filter state. These voices
 * are persistent and DO NOT clear filter state on trigger. That is what stops
 * fast repeats from clicking, and it is what real hardware does — a filter
 * does not forget because you hit the pad again.
 *
 * sc808.scd is MIT (Sonic Pi, etc/synthdefs); 8W8 is GPL-3.0.
 * Original 808 SynthDefs by Yoshinosuke Horiuchi, adapted by Sam Aaron.
 */
#ifndef SC808_VOICES_H
#define SC808_VOICES_H

#include "sc_ugens.h"

namespace sc808 {

using namespace sc;

/*
 * The six Schmitt-trigger oscillators.
 *
 * sc808 uses 203.52, 366.31, 301.77, 518.19, 811.16, 538.75 Hz. Werner, Abel
 * and Smith measured the real HD14584 bank at 205.3, 369.6, 304.4, 522.7 Hz
 * hardwired plus two factory-trimmed to 800 and 540 — so Horiuchi's set is
 * within about 1% of the hardware. They are kept as sc808 has them, because
 * this engine's job is to reproduce sc808; Engine B will use the measured set.
 *
 * The real machine shares one chip between the cowbell, cymbal, open hat and
 * closed hat, and sc808 preserves that: the cowbell's two "notes" resolve to
 * 811.4 and 538.7 Hz, which are oscillators 5 and 6 of this bank.
 *
 * LFPulse is unipolar, so this sum sits around +3 and every voice's filter
 * chain starts by throwing that DC away.
 */
static const double kMetalFreq[6] = {
    203.52, 366.31, 301.77, 518.19, 811.16, 538.75
};

class MetalBank {
public:
    /* SC starts every LFPulse at phase 0 in a fresh synth, so a trigger
     * restarts the bank. That phase alignment is what gives the hats their
     * consistent attack transient. */
    void retrigger() { for(int i = 0; i < 6; ++i) osc_[i].reset(0.0); }

    /* Unipolar sum of the six, scaled. `ratio` tunes the whole bank. */
    float process(const double _ratio, const double _sr, const float _scale)
    {
        float s = 0.0f;
        for(int i = 0; i < 6; ++i)
            s += osc_[i].process(kMetalFreq[i] * _ratio, 0.5, _sr);
        return s * _scale;
    }

private:
    LFPulse osc_[6];
};

/* Every voice reports when it has gone quiet so the engine can stop calling
 * it. sc808 uses DetectSilence for this; we use an explicit envelope-done
 * flag plus a decayed-output test, because a filter tail can outlive its
 * envelope and cutting it dead would click. */
class VoiceBase {
public:
    bool active() const { return active_; }
protected:
    /* Silence gate: 200 consecutive samples under -90 dBFS ends the note.
     * A single sample under threshold is just a zero crossing. */
    void gate(const float _y)
    {
        if(_y > 3.2e-5f || _y < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 200) active_ = false;
    }
    void arm() { active_ = true; quiet_ = 0; }
    bool active_ = false;
    int  quiet_  = 0;
};

/* ---- bass drum -------------------------------------------------------- */

/*
 * sonic-pi-sc808_bassdrum.
 *
 * Two sines and a triangle on a shared pitch envelope that falls from 7x the
 * note to 1.35x to the note. The "punch" is a second sine on a marginally
 * faster envelope, highpassed at 350 Hz — the click you hear is the top of
 * that sweep before it drops out of the passband.
 *
 * THE LIMITER. sc808 ends with Limiter.ar(sig, 0.5), and SC's Limiter is a
 * lookahead design: it delays by two buffers, 20 ms at its default, and it is
 * doing about 23 dB of gain reduction on the way through.
 *
 * In SuperCollider each drum is its own synth and nobody notices either. In a
 * drum machine both are unacceptable — a kick 20 ms behind the snare is not a
 * kick, and squashing the first 100 ms flattens exactly the attack and decay
 * that make one.
 *
 * So the limiter is REPRODUCED (the null test needs it, and it is what sc808
 * sounds like) but the engine switches it off with setLimiter(false). With it
 * off the voice emits the raw sum and the engine's own per-voice Drive stage
 * catches the peaks — which is the stage that exists for exactly that, and
 * unlike the limiter it is under a knob.
 *
 * Bypassing it needs a fixed gain in its place, or the voice comes out 18 dB
 * hot and clips: measured on the default kick, the limiter is holding a peak
 * of 7.945 down to 1.0 while only pulling the RMS from 0.80 to 0.31. That
 * spread is the point — it is squashing an 8:1 transient, and that transient
 * is the beater. Replacing 18 dB of dynamic gain reduction with 18 dB of
 * static gain hands the transient back.
 */
class BassDrum : public VoiceBase {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        punchHp_.set(350.0, sr_);
        lim_.set(0.01f, sr_);
    }

    /* sc808's lookahead limiter: on for the null test, off in the engine.
     * See the note above — it costs 20 ms of latency and 23 dB of the
     * dynamics, and it is not what a drum machine wants. */
    void setLimiter(const bool _on) { useLimiter_ = _on; }

    /* freqHz: the note. click: the 1.45 ms pre-level. decay: seconds.
     * punch: sc808's fixed 2.0, exposed here as Tone. */
    void trigger(const double _freqHz, const float _click, const float _decay,
                 const float _punch)
    {
        const float note = (float)_freqHz;
        punch_ = _punch;

        const EnvSeg a[2] = { { 1.0f, 0.0f, -7.0f }, { 0.0f, _decay, -7.0f } };
        env_.set(_click, a, 2, sr_);

        const EnvSeg t[2] = { { 0.6f, 0.0f, -230.0f }, { 0.0f, _decay, -230.0f } };
        trienv_.set(_click, t, 2, sr_);

        /* Two pitch envelopes, 0.05 s and 0.03 s to the first node. The 20 ms
         * difference between them is the entire "punch". */
        const EnvSeg f[2] = { { note * 1.35f, 0.05f, -14.0f }, { note, 0.6f, -14.0f } };
        fenv_.set(note * 7.0f, f, 2, sr_);

        const EnvSeg p[2] = { { note * 1.35f, 0.03f, -10.0f }, { note, 0.6f, -10.0f } };
        pfenv_.set(note * 7.0f, p, 2, sr_);

        /* All three oscillators start at pi/2 — SinOsc.ar(fenv, pi/2) and
         * LFTri.ar(fenv, pi/2) in the SynthDef. Starting a drum's body
         * oscillator at a zero crossing instead of a peak is not a subtle
         * difference: it moves the whole attack. */
        sin_.reset(kPi * 0.5);
        psin_.reset(kPi * 0.5);
        tri_.reset(kPi * 0.5);

        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;

        const float env    = env_.next();
        const float trienv = trienv_.next();
        /* nextHeld, not next: these drive oscillator FREQUENCY, which SC
         * samples once per control block and holds. */
        const double fe    = (double)fenv_.nextHeld();
        const double pfe   = (double)pfenv_.nextHeld();

        const float sig   = sin_.process(fe, sr_) * env;
        const float sub   = tri_.process(fe, sr_) * trienv * 0.05f;
        float punch       = psin_.process(pfe, sr_) * env * punch_;
        punch             = punchHp_.process(punch);

        const float mixed = (sig + sub + punch) * 2.5f;
        /* amp = 2 * amp in the SynthDef, folded in here. */
        /* kBypassGain: the PEAK reduction the limiter was doing, as a fixed
         * gain, so both paths leave the voice at the same peak level and one
         * per-lane trim serves either. */
        static const float kBypassGain = 1.0f / 7.945f;
        const float y = useLimiter_ ? lim_.process(mixed, 0.5f) * 2.0f
                                    : mixed * 2.0f * kBypassGain;

        /* Gate on the ENVELOPE, not the output: with the limiter in, its
         * two-buffer pipeline still holds 20 ms of audio after the envelope
         * ends, and gating on y would truncate the tail still in flight. */
        if(env_.done() && env < 1e-6f) gate(y);
        return y;
    }

    long latency() const { return useLimiter_ ? lim_.latency() : 0; }

private:
    double sr_ = 44100.0;
    float  punch_ = 2.0f;
    bool   useLimiter_ = true;
    Env    env_, trienv_, fenv_, pfenv_;
    SinOsc sin_, psin_;
    LFTri  tri_;
    HPF    punchHp_;
    Limiter lim_;
};

/* ---- snare ------------------------------------------------------------ */

/*
 * sonic-pi-sc808_snare. Two detuned sines for the shell (65 and 54 semitones,
 * an octave apart less a semitone) crossfaded against bandpassed noise. `mix`
 * is the 808's Snappy: 0 is all shell, 1 is all wires.
 *
 * sc808's `click` argument is inverted and clipped —
 * click = clip(0.1 - click/10, 0, 0.1) — so a HIGHER click value gives a
 * SHORTER attack segment. Kept as-is; the engine maps the pot so the panel
 * still reads the sensible way round.
 */
class Snare : public VoiceBase {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        headHp_.set(30.0, sr_);
        rng_.seed(0x808501u);
    }

    void trigger(const double _bodyHz, const double _detuneHz,
                 const float _decay, const float _mix,
                 const double _hpfHz, const double _lpfHz, const float _click)
    {
        mix_ = clampf(_mix, 0.0f, 1.0f);
        const float click = clampf(0.1f - (_click / 10.0f), 0.0f, 0.1f);

        noiseHp_.set(_hpfHz, sr_);
        noiseLp_.set(_lpfHz, sr_);

        envPerc(noiseEnv_, click, _decay, 1.0f, -115.0f, sr_);
        envPerc(atkEnv_,   click, 0.8f,   1.0f,  -95.0f, sr_);

        osc1_.set(_detuneHz, sr_);
        osc2_.set(_bodyHz,   sr_);
        osc1_.reset(kPi * 0.5);
        osc2_.reset(kPi * 0.5);
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;

        float noise = rng_.frand2();
        noise = noiseHp_.process(noise);
        noise = noiseLp_.process(noise);
        noise *= noiseEnv_.next();

        const float osc1 = osc1_.process() * 0.6f;
        const float osc2 = osc2_.process() * 0.7f;
        float snd = (osc1 + osc2) * atkEnv_.next() * 2.5f;

        snd = (mix_ * noise) + ((1.0f - mix_) * snd);
        snd = headHp_.process(snd * 2.0f);

        if(noiseEnv_.done() && atkEnv_.done()) gate(snd);
        return snd;
    }

private:
    double sr_ = 44100.0;
    float  mix_ = 0.7f;
    Env    noiseEnv_, atkEnv_;
    SinOsc osc1_, osc2_;
    HPF    noiseHp_, headHp_;
    LPF    noiseLp_;
    RGen   rng_;
};

/* ---- toms and congas -------------------------------------------------- */

/*
 * sonic-pi-sc808_tomlo / tommid / tomhi / congalo / congamid / congahi.
 *
 * All six are the same graph — a sine on a falling pitch envelope under an
 * amplitude envelope with a very steep curve (-250) — differing only in note,
 * decay, click and the two detune ratios. One class, six parameter sets.
 *
 * That is not a shortcut: on a real 808 the low tom and low conga ARE one
 * circuit with a panel switch, and likewise mid and high. sc808 splits them
 * into six SynthDefs; the hardware has three.
 *
 * sc808 sets congahi's note to 52, the same as congalo — plainly a copy-paste
 * slip, since the mid conga sits at 57 between them. The transcription keeps
 * the graph honest and the engine's DEFAULT POT for the high conga is raised
 * instead, which is a knob position rather than a change to this code.
 */
struct TomSpec {
    float note;      /* MIDI note */
    float decay;     /* seconds */
    float click;     /* pre-level */
    float detune1;   /* start of the pitch envelope, x note */
    float detune2;   /* middle node, x note */
};

/* sc808's values, verbatim. */
static const TomSpec kTomLo   = { 40.0f, 20.0f, 0.40f, 1.25f,     1.125f    };
static const TomSpec kTomMid  = { 44.0f, 16.0f, 0.40f, 1.3333f,   1.125f    };
static const TomSpec kTomHi   = { 52.0f, 11.0f, 0.40f, 1.3333f,   1.121212f };
static const TomSpec kCongaLo = { 52.0f, 18.0f, 0.15f, 1.333333f, 1.121212f };
static const TomSpec kCongaMid= { 57.0f, 18.0f, 0.13f, 1.333333f, 1.121212f };
static const TomSpec kCongaHi = { 52.0f, 18.0f, 0.15f, 1.333333f, 1.121212f };

class Tom : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; }

    void trigger(const TomSpec &_s, const double _freqHz, const float _decay)
    {
        const EnvSeg a[2] = { { 1.0f, 0.0f, -250.0f }, { 0.0f, _decay, -250.0f } };
        env_.set(_s.click, a, 2, sr_);

        const float n = (float)_freqHz;
        const EnvSeg f[2] = { { n * _s.detune2, 0.1f, -4.0f },
                              { n,              0.5f, -4.0f } };
        fenv_.set(n * _s.detune1, f, 2, sr_);

        osc_.reset(kPi * 0.5);
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float env = env_.next();
        /* nextHeld: a frequency input is a staircase, not a ramp. */
        const float y = osc_.process((double)fenv_.nextHeld(), sr_) * env;
        if(env_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0;
    Env    env_, fenv_;
    SinOsc osc_;
};

/* ---- rim shot / claves ------------------------------------------------ */

/*
 * sonic-pi-sc808_rimshot and sonic-pi-sc808_claves in one lane.
 *
 * They share a pad because they share a channel on the hardware: the 808's
 * front panel has one RS/CL selector, and you cannot have both in a pattern.
 * Mode picks which graph runs.
 *
 * Rim shot: a triangle and an 80%-duty pulse, a detuned pair 1.1x above their
 * nominal notes, plus a noise transient, through a fat +8 dB peak at 464 Hz —
 * that peak is the "tock".
 * Claves: one sine, one envelope. The simplest voice in the file.
 */
class RimClave : public VoiceBase {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        rng_.seed(0x8081DDAu);
        peak_.setPeakEQ(464.0, 0.44, 8.0, sr_);
    }

    /* mode 0 = rim shot, 1 = claves. */
    void trigger(const int _mode, const double _freqHz, const float _decay,
                 const double _hpfHz, const double _lpfHz)
    {
        mode_ = _mode;
        if(mode_ == 0)
        {
            const EnvSeg a[2] = { { 1.0f, 0.00272f, -42.0f },
                                  { 0.0f, _decay,   -42.0f } };
            env_.set(1.0f, a, 2, sr_);
            triHz_ = _freqHz * 1.1;
            /* detune = -22 semitones on the pulse, then the same 1.1 lift. */
            pulHz_ = _freqHz * powf(2.0f, -22.0f / 12.0f) * 1.1;
            tri_.reset(1.0);
            pul_.reset(0.0);
            hp_.set(_hpfHz, sr_);
            lp_.set(_lpfHz, sr_);
        }
        else
        {
            const EnvSeg a[2] = { { 1.0f, 0.0f,   -20.0f },
                                  { 0.0f, _decay, -20.0f } };
            env_.set(1.0f, a, 2, sr_);
            /* Claves sit far above the rim shot: sc808 puts them at note 99
             * against the rim's 92, so the lane's Tune pot moves both and the
             * offset is applied here. */
            clvHz_ = _freqHz * powf(2.0f, 7.0f / 12.0f);
            sin_.set(clvHz_, sr_);
            sin_.reset(kPi * 0.5);
        }
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float env = env_.next();
        float sig;

        if(mode_ == 0)
        {
            const float tri1  = tri_.process(triHz_, sr_) * env;
            const float tri2  = pul_.process(pulHz_, 0.8, sr_) * env;
            const float punch = rng_.frand2() * env * 0.46f;
            sig = peak_.process(tri1 + tri2 + punch);
            sig = lp_.process(hp_.process(sig));
        }
        else
        {
            sig = sin_.process() * env;
        }

        if(env_.done()) gate(sig);
        return sig;
    }

private:
    double sr_ = 44100.0;
    int    mode_ = 0;
    double triHz_ = 1827.0, pulHz_ = 512.0, clvHz_ = 2489.0;
    Env    env_;
    LFTri  tri_;
    LFPulse pul_;
    SinOsc sin_;
    Biquad peak_;
    HPF    hp_;
    LPF    lp_;
    RGen   rng_;
};

/* ---- maracas ---------------------------------------------------------- */

/*
 * sonic-pi-sc808_maracas. Noise, a highpass at about 5.6 kHz, and an envelope
 * that ramps 0.3 -> 1 over 27 ms before collapsing. That short RAMP UP is the
 * shaker's rattle and is the one place in sc808 where the first envelope
 * segment has a real duration rather than being an instant jump.
 */
class Maracas : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; rng_.seed(0x808A4Au); }

    void trigger(const double _hpfHz, const float _click, const float _decay)
    {
        const EnvSeg a[2] = { { 1.0f, _click, -250.0f },
                              { 0.0f, _decay, -250.0f } };
        env_.set(0.3f, a, 2, sr_);
        hp_.set(_hpfHz, sr_);
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float env = env_.next();
        const float y = hp_.process(rng_.frand2() * env);
        if(env_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0;
    Env    env_;
    HPF    hp_;
    RGen   rng_;
};

/* ---- hand clap -------------------------------------------------------- */

/*
 * sonic-pi-sc808_clap.
 *
 * Not the hardware's clap. A real 808 fires three fast noise bursts about
 * 10 ms apart before the tail; sc808 uses one immediate burst plus a second
 * one delayed 26 ms, then a long diffuse "reverb" of quiet noise. It reads as
 * a clap and it is what we are transcribing, but it is the voice with the
 * most daylight between sc808 and an 808, and the first candidate for
 * Engine B.
 *
 * Two sc808 quirks preserved: `amp` scales only the delayed burst and the
 * tail, never the attack burst; and the SynthDef reuses the name `decay` for
 * a signal after using it as a time, so the tail's length ends up read off an
 * audio-rate value. We use the nominal 4 s + decay and note that the clap is
 * expected to null only approximately.
 */
class Clap : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; rng_.seed(0x808C1AAu); }

    void trigger(const double _hpfHz, const double _bpfHz,
                 const float _click, const float _decay, const float _spread,
                 const float _rev)
    {
        rev_ = _rev;
        const EnvSeg a[2] = { { 1.0f, 0.0f,   -160.0f },
                              { 0.0f, _decay, -160.0f } };
        atkenv_.set(_click, a, 2, sr_);

        /* Env.dadsr(delay, attack=0, decay=6, sustain=0, ...): silence for
         * `spread`, then an instant jump to 1 and a 6 s collapse. `spread` is
         * sc808's fixed 26 ms, exposed on the panel. */
        const EnvSeg d[3] = { { 0.0f, _spread, -160.0f },
                              { 1.0f, 0.0f,    -160.0f },
                              { 0.0f, 6.0f,    -160.0f } };
        denv_.set(0.0f, d, 3, sr_);

        envPerc(revgen_, 0.1f, _decay + 4.0f, 1.0f, -9.0f, sr_);

        hp_.set(_hpfHz, sr_);
        bp_.set(_bpfHz, 0.5, sr_);
        revHp_.set(_hpfHz, sr_);
        revLp_.set(_bpfHz, sr_);
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;

        const float atk = rng_.frand2() * atkenv_.next() * 1.4f;
        const float dec = rng_.frand2() * denv_.next();
        float sum = bp_.process(hp_.process(atk + dec)) * 1.5f;

        float reverb = rng_.frand2() * revgen_.next() * 0.02f;
        reverb = revLp_.process(revHp_.process(reverb)) * rev_;

        sum += reverb;
        if(revgen_.done() && denv_.done()) gate(sum);
        return sum;
    }

private:
    double sr_ = 44100.0;
    float  rev_ = 1.0f;
    Env    atkenv_, denv_, revgen_;
    HPF    hp_, revHp_;
    BPF    bp_;
    LPF    revLp_;
    RGen   rng_;
};

/* ---- cowbell ---------------------------------------------------------- */

/*
 * sonic-pi-sc808_cowbell. Two pulse oscillators, no filtering of the square
 * edges beyond a 247 Hz highpass and a 4.4 kHz lowpass, and two envelopes
 * summed: a 1 s spike at 6x for the attack and the long body.
 *
 * sc808 writes the notes as 79.58979585613574 and 72.50534928521387, which
 * resolve to 811.4 and 538.7 Hz — oscillators 5 and 6 of the metal bank,
 * exactly as the real 808 wires them. The odd decimals are somebody having
 * back-solved the frequencies into MIDI notes.
 */
class Cowbell : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; }

    void trigger(const double _ratio, const float _decay,
                 const double _hpfHz, const double _lpfHz)
    {
        f1_ = kMetalFreq[4] * _ratio;   /* 811.16 */
        f2_ = kMetalFreq[5] * _ratio;   /* 538.75 */
        envPerc(atkenv_, 0.0f,  1.0f,   1.0f, -215.0f, sr_);
        envPerc(env_,    0.01f, _decay, 1.0f,  -90.0f, sr_);
        pul1_.reset(0.0);
        pul2_.reset(0.0);
        hp_.set(_hpfHz, sr_);
        lp_.set(_lpfHz, sr_);
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float p = pul1_.process(f1_, 0.5, sr_) + pul2_.process(f2_, 0.5, sr_);
        const float atk  = p * atkenv_.next() * 6.0f;
        const float datk = p * env_.next();
        const float y = lp_.process(hp_.process((atk + datk) / 6.0f));
        if(env_.done() && atkenv_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0, f1_ = 811.16, f2_ = 538.75;
    Env    atkenv_, env_;
    LFPulse pul1_, pul2_;
    HPF    hp_;
    LPF    lp_;
};

/* ---- closed hi-hat ---------------------------------------------------- */

/*
 * sonic-pi-sc808_closed_hihat. The metal bank split into two paths — a
 * bandpass/highpass pair around 9 kHz and a steeper BBandPass/BHiPass pair —
 * summed and peaked at 9.7 kHz. The x12 at the end is because six unipolar
 * pulses through that much highpass leave very little behind.
 */
class ClosedHat : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; }

    void trigger(const double _ratio, const float _decay,
                 const double _hpfHz, const double _lpfHz)
    {
        ratio_ = _ratio;
        envPerc(env_, 0.005f, _decay, 1.0f, -30.0f, sr_);
        hiBp_.set(_lpfHz, 1.0, sr_);
        hiHp_.set(_hpfHz, sr_);
        lowBp_.setBandPass(8900.0 * _ratio, 0.8, sr_);
        lowHp_.setHiPass(9000.0 * _ratio, 0.3, sr_);
        peak_.setPeakEQ(9700.0 * _ratio, 0.8, 0.7, sr_);
        bank_.retrigger();
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float s = bank_.process(ratio_, sr_, 1.0f);
        const float sighi  = hiHp_.process(hiBp_.process(s));
        const float siglow = lowHp_.process(lowBp_.process(s));
        const float y = peak_.process(siglow + sighi) * env_.next() * 12.0f;
        if(env_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0, ratio_ = 1.0;
    MetalBank bank_;
    Env    env_;
    BPF    hiBp_;
    HPF    hiHp_;
    Biquad lowBp_, lowHp_, peak_;
};

/* ---- open hi-hat ------------------------------------------------------ */

/*
 * sonic-pi-sc808_open_hihat. One filter chain, two envelopes across it: a
 * 0.1 s-attack body and a much longer, far more front-loaded tail (curve
 * -150 over five times the decay). The tail is what makes an open hat an open
 * hat rather than a long closed one.
 */
class OpenHat : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; }

    void trigger(const double _ratio, const float _decay,
                 const double _hpfHz, const double _lpfHz)
    {
        ratio_ = _ratio;
        envPerc(env1_, 0.1f, _decay, 1.0f, -3.0f, sr_);
        const EnvSeg t[2] = { { 1.0f, 0.0f,          -150.0f },
                              { 0.0f, _decay * 5.0f, -150.0f } };
        env2_.set(0.0f, t, 2, sr_);

        ls_.setLowShelf(990.0 * _ratio, 2.0, -3.0, sr_);
        bp_.set(_hpfHz, 1.0, sr_);
        peak_.setPeakEQ(7200.0 * _ratio, 0.5, 5.0, sr_);
        hp4_.set(8100.0 * _ratio, 0.7, sr_);
        hs_.setHiShelf(9400.0 * _ratio, 1.0, 5.0, sr_);
        lp_.set(_lpfHz, sr_);
        bank_.retrigger();
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        float sig = bank_.process(ratio_, sr_, 0.6f);
        sig = ls_.process(sig);
        sig = bp_.process(sig);
        sig = peak_.process(sig);
        sig = hp4_.process(sig);
        sig = hs_.process(sig);

        const float siga = sig * env1_.next() * 0.6f;
        const float sigb = sig * env2_.next();
        const float y = lp_.process(siga + sigb) * 7.0f;

        if(env1_.done() && env2_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0, ratio_ = 1.0;
    MetalBank bank_;
    Env    env1_, env2_;
    Biquad ls_, peak_, hs_;
    HiPass4 hp4_;
    BPF    bp_;
    LPF    lp_;
};

/* ---- cymbal ----------------------------------------------------------- */

/*
 * sonic-pi-sc808_cymbal. The big one: the same six oscillators through THREE
 * parallel filter chains, four envelopes between them.
 *
 *   sig1  low band, ~2.4-3 kHz, under the main decay, scaled by Tone
 *   sig2  high band, ~7 kHz, under two envelopes at once — a 0.7x-decay body
 *         and a very long, very steep shimmer (decay x20, curve -120)
 *   sig3  the top, ~10 kHz and up, a short bright transient
 *
 * The `tone` argument is multiplied by 0.008 before use, so the panel's Tone
 * is really "how much low band", and even at maximum it is a whisper next to
 * sig2. That is correct: an 808 cymbal is mostly the 7 kHz band.
 *
 * sc808 includes BLowShelf(sig1, 1000, 1, 0) — a 0 dB shelf, which is a
 * no-op. It is kept so the chain matches line for line; the compiler is
 * welcome to notice.
 */
class Cymbal : public VoiceBase {
public:
    void init(const double _sr) { sr_ = _sr; }

    void trigger(const double _ratio, const float _decay, const float _tone)
    {
        ratio_ = _ratio;
        tone_  = _tone * 0.008f;

        envPerc(env1_, 0.3f, _decay, 1.0f, -3.0f, sr_);
        const EnvSeg e2[2]  = { { 0.6f, 0.1f, -5.0f   }, { 0.0f, _decay * 0.7f, -5.0f   } };
        const EnvSeg e2b[2] = { { 0.3f, 0.1f, -120.0f }, { 0.0f, _decay * 20.0f, -120.0f } };
        const EnvSeg e3[2]  = { { 1.0f, 0.0f, -150.0f }, { 0.0f, _decay * 5.0f, -150.0f } };
        env2_.set(0.0f, e2,  2, sr_);
        env2b_.set(0.0f, e2b, 2, sr_);
        env3_.set(0.0f, e3,  2, sr_);

        const double r = _ratio;
        a_ls_.setLowShelf(2000.0 * r, 1.0, 5.0, sr_);
        a_bp_.set(3000.0 * r, 1.0, sr_);
        a_pk_.setPeakEQ(2400.0 * r, 0.5, 5.0, sr_);
        a_hp_.setHiPass(1550.0 * r, 0.7, sr_);
        a_lp_.set(3000.0 * r, sr_);
        a_ls2_.setLowShelf(1000.0 * r, 1.0, 0.0, sr_);

        b_ls_.setLowShelf(990.0 * r, 2.0, -5.0, sr_);
        b_bp_.set(7400.0 * r, 1.0, sr_);
        b_pk_.setPeakEQ(7200.0 * r, 0.5, 5.0, sr_);
        b_hp4_.set(6800.0 * r, 0.7, sr_);
        b_hs_.setHiShelf(10000.0 * r, 1.0, -4.0, sr_);

        c_ls_.setLowShelf(990.0 * r, 2.0, -15.0, sr_);
        c_bp_.set(6500.0 * r, 1.0, sr_);
        c_pk_.setPeakEQ(7400.0 * r, 0.35, 10.0, sr_);
        c_hp4_.set(10500.0 * r, 0.8, sr_);

        out_lp_.set(4000.0 * r, sr_);
        bank_.retrigger();
        arm();
    }

    float process()
    {
        if(!active_) return 0.0f;
        const float sig = bank_.process(ratio_, sr_, 0.6f);

        float s1 = a_ls_.process(sig);
        s1 = a_bp_.process(s1);
        s1 = a_pk_.process(s1);
        s1 = a_hp_.process(s1);
        s1 = a_lp_.process(s1);
        s1 = a_ls2_.process(s1);
        s1 *= env1_.next() * tone_;

        float s2 = b_ls_.process(sig);
        s2 = b_bp_.process(s2);
        s2 = b_pk_.process(s2);
        s2 = b_hp4_.process(s2);
        s2 = b_hs_.process(s2);
        const float s2a = s2 * env2_.next()  * 0.3f;
        const float s2b = s2 * env2b_.next() * 0.6f;

        float s3 = c_ls_.process(sig);
        s3 = c_bp_.process(s3);
        s3 = c_pk_.process(s3);
        s3 = c_hp4_.process(s3) * 2.0f;   /* BHiPass4's `mul` argument */
        s3 *= env3_.next();

        const float y = out_lp_.process(s1 + s2a + s2b + s3) * 12.0f;
        if(env1_.done() && env2b_.done() && env3_.done()) gate(y);
        return y;
    }

private:
    double sr_ = 44100.0, ratio_ = 1.0;
    float  tone_ = 0.002f;
    MetalBank bank_;
    Env    env1_, env2_, env2b_, env3_;
    Biquad a_ls_, a_pk_, a_hp_, a_ls2_;
    BPF    a_bp_;
    LPF    a_lp_;
    Biquad b_ls_, b_pk_, b_hs_;
    BPF    b_bp_;
    HiPass4 b_hp4_;
    Biquad c_ls_, c_pk_;
    BPF    c_bp_;
    HiPass4 c_hp4_;
    LPF    out_lp_;
};

} /* namespace sc808 */

#endif /* SC808_VOICES_H */
