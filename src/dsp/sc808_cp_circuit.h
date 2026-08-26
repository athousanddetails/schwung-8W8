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
 * The tail's level against the burst, FIXED — the Room pot is gone. Fitted
 * against Roland's own model rendering the default kit: its tail sits about
 * 30 dB under the burst peak, reaching 1% of peak at 0.364 s and 0.1% at
 * 0.98 s. 0.025 reproduces both to within a few percent. The tail is the
 * "ffft" after the smack, not a second clap.
 */
static const double kCP_TailMix = 0.025;

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
    void trigger(const double _tuneRatio, const float _decaySec,
                 const float _spreadSec, const float _room)
    {
        setTune(_tuneRatio);

        room_      = (double)_room;
        tailLevel_ = 1.0;
        tailDecay_ = exp(-1.0 / (double)(_decaySec > 1.0e-4f ? _decaySec : 1.0e-4f)
                              / sr_);

        spread_    = (int)(_spreadSec * sr_ + 0.5);
        if(spread_ < 1) spread_ = 1;
        pulseDecay_ = exp(-1.0 / (kCP_PulseTau * sr_));

        /*
         * Fire the first pulse now and schedule the rest. The burst is
         * counted in samples from the trigger rather than retriggered from
         * the previous pulse, so a wide Spread does not drift.
         */
        pulseLevel_ = 1.0;
        fired_      = 1;
        age_        = 0;

        active_ = true;
        quiet_  = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        /* the burst: three pulses, each an instant open and a fast collapse */
        if(fired_ < kCP_PULSES && age_ >= fired_ * spread_)
        {
            pulseLevel_ = 1.0;
            ++fired_;
        }
        ++age_;

        const double ctrl = pulseLevel_ + room_ * tailLevel_;
        pulseLevel_ *= pulseDecay_;
        tailLevel_  *= tailDecay_;

        /*
         * BA662, modelled as Werner models the snare's: half-wave
         * rectification times the control voltage. Same chip, same treatment,
         * and it is what gives the clap its slightly hard edge.
         */
        const double n   = (double)rng_.frand2();
        const double vca = (n > 0.0 ? n : 0.0) * ctrl;

        /*
         * VCA first, bandpass second — the order the board has, and the order
         * that matters: the filter rounds off the chopped edges of the burst,
         * which is why an 808 clap is a "phhat" and not three hard gates.
         */
        double y = bp_.process(vca) * kCP_OutScale;
        y = (double)hp_.process((float)y);

        const float out = (float)y;
        if(out > 3.2e-5f || out < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 200) active_ = false;
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
    double room_ = 1.0;
    double pulseLevel_ = 0.0, pulseDecay_ = 0.0;
    double tailLevel_  = 0.0, tailDecay_  = 0.0;
    int    spread_ = 441, fired_ = 0, age_ = 0;
    bool   active_ = false;
    int    quiet_  = 0;

    BridgedT bp_;      /* the MFB bandpass, as a constant-peak-gain 2-pole */
    sc::HPF  hp_;
    sc::RGen rng_;
};

} /* namespace sc808 */

#endif /* SC808_CP_CIRCUIT_H */
