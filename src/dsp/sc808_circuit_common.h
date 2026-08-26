/*
 * sc808_circuit_common.h — the blocks the 808's voice circuits share.
 *
 * The TR-808 does not have eleven different voice designs. It has a handful
 * of blocks wired up in different arrangements: a trigger pulse, a shaper
 * that turns it into two spikes, a bridged-T network that rings, and an
 * op-amp that cannot swing past its rails. Werner, Abel and Smith's analysis
 * of the bass drum names those blocks, and their paper notes that the snare,
 * the toms and congas, and the rim shot and claves "all use bridged-T
 * networks in similar ways to the bass drum".
 *
 * So they live here, once, rather than being copied per voice — a copy that
 * drifts is worse than no sharing at all.
 *
 * GPL-3.0.
 */
#ifndef SC808_CIRCUIT_COMMON_H
#define SC808_CIRCUIT_COMMON_H

#include <math.h>

namespace sc808 {

static const double kCircPi = 3.14159265358979323846;

/*
 * The pulse shaper.
 *
 * A first-order high shelf followed by a diode. The shelf passes the high
 * frequencies of the CPU's 1 ms trigger pulse and throws away its body, which
 * leaves two spikes — one per edge — and those spikes are what actually kick
 * the bridged-T network. The diode then holds negative excursions at about
 * one diode drop, which is why the rising and falling edges do not simply
 * cancel each other out.
 *
 * From the bass drum's R162/R163/C40: 0.045 at DC rising to unity, cornering
 * at (R162+R163)/(R162 R163 C40) = 2.36 kHz. Other voices use the same
 * topology with their own values; the defaults here are the bass drum's.
 */
class PulseShaper {
public:
    void init(const double _sampleRate,
              const double _r1 = 4.7e3,
              const double _r2 = 100.0e3,
              const double _c  = 0.015e-6)
    {
        const double sr = _sampleRate >= 8000.0 ? _sampleRate : 44100.0;
        dc_ = _r1 / (_r1 + _r2);
        const double corner = (_r1 + _r2) / (_r1 * _r2 * _c);
        a_ = exp(-corner / sr);
        state_ = 0.0;
    }

    void reset() { state_ = 0.0; }

    /* `gate` is the trigger voltage while the pulse is high, 0 otherwise. */
    double process(const double _gate)
    {
        state_ = _gate + a_ * (state_ - _gate);
        double v = (_gate - state_) + dc_ * state_;
        /*  V+ >= 0 : V+
         *  V+ <  0 : 0.71 (e^V+ - 1)      — one diode drop, clamped
         * The exponent is floored because a large negative excursion would
         * otherwise underflow to the same answer more slowly. */
        if(v < 0.0) v = 0.71 * (exp(v > -20.0 ? v : -20.0) - 1.0);
        return v;
    }

private:
    double a_ = 0.0, dc_ = 0.045, state_ = 0.0;
};

/*
 * Op-amp saturation.
 *
 * Not a detail. With a decay pot up, a bridged-T's loop gain approaches unity
 * and a linear loop would run away; on the hardware the uPC4558 simply cannot
 * swing past its rails, so the note grows until the clip stops it and then
 * decays. That is where a long 808 note's slightly sat-on quality comes from.
 */
static inline double opampClip(const double _v, const double _rail = 12.0)
{
    return _rail * tanh(_v / _rail);
}

/*
 * A bridged-T network, as a resonator.
 *
 * Constant-peak-gain two-pole bandpass, unity at f0, transposed direct form
 * II — the form the paper recommends, because the coefficients here are
 * constantly time-varying and TDF-II behaves well under that.
 *
 * Normalising the peak to unity is what makes a feedback gain around this
 * mean exactly "loop gain". It also throws away the network's real forward
 * gain, so anything using it has to put that back on the input side. See
 * bd_forward_gain().
 */
class BridgedT {
public:
    void reset() { z1_ = z2_ = 0.0; }

    void set(const double _f0, const double _q, const double _sr)
    {
        const double f = _f0 < 20.0 ? 20.0 : (_f0 > _sr * 0.4 ? _sr * 0.4 : _f0);
        const double q = _q < 0.3 ? 0.3 : (_q > 60.0 ? 60.0 : _q);
        const double w0 = 2.0 * kCircPi * f / _sr;
        const double alpha = sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ =  alpha / a0;
        a1_ = -2.0 * cos(w0) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    double process(const double _x)
    {
        const double y = b0_ * _x + z1_;
        z1_ = -a1_ * y + z2_;          /* b1 is zero for a bandpass */
        z2_ = -b0_ * _x - a2_ * y;
        return y;
    }

private:
    double b0_ = 0.0, a1_ = 0.0, a2_ = 0.0, z1_ = 0.0, z2_ = 0.0;
};

/*
 * The two numbers a bridged-T's component values give you.
 *
 *   f0 = 1 / (2 pi sqrt(R1 R2 C1 C2))
 *   Q  = (1/2) sqrt(R2 / R1)
 *
 * R1 is the shunt arm and R2 the series arm. Both forms are in the bass drum
 * paper; the frequency one is also how Werner derives the snare's two
 * networks from the service notes' component values.
 */
static inline double bridgedTFreq(const double _r1, const double _r2,
                                  const double _c1, const double _c2)
{
    return 1.0 / (2.0 * kCircPi * sqrt(_r1 * _r2 * _c1 * _c2));
}

static inline double bridgedTQ(const double _r1, const double _r2)
{
    return 0.5 * sqrt(_r2 / _r1);
}

/* one-pole high pass */
class OnePoleHP {
public:
    void set(const double _f, const double _sr)
    { a_ = exp(-2.0 * kCircPi * _f / _sr); z_ = y_ = 0.0; }
    double process(const double _x)
    { y_ = a_ * (y_ + _x - z_); z_ = _x; return y_; }
    void reset() { z_ = y_ = 0.0; }
private:
    double a_ = 0, z_ = 0, y_ = 0;
};

/*
 * A multiple-feedback (Deliyannis) bandpass, from its component values.
 *
 * The other filter the 808 builds over and over. Where the bridged-T rings in
 * a feedback loop and makes a drum, this one just shapes noise, and the hand
 * clap's is the clearest example: R342 in, C128 and C129 equal, R334 around
 * the op-amp, no shunt leg to ground.
 *
 * With C1 = C2 = C and no shunt resistor:
 *
 *   f0 = 1 / (2 pi C sqrt(Rin Rf))
 *   Q  = (1/2) sqrt(Rf / Rin)
 *
 * Realised with BridgedT, which is the same constant-peak-gain two-pole
 * bandpass — the MFB's own passband gain (Rf / 2 Rin) is a property of the
 * stage, not of the response, so anything using this puts it back on the
 * input side exactly as the bridged-T voices do.
 */
static inline double mfbBandpassFreq(const double _rin, const double _rf,
                                     const double _c)
{
    return 1.0 / (2.0 * kCircPi * _c * sqrt(_rin * _rf));
}

static inline double mfbBandpassQ(const double _rin, const double _rf)
{
    return 0.5 * sqrt(_rf / _rin);
}

} /* namespace sc808 */

#endif /* SC808_CIRCUIT_COMMON_H */
