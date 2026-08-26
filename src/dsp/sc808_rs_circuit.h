/*
 * sc808_rs_circuit.h — the TR-808 rim shot and claves, from the circuit.
 *
 * One file because it is one channel: the hardware's SW11 selects RS or CL
 * on a shared pair of op-amp bridged-T networks (IC20/IC21 on the voicing
 * board). 8W8 gives each a lane, so this class takes a mode — the same
 * arrangement the tom/conga split uses.
 *
 * WHAT IS DERIVED — service schematic, RIM SHOT / CLAVES block:
 *
 *   Network 1 (IC21): R315 5.6k shunt, R316 1M series, C115 = C116 4.7 nF.
 *       f0 = 1/(2 pi sqrt(R1 R2 C1 C2)) = 452.5 Hz
 *       Q  = (1/2) sqrt(R2/R1)          = 6.68
 *
 *   Network 2 (IC20): R308 820k series, C117 = C119 2.2 nF, and the shunt
 *   arm is what SW11 switches: the CL position leaves ~1k (R312), the RS
 *   position adds about another 1k in the path.
 *       CL: R1 = 1.0k -> f0 = 2524 Hz, Q = 14.3
 *       RS: R1 = 2.0k -> f0 = 1785 Hz, Q = 10.1
 *   Roland's own plugin, with the kit switch flipped programmatically,
 *   measures the pair at 461 + 1788 Hz for the rim and 2518 Hz for the
 *   claves — component-value agreement to within 2%.
 *
 * WHAT IS FITTED — the loop gains. Both networks sit in op-amp feedback
 * (the second amp around IC20 is a regeneration path, R309/R314 33k), and
 * a bridged-T at these Qs alone rings for 8 ms where the references ring
 * for 16 (rim) and 30 (claves). The loop gains are fitted to those two
 * measured ring times; everything else about the loop is the same
 * unit-delay arrangement every other 8W8 circuit voice uses.
 *
 * GPL-3.0.
 */
#ifndef SC808_RS_CIRCUIT_H
#define SC808_RS_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* network 1 — the rim's body */
static const double kRS_R315 = 5.6e3;
static const double kRS_R316 = 1.0e6;
static const double kRS_C1   = 4.7e-9;

/* network 2 — the "tock" (RS) or the whole voice (CL) */
static const double kRS_R308  = 820.0e3;
static const double kRS_C2    = 2.2e-9;
static const double kRS_ShuntRS = 2.0e3;   /* SW11 in RS: R312 + the path */
static const double kRS_ShuntCL = 1.0e3;   /* SW11 in CL: R312 alone      */

/*
 * FITTED to Roland's render: rim to 1% of peak in 16 ms, claves in 30 ms.
 * The claves' higher loop gain is the audible difference between a click
 * and a ping, and it is what the extra feedback resistor in the CL position
 * of the real switch buys.
 */
static const double kRS_LoopRim1  = 0.10;   /* 452 Hz network, rim mode  */
static const double kRS_LoopRim2  = 0.42;   /* 1785 Hz network, rim mode */
static const double kRS_LoopClave = 0.74;   /* 2524 Hz network, CL mode  */

/* balance of the two networks in rim mode — the AU reference has them
 * within half a dB of each other */
static const double kRS_MixBody = 1.0;
static const double kRS_MixTock = 1.55;

/* fitted so a default hit peaks near 1.0 into the shared lane trim */
static const double kRS_OutRim   = 0.40;
static const double kRS_OutClave = 0.55;

class RimClaveCircuit {
public:
    /* mode 0 = rim shot, 1 = claves */
    void init(const double _sr, const int _mode)
    {
        sr_ = _sr; mode_ = _mode;
        shaper_.init(_sr);
        reset();
    }

    bool active() const { return active_; }

    /*
     * tuneRatio  multiplies both networks' centres — a ratio, because the
     *            hardware has no tuning here at all and a note would lie.
     * decayScale 0..1-ish from the Decay pot: scales the fitted loop gains
     *            toward and past their reference values, so the default
     *            pot IS the measured hardware and the knob still plays.
     * accentV    trigger volts, 4..14.
     */
    void trigger(const double _tuneRatio, const float _decayScale,
                 const float _accentV)
    {
        const double r = _tuneRatio < 0.25 ? 0.25 : (_tuneRatio > 4.0 ? 4.0 : _tuneRatio);
        const double d = (double)(_decayScale < 0.0f ? 0.0f
                                 : (_decayScale > 1.0f ? 1.0f : _decayScale));
        /* Pot centre = the fitted reference exactly; the ends halve and
         * double the ring: factor = 4^(d - 1/2). */
        const double stretch = pow(4.0, d - 0.5);

        if(mode_ == 0)
        {
            const double f1 = bridgedTFreq(kRS_R315, kRS_R316, kRS_C1, kRS_C1) * r;
            const double f2 = bridgedTFreq(kRS_ShuntRS, kRS_R308, kRS_C2, kRS_C2) * r;
            bt1_.set(f1, bridgedTQ(kRS_R315, kRS_R316), sr_);
            bt2_.set(f2, bridgedTQ(kRS_ShuntRS, kRS_R308), sr_);
            g1_ = 1.0 - (1.0 - kRS_LoopRim1) / stretch;
            g2_ = 1.0 - (1.0 - kRS_LoopRim2) / stretch;
            if(g1_ < 0.0) g1_ = 0.0;
            if(g2_ < 0.0) g2_ = 0.0;
        }
        else
        {
            const double f2 = bridgedTFreq(kRS_ShuntCL, kRS_R308, kRS_C2, kRS_C2) * r;
            bt2_.set(f2, bridgedTQ(kRS_ShuntCL, kRS_R308), sr_);
            g1_ = 0.0;
            g2_ = 1.0 - (1.0 - kRS_LoopClave) / stretch;
            if(g2_ < 0.0) g2_ = 0.0;
        }

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

        double y;
        if(mode_ == 0)
        {
            const double y1 = bt1_.process(strike + g1_ * opampClip(fb1_));
            const double y2 = bt2_.process(strike + g2_ * opampClip(fb2_));
            fb1_ = y1; fb2_ = y2;
            y = (y1 * kRS_MixBody + y2 * kRS_MixTock) * kRS_OutRim;
        }
        else
        {
            const double y2 = bt2_.process(strike + g2_ * opampClip(fb2_));
            fb2_ = y2;
            y = y2 * kRS_OutClave;
        }

        if(!(y > -50.0 && y < 50.0)) { reset(); return 0.0f; }
        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 300) active_ = false;
        return o;
    }

private:
    void reset()
    {
        bt1_.reset(); bt2_.reset();
        fb1_ = fb2_ = 0.0;
        gateSamples_ = 0;
        active_ = false; quiet_ = 0;
    }

    double sr_ = 44100.0;
    int    mode_ = 0;
    double g1_ = 0, g2_ = 0, fb1_ = 0, fb2_ = 0;
    double accentV_ = 8.0;
    int    gateSamples_ = 0;
    bool   active_ = false;
    int    quiet_ = 0;

    PulseShaper shaper_;
    BridgedT    bt1_, bt2_;
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

    void trigger(const double _tuneRatio, const float _decaySec,
                 const float _accentV)
    {
        setTune(_tuneRatio);
        const double t = _decaySec > 0.008f ? (double)_decaySec : 0.008;
        levelA_ = 0.0;
        atkCoef_ = exp(-1.0 / (0.0012 * sr_));
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
