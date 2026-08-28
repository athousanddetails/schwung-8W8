/*
 * sc808_mt_circuit.h — the TR-808 metal voices, from the circuit.
 *
 * One file because it is one circuit: the 808 generates its cowbell, cymbal,
 * open hat and closed hat from a SINGLE bank of six Schmitt-trigger square
 * oscillators (half of one HD14584), and the four voices are four different
 * ways of filtering and enveloping that bank. Werner, Abel and Smith's ICMC
 * 2014 paper analyses the cymbal end to end; the hi-hat side is read off the
 * service schematics (Q26..Q31 on the voicing board); the cowbell reuses
 * oscillators 5 and 6 exactly as the hardware does.
 *
 * WHAT IS DERIVED, AND FROM WHERE
 *
 *   Oscillator bank — Werner §3: nominal 205.3, 369.6, 304.4, 522.7 Hz,
 *   plus 800 and 540 on internal trimpots TM1/TM2. Duty cycle 47.98% and
 *   amplitude 5 V from the HD14584's thresholds. FREE-RUNNING: the bank
 *   never stops on the hardware, so no two hits are the same. All four
 *   voices read the SAME bank, which is why an 808's cymbal and hats sit
 *   together the way they do.
 *
 *   Band pass filters — Werner §4, the paper's H(s) by bilinear transform,
 *   coefficients from component values (R56 56k, R57 82k; C 6.8n pairs
 *   for BP1, 3.3n for BP2). The input impedance is the mixer's ~1k bus,
 *   solved exactly so the response peaks at the paper's stated centres:
 *   3445 Hz and 7101 Hz.
 *
 *   Hi-hat filter ladders — service schematic: the bank couples to each
 *   hat's VCA through a two-section C-R high-pass, 1.5 nF sections for the
 *   OPEN hat (C64/C65/C66/C68 with R147 2.7k, R146 68k) and 1.0 nF for the
 *   CLOSED (C69/C70/C72/C73, R153 2.7k, R155 68k) — the closed hat is
 *   high-passed HIGHER than the open, which is most of why it is the
 *   thinner, tickier voice. Output stage: inverting amp, gain R159/R160 =
 *   470k/39k = 12 with 220 pF across the feedback (1.54 kHz corner).
 *
 *   Envelope times — the OPEN hat's decay is VR (2M) against C62 0.47 uF
 *   (up to ~1 s); the CLOSED hat has no decay control: R173 330k into C63
 *   0.47 uF, with the diode gate cutting the tail. The cymbal's three
 *   envelopes are Werner §7/Fig 7: a fast tall crash (EG3), a mid body
 *   (EG2), and the pot-controlled sustain (EG1) whose release runs through
 *   VR2 — the panel's CY Decay.
 *
 *   Swing VCAs — Werner §8: half-wave conduction scaled by the envelope,
 *   with the DIODE GATE that shuts the voice completely once the envelope
 *   falls below the diode's forward drop (V_on = 0.5899 V) — the abrupt
 *   little death at the end of an 808 hat tail is that diode.
 *
 *   Sallen-Key high passes — Werner §9: HP1 2nd order from R124/R127 22k/82k
 *   with 1.5 n (2.50 kHz, Q 0.97); HP2 2nd order non-unity gain; HP3 3rd
 *   order with the resonance near 10.5 kHz the paper calls out. HP3's peak
 *   is where the cymbal's sizzle lives.
 *
 * WHAT IS FITTED, AND AGAINST WHAT
 *
 *   Envelope time constants and band levels are fitted against Roland's own
 *   TR-808 plugin rendered offline at the default kit (the same reference
 *   the toms were corrected against) and against hardware sample decays:
 *   CH to 1% of peak in 85 ms, OH 0.40 s, CY 1.79 s, CB 0.43 s. Each
 *   fitted constant says so beside its value. tools/mt_check asserts the
 *   lot.
 *
 * GPL-3.0.
 */
#ifndef SC808_MT_CIRCUIT_H
#define SC808_MT_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* ---- the oscillator bank ---------------------------------------------- */

/*
 * Werner §3's measured values — from ONE unit, on parts with several
 * percent of tolerance — nudged by fractions of a percent so that no two
 * harmonics of different oscillators sit within a few hertz of each other
 * in the hats' passband. Exactly coincident digital oscillators beat
 * SLOWLY and deeply: harmonics slid into alignment 200-330 ms into a note
 * and rose 10 dB out of the falling tail, which the field heard as a
 * phantom second hit. Real units sit at their own slightly-off points and
 * their near-collisions shimmer fast instead of swelling. These offsets
 * stay well inside the hardware's own spread.
 */
static const double kMT_OscHz[6] = { 206.1, 368.2, 305.9, 521.0, 803.1, 541.7 };
static const double kMT_Duty    = 0.4798;   /* HD14584 thresholds          */
static const double kMT_OscAmp  = 2.5;      /* +/-2.5 V about the midpoint */

/*
 * The passive mixing network: each oscillator reaches the bus through 120k
 * into R53 1k, so each contributes V * R53/(R53 + 120k||the others). With
 * six 120k sources the parallel load is 20k and the divider lands near
 * 1/21 per leg. One constant, derived.
 */
static const double kMT_MixAtten = 1.0 / 21.0;

class SchmittBank {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        for(int i = 0; i < 6; ++i) { phase_[i] = 0.25 * i; }   /* staggered */
        for(int i = 0; i < 6; ++i) inc_[i] = kMT_OscHz[i] / _sr;
        for(int i = 0; i < 6; ++i) drift_[i] = 0.0;
        for(int i = 0; i < 6; ++i) jit_[i] = 1.0;
        rng_.seed(0x808D51F7u);
        driftCnt_ = 0;
        ratio_ = 1.0;
    }

    /* Tune, as a ratio on the whole bank — the "circuit bend" every 808
     * modder does first, and the same control the sc808 metal had. */
    void setRatio(const double _r)
    {
        const double r = _r < 0.25 ? 0.25 : (_r > 4.0 ? 4.0 : _r);
        for(int i = 0; i < 6; ++i) inc_[i] = kMT_OscHz[i] * r / sr_;
        ratio_ = r;
    }

    /*
     * One sample of the summed bus, BANDLIMITED. Naive squares seemed
     * defensible (Werner forgoes alias suppression too) until the aliases
     * were caught red-handed: folded ultrasonic harmonics land hertz away
     * from direct lines in the hats' passband, and those pairs beat SLOWLY
     * and deeply — a swell 300 ms into the open hat's tail that the field
     * heard as a phantom second hit, and that no detune could kill because
     * an analog square simply has no folded lines to collide with. PolyBLEP
     * rounds each edge over two samples; the folded energy drops ~30 dB and
     * the collisions go with it.
     *
     * THE ENGINE CALLS THIS ONCE PER SAMPLE and hands the bus to every
     * metal voice. A voice must never tick the bank itself: two sounding
     * voices would advance the oscillators twice per sample and detune the
     * whole bank exactly when the hats and cymbal play together.
     */
    static double blep(const double _t, const double _dt)
    {
        /* standard two-sample polynomial edge correction */
        if(_t < _dt)       { const double x = _t / _dt;         return x + x - x * x - 1.0; }
        if(_t > 1.0 - _dt) { const double x = (_t - 1.0) / _dt; return x * x + x + x + 1.0; }
        return 0.0;
    }
    double tick()
    {
        /*
         * PER-CYCLE JITTER — the "refined noise". A real Schmitt trigger's
         * thresholds carry noise, so every period is slightly its own
         * length, and each harmonic h is broadened by h times the jitter:
         * the fundamentals stay tonal while the high harmonics — where the
         * cymbal and hats live — blur into narrowband noise. The player's
         * reference measures spectral flatness 0.21 in the crash band where
         * exact squares gave a comb at 0.08; this is the difference between
         * a wash and a chord. Sigma calibrated to that measurement.
         */
        /*
         * DRIFT. Digitally exact oscillators produce exact, coherent beats:
         * near-coincident harmonics of the six squares slide into
         * constructive alignment a couple of hundred milliseconds into a
         * note and rise 8-10 dB out of a falling tail — the field heard it
         * as "another hit at the end". Real HD14584 stages drift with
         * temperature and supply (Werner's footnote credits exactly this
         * variation with each 808's individual character), and that drift
         * is what keeps the beats smeared. A bounded random walk, about
         * +/-0.1%, updated every ~6 ms per oscillator, does the same.
         */
        if(--driftCnt_ <= 0)
        {
            driftCnt_ = 256;
            for(int i = 0; i < 6; ++i)
            {
                drift_[i] += 2.5e-4 * (double)rng_.frand2();
                drift_[i] *= 0.98;             /* bounded, zero-mean */
            }
        }
        double sum = 0.0;
        for(int i = 0; i < 6; ++i)
        {
            const double dt = inc_[i] * (1.0 + drift_[i]) * jit_[i];
            phase_[i] += dt;
            if(phase_[i] >= 1.0)
            {
                phase_[i] -= 1.0;
                jit_[i] = 1.0 + 0.0052 * (double)rng_.frand2();
            }
            double v = phase_[i] < kMT_Duty ? 1.0 : -1.0;
            /* rising edge at phase 0, falling at kMT_Duty */
            v += blep(phase_[i], dt);
            double tf = phase_[i] - kMT_Duty; if(tf < 0.0) tf += 1.0;
            v -= blep(tf, dt);
            sum += v * kMT_OscAmp;
        }
        return sum * kMT_MixAtten;
    }

    /* oscillator 5 and 6 alone — the cowbell's pair, read (not ticked)
     * from the phase the engine's single tick() advanced */
    double cowbellPair(double *o5, double *o6) const
    {
        *o5 = phase_[4] < kMT_Duty ? kMT_OscAmp : -kMT_OscAmp;
        *o6 = phase_[5] < kMT_Duty ? kMT_OscAmp : -kMT_OscAmp;
        return *o5 + *o6;
    }

private:
    double sr_ = 44100.0, ratio_ = 1.0;
    double phase_[6] = {0}, inc_[6] = {0}, drift_[6] = {0};
    double jit_[6] = {1, 1, 1, 1, 1, 1};
    int    driftCnt_ = 0;
    sc::RGen rng_;
};

/* ---- Werner's band pass filters, by bilinear transform ----------------- */

/*
 * H(s) = (b2 s^2 + b1 s) / (a3 s^3 + a2 s^2 + a1 s + 1), coefficients from
 * the component values (Werner Eq 5). Discretised once at init; Direct Form
 * II over three states.
 */
class WernerBandpass {
public:
    void set(const double _rIn, const double _rQ, const double _rF,
             const double _cA, const double _cB, const double _cIn,
             const double _sr)
    {
        /* paper naming for BP#1: rIn=R52 rQ=R56 rF=R57 cA=C13 cB=C14 cIn=C10 */
        const double b2 = -_rQ * _rF * (_cA + _cB) * _cIn;
        const double b1 = -_rF * _cIn;
        const double a3 = _rQ * _rF * _rIn * _cA * _cB * _cIn;
        const double a2 = _rQ * _rF * _cA * _cB + _rQ * _rIn * (_cA + _cB) * _cIn;
        const double a1 = _rQ * _cA + _rQ * _cB + _rIn * _cIn;

        /* bilinear: s = K (1-z^-1)/(1+z^-1), K = 2 fs. Over the common
         * (1+z^-1)^3: s^2 maps through (1-z)^2(1+z) = 1 - z - z^2 + z^3,
         * s through (1-z)(1+z)^2 = 1 + z - z^2 - z^3. */
        const double K = 2.0 * _sr, K2 = K * K, K3 = K2 * K;
        const double A = a3 * K3 + a2 * K2 + a1 * K + 1.0;
        nb_[0] = (b2 * K2 * 1.0 + b1 * K * 1.0) / A;
        nb_[1] = (b2 * K2 * -1.0 + b1 * K * 1.0) / A;
        nb_[2] = (b2 * K2 * -1.0 + b1 * K * -1.0) / A;
        nb_[3] = (b2 * K2 * 1.0 + b1 * K * -1.0) / A;
        /* denominator: a3 K^3(1-z)^3 + a2 K^2(1-z)^2(1+z) + a1 K(1-z)(1+z)^2 + (1+z)^3 */
        da_[0] = 1.0;
        da_[1] = (a3 * K3 * -3.0 + a2 * K2 * -1.0 + a1 * K * 1.0 + 3.0) / A;
        da_[2] = (a3 * K3 *  3.0 + a2 * K2 * -1.0 + a1 * K * -1.0 + 3.0) / A;
        da_[3] = (a3 * K3 * -1.0 + a2 * K2 *  1.0 + a1 * K * -1.0 + 1.0) / A;
        z1_ = z2_ = z3_ = 0.0;
    }

    double process(const double _x)
    {
        /* DF-II transposed, 3rd order */
        const double y = nb_[0] * _x + z1_;
        z1_ = nb_[1] * _x - da_[1] * y + z2_;
        z2_ = nb_[2] * _x - da_[2] * y + z3_;
        z3_ = nb_[3] * _x - da_[3] * y;
        return y;
    }

    void reset() { z1_ = z2_ = z3_ = 0.0; }

private:
    double nb_[4] = {0}, da_[4] = {0};
    double z1_ = 0, z2_ = 0, z3_ = 0;
};

/* ---- small blocks ------------------------------------------------------ */

/* 2nd-order Sallen-Key high pass as a biquad: f, Q, and a gain. */
class SKHighpass {
public:
    void set(const double _f, const double _q, const double _gain, const double _sr)
    {
        const double w0 = 2.0 * kCircPi * _f / _sr;
        const double alpha = sin(w0) / (2.0 * _q);
        const double c = cos(w0), a0 = 1.0 + alpha;
        b0_ = _gain * (1.0 + c) * 0.5 / a0;
        b1_ = _gain * -(1.0 + c) / a0;
        b2_ = _gain * (1.0 + c) * 0.5 / a0;
        a1_ = -2.0 * c / a0;
        a2_ = (1.0 - alpha) / a0;
        z1_ = z2_ = 0.0;
    }
    double process(const double _x)
    {
        const double y = b0_ * _x + z1_;
        z1_ = b1_ * _x - a1_ * y + z2_;
        z2_ = b2_ * _x - a2_ * y;
        return y;
    }
    void reset() { z1_ = z2_ = 0.0; }
private:
    double b0_=0,b1_=0,b2_=0,a1_=0,a2_=0,z1_=0,z2_=0;
};

/*
 * The swing VCA, with its diode gate.
 *
 * Werner §8: half-wave conduction scaled by the envelope; the diode stops
 * conducting once the envelope is below its forward drop, so the voice does
 * not fade to nothing — it fades to V_on and then DIES. That little gated
 * ending is an 808 signature and the reason a hat tail never quite behaves
 * like a reverb tail.
 */
static const double kMT_DiodeVon = 0.5899;   /* Werner's least-squares fit */

static inline double swingVCA(const double _x, const double _env,
                              const double _knee = 0.35)
{
    /*
     * The gate has a KNEE, not an edge. Werner fits the VCA's lower edge as
     * a sum of stretched exponentials — the diode comes out of conduction
     * over a region, not at a point — and a hard cut here measured as the
     * whole tail ending 40 dB early. Quadratic through the knee's span of
     * approach reproduces the soft landing without the curve-fit costs.
     * The knee is per voice: the hats' references glide through a one-to-
     * two-percent tail for twenty milliseconds after the note has died,
     * and the narrow default cut that glide off.
     */
    const double over = _env - kMT_DiodeVon;
    double drive;
    if(over <= 0.0) return 0.0;
    /* over^2/(2 knee): meets the linear branch at knee/2 WITH matching
     * slope. The first version dropped the factor of two, and the branch
     * boundary was a doubling step the envelope crossed on the way down —
     * an audible phantom hit late in every tail, position fixed at
     * env = Von + knee whatever the oscillators did. */
    else if(over < _knee) drive = over * over / (2.0 * _knee);
    else drive = over - _knee * 0.5;
    const double hw = _x > 0.0 ? _x : 0.0;        /* half-wave conduction */
    return hw * drive;
}

/* attack-smoothed envelope: 1-pole up at Werner's tau, exponential down */
class MetalEnv {
public:
    void init(const double _sr)
    {
        sr_ = _sr;
        up_ = exp(-1.0 / (1.0244e-4 * _sr));     /* Werner §6 attack smoother */
        v_ = 0.0; target_ = 0.0; down_ = 1.0;
    }
    void trigger(const double _peak, const double _decaySeconds)
    {
        target_ = _peak;
        down_ = exp(-1.0 / (_decaySeconds * sr_));
    }
    double tick()
    {
        /*
         * PER-CYCLE JITTER — the "refined noise". A real Schmitt trigger's
         * thresholds carry noise, so every period is slightly its own
         * length, and each harmonic h is broadened by h times the jitter:
         * the fundamentals stay tonal while the high harmonics — where the
         * cymbal and hats live — blur into narrowband noise. The player's
         * reference measures spectral flatness 0.21 in the crash band where
         * exact squares gave a comb at 0.08; this is the difference between
         * a wash and a chord. Sigma calibrated to that measurement.
         */
        if(target_ > 0.0)
        {
            v_ = target_ + (v_ - target_) * up_;
            if(v_ >= target_ * 0.995) target_ = 0.0;   /* attack done */
        }
        else v_ *= down_;
        return v_;
    }
    bool dead() const { return target_ <= 0.0 && v_ < kMT_DiodeVon * 0.5; }
    void kill() { v_ = 0.0; target_ = 0.0; }
private:
    double sr_ = 44100.0, v_ = 0, target_ = 0, up_ = 0, down_ = 1;
};

/* ---- the cymbal -------------------------------------------------------- */

/*
 * Werner's three-path structure, voiced entirely to the player's own
 * reference render (808cy.wav), whose per-band story is:
 *
 *   5.8-9 kHz — the LOUDEST band: the crash, tau ~135 ms, over a long
 *   quiet tail. 9.5-14 kHz — the top: -6.6 dB, fastest, tau ~85 ms.
 *   2.8-5.2 kHz — the shimmer: -12 dB QUIET, RISING to its peak around
 *   200 ms and ringing with tau ~600 ms. The whole-hit spectrum:
 *   -13 / -8 / -4 / 0 / -7.5 / -7.
 *
 * The VCAs are LINEAR with the diode gate in the envelope — the same
 * lesson the hats taught twice: half-wave rectifying a many-line cluster
 * mints broadband difference tones ("garbled, dark, full of shit" was the
 * field report, and it was the rectifier). And no Tone pot: the panel is
 * Tune / Decay / Level like the lane deserves; the crash balance is the
 * reference's, fixed.
 */
static double kCY_SusW    = 0.032;
static double kCY_BodyW   = 1.00;
static double kCY_TopW    = 0.14;
static double kCY_SusHPHz = 4300.0;
static double kCY_SusLPHz = 5200.0;
static double kCY_SusAtk  = 0.070;   /* the shimmer BLOOMS, ~200 ms to peak */
static double kCY_EG1Div  = 2.2;     /* pot seconds -> shimmer tau */
static double kCY_BodyLPHz = 9200.0; /* the band above 9 k belongs to the fast top path */
static double kCY_EG2Tau  = 0.220;
static double kCY_EG3Tau  = 0.085;
static double kCY_FloorW  = 0.004;   /* the long quiet tail under the crash */
static constexpr double kCY_OutScale = 0.74;
static const double kCY_Knee = 1.2;

class CymbalCircuit {
public:
    void init(const double _sr, SchmittBank *_bank)
    {
        sr_ = _sr; bank_ = _bank;
        bp1_.set(870.0, 56.0e3, 82.0e3, 6.8e-9, 6.8e-9, 3.3e-9, _sr);
        bp2_.set(1393.0, 56.0e3, 82.0e3, 3.3e-9, 3.3e-9, 1.0e-9, _sr);
        susHp_.set(kCY_SusHPHz, 0.9, 1.0, _sr);
        susLpA_ = exp(-2.0 * kCircPi * kCY_SusLPHz / _sr);
        susLpZ_ = 0.0;
        bodySk_.set(6500.0, 0.9, 1.0, _sr);
        bodyHpA_.set(5000.0, _sr);
        bodyHpB_.set(4400.0, _sr);
        bodyLpA_ = exp(-2.0 * kCircPi * kCY_BodyLPHz / _sr);
        bodyLpZ_[0] = bodyLpZ_[1] = 0.0;
        topHp_.set(9500.0, 1.2, 1.0, _sr);
        dc_.set(200.0, _sr);
        e1_ = e2_ = e3_ = 0.0; e1t_ = 0.0;
        d1_ = d2_ = d3_ = 1.0; atkC_ = 0.0;
        active_ = false; quiet_ = 0;
    }

    bool active() const { return active_; }

    void trigger(const float _decaySec, const float _accentV)
    {
        const double a = (double)_accentV / 8.0;
        const double t = _decaySec > 0.1f ? (double)_decaySec : 0.1;
        susHp_.set(kCY_SusHPHz, 0.9, 1.0, sr_);
        susLpA_ = exp(-2.0 * kCircPi * kCY_SusLPHz / sr_);
        bodyLpA_ = exp(-2.0 * kCircPi * kCY_BodyLPHz / sr_);
        e1t_ = 8.0 * a;                 /* shimmer target, reached slowly */
        atkC_ = 1.0 - exp(-1.0 / (kCY_SusAtk * sr_));
        d1_ = exp(-1.0 / ((t / kCY_EG1Div) * sr_));
        e2_ = 8.0 * a; d2_ = exp(-1.0 / (kCY_EG2Tau * sr_));
        e3_ = 8.0 * a; d3_ = exp(-1.0 / (kCY_EG3Tau * sr_));
        active_ = true; quiet_ = 0;
    }

    float process(const double _bus)
    {
        if(!active_) return 0.0f;

        const double b1 = opampClip(bp1_.process(_bus), 14.0);
        const double b2 = opampClip(bp2_.process(_bus), 14.0);

        /* shimmer env: attack toward target, then the pot's decay */
        if(e1t_ > 0.0)
        {
            e1_ += (e1t_ - e1_) * atkC_;
            if(e1_ > e1t_ * 0.99) e1t_ = 0.0;
        }
        else e1_ *= d1_;
        e2_ *= d2_;
        e3_ *= d3_;

        /* linear VCAs, diode gate in the envelope */
        const double g1 = gate(e1_), g2 = gate(e2_), g3 = gate(e3_);

        double sus = susHp_.process(b1) * g1;
        susLpZ_ += (sus - susLpZ_) * (1.0 - susLpA_);
        sus = susLpZ_;

        const double bodyIn = bodyHpB_.process(bodyHpA_.process(bodySk_.process(b2)));
        double bodyDk = bodyIn;
        bodyLpZ_[0] += (bodyDk - bodyLpZ_[0]) * (1.0 - bodyLpA_); bodyDk = bodyLpZ_[0];
        bodyLpZ_[1] += (bodyDk - bodyLpZ_[1]) * (1.0 - bodyLpA_); bodyDk = bodyLpZ_[1];
        const double body = bodyDk * (g2 + kCY_FloorW * g1);
        const double top  = topHp_.process(bodyIn) * g3;

        double y = sus * kCY_SusW + body * kCY_BodyW + top * kCY_TopW;
        y = dc_.process(y * kCY_OutScale);

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400 && e1_ < kMT_DiodeVon * 0.5 && e2_ < kMT_DiodeVon
                && e3_ < kMT_DiodeVon)
            active_ = false;
        return o;
    }

private:
    static double gate(const double _e)
    {
        const double over = _e - kMT_DiodeVon;
        if(over <= 0.0) return 0.0;
        if(over < kCY_Knee) return over * over / (2.0 * kCY_Knee);
        return over - kCY_Knee * 0.5;
    }

    double sr_ = 44100.0;
    SchmittBank *bank_ = nullptr;
    WernerBandpass bp1_, bp2_;
    SKHighpass susHp_, topHp_, bodySk_;
    OnePoleHP bodyHpA_, bodyHpB_, dc_;
    double susLpA_ = 0, susLpZ_ = 0, bodyLpA_ = 0, bodyLpZ_[2] = {0, 0};
    double e1_ = 0, e2_ = 0, e3_ = 0, e1t_ = 0;
    double d1_ = 1, d2_ = 1, d3_ = 1, atkC_ = 0;
    bool active_ = false;
    int  quiet_ = 0;
};

/* ---- the hi-hats ------------------------------------------------------- */

/*
 * Schematic: bank -> two-section C-R ladder -> swing VCA -> x12 inverting
 * amp with a 1.54 kHz feedback corner. OPEN couples through 1.5 nF, CLOSED
 * through 1.0 nF — the closed hat's ladder corner sits higher, which is the
 * circuit's way of making it the thinner voice. The open hat's decay is the
 * panel pot (VR against C62 0.47 uF); the closed hat's is fixed by R173
 * 330k. Both tails end at the diode, not at zero.
 */
class HatCircuit {
public:
    /* mode 0 = closed, 1 = open */
    void init(const double _sr, SchmittBank *_bank, const int _mode)
    {
        sr_ = _sr; bank_ = _bank; mode_ = _mode;
        /*
         * The coupling ladder, read off the board AS COMPONENT VALUES this
         * time: the series caps (1 nF closed, 1.5 nF open) against the
         * 2.7 k shunt put the section corners at 59 and 39 kHz — ABOVE
         * NYQUIST. These sections are not "highpass corners", they are
         * DIFFERENTIATORS: +6 dB per octave each across the whole audio
         * band. Modelling them as one-poles with in-band corners was the
         * closed hat's entire mud problem: the bank's strong 2-4 kHz
         * harmonics only fell 13 dB when the references have them at 22.
         */
        d1_ = d2_ = 0.0;
        /*
         * The junk wall: the VCA input network and output coupling as two
         * true 2nd-order sections. Corners fitted against the band tables
         * of Roland's render and the player's pattern: 3.5 kHz closed,
         * 2.3 kHz open (the open hat's bigger caps sit its whole voice
         * lower). Result matches the AU's six-band split within ~1 dB.
         */
        /* asymmetric pair: skA sits resonant at the top of the wall and
         * its Q-bump restores the 4.5-6.5 kHz the reference keeps flat;
         * skB below it does the junk rejection */
        if(_mode == 0) { skA_.set(5200.0, 1.6, 1.0, _sr); skB_.set(2700.0, 0.75, 1.0, _sr); }
        else           { skA_.set(6200.0, 0.9, 1.0, _sr); skB_.set(5200.0, 0.8, 1.0, _sr); }
        /*
         * The top. Closed: one pole at 10.5 kHz flattening the aliased
         * edges — the CH is the BRIGHT hat. Open: the references (Roland's
         * render and the player's, in exact agreement) peak at 4.5-6.5 kHz
         * and FALL -4 / -9.6 / -12 dB up the octave bands — the OH is the
         * DARK hat. Two poles above the peak build that fall.
         */
        lpTopA_ = exp(-2.0 * kCircPi * (_mode == 1 ? 5900.0 : 10500.0) / _sr);
        for(int k = 0; k < 5; ++k) lpZ_[k] = 0.0;
        env_.init(_sr);
        dc_.set(200.0, _sr);
        outScale_ = _mode == 1 ? 5.0 : 8.0;
        active_ = false; quiet_ = 0;
    }

    bool active() const { return active_; }

    void trigger(const float _decaySec, const float _accentV)
    {
        const double a = (double)_accentV / 8.0;
        const double t = _decaySec > 0.02f ? (double)_decaySec : 0.02;
        if(mode_ == 1)
        {
            /*
             * The open hat's envelope is a LINEAR discharge, not an
             * exponential: every reference holds a near-flat plateau and
             * then collapses — C62 discharging through the decay pot into
             * the diode's cut. An exponential here decays audibly from the
             * first millisecond and reads as a completely different
             * instrument, which the field confirmed in those words.
             */
            /* hold flat for a third of the note, then discharge — the
             * reference's plateau-then-ramp, C62 held up by the follower
             * before the pot wins */
            /* C62 is half a microfarad: it CHARGES over several
             * milliseconds, and the references bloom — the player's render
             * puts 0.0%% of its energy in the first three milliseconds
             * where ours stabbed in at five. Attack ramp, then plateau,
             * then the discharge. */
            linTarget_ = 8.0 * a;
            linLevel_  = 0.0;
            linAtkC_   = 1.0 - exp(-1.0 / (0.006 * sr_));
            linHold_   = (int)(0.30 * t * sr_);
            linStep_   = linTarget_ / (0.70 * t * sr_);
            linear_    = true;
        }
        else
        {
            env_.trigger(8.0 * a, t / 2.0);
            linear_ = false;
        }
        active_ = true; quiet_ = 0;
    }

    void choke() { env_.kill(); linLevel_ = 0.0; }

    float process(const double _bus)
    {
        if(!active_) return 0.0f;

        /* two differentiator sections; x30 restores working level */
        const double t1 = _bus - d1_; d1_ = _bus;
        const double hp = (t1 - d2_) * 30.0; d2_ = t1;

        /*
         * The transistor as an amplifier with the diode's gate in the
         * ENVELOPE, not as a half-wave rectifier of the signal — half-wave
         * rectification of a six-square cluster mints broadband difference
         * tones no coupling can reject without killing the mids.
         */
        double e;
        if(linear_)
        {
            e = linLevel_;
            if(linTarget_ > 0.0)
            {
                linLevel_ += (linTarget_ - linLevel_) * linAtkC_;
                if(linLevel_ > linTarget_ * 0.99) linTarget_ = 0.0;
            }
            else if(linHold_ > 0) --linHold_;
            else
            {
                linLevel_ -= linStep_;
                if(linLevel_ < 0.0) linLevel_ = 0.0;
            }
        }
        else e = env_.tick();
        const double over = e - kMT_DiodeVon;
        double drive;
        const double knee = 1.2;
        if(over <= 0.0) drive = 0.0;
        else if(over < knee) drive = over * over / (2.0 * knee);
        else drive = over - knee * 0.5;

        double v = skB_.process(skA_.process(hp * drive));
        /* closed: one flattening pole; open: FOUR poles — the fall has to
         * beat the differentiators' +11.5 dB/octave tilt and then some */
        const int npoles = mode_ == 1 ? 5 : 1;
        for(int k = 0; k < npoles; ++k)
        {
            lpZ_[k] += (v - lpZ_[k]) * (1.0 - lpTopA_);
            v = lpZ_[k];
        }
        const double y = dc_.process(v * outScale_);

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400 && (linear_ ? linLevel_ <= 0.0 : env_.dead()))
            active_ = false;
        return o;
    }

private:
    double sr_ = 44100.0;
    int    mode_ = 0;
    SchmittBank *bank_ = nullptr;
    OnePoleHP dc_;
    SKHighpass skA_, skB_;
    MetalEnv env_;
    double d1_ = 0, d2_ = 0;
    double lpTopA_ = 0, lpZ_[5] = {0, 0, 0, 0, 0};
    double linLevel_ = 0, linStep_ = 0, linTarget_ = 0, linAtkC_ = 0;
    int    linHold_ = 0;
    bool   linear_ = false;
    double outScale_ = 1.0;
    bool active_ = false;
    int  quiet_ = 0;
};

/* ---- the cowbell ------------------------------------------------------- */

/*
 * Oscillators 5 and 6 — 800 and 540 Hz, the pair the trimpots exist for —
 * and everything else is measured off Roland's own render, which told a
 * different story from the first guess:
 *
 *   THE 812 CARRIES THE BELL. In both the attack and the body the 812 Hz
 *   oscillator dominates with 543 FOURTEEN dB below it, 1635 (2 x 812) at
 *   -20 and 2445 (3 x 812) at -21. A single band pass centred on the 812,
 *   Q 5.5, reproduces every one of those ratios from the raw squares.
 *
 *   TWO-STAGE ENVELOPE, decomposed from the reference's 10 ms windows: a
 *   clank of about 15 ms time constant carrying ~77% of the front, over a
 *   body near 140 ms carrying the rest; the tail is alive past 400 ms, so
 *   no hard diode gate here.
 *
 *   NO half-wave VCA. The first version used the swing-VCA treatment and
 *   its rectification minted a 1355 Hz intermod (543 + 812) that the
 *   reference simply does not contain. Linear VCA, measured clean.
 */
static const double kCB_BandQ    = 5.5;
static const double kCB_ClankTau = 0.015;
static const double kCB_BodyTau  = 0.140;
static const double kCB_ClankMix = 0.77;
static const double kCB_BodyMix  = 0.23;
static const double kCB_OutScale = 1.9;      /* fitted, shared trim */

/*
 * The output transistor stage's asymmetry. A clean band-passed pair is too
 * PURE to be this bell: the reference carries 2 x 812 at -18 dB and
 * 3 x 812 at -20 — even harmonics a square pair cannot supply and a Q-5.5
 * filter would bury. The single-ended stage (Q21-23 region) clips one side
 * before the other, and that asymmetry mints exactly those partials.
 * Coefficients calibrated against the reference's measured levels.
 */
static double kCB_Asym  = 2.0;

/*
 * The pair's own upper harmonics, bled around the band pass through the
 * stage's coupling caps — a memoryless polynomial cannot SUSTAIN a third
 * harmonic in the body (its product decays as the cube of a decaying
 * envelope), but the squares never stop supplying theirs: 3 x 540 = 1620
 * and 3 x 804 = 2413 arrive at the square's natural -9.5 dB and only need
 * the right level. High-passed so the fundamentals stay the filter's.
 */
static double kCB_BleedHPHz = 1400.0;
static double kCB_Bleed     = 0.55;   /* through TWO poles at the corner */

class CowbellCircuit {
public:
    void init(const double _sr, SchmittBank *_bank)
    {
        sr_ = _sr; bank_ = _bank;
        bp_.set(812.0, kCB_BandQ, _sr);
        hp_.set(200.0, _sr);
        bleedHpA_.set(kCB_BleedHPHz, _sr);
        bleedHpB_.set(kCB_BleedHPHz, _sr);
        clank_ = body_ = 0.0;
        clankD_ = bodyD_ = 1.0;
        ratio_ = 1.0;
        active_ = false; quiet_ = 0;
    }

    bool active() const { return active_; }

    /* decaySec scales the body's ring (to 1% of peak, seconds); the clank
     * is the strike and stays put. */
    void trigger(const float _decaySec, const float _accentV)
    {
        const double a = (double)_accentV / 8.0;
        const double t = _decaySec > 0.05f ? (double)_decaySec : 0.05;
        /* 0.43 s to 1% at the default pot = the derived kCB_BodyTau — the
         * pot moves the body proportionally around it */
        const double scale = t / 0.43;
        clank_  = kCB_ClankMix * a;
        body_   = kCB_BodyMix * a;
        clankD_ = exp(-1.0 / (kCB_ClankTau * sr_));
        bodyD_  = exp(-1.0 / (kCB_BodyTau * scale * sr_));
        active_ = true; quiet_ = 0;
    }

    void setRatio(const double _r)
    {
        const double r = _r < 0.25 ? 0.25 : (_r > 4.0 ? 4.0 : _r);
        if(r != ratio_) { bp_.set(812.0 * r, kCB_BandQ, sr_); ratio_ = r; }
    }

    float process()
    {
        if(!active_) return 0.0f;
        double o5, o6;
        bank_->cowbellPair(&o5, &o6);
        const double x = bp_.process((o5 + o6) * 0.5);

        const double env = clank_ + body_;
        clank_ *= clankD_;
        body_  *= bodyD_;

        double v = (x + kCB_Bleed * bleedHpB_.process(bleedHpA_.process((o5 + o6) * 0.5))) * env;
        /* the stage's one-sided curvature */
        v = v + kCB_Asym * v * v;
        const double y = hp_.process(v * kCB_OutScale);

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400 && env < 1e-4) active_ = false;
        return o;
    }

private:
    double sr_ = 44100.0, ratio_ = 1.0;
    SchmittBank *bank_ = nullptr;
    BridgedT  bp_;
    OnePoleHP hp_, bleedHpA_, bleedHpB_;
    double clank_ = 0, body_ = 0, clankD_ = 1, bodyD_ = 1;
    bool active_ = false;
    int  quiet_ = 0;
};

} /* namespace sc808 */

#endif /* SC808_MT_CIRCUIT_H */
