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

/* Werner §3. 1-4 nominal, 5-6 the TM1/TM2 factory settings. */
static const double kMT_OscHz[6] = { 205.3, 369.6, 304.4, 522.7, 800.0, 540.0 };
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
     * One sample of the summed bus. Naive squares: the HD14584 switches in
     * under 250 ns and Werner forgoes alias suppression for exactly the
     * reason we can — everything downstream is band-passed hard.
     *
     * THE ENGINE CALLS THIS ONCE PER SAMPLE and hands the bus to every
     * metal voice. A voice must never tick the bank itself: two sounding
     * voices would advance the oscillators twice per sample and the whole
     * bank would jump a semitone-ish sharp exactly when the hats and
     * cymbal play together — which is most patterns.
     */
    double tick()
    {
        double sum = 0.0;
        for(int i = 0; i < 6; ++i)
        {
            phase_[i] += inc_[i];
            if(phase_[i] >= 1.0) phase_[i] -= 1.0;
            sum += phase_[i] < kMT_Duty ? kMT_OscAmp : -kMT_OscAmp;
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
    double phase_[6] = {0}, inc_[6] = {0};
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
    else if(over < _knee) drive = over * over / _knee;
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
 * Three bands, as the block diagram has it: BP1 (3.44 kHz) through VCA1 is
 * the pot-controlled SUSTAIN; BP2 (7.1 kHz) through VCA2 is the BODY, and
 * through VCA3 with the tall fast envelope is the CRASH, whose HP3 puts the
 * resonant 10.5 kHz edge on the front of the note.
 *
 * Envelope peaks are Fig 7's voltages; time constants FITTED to Roland's
 * own render at the default kit (to 1% of peak in 1.79 s).
 */
class CymbalCircuit {
public:
    void init(const double _sr, SchmittBank *_bank)
    {
        sr_ = _sr; bank_ = _bank;
        /*
         * The input impedance each filter actually sees is the passive
         * mixer's ~1k bus, not the 22k printed beside the input cap (that
         * resistor biases, it does not source). Solved so the paper's own
         * transfer function lands on the paper's own stated centres:
         * 870 ohm -> 3445 Hz, 1393 ohm -> 7101 Hz.
         */
        bp1_.set(870.0, 56.0e3, 82.0e3, 6.8e-9, 6.8e-9, 3.3e-9, _sr);
        bp2_.set(1393.0, 56.0e3, 82.0e3, 3.3e-9, 3.3e-9, 1.0e-9, _sr);
        hp1_.set(2500.0, 0.97, 1.0, _sr);              /* R124/R127, derived */
        hp2_.set(5200.0, 1.0, 2.2, _sr);               /* fitted to Fig 4    */
        hp3_.set(10500.0, 2.0, 1.4, _sr);              /* the resonance      */
        hp3b_.set(9000.0, _sr);                        /* 3rd-order's extra  */
        /* the same rectification story as the hats: each VCA couples out
         * through small caps, and the lows the half-wave makes must go */
        cpl1_.set(2400.0, _sr);
        cpl2_.set(4700.0, _sr);
        cpl3_.set(8700.0, _sr);
        eg1_.init(_sr); eg2_.init(_sr); eg3_.init(_sr);
        dc_.set(20.0, _sr);
        active_ = false; quiet_ = 0;
    }

    bool active() const { return active_; }

    /* decaySec: the CY Decay pot, seconds of audible ring (to 1%). tone:
     * 0..1, primarily the level of the crash band, as the paper describes
     * the hardware control. accentV: trigger volts. */
    void trigger(const float _decaySec, const float _tone, const float _accentV)
    {
        const double a = (double)_accentV / 8.0;
        tone_ = (double)(_tone < 0.0f ? 0.0f : (_tone > 1.0f ? 1.0f : _tone));
        /*
         * Fig 7: EG3 tall and fast (the crash), EG2 mid, EG1 the sustain
         * whose release is the pot. Peaks in volts, from the figure; decay
         * of EG1 set so the whole note reaches 1% at the pot's seconds.
         */
        eg3_.trigger(13.0 * a, 0.030);
        eg2_.trigger(6.0 * a, 0.180);
        const double t = _decaySec > 0.1f ? (double)_decaySec : 0.1;
        /* tau = t/2.3, not t/ln(100): the diode gate ends the tail after
         * about 23 dB of envelope travel, so the audible-seconds pot maps
         * through the span the gate actually allows. Calibrated against
         * the measured renders. */
        eg1_.trigger(7.0 * a, t / 2.3);
        active_ = true; quiet_ = 0;
    }

    float process(const double _bus)
    {
        if(!active_) return 0.0f;

        const double b1 = opampClip(bp1_.process(_bus), 14.0);
        const double b2 = opampClip(bp2_.process(_bus), 14.0);

        const double e1 = eg1_.tick(), e2 = eg2_.tick(), e3 = eg3_.tick();

        const double sus   = hp1_.process(cpl1_.process(swingVCA(b1, e1)));
        const double body  = hp2_.process(cpl2_.process(swingVCA(b2, e2)));
        double crash = cpl3_.process(swingVCA(b2, e3));
        crash = hp3_.process(hp3b_.process(crash));

        /* band weights fitted to Roland's own render: its default cymbal
         * is body-and-crash forward (6-9 kHz), the sustain a floor under
         * it. Tone's main act, per the paper, is the crash band's level. */
        double y = sus * 0.42 + body * 0.85 + crash * (0.15 + 0.70 * tone_);
        y = dc_.process(y * kCY_OutScale);

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400 && eg1_.dead() && eg2_.dead() && eg3_.dead())
            active_ = false;
        return o;
    }

private:
    /* fitted: circuit and sc808 engines share the lane trim */
    static constexpr double kCY_OutScale = 0.74;

    double sr_ = 44100.0, tone_ = 0.5;
    SchmittBank *bank_ = nullptr;
    WernerBandpass bp1_, bp2_;
    SKHighpass hp1_, hp2_, hp3_;
    OnePoleHP hp3b_, cpl1_, cpl2_, cpl3_, dc_;
    MetalEnv eg1_, eg2_, eg3_;
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
        const double wall = _mode == 1 ? 2300.0 : 3500.0;
        skA_.set(wall, 0.80, 1.0, _sr);
        skB_.set(wall, 0.80, 1.0, _sr);
        /* the top: the naive squares' aliased edges, pulled flat */
        lpTopA_ = exp(-2.0 * kCircPi * 10500.0 / _sr);
        lpTopZ_ = 0.0;
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
        env_.trigger(8.0 * a, t / 2.0);
        active_ = true; quiet_ = 0;
    }

    void choke() { env_.kill(); }

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
        const double e = env_.tick();
        const double over = e - kMT_DiodeVon;
        double drive;
        const double knee = 1.2;
        if(over <= 0.0) drive = 0.0;
        else if(over < knee) drive = over * over / knee;
        else drive = over - knee * 0.5;

        double v = skB_.process(skA_.process(hp * drive));
        lpTopZ_ += (v - lpTopZ_) * (1.0 - lpTopA_);
        const double y = dc_.process(lpTopZ_ * outScale_);

        const float o = (float)y;
        if(o > 3.2e-5f || o < -3.2e-5f) quiet_ = 0;
        else if(++quiet_ > 400 && env_.dead()) active_ = false;
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
    double lpTopA_ = 0, lpTopZ_ = 0;
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
