/*
 * sc808_rs_circuit.h — the TR-808 claves, from the circuit, and the maracas.
 *
 * WHAT HAPPENED TO THE RIM SHOT. This file used to model both sides of the
 * hardware's RS/CL channel. The rim was built twice from the schematic —
 * first as the two bridged-T networks alone, then with the trigger edge's
 * wideband crack over them — and both lost to sc808's algorithm on
 * hardware, the second badly ("not good at all, I prefer sc808"). The rim
 * lane now runs the sc808 voice with no switch, tuned to rim808.wav, and
 * the rim modelling is GONE from here rather than left as unreachable code
 * pretending to be a rim shot.
 *
 * The measurements that pass are kept in the git history for whoever tries
 * again: the reference's first four milliseconds are the 1788 Hz network
 * under a crack reaching past 13 kHz, and by six milliseconds the tock is
 * 19 dB down while the 461 Hz body carries the note alone. What none of the
 * component values explain is the CHARACTER of that crack, and guessing at
 * it twice is what produced two voices worse than the transcription.
 *
 * WHAT IS DERIVED — service schematic, RIM SHOT / CLAVES block:
 *
 *   The claves' network (IC20): R308 820k series, C117 = C119 2.2 nF, and
 *   SW11 in the CL position leaving R312 ~1k in the shunt arm.
 *       f0 = 1/(2 pi sqrt(R1 R2 C1 C2)) = 2524 Hz
 *       Q  = (1/2) sqrt(R2/R1)          = 14.3
 *   Roland's own plugin, with the kit switch flipped programmatically,
 *   measures the claves at 2518 Hz — agreement to a quarter percent.
 *
 * WHAT IS FITTED — the loop gain. The network sits in op-amp feedback and
 * a bridged-T at Q 14 alone rings for 8 ms where the reference rings for
 * 30, so the second amp around IC20 is a regeneration path; its gain is
 * fitted to that measured ring time.
 *
 * GPL-3.0.
 */
#ifndef SC808_RS_CIRCUIT_H
#define SC808_RS_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* the claves' network — IC20 with SW11 in the CL position */
static const double kRS_R308  = 820.0e3;
static const double kRS_C2    = 2.2e-9;
static const double kRS_ShuntCL = 1.0e3;   /* R312 alone */

/* FITTED to Roland's render: the claves ring 30 ms to 1% of peak, where
 * the network's bare Q gives 8. */
static const double kRS_LoopClave = 0.74;

/* fitted so a default hit peaks near 1.0 into the shared lane trim */
static const double kRS_OutClave = 0.55;

class ClaveCircuit {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        shaper_.init(_sr);
        reset();
    }

    bool active() const { return active_; }

    /*
     * tuneRatio  multiplies the network's centre — a ratio, because the
     *            hardware has no tuning here at all and a note would lie.
     * decayScale 0..1 from the Decay pot: pot centre IS the measured
     *            hardware ring, the ends halve and double it.
     * accentV    trigger volts, 4..14.
     */
    void trigger(const double _tuneRatio, const float _decayScale,
                 const float _accentV)
    {
        const double r = _tuneRatio < 0.25 ? 0.25 : (_tuneRatio > 4.0 ? 4.0 : _tuneRatio);
        const double d = (double)(_decayScale < 0.0f ? 0.0f
                                 : (_decayScale > 1.0f ? 1.0f : _decayScale));
        const double stretch = pow(4.0, d - 0.5);

        bt_.set(bridgedTFreq(kRS_ShuntCL, kRS_R308, kRS_C2, kRS_C2) * r,
                bridgedTQ(kRS_ShuntCL, kRS_R308), sr_);
        g_ = 1.0 - (1.0 - kRS_LoopClave) / stretch;
        if(g_ < 0.0) g_ = 0.0;

        accentV_     = (double)_accentV;
        gateSamples_ = (int)(0.001 * sr_);
        active_ = true;
        quiet_  = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        double gate = 0.0;
        if(gateSamples_ > 0) { gate = accentV_; --gateSamples_; }
        const double strike = shaper_.process(gate);

        const double y0 = bt_.process(strike + g_ * opampClip(fb_));
        fb_ = y0;
        const double y = y0 * kRS_OutClave;

        if(!(y > -50.0 && y < 50.0)) { reset(); return 0.0f; }
        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 300) active_ = false;
        return o;
    }

private:
    void reset()
    {
        bt_.reset();
        fb_ = 0.0;
        gateSamples_ = 0;
        active_ = false; quiet_ = 0;
    }

    double sr_ = 44100.0;
    double g_ = 0.0, fb_ = 0.0;
    double accentV_ = 8.0;
    int    gateSamples_ = 0;
    bool   active_ = false;
    int    quiet_ = 0;

    PulseShaper shaper_;
    BridgedT    bt_;
};

/*
 * The maracas — HAND CLAP / MARACAS block, MA side of SW12.
 *
 * White noise from the machine's WN bus, high-passed hard, through the same
 * swing-VCA family as everything else, under a fast envelope. The corner
 * and the times are fitted to Roland's render with the kit switch flipped
 * to MA: energy centred 9.6-13 kHz, to 1% of peak in 39 ms, attack inside
 * two milliseconds.
 */
class MaracasCircuit {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        rng_.seed(0x808AAAC5u);
        setTune(1.0);
        active_ = false; quiet_ = 0;
    }

    bool active() const { return active_; }

    /*
     * attack01  the Attack pot's POSITION, 0..1 — not a time in seconds.
     *
     * THE POT USED TO DO NOTHING. It fed the sc808 maracas, and when the
     * Engine switches came off that branch went with it while this trigger
     * never took an attack at all: the knob stayed on the panel, resolved a
     * slot, and changed not one sample. Measured, not guessed — every
     * position hashed identically.
     *
     * It scales the FITTED attack rather than replacing it. kMA_AtkTau is
     * 1.2 ms because that is what Roland's own render measures, and the
     * scale is (position / default position) squared, which is exactly 1.0
     * at the shipped default — so the maracas that was signed off does not
     * move, and the knob opens a musical range either side: about 0.2 ms at
     * the bottom for a dry tsk, about 19 ms at the top for a soft shh.
     */
    void trigger(const double _tuneRatio, const float _decaySec,
                 const float _attack01, const float _accentV)
    {
        setTune(_tuneRatio);
        const double t = _decaySec > 0.008f ? (double)_decaySec : 0.008;
        levelA_ = 0.0;
        const double a = (double)sc::clampf(_attack01, 0.0f, 1.0f) / kMA_AtkDefault01;
        double atk = kMA_AtkTau * a * a;
        if(atk < 2.0e-4) atk = 2.0e-4;      /* a floor, not a click */
        atkCoef_ = exp(-1.0 / (atk * sr_));
        decCoef_ = exp(-1.0 / ((t * 0.85 / log(100.0)) * sr_));   /* meter-calibrated */
        peak_ = (double)_accentV / 8.0;
        rising_ = true;
        active_ = true; quiet_ = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;
        if(rising_)
        {
            levelA_ = peak_ + (levelA_ - peak_) * atkCoef_;
            if(levelA_ > peak_ * 0.99) rising_ = false;
        }
        else levelA_ *= decCoef_;

        const double n = (double)rng_.frand2();
        const double y = hpB_.process(hpA_.process(n)) * levelA_ * kMA_OutScale;

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 300) active_ = false;
        return o;
    }

private:
    static constexpr double kMA_OutScale = 1.9;   /* fitted, shared trim */

    /* The attack Roland's render measures: inside two milliseconds. */
    static constexpr double kMA_AtkTau = 0.0012;
    /*
     * The Attack pot's shipped default POSITION, 32/127 — spelled as the
     * float division the engine performs, so the scale lands on exactly 1.0
     * there and the default hit is bit-identical. Written as a double it
     * would sit an ulp away and move the voice; the snare's Decay fix
     * learned that one.
     */
    static constexpr double kMA_AtkDefault01 = (double)(32.0f / 127.0f);

    void setTune(const double _r)
    {
        const double r = _r < 0.25 ? 0.25 : (_r > 4.0 ? 4.0 : _r);
        hpA_.set(6300.0 * r > 15000.0 ? 15000.0 : 6300.0 * r, sr_);
        hpB_.set(4600.0 * r > 12000.0 ? 12000.0 : 4600.0 * r, sr_);
    }

    double sr_ = 44100.0;
    double levelA_ = 0, atkCoef_ = 0, decCoef_ = 0, peak_ = 1.0;
    bool   rising_ = false;
    bool   active_ = false;
    int    quiet_ = 0;

    OnePoleHP hpA_, hpB_;
    sc::RGen  rng_;
};

} /* namespace sc808 */

#endif /* SC808_RS_CIRCUIT_H */
