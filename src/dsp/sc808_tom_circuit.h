/*
 * sc808_tom_circuit.h — the TR-808 tom / conga channel, from the circuit.
 *
 * Engine B for all six tom and conga lanes, following the bass drum and the
 * snare: sc808's transcription stays exactly as it is and keeps its place in
 * the null test, this runs alongside it, and the panel switches.
 *
 * WHY THIS EXISTS
 *
 * "Hi Tom and Conga are very similar", and they were, for a reason that is
 * visible in sc808's own arguments:
 *
 *     kTomHi   = { note 52, decay 11, click 0.40, 1.3333, 1.121212 }
 *     kCongaHi = { note 52, decay 18, click 0.15, 1.333333, 1.121212 }
 *
 * Same note, same pitch-envelope ratios, same graph — a sine under a steep
 * curve. Across the whole file the only thing separating a tom from a conga
 * is how long it rings and how loud its click is. (The matching note is
 * sc808's copy-paste slip, congahi taking congalo's 52 while congamid sits at
 * 57 between them; 8W8 already works around it with a pot default. Fixing the
 * pitch does not fix the timbre, which is the actual complaint.)
 *
 * The hardware separates them by more than that, and 8W8 needs it to: the
 * 808 has three tom/conga CHANNELS and a switch, so it can never play a hi
 * tom and a hi conga together, while 8W8 gives them a pad each.
 *
 * WHAT THE HARDWARE DOES
 *
 * From the TR-808 service notes, voicing board — LOW TOM / LOW CONGA,
 * MID TOM / MID CONGA, HI TOM / HI CONGA, three copies of one design:
 *
 *   - A bridged-T network, as everywhere else in this machine. Its two
 *     resistors are the same in every channel and are clearly marked:
 *     4.7k in the shunt arm (R219, R274) and 2.2M in the series arm (R218,
 *     R276). By the paper's form that is
 *
 *         Q = (1/2) sqrt(R2 / R1) = (1/2) sqrt(2.2M / 4.7k) = 10.82
 *
 *     and a Q of 10.8 alone rings for about Q/(pi f0) — 21 ms at the hi tom's
 *     pitch. An 808 tom rings far longer than that, so the network is inside
 *     a feedback loop exactly as the bass drum's is, and DECAY IS LOOP GAIN
 *     here too.
 *
 *   - The TOM/CONGA switch. In the CONGA position it grounds a node; in the
 *     TOM position it puts 1.5k (R224, R280) there instead. It is in the
 *     TRIGGER path, not the tuning path — so what the switch changes is how
 *     the network is struck.
 *
 *   - Each channel takes P.N., the machine's pink noise, alongside its
 *     trigger. That is the drum head: the part of a tom that is a stick on a
 *     skin rather than a shell ringing.
 *
 * WHAT IS DERIVED AND WHAT IS NOT
 *
 * Derived, and marked below: Q = 10.82, from two resistors that are legible
 * in two of the three channels and identical in both.
 *
 * NOT derived, and fitted:
 *
 *   - The centre frequency. On the hardware it is set by the caps and a
 *     per-channel tuning trimmer, and here it is the lane's Tune pot around
 *     its base note, so it is a control rather than a constant and there is
 *     nothing to derive. The defaults stay on sc808's notes, which is what
 *     the kit was balanced and voiced against.
 *
 *   - The loop-gain map, the pitch drop, and the level and length of the
 *     noise skin. The schematic shows the noise is THERE and shows that the
 *     switch changes the strike; how much of each is a measurement nobody has
 *     published for this voice. Werner's papers do that work for the bass
 *     drum and the cymbal, and there is no equivalent for the toms. These are
 *     fitted by ear against 808 recordings and they are the weakest numbers
 *     in this file — which is exactly why they are named here rather than
 *     buried.
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
 * The TOM/CONGA switch, as the two things it actually changes.
 *
 * Conga grounds the node; tom puts 1.5k there. A grounded node is a cleaner,
 * smaller strike, which is the drier conga; 1.5k lets the pulse through, and
 * with it the pink noise that shares the path. So: the tom is struck harder
 * and gets the head, the conga is struck cleanly and is all shell.
 *
 * FITTED. The direction is the schematic's, the amounts are by ear.
 */
static const double kTOM_SkinLevel = 0.75;   /* noise into the strike, tom  */
static const double kTOM_SkinTau   = 0.0075; /* how long the head speaks    */
static const double kTOM_StrikeTom = 1.0;
static const double kTOM_StrikeCga = 0.72;

/* The pitch drop. sc808 falls from 1.33x through 1.12x to the note; the
 * circuit does it because the strike shifts the network's operating point and
 * it recovers. One exponential is close enough to that shape and this is a
 * fitted number either way. */
static const double kTOM_PitchLift = 0.33;
static const double kTOM_PitchTau  = 0.030;

/*
 * Loop gain at the two ends of the Decay pot.
 *
 * A bridged-T at Q inside a loop of gain g rings for roughly
 * Q / (pi f0 (1 - g)), so the top of the pot is set by how long an 808 tom
 * actually goes rather than by how close to unity the loop can get: 0.955
 * gives the hi tom about two seconds and the low tom about four, which is
 * already past anything musical. 0.995 gave it EIGHT, and a lane that rings
 * for eight seconds is a CPU bill, not a feature.
 */
static const double kTOM_GainMin = 0.750;
static const double kTOM_GainMax = 0.955;

/*
 * Putting the forward gain back.
 *
 * BridgedT normalises its peak to unity — that is what makes the feedback
 * gain around it mean exactly "loop gain" — and its own comment says the
 * caller has to restore the network's real forward gain on the input side.
 * Here that shows up as a frequency dependence: at a fixed Q the resonator's
 * bandwidth goes with f0, so the same strike puts less through a low drum
 * than a high one, and without this the low tom lands 5 dB under the hi tom
 * against sc808's, whose voices are all normalised by their own envelopes.
 *
 * The exponent is fitted over the six lanes, not derived.
 */
static const double kTOM_FwdRefHz = 120.0;
static const double kTOM_FwdPower = 0.30;

/*
 * Fitted so an unaccented hit at the default pots peaks where the sc808 voice
 * does — separately per mode, because the tom's noise skin makes it far
 * louder than the conga and the two are different lanes.
 *
 * This is not cosmetic. Each lane has ONE trim, shared by both engines, so if
 * they disagree about what a tom comes out at then switching engines changes
 * the mix. tools/tom_check asserts they still agree.
 */
static const double kTOM_OutScaleTom =  3.91;
static const double kTOM_OutScaleCga = 14.26;

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
        skinBp_.set(400.0, 0.9, _sr);
        reset();
    }

    bool active() const { return active_; }

    /*
     * freqHz    the lane's pitch, from its base note and Tune.
     * decay01   RAW POT POSITION, because here Decay is loop gain and not a
     *           time — the same reading the circuit kick and snare use.
     * accentV   trigger volts, 4 to 14, as the hardware's accent bus swings.
     */
    void trigger(const double _freqHz, const float _decay01, const float _accentV)
    {
        f0_ = _freqHz < 20.0 ? 20.0 : (_freqHz > sr_ * 0.25 ? sr_ * 0.25 : _freqHz);

        const double d = (double)(_decay01 < 0.0f ? 0.0f
                                : (_decay01 > 1.0f ? 1.0f : _decay01));
        loopGain_ = kTOM_GainMin + (kTOM_GainMax - kTOM_GainMin) * d;

        accentV_     = (double)_accentV;
        gateSamples_ = (int)(0.001 * sr_);      /* the CPU's 1 ms trigger */

        pitchEnv_  = 1.0;
        pitchCoef_ = exp(-1.0 / (kTOM_PitchTau * sr_));

        fwd_ = pow(kTOM_FwdRefHz / f0_, kTOM_FwdPower)
             * (mode_ == 0 ? kTOM_OutScaleTom : kTOM_OutScaleCga);

        skinEnv_  = (mode_ == 0) ? 1.0 : 0.0;
        skinCoef_ = exp(-1.0 / (kTOM_SkinTau * sr_));
        skinBp_.set(f0_ * 2.4, 0.9, sr_);

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

        const double strike = shaper_.process(gate)
                            * (mode_ == 0 ? kTOM_StrikeTom : kTOM_StrikeCga);

        /*
         * The head. Pink noise rides in with the trigger on the tom side of
         * the switch and is gone in a few milliseconds — a stick on a skin,
         * not a noise layer. The conga does not get it, and that is the
         * difference you actually hear between the two.
         */
        double skin = 0.0;
        if(skinEnv_ > 1e-5)
        {
            skin = (double)rng_.frand2() * skinEnv_ * kTOM_SkinLevel * accentV_;
            skinEnv_ *= skinCoef_;
        }

        /* ---- the pitch drop ---- */
        pitchEnv_ *= pitchCoef_;
        if(pitchEnv_ < 1e-5) pitchEnv_ = 0.0;
        if(--coefAge_ <= 0)
        {
            coefAge_ = 16;   /* 0.36 ms, far inside the drop */
            bt_.set(f0_ * (1.0 + kTOM_PitchLift * pitchEnv_), kTOM_Q, sr_);
        }

        /* ---- the loop ----
         * fb_ is last sample's output: the unit delay that breaks what is
         * otherwise a delay-free loop, as the bass drum paper prescribes. */
        const double x = strike + skin + loopGain_ * opampClip(fb_);
        const double y = bt_.process(x);
        fb_ = y;

        /* A little of the head goes round the resonator rather than through
         * it, or the skin would just be more shell. */
        double out = y + skinBp_.process(skin) * 0.35;

        /* the output buffer's series capacitor */
        dcZ_ += (out - dcZ_) * kDcCoef;
        out  -= dcZ_;

        if(!(out > -50.0 && out < 50.0)) { reset(); return 0.0f; }
        out *= fwd_;

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
        skinBp_.reset();
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
    double fb_ = 0.0, dcZ_ = 0.0;
    int    gateSamples_ = 0, coefAge_ = 0;
    bool   active_ = false;
    int    quiet_  = 0;

    PulseShaper shaper_;
    BridgedT    bt_, skinBp_;
    sc::RGen    rng_;
};

} /* namespace sc808 */

#endif /* SC808_TOM_CIRCUIT_H */
