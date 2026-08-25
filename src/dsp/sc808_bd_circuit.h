/*
 * sc808_bd_circuit.h — the TR-808 bass drum, as a circuit.
 *
 * This is the voice 8W8 exists for. Everything else in the kit can be a
 * faithful transcription of sc808 and be fine; the kick cannot, because
 * sc808's kick is a sine on a pitch envelope and an 808's kick is not.
 *
 * WHAT AN 808 KICK ACTUALLY IS
 *
 * A bridged-T network sitting in the negative feedback path of an op-amp.
 * That arrangement is a resonator, and it is kicked by a ~1 ms pulse from the
 * trigger. Left alone it would ring for about 100 ms. What makes the long
 * 808 boom is a second path: the output goes through a feedback buffer whose
 * gain is the DECAY POTENTIOMETER and comes back into the network. Decay is
 * not an envelope on this circuit — it is loop gain, and the note lasts as
 * long as the loop takes to lose it. Turn it up and the thing edges toward
 * self-oscillation.
 *
 * That single fact is why an 808 kick behaves the way it does and why an
 * envelope-based imitation never quite does:
 *
 *   - the pitch SIGHS downward through the note instead of holding, because
 *     leakage current shifts the network's effective resistance as the note
 *     decays
 *   - the ATTACK is a real jump in the resonator's centre frequency and Q for
 *     a few milliseconds, not a second oscillator layered on top
 *   - retriggering before the previous note has died does NOT machine-gun,
 *     because the filter state is still there and the new pulse adds to it
 *   - accent changes the sound and not only the level, because the trigger
 *     voltage drives a diode whose behaviour is not linear in it
 *
 * PROVENANCE, and what is and is not taken from the paper
 *
 * Kurt James Werner, Jonathan Abel and Julius Smith, "A Physically-Informed,
 * Circuit-Bendable, Digital Model of the Roland TR-808 Bass Drum Circuit",
 * DAFx-14. The block decomposition, the identification of what each block
 * does, the pulse shaper's shelf-plus-diode form, the decay pot as loop gain,
 * the attack-time frequency shift and the leakage pitch sigh are all theirs.
 *
 * The coefficients here are RE-DERIVED rather than transcribed, deliberately.
 * The paper's published coefficient list for the bridged-T in feedback is
 * dimensionally inconsistent as printed (its s^2 and s^0 terms differ by an
 * ohm), and the component values legible in the schematic figure give a
 * centre frequency of 100 Hz where the text states 49.5 Hz. Rather than ship
 * numbers that cannot be checked, this file works from the STATED BEHAVIOUR —
 * centre frequency, Q, the attack shift, the sigh's depth and time constant,
 * the decay range — and derives the resonator from those. Every such choice
 * is marked and its source given.
 *
 * That distinction matters: this is a physically-informed model built to a
 * published analysis, not a transcription, and unlike Engine A it has no null
 * test behind it. It is checked against the paper's own figures — the
 * instantaneous-frequency trace and the retrigger test — by tools/bd_check.
 *
 * GPL-3.0.
 */
#ifndef SC808_BD_CIRCUIT_H
#define SC808_BD_CIRCUIT_H

#include <math.h>
#include <stdint.h>

#include "sc808_circuit_common.h"

namespace sc808 {

/*
 * Component values, from the TR-808 service notes as reproduced in the
 * paper's Figure 1. Only the ones that survive into the derivation below are
 * kept; the rest are in the paper.
 */
static const double kBD_R167 = 1.0e6;    /* bridged-T series arm      */
static const double kBD_R162 = 4.7e3;    /* pulse shaper              */
static const double kBD_R163 = 100.0e3;
static const double kBD_C40  = 0.015e-6;
static const double kBD_R169 = 47.0e3;   /* feedback buffer           */
static const double kBD_VR6  = 500.0e3;  /* the DECAY potentiometer   */

/*
 * The resonator's nominal centre frequency and Q.
 *
 * The paper states a centre frequency of 49.5 Hz for the un-modulated
 * network, "close to the entry Roland's typical and variable tuning chart
 * (56 Hz) and the sound of a real 808 bass drum". Working backwards through
 *
 *     f0 = 1 / (2 pi sqrt(Reff * R167 * C41 * C42))
 *
 * that fixes Reff at about 45 kOhm, and the bridged-T's pole Q is then
 *
 *     Q = (1/2) sqrt(R167 / Reff)
 *
 * which is 2.36. Both numbers are used below rather than the raw resistor
 * values, because they are what the paper actually asserts about the
 * circuit's behaviour and they are checkable against its figures.
 */
static const double kBD_F0_NOMINAL = 49.5;
static const double kBD_Q_INTRINSIC = 2.356;

/* Reff implied by a given centre frequency — this is the "tuning mod", and
 * it is the one bend the paper singles out as the obvious one. */
static inline double bd_reff_for(const double _f0)
{
    const double w = 2.0 * 3.14159265358979323846 * _f0;
    /* C41 = C42 = 0.015 uF; the product is what appears in f0. */
    const double cc = 0.015e-6 * 0.015e-6;
    return 1.0 / (w * w * kBD_R167 * cc);
}

/*
 * The bridged-T's gain at its own centre frequency.
 *
 * Evaluating the paper's H(s) at w0 collapses both the s^2 and constant terms
 * (they are equal and opposite there), leaving the ratio of the s terms:
 *
 *     |H(w0)| = beta1/alpha1 = (2 Reff + R167) / (2 Reff) = 1 + R167/(2 Reff)
 *
 * which is about 12 at the nominal tuning. That is a real 21 dB of forward
 * gain and it is NOT optional: normalising the resonator to unity peak gain —
 * which is the convenient thing to do, because it makes the decay pot exactly
 * the loop gain — silently throws it away and leaves the voice 20 dB down and
 * its pitch sigh 8x too shallow, because the sigh is driven by the signal's
 * own amplitude.
 *
 * So the resonator stays unity-normalised, for the decay pot's sake, and this
 * goes back in on the input side where it belongs.
 */
static inline double bd_forward_gain(const double _reff)
{
    return 1.0 + kBD_R167 / (2.0 * _reff);
}

class CircuitBassDrum {
public:
    void init(const double _sampleRate)
    {
        sr_ = _sampleRate >= 8000.0 ? _sampleRate : 44100.0;

        /* Pulse shaper with the bass drum's own component values. See
         * sc808_circuit_common.h for what it does and why. */
        shaper_.init(sr_, kBD_R162, kBD_R163, kBD_C40);

        reset();
    }

    void reset()
    {
        bt_.reset();
        shaper_.reset();
        fb_ = 0.0;
        gateSamples_ = 0;
        attackEnv_ = 0.0;
        sighEnv_ = 0.0;
        toneZ_ = 0.0;
        dcZ_ = 0.0;
        active_ = false;
        quiet_ = 0;
        coefAge_ = 0;
        setCoefficients(f0_, kBD_Q_INTRINSIC);
    }

    bool active() const { return active_; }

    /*
     * freqHz   the resonator's resting centre frequency (Tune)
     * decayK   the decay pot, 0..1 — LOOP GAIN, not a time
     * tone     0..1, the output low-pass (the panel's Tone control)
     * attack   0..1, how much of the attack-time frequency/Q jump to apply
     * accentV  the trigger voltage, 4..14 V as on the hardware
     *
     * NOTE what is NOT reset here: the resonator state, the feedback state
     * and the tone filter. That omission is the model. A real 808 hit while
     * the previous note is still ringing adds to what is there, which is why
     * fast 808 kicks do not machine-gun, and clearing the state would throw
     * away exactly the behaviour this file exists to reproduce.
     */
    void trigger(const double _freqHz, const float _decayK, const float _tone,
                 const float _attack, const float _accentV)
    {
        f0_ = _freqHz > 20.0 ? (_freqHz < 400.0 ? _freqHz : 400.0) : 20.0;

        /*
         * Forward gain, NORMALISED against the nominal tuning.
         *
         * The physical gain is 1 + R167/(2 Reff) and Reff goes as 1/f0^2, so
         * tuning up an octave multiplies it by about five. That is what the
         * circuit does and it is not what a Tune knob should do: across 8W8's
         * +/-12 semitones the raw gain runs from 3 to 62, a 26 dB swing, and
         * at the top the loop drives itself into the safety limiter and the
         * voice resets on every hit.
         *
         * On the hardware this is not a performance control at all — Reff is
         * set by fixed resistors and, on a modded 808, a trimmer somebody
         * adjusts once. The level is then made up downstream and never
         * thought about again. So the pitch and the Q still move with Reff,
         * exactly as the circuit says, and only the overall level is held
         * still. It is the one place this file trades the circuit's behaviour
         * for a usable knob, and it is the whole trade.
         */
        static const double kNominalForward =
            1.0 + kBD_R167 / (2.0 * 45032.0);      /* Reff at 49.5 Hz */
        const double raw = bd_forward_gain(bd_reff_for(f0_));
        forward_ = raw;
        tuneTrim_ = kNominalForward / (raw > 1e-9 ? raw : 1e-9);

        /*
         * Decay is loop gain. The feedback buffer's audio-band gain is
         *
         *     g = VR6 k / (R169 + VR6 k)
         *
         * (its shelf corner sits in the millihertz, so across the audio band
         * it is a flat gain). k = 1 gives 0.914 and a note of well over a
         * second; k small gives a short thud. The circuit's own Q is only
         * 2.36 — around 100 ms — so essentially all of an 808 kick's length
         * comes from this one number.
         */
        const double k = _decayK < 0.0f ? 0.0 : (_decayK > 1.0f ? 1.0 : _decayK);
        const double vr = kBD_VR6 * (k * 0.999 + 0.001);   /* pot never truly 0 */
        loopGain_ = vr / (kBD_R169 + vr);

        attackAmount_ = _attack < 0.0f ? 0.0 : (_attack > 1.0f ? 1.0 : _attack);
        accentV_ = _accentV < 1.0f ? 1.0 : (_accentV > 20.0f ? 20.0 : _accentV);

        /* Tone: a one-pole low-pass from 300 Hz to 6 kHz. The hardware's tone
         * stage is a low-pass into a divider into a high-pass; the divider is
         * Level and the high-pass is fixed, so this is the part that moves. */
        const double tHz = 300.0 * pow(20.0, (double)(_tone < 0.0f ? 0.0f
                                                    : (_tone > 1.0f ? 1.0f : _tone)));
        toneA_ = exp(-2.0 * 3.14159265358979323846 * tHz / sr_);

        /* 1 ms of gate, as the CPU delivers. */
        gateSamples_ = (int)(sr_ * 0.001);

        /*
         * The attack-time frequency shift. Current into Q43's base drops the
         * network's effective resistance for a few milliseconds, taking the
         * centre frequency up by more than an octave and the Q with it. The
         * paper is explicit that this is NOT heard as a pitch — it is six
         * milliseconds, less than one period at the higher frequency — but
         * that it is what makes the attack punchy and crisp.
         */
        attackEnv_ = 1.0;
        attackCoef_ = exp(-1.0 / (0.006 * sr_));

        active_ = true;
        quiet_ = 0;
        coefAge_ = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        /* ---- trigger pulse, shaped ---- */
        double gate = 0.0;
        if(gateSamples_ > 0) { gate = accentV_; --gateSamples_; }

        const double vplus = shaper_.process(gate);

        /* ---- the attack shift, and the pitch sigh ---- */
        attackEnv_ *= attackCoef_;
        if(attackEnv_ < 1e-6) attackEnv_ = 0.0;

        /*
         * The sigh. Leakage through R161 lifts Q43's base, drawing a current
         * that changes the effective resistance and therefore the centre
         * frequency — so the pitch is highest while the note is loudest and
         * falls as it dies. The paper's Figure 11 shows roughly 57 Hz at the
         * start settling to 49 Hz over 300 ms, with a wobble at the note's
         * own period. Tracked here from the output's own envelope, which is
         * what drives it in the circuit; the wobble falls out for free
         * because the envelope follower is deliberately fast enough to see
         * the oscillation.
         */
        /* Normalised by the level a nominal hit reaches, so the sigh's depth
         * is a property of the circuit and not of wherever the gain staging
         * happens to sit. */
        const double mag = (fb_ < 0.0 ? -fb_ : fb_) * (1.0 / kNominalPeak);
        sighEnv_ += (mag - sighEnv_) * kSighTrack;

        /* Coefficients move continuously, but recomputing a sin and a cos per
         * sample for a voice that is mostly a decaying tone is waste. Every
         * 16 samples is 0.36 ms — far inside the 6 ms attack. */
        if(--coefAge_ <= 0)
        {
            coefAge_ = 16;
            const double sigh = 1.0 + kSighDepth * (sighEnv_ < 1.0 ? sighEnv_ : 1.0);
            /* Attack: Reff drops, so f0 rises and Q rises with it — both go
             * as 1/sqrt(Reff). The paper puts the shift at "more than an
             * octave", so full deflection is 3x, not 2x: a 2x ceiling
             * measures as barely half an octave once the first half-period is
             * averaged, because the shift has already begun decaying inside
             * it. At 3x the first half-period lands an octave up, which is
             * what the circuit does. */
            const double lift = 1.0 + 2.0 * attackAmount_ * attackEnv_;
            setCoefficients(f0_ * sigh * lift, kBD_Q_INTRINSIC * lift);
        }

        /* ---- the loop ----
         * Input plus the feedback buffer's contribution. fb_ is last sample's
         * output: the unit delay the paper prescribes to break what is
         * otherwise a delay-free loop. */
        const double x = vplus * forward_ + loopGain_ * opampClip(fb_);

        const double y = bt_.process(x);
        fb_ = y;

        /* ---- output stage ---- */
        toneZ_ += (y - toneZ_) * (1.0 - toneA_);
        /* The output buffer's series capacitor: a fixed high-pass that keeps
         * the sigh's DC wander out of the mix. */
        dcZ_ += (toneZ_ - dcZ_) * kDcCoef;
        double out = toneZ_ - dcZ_;

        if(!(out > -50.0 && out < 50.0)) { reset(); return 0.0f; }
        out *= kOutScale * tuneTrim_;

        if(out > 3.2e-5 || out < -3.2e-5) quiet_ = 0;
        else if(++quiet_ > 400) active_ = false;
        return (float)out;
    }

private:
    /* Roughly how far the pitch sighs, and how fast the follower that drives
     * it tracks. 0.16 puts the start of the note about 8 Hz above its resting
     * pitch, matching the paper's Figure 11; the tracking constant is fast
     * enough to leave the per-period wobble that figure also shows. */
    static constexpr double kSighDepth = 0.16;
    static constexpr double kSighTrack = 0.0016;
    static constexpr double kDcCoef    = 0.0009;    /* ~6 Hz high-pass */
    /* The resonator runs in volts; this brings a 14 V accent to about full
     * scale. Set once, here, rather than smeared through the signal path. */
    /* The level a nominal hit reaches inside the loop, measured. Used to
     * normalise the sigh so its depth is a property of the circuit rather
     * than of the gain staging. */
    static constexpr double kNominalPeak = 1.35;
    /*
     * Set so an UNACCENTED hit at the default pots peaks at 1.0 — the same
     * place the sc808 kick lands. The two engines share one per-lane trim, so
     * if they do not agree on what "a kick" comes out at, switching engines
     * changes the level by 11 dB and the kit balance is wrong for one of them.
     */
    static constexpr double kOutScale    = 1.522;

    void setCoefficients(const double _f0, const double _q)
    { bt_.set(_f0, _q, sr_); }

    double sr_ = 44100.0;
    double f0_ = kBD_F0_NOMINAL;
    double loopGain_ = 0.9;
    double forward_ = 12.1, tuneTrim_ = 1.0;
    double attackAmount_ = 1.0, attackEnv_ = 0.0, attackCoef_ = 0.0;
    double accentV_ = 8.0;

    BridgedT    bt_;
    PulseShaper shaper_;
    double fb_ = 0.0;

    double toneA_ = 0.0, toneZ_ = 0.0, dcZ_ = 0.0;
    double sighEnv_ = 0.0;

    int gateSamples_ = 0;
    int coefAge_ = 0;
    int quiet_ = 0;
    bool active_ = false;
};

} /* namespace sc808 */

#endif /* SC808_BD_CIRCUIT_H */
