/*
 * sc808_tom_circuit.h — the TR-808 tom / conga channel, from the circuit.
 *
 * Engine B for all six tom and conga lanes. sc808's transcription stays
 * exactly as it is and keeps its place in the null test; this runs alongside
 * it and the panel switches.
 *
 * SECOND VERSION, and the first one is worth an honest paragraph. It guessed
 * where it had no data: a 33% pitch lift, a loud white-noise "skin" on the
 * toms, and a conga that was the drier, shorter one. Then reference arrived —
 * hardware samples with note names in the filenames, and Roland's own plugin
 * to render — and measurement said otherwise on every count:
 *
 *   TUNING (measured fundamentals, settled):
 *     tom    F2 87.3      C3 130.8     G3 196.0     — seven semitones apart
 *     conga  G3 196.0     D4 293.7     A4 440.0     — seven semitones apart
 *   The congas sit more than an OCTAVE above their toms. The old defaults
 *   (played 164.8 / 220 / 293.7 as toms at 82/104/165) were both mistuned
 *   and wrongly spaced, which is why the lanes "did not overlap".
 *
 *   PITCH DROP: low tom 94.8 -> 87.7 Hz (+8.6% at onset), hi tom +2.3%,
 *   congas under +1%. The old 33% was a caricature.
 *
 *   NOISE: the low tom sample has 99.9% of its energy below 400 Hz. The
 *   noise head is a texture on the strike, not a layer you hear as noise —
 *   the old 0.75 white-noise skin was reported, accurately, as hiss.
 *
 *   DECAY (to 1% of peak): toms 0.39 / 0.23 / 0.18 s, congas 0.36 / 0.16 /
 *   0.145 s. At the SAME pitch the conga rings TWICE as long as the tom
 *   (G3: 0.36 vs 0.18) — the first version had that exactly backwards.
 *
 * WHAT THE SWITCH IS, then, as measured: the tom position damps the ring and
 * dirties the strike (the noise path and 1.5k are in circuit); the conga
 * position is the cleaner, longer-ringing, higher-tuned one. The samples'
 * conga peaks arrive at 2-5 ms (resonance building); the toms' inside 1 ms
 * (struck, with the head).
 *
 * STILL FROM THE SCHEMATIC, unchanged: the bridged-T with Q = 10.82 from
 * R218/R219 = 2.2M/4.7k, the loop (decay as loop gain), the pulse shaper,
 * the op-amp clip.
 *
 * THE DECAY POT IS SECONDS. The knob maps to ring time (to 1% of peak) and
 * the circuit solves the loop gain that produces it at the current pitch:
 *
 *     g = 1 - ln(100) * Q / (pi * f0 * t)
 *
 * so the knob keeps its meaning when Tune moves — the same seconds at any
 * pitch — and both engines on the lane can share one number. Clamped at
 * g >= 0: below natural ring (Q alone) the network cannot be pushed shorter.
 *
 * GPL-3.0.
 */
#ifndef SC808_TOM_CIRCUIT_H
#define SC808_TOM_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* ---- component values, TR-808 service notes, voicing board ------------- */

static const double kTOM_R1 = 4.7e3;     /* R219 / R274, shunt arm  */
static const double kTOM_R2 = 2.2e6;     /* R218 / R276, series arm */

/* 10.82. The one number here that comes straight off the schematic — and
 * computed from the two resistors rather than written out, so the value and
 * the components it claims to come from cannot drift apart. */
static const double kTOM_Q = bridgedTQ(kTOM_R1, kTOM_R2);

/*
 * The switch, as the measurements have it.
 *
 * Tom: struck hard and dirty — full pulse, plus a short burst of low-passed
 * noise into the loop (the P.N. bus through the 1.5k path). The head noise is
 * a TEXTURE: the low tom sample keeps 99.9% of its energy below 400 Hz, so
 * the burst is quiet and coloured by the resonator, not a hiss layer.
 *
 * Conga: struck clean — softer pulse, no noise — and ringing longer, which
 * the Decay pot's per-lane default carries (0.36 s against the same-pitch
 * tom's 0.18).
 */
static const double kTOM_SkinLevel = 0.060;  /* noise into the loop, tom only */
static const double kTOM_SkinTau   = 0.012;  /* the burst, roughly one period */
static const double kTOM_StrikeTom = 1.0;
static const double kTOM_StrikeCga = 0.80;

/* Measured: the low tom settles 8.6% under its onset reading, the hi tom
 * only 2.3%, congas under 1% — the sweep SHRINKS as pitch rises, so the tom
 * lift is referenced to the low tom's 87.31 Hz and scaled by 1/f0. */
static const double kTOM_PitchLiftTom = 0.09;    /* at F2; x 87.31/f0 */
static const double kTOM_PitchLiftCga = 0.02;
static const double kTOM_PitchTau     = 0.040;

/* g = 1 - ln(100) Q / (pi f0 t), from the ring-time pot. The ceiling keeps a
 * detuned-low, decay-high corner from parking the loop at unity. */
static const double kTOM_GainMax = 0.993;

/*
 * Forward gain, refitted for the new tuning span (87..440 Hz): BridgedT
 * normalises its peak away and the caller puts the network's real forward
 * gain back on the input side. The frequency shape compensates the
 * bandwidth-with-f0 dependence; exponent fitted over the six lanes.
 */
static const double kTOM_FwdRefHz = 120.0;
static const double kTOM_FwdPower = 0.90;

/*
 * Fitted so an unaccented hit at the default pots peaks where the sc808
 * voice does — each lane has ONE trim shared by both engines, and
 * tools/tom_check asserts they still agree.
 */
static const double kTOM_OutScaleTom = 20.4;
static const double kTOM_OutScaleCga = 25.3;

/*
 * The strike's direct bleed to the output. Every tom sample PEAKS at the
 * strike, 0.5-0.7 ms in, with the ring's own peak a little under it — the
 * click is the loudest instant of the note. Absolute (outside the
 * frequency-dependent forward gain), so the click-to-ring proportion holds
 * across the lanes as it does across the samples. The conga's strike is
 * already rounded soft by the switch, so its bleed is a push, not a click —
 * which is exactly the difference the samples show.
 */
static const double kTOM_ClickThru = 0.16;

class TomCircuit {
public:
    /* mode 0 = tom (skin, harder strike), 1 = conga (dry, all shell). */
    void init(const double _sr, const int _mode)
    {
        sr_   = _sr;
        mode_ = _mode;
        rng_.seed(_mode == 0 ? 0x808704Du : 0x808C04Au);

        /*
         * init(), not reset().
         *
         * sc808_create() allocates the engine with calloc, so no constructor
         * ever runs on anything inside it and C++ default member initialisers
         * are silently replaced by zero bytes. PulseShaper's dc_ defaults to
         * 0.045 and came out 0, which makes it return exactly zero for any
         * input — so the strike vanished, and the congas, which have no noise
         * head to fall back on, went completely silent while the toms carried
         * on sounding off their skin alone.
         *
         * Anything here that needs a nonzero starting value has to be given
         * one explicitly, in this function. Do not trust a member initialiser.
         *
         * The shaper's own component values for this channel are not
         * identified, so it keeps the class's bass drum defaults.
         */
        shaper_.init(_sr);
        reset();
    }

    bool active() const { return active_; }

    /*
     * freqHz     the lane's pitch, from its base note and Tune.
     * ringSec    ring time to 1% of peak, in SECONDS, straight off the pot.
     *            The loop gain that produces it at this pitch is solved here.
     * accentV    trigger volts, 4 to 14, as the hardware's accent bus swings.
     */
    void trigger(const double _freqHz, const float _ringSec, const float _accentV)
    {
        f0_ = _freqHz < 20.0 ? 20.0 : (_freqHz > sr_ * 0.25 ? sr_ * 0.25 : _freqHz);

        const double t = _ringSec > 0.02f ? (double)_ringSec : 0.02;
        double g = 1.0 - log(100.0) * kTOM_Q / (kCircPi * f0_ * t);
        if(g < 0.0) g = 0.0;                 /* can't ring shorter than Q alone */
        if(g > kTOM_GainMax) g = kTOM_GainMax;
        loopGain_ = g;

        accentV_     = (double)_accentV;
        gateSamples_ = (int)(0.001 * sr_);      /* the CPU's 1 ms trigger */

        pitchEnv_  = 1.0;
        pitchCoef_ = exp(-1.0 / (kTOM_PitchTau * sr_));

        skinEnv_  = (mode_ == 0) ? 1.0 : 0.0;
        skinCoef_ = exp(-1.0 / (kTOM_SkinTau * sr_));
        /* the burst is coloured toward the drum before it even reaches the
         * resonator — a stick on a skin, not a tweeter */
        skinLpA_ = exp(-2.0 * kCircPi * (f0_ * 4.0) / sr_);

        fwd_ = pow(kTOM_FwdRefHz / f0_, kTOM_FwdPower)
             * (mode_ == 0 ? kTOM_OutScaleTom : kTOM_OutScaleCga);

        coefAge_ = 0;
        active_  = true;
        quiet_   = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        /* ---- the strike ---- */
        double gate = 0.0;
        if(gateSamples_ > 0) { gate = accentV_; --gateSamples_; }

        double strike = shaper_.process(gate)
                      * (mode_ == 0 ? kTOM_StrikeTom : kTOM_StrikeCga);
        /*
         * The conga's strike is SOFT: with the switch grounding the pulse
         * node, the edge that reaches the network is rounded, and the drum
         * blooms instead of clicking. Measured in the samples as time to
         * peak — toms inside 1 ms, congas at 2-5 ms — and modelled by
         * rounding the conga's strike through the same one-pole the tom's
         * head uses (idle in conga mode, corner 4 x f0).
         */
        if(mode_ == 1)
        {
            strikeLpZ_ += (strike - strikeLpZ_) * (1.0 - skinLpA_);
            strike = strikeLpZ_;
        }

        /*
         * The head, tom side only: a short burst of noise, LOW-PASSED near
         * the drum before it reaches the resonator, injected INTO the loop so
         * the network colours it further. The reference low tom keeps 99.9%
         * of its energy below 400 Hz — this is a texture on the strike, and
         * any version of it you can point to as "the noise" is too loud.
         */
        double skin = 0.0;
        if(skinEnv_ > 1e-5)
        {
            const double n = (double)rng_.frand2() * skinEnv_ * kTOM_SkinLevel * accentV_;
            skinLpZ_ += (n - skinLpZ_) * (1.0 - skinLpA_);
            skin = skinLpZ_;
            skinEnv_ *= skinCoef_;
        }

        /* ---- the pitch drop ---- */
        pitchEnv_ *= pitchCoef_;
        if(pitchEnv_ < 1e-5) pitchEnv_ = 0.0;
        if(--coefAge_ <= 0)
        {
            coefAge_ = 16;   /* 0.36 ms, far inside the drop */
            const double lift = mode_ == 0 ? kTOM_PitchLiftTom * (87.31 / f0_)
                                           : kTOM_PitchLiftCga;
            bt_.set(f0_ * (1.0 + lift * pitchEnv_), kTOM_Q, sr_);
        }

        /* ---- the loop ----
         * fb_ is last sample's output: the unit delay that breaks what is
         * otherwise a delay-free loop, as the bass drum paper prescribes. */
        const double x = strike + skin + loopGain_ * opampClip(fb_);
        const double y = bt_.process(x);
        fb_ = y;

        double out = y * fwd_ + strike * kTOM_ClickThru;

        /* the output buffer's series capacitor */
        dcZ_ += (out - dcZ_) * kDcCoef;
        out  -= dcZ_;

        if(!(out > -50.0 && out < 50.0)) { reset(); return 0.0f; }

        const float o = (float)out;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400) active_ = false;
        return o;
    }

private:
    static constexpr double kDcCoef = 0.0009;   /* ~6 Hz high-pass */

    void reset()
    {
        bt_.reset();
        skinLpZ_ = strikeLpZ_ = 0.0;
        fb_ = dcZ_ = 0.0;
        gateSamples_ = 0;
        active_ = false;
        quiet_  = 0;
    }

    double sr_ = 44100.0;
    int    mode_ = 0;
    double f0_ = 165.0;
    double loopGain_ = 0.9, accentV_ = 8.0, fwd_ = 1.0;
    double pitchEnv_ = 0.0, pitchCoef_ = 0.0;
    double skinEnv_  = 0.0, skinCoef_  = 0.0;
    double skinLpA_  = 0.0, skinLpZ_   = 0.0, strikeLpZ_ = 0.0;
    double fb_ = 0.0, dcZ_ = 0.0;
    int    gateSamples_ = 0, coefAge_ = 0;
    bool   active_ = false;
    int    quiet_  = 0;

    PulseShaper shaper_;
    BridgedT    bt_;
    sc::RGen    rng_;
};

} /* namespace sc808 */

#endif /* SC808_TOM_CIRCUIT_H */
