/*
 * sc808_cp_circuit.h — the TR-808 hand clap, from the circuit.
 *
 * Engine B for the clap lane, the same arrangement the bass drum and the
 * snare already use: sc808's transcription stays exactly as it is and keeps
 * its place in the null test, this runs alongside it, and the panel switches.
 *
 * WHY THIS EXISTS
 *
 * sc808's clap is one immediate noise burst, a second one 26 ms later, and a
 * long diffuse tail. Two things follow from that shape and both of them were
 * reported as faults, correctly:
 *
 *   - "Decay doesn't work". In the SynthDef `decay` sets the attack burst's
 *     envelope, whose curve is -160 — so steep that the audible part lasts
 *     decay/160, under two milliseconds across the whole pot. The part you
 *     actually hear is a second envelope hard-coded to six seconds, which no
 *     pot reaches. The knob was real; it was wired to the inaudible half.
 *
 *   - "Spread is odd". One delayed burst over a 5..100 ms range is a flam,
 *     not a clap. The hardware does not have one delayed burst.
 *
 * WHAT THE HARDWARE DOES
 *
 * From the TR-808 service notes, voicing board, HAND CLAP / MARACAS:
 *
 *   - Q69 takes the trigger. Q70, with the C.P. OFFSET trimmer TM3 on its
 *     base, turns it into a SHORT BURST OF PULSES rather than one pulse. That
 *     burst is the clap: several hands, not quite together.
 *
 *   - C143 (1 uF) charges on the trigger and bleeds off through R362 (330k).
 *     That is the tail, and its time constant is a number you can read
 *     straight off the schematic: 330k x 1 uF = 330 ms. sc808's `decay`
 *     default is 0.3, so somebody knew; it just never reached the tail.
 *
 *   - The noise runs through IC22 (BA662, the same swing-type VCA the snare
 *     uses) and IC21's bandpass: R342 15k in, C128 = C129 = 0.0047 uF, R334
 *     100k around the op-amp, no shunt leg. That is a multiple-feedback
 *     bandpass at
 *
 *         f0 = 1 / (2 pi C sqrt(Rin Rf)) = 874.4 Hz
 *         Q  = (1/2) sqrt(Rf / Rin)      = 1.291
 *
 *     and 874 Hz is why an 808 clap sits where it does in a mix.
 *
 * WHAT IS DERIVED AND WHAT IS NOT
 *
 * Derived from component values, and marked below: the bandpass frequency and
 * Q, and the tail time constant.
 *
 * NOT derived, and fitted instead: the number of pulses in the burst and
 * their spacing. Q70's burst rate falls out of a transistor's switching
 * behaviour around TM3, which is exactly the kind of thing Werner's papers
 * spend pages on for the bass drum and which nobody has published for the
 * clap. Three pulses about 10 ms apart is the figure the 808 literature
 * agrees on and it is what recordings show; it is a measurement, not an
 * analysis, and it is a knob here rather than a constant so it does not have
 * to be exactly right. The per-pulse decay is fitted by ear.
 *
 * GPL-3.0.
 */
#ifndef SC808_CP_CIRCUIT_H
#define SC808_CP_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* ---- component values, TR-808 service notes, voicing board ------------- */

/* IC21's multiple-feedback bandpass. */
static const double kCP_R342 = 15.0e3;      /* input            */
static const double kCP_R334 = 100.0e3;     /* feedback         */
static const double kCP_C128 = 0.0047e-6;   /* = C129           */

/* C143 / R362: the tail. */
static const double kCP_R362 = 330.0e3;
static const double kCP_C143 = 1.0e-6;

/*
 * 330 ms. This is the hardware's tail and it is what the Decay pot's default
 * position means — gen_params.py sets cp_decay's default to this number, so
 * the knob at its default IS the circuit, and moving it is a departure you
 * chose. tools/cp_check asserts the two still agree.
 */
static const double kCP_TailTau = kCP_R362 * kCP_C143;

/*
 * Fitted, not derived. Three pulses is what the burst is generally measured
 * to be; the fourth "pulse" is the tail, which is a different circuit.
 */
static const int    kCP_PULSES    = 3;
static const double kCP_PulseTau  = 0.0026;  /* per-pulse collapse, by ear  */

/*
 * Output level, fitted so that the circuit clap and the sc808 clap sit at the
 * same loudness when the panel switches between them. Neither engine should
 * change the mix.
 */
static const double kCP_OutScale = 0.62;

/*
 * THE ENVELOPE, from the schematic this time.
 *
 * Two RC pairs, two different jobs, and the first version had them
 * conflated:
 *
 *   C144 0.47 uF x R365 82k = 38.5 ms   — the MAIN decay. The clap you
 *       hear: opens right after the comparator chain stops chopping and
 *       falls on this constant. The reference render decays 100 -> 45 in
 *       twenty milliseconds, which is exactly this RC.
 *
 *   C143 1 uF x R362 330k = 330 ms      — the FLOOR. The quiet "ffft"
 *       shelf the reference holds at 6-8% of peak out past 100 ms. This is
 *       the constant the first version used for the WHOLE tail, which is
 *       why the clap was a smack with nothing behind it at one setting and
 *       an endless wash at another.
 *
 * The AN6912 comparators chop the first three burst-spacings into teeth;
 * the reference puts the teeth at 47% of the main hit, each falling on a
 * ~4.5 ms constant, with the main envelope opening half a spacing after
 * the last tooth. Those three numbers are read off the reference's 5 ms
 * envelope table; everything else above is component-derived.
 *
 * The Decay pot scales BOTH tails together, default 1.0 = the hardware.
 */
static const double kCP_MainTau   = 0.0385;   /* C144 x R365, derived   */
static const double kCP_FloorTau  = 0.330;    /* C143 x R362, derived   */
static const double kCP_ToothLevel = 0.80;    /* reference envelope (post-BP) */
static const double kCP_ToothTau  = 0.0045;   /* reference envelope     */
static const double kCP_FloorMix  = 0.030;    /* reference envelope     */

class ClapCircuit {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        rng_.seed(0x808C1A7u);
        setTune(1.0);
        hp_.set(720.0, _sr);           /* C152/R376, 47k x 0.0047u = 720 Hz */
        hp_.reset();
    }

    bool active() const { return active_; }

    /*
     * tuneRatio  multiplies the bandpass centre — Tune, as a ratio, because
     *            this voice has no note to offset.
     * decaySec   the tail's time constant. The hardware's is 330 ms and that
     *            is the pot's default; this is the knob the report was about.
     * spreadSec  spacing between the pulses of the burst. ~10 ms on the
     *            hardware. Short is a tight clap, long is a room full of
     *            people failing to clap together, and both are useful.
     * room       level of the tail against the burst.
     */
    void trigger(const double _tuneRatio, const float _decayScale,
                 const float _spreadSec, const float _unused)
    {
        (void)_unused;
        setTune(_tuneRatio);

        /* pot centre = the hardware's derived constants; ends halve/double */
        const double d = (double)(_decayScale < 0.05f ? 0.05f
                                 : (_decayScale > 4.0f ? 4.0f : _decayScale));
        mainDecay_  = exp(-1.0 / (kCP_MainTau  * d * sr_));
        floorDecay_ = exp(-1.0 / (kCP_FloorTau * d * sr_));
        toothDecay_ = exp(-1.0 / (kCP_ToothTau * sr_));

        spread_ = (int)(_spreadSec * sr_ + 0.5);
        if(spread_ < 1) spread_ = 1;

        tooth_     = kCP_ToothLevel;          /* first tooth, now */
        fired_     = 1;
        age_       = 0;
        mainLevel_ = 0.0;
        mainTarget_= 0.0;
        floorLevel_= 0.0;
        /* the main envelope opens as the last tooth ends and charges over
         * a few milliseconds — the reference rises through window 6 and
         * peaks in window 7, with no dead gap after the teeth */
        mainOpen_  = (int)(2.8 * spread_);
        mainAtk_   = 1.0 - exp(-1.0 / (0.0030 * sr_));

        active_ = true;
        quiet_  = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        /* the teeth */
        if(fired_ < kCP_PULSES && age_ >= fired_ * spread_)
        {
            tooth_ = kCP_ToothLevel;
            ++fired_;
        }
        if(age_ == mainOpen_) { mainTarget_ = 1.0; floorLevel_ = kCP_FloorMix; }
        ++age_;

        const double ctrl = tooth_ + mainLevel_ + floorLevel_;
        tooth_ *= toothDecay_;
        if(mainTarget_ > 0.0)
        {
            mainLevel_ += (mainTarget_ - mainLevel_) * mainAtk_;
            if(mainLevel_ > 0.98) mainTarget_ = 0.0;    /* attack done */
        }
        else mainLevel_ *= mainDecay_;
        floorLevel_ *= floorDecay_;

        /* BA662, as Werner models it: half-wave conduction times the
         * control voltage */
        const double n   = (double)rng_.frand2();
        const double vca = (n > 0.0 ? n : 0.0) * ctrl;

        double y = bp_.process(vca) * kCP_OutScale;
        y = (double)hp_.process((float)y);

        const float out = (float)y;
        if(out > 3.2e-5f || out < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400) active_ = false;
        return out;
    }

private:
    void setTune(const double _ratio)
    {
        const double f = mfbBandpassFreq(kCP_R342, kCP_R334, kCP_C128) * _ratio;
        const double q = mfbBandpassQ(kCP_R342, kCP_R334);
        bp_.set(f, q, sr_);
    }

    double sr_ = 44100.0;
    double tooth_ = 0.0, toothDecay_ = 0.0;
    double mainLevel_ = 0.0, mainDecay_ = 0.0, mainTarget_ = 0.0, mainAtk_ = 0.0;
    double floorLevel_ = 0.0, floorDecay_ = 0.0;
    int    spread_ = 441, fired_ = 0, age_ = 0, mainOpen_ = 0;
    bool   active_ = false;
    int    quiet_  = 0;

    BridgedT bp_;      /* the MFB bandpass, as a constant-peak-gain 2-pole */
    sc::HPF  hp_;
    sc::RGen rng_;
};

} /* namespace sc808 */

#endif /* SC808_CP_CIRCUIT_H */
