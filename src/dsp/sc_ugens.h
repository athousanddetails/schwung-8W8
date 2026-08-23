/*
 * sc_ugens.h — the SuperCollider UGens sc808 uses, as C++.
 *
 * 8W8's Engine A is a transcription of Sonic Pi's sc808.scd. A transcription
 * is only worth anything if it is EXACT, so nothing in this file is written
 * from memory or from a textbook: every formula below was read out of
 * SuperCollider's own server plugin sources and the provenance is named at
 * each one. The intent is that rendering a voice here and rendering it in
 * scsynth produces the same samples, and test/nulltest.sh proves it.
 *
 * Where a UGen is a wavetable in SC and a function here (SinOsc), that is
 * called out. Where SC runs something at control rate and we must match its
 * blockiness to null (EnvGen.kr), we reproduce the blockiness rather than
 * "improving" it — an improvement that breaks the null test costs more than
 * it is worth.
 *
 * References, all from supercollider/server/plugins @ develop:
 *   FilterUGens.cpp — LPF, HPF, BPF, the B-suite, Limiter
 *   LFUGens.cpp     — LFPulse, LFTri, EnvGen curve segments
 *   NoiseUGens.cpp  — WhiteNoise
 *
 * GPL-3.0.
 */
#ifndef SC_UGENS_H
#define SC_UGENS_H

#include <math.h>
#include <stdint.h>

namespace sc {

static const double kPi    = 3.14159265358979323846;
static const double kTwoPi = 6.28318530717958647693;
static const double kSqrt2 = 1.41421356237309504880;

/* SC's default control block. EnvGen.kr ticks once per block and the binary
 * op that multiplies it into an audio signal ramps across the block, so this
 * number is audible in the output and has to match scsynth's -z. */
static const int kBlockSize = 64;

inline float midicps(const float _note)
{
    return 440.0f * powf(2.0f, (_note - 69.0f) / 12.0f);
}

inline float clampf(const float _x, const float _lo, const float _hi)
{
    return _x < _lo ? _lo : (_x > _hi ? _hi : _x);
}

/* ---- noise ------------------------------------------------------------ */

/*
 * SC's taus88 combined Tausworthe generator, and its frand2 — uniform on
 * [-1, 1). Reproduced exactly rather than substituting an LCG so that a
 * seed-matched null test of the noise voices is possible at all; with a
 * different generator only a spectral comparison is available.
 */
class RGen {
public:
    void seed(uint32_t s)
    {
        /* SC requires s1 > 1, s2 > 7, s3 > 15. */
        s1_ = 1243598713u ^ s; if(s1_ <  2u) s1_ = 1243598713u;
        s2_ = 3093459404u ^ s; if(s2_ <  8u) s2_ = 3093459404u;
        s3_ = 1821928721u ^ s; if(s3_ < 16u) s3_ = 1821928721u;
    }

    uint32_t trand()
    {
        s1_ = ((s1_ & 0xFFFFFFFEu) << 12) ^ (((s1_ << 13) ^ s1_) >> 19);
        s2_ = ((s2_ & 0xFFFFFFF8u) <<  4) ^ (((s2_ <<  2) ^ s2_) >> 25);
        s3_ = ((s3_ & 0xFFFFFFF0u) << 17) ^ (((s3_ <<  3) ^ s3_) >> 11);
        return s1_ ^ s2_ ^ s3_;
    }

    /* frand2: bit-stuff the mantissa of a float in [2,4), then subtract 3. */
    float frand2()
    {
        union { uint32_t u; float f; } u;
        u.u = 0x40000000u | (trand() >> 9);
        return u.f - 3.0f;
    }

private:
    uint32_t s1_ = 1243598713u, s2_ = 3093459404u, s3_ = 1821928721u;
};

/* ---- SC's per-note construction step ----------------------------------
 *
 * SC builds a new synth for every note, and every UGen Ctor finishes by
 * calling its own next(unit, 1): one sample runs through the entire graph
 * with all inputs at their construction-time values before the first real
 * block. Filters see their first input sample twice; oscillators take one
 * extra phase step; a swept oscillator takes that step at the envelope's
 * initial value.
 *
 * 8W8's voices are persistent — constructed once, retriggered thereafter —
 * so reproducing this in the engine would be a BUG, not fidelity: it would
 * repeat a sample and jump a phase on every hit. It exists only so the null
 * test compares like with like, and it is compiled out of the module: the
 * flag and its branch live behind SC808_NULLTEST, which only the test tools
 * define.
 *
 * The harness raises the flag, renders exactly one sample, lowers it, and
 * discards that sample.
 */
#ifdef SC808_NULLTEST
extern bool g_scPriming;
#endif

/* ---- envelopes -------------------------------------------------------- */

/*
 * One segment of an SC Env with a "curve" shape.
 *
 * From LFUGens.cpp, Env_next / shape_Curve:
 *     a1   = (endLevel - level) / (1 - exp(curve))
 *     a2   = level + a1
 *     b1   = a1
 *     grow = exp(curve / durationInSamples)
 *     per step:  b1 *= grow;  out = a2 - b1
 *
 * which is level + (endLevel-level)(1 - e^{curve·t})/(1 - e^{curve}), t∈[0,1].
 * |curve| < 0.001 degenerates to linear, exactly as SC does — sc808 never
 * relies on that, but a pot sweeping a curve through zero would.
 */
struct EnvSeg {
    float endLevel;
    float durSeconds;
    float curve;
};

#define SC_ENV_MAX_SEGS 4

/*
 * EnvGen.kr, and it really is .kr — every envelope in sc808 is.
 *
 * That is not a detail to smooth away. An EnvGen at control rate steps once
 * per 64-sample block, so its "sample rate" is 689 Hz at 44.1k, and a segment
 * of ZERO duration still occupies one whole control period — 1.45 ms. sc808
 * leans on this: every voice's amplitude envelope is
 *
 *     Env.new([click, 1, 0], [0, decay], curve)
 *
 * whose first segment has duration 0. Run that at audio rate and `click`
 * lasts one sample and does nothing; run it at control rate, as SC does, and
 * `click` is a 1.45 ms pre-level at the front of the note — an audible part
 * of the attack of every drum in the kit. Getting this wrong makes the whole
 * kit sound soft and makes the `click` parameter look vestigial.
 *
 * The value is then linearly ramped across the block by whatever consumes it:
 * a BinaryOpUGen multiplying an audio signal, or SinOsc's frequency input.
 * Both use CALCSLOPE, so one interpolation rule covers both.
 */
class Env {
public:
    /* levels[0] is the start level; segs[i] carries the target and time. */
    void set(const float _startLevel, const EnvSeg *_segs, const int _count,
             const double _sampleRate)
    {
        start_ = _startLevel;
        count_ = _count > SC_ENV_MAX_SEGS ? SC_ENV_MAX_SEGS : _count;
        ctrlRate_ = _sampleRate / (double)kBlockSize;
        for(int i = 0; i < count_; ++i) seg_[i] = _segs[i];
        rewind();
    }

    void rewind()
    {
        segIndex_ = 0;
        level_    = start_;
        done_     = count_ == 0;
        armSegment();
        prev_  = start_;
        cur_   = start_;
        phase_ = kBlockSize;   /* forces a control step on the first sample */
    }

    bool done() const { return done_; }

    /* One audio-rate sample: step the control-rate generator on block
     * boundaries, ramp linearly in between. */
    float next()
    {
#ifdef SC808_NULLTEST
        /* During the construction sample an EnvGen reports its initial level
         * and does not step: EnvGen_Ctor's next(unit,1) runs with
         * m_stage == ENVGEN_NOT_STARTED. */
        if(g_scPriming) return start_;
#endif
        if(phase_ >= kBlockSize)
        {
            phase_ = 0;
            prev_  = cur_;
            cur_   = stepControl();
        }
        /* n/64 for n = 0..63, so the FIRST sample of a block is exactly the
         * previous control value and the block never reaches `cur` — that is
         * what CALCSLOPE does (`out = a * b; b += slope;`, b starting at the
         * old value). Taking t after the increment instead puts the whole
         * envelope one sample early, which is worth ~35 dB of null. */
        const float t = (float)phase_ / (float)kBlockSize;
        ++phase_;
        return prev_ + (cur_ - prev_) * t;
    }

    /*
     * The block's control value, HELD constant for all 64 samples.
     *
     * This is what a UGen with a control-rate FREQUENCY input sees, and it is
     * not the same as next(). From Osc_ikk_perform in OscUGens.cpp:
     *
     *     int32 freq = (int32)(unit->m_cpstoinc * freqin);
     *     int32 phaseinc = freq + (int32)(CALCSLOPE(phasein, ...) * ...);
     *     LOOP1(n, ZXP(out) = lookupi1(...phase...); phase += phaseinc;);
     *
     * `freqin` is read ONCE per block and the increment is constant across
     * it. Only the PHASE input is ramped. So an envelope driving a frequency
     * is a staircase, while the same envelope multiplying an audio signal is
     * a ramp — the two consumers see genuinely different signals.
     *
     * Interpolating the frequency instead is smoother and wrong: over a fast
     * pitch sweep the difference integrates into a permanent phase error,
     * which is what left the bass drum sitting at a flat -19 dB residual for
     * its whole tail while its envelope and filters were already exact.
     */
    float nextHeld()
    {
#ifdef SC808_NULLTEST
        if(g_scPriming) return start_;
#endif
        if(phase_ >= kBlockSize)
        {
            phase_ = 0;
            prev_  = cur_;
            cur_   = stepControl();
        }
        ++phase_;
        return cur_;
    }

    /* The raw control-rate level, for analysis. */
    float peek() const { return (float)level_; }

private:
    /*
     * EnvGen_initSegment, LFUGens.cpp @ Version-3.11.2:
     *
     *     counter = (int32)(dur * SAMPLERATE);
     *     counter = sc_max(1, counter);
     *     if (counter == 1) unit->m_shape = shape_Linear;
     *     ...
     *     unit->m_grow = exp(curve / counter);
     *
     * Three things, and each one was worth measurable dB:
     *
     *   - the segment runs an INTEGER number of control periods, TRUNCATED.
     *     Treating the duration as fractional and comparing position against
     *     it holds every segment one period too long — an envelope sitting at
     *     full level for an extra 1.45 ms, and 30 dB of null.
     *   - `counter` — the integer — is also what the curve rate is computed
     *     over. Using the fractional duration instead put the bass drum's
     *     pitch envelope at 334.26 Hz where SC has 332.80, which sounds like
     *     nothing and drifts into a total phase decorrelation over a second.
     *   - a one-period segment is forced to Linear regardless of curve. This
     *     is what actually renders sc808's zero-duration `click` segments:
     *     one linear control period straight to the node.
     *
     * SAMPLERATE here is the CONTROL rate, because these are all EnvGen.kr.
     *
     * VERSION NOTE. SuperCollider's development branch has since reworked
     * this: it keeps a fractional duration for the curve, floors separately
     * for the step count, and carries the remainder into the next segment so
     * a chain of segments does not drift late. That is a better envelope and
     * it is NOT what is implemented here, because the reference renders come
     * from 3.11.2 and a transcription that cannot be verified is worth less
     * than one that can. If the reference is ever moved to a newer SC, this
     * is the function to change and test/nulltest.sh will say so loudly.
     */
    void armSegment()
    {
        if(segIndex_ >= count_) { done_ = true; return; }
        const EnvSeg &s = seg_[segIndex_];

        counter_ = (int)((double)s.durSeconds * ctrlRate_);
        if(counter_ < 1) counter_ = 1;

        const double end = (double)s.endLevel;
        if(counter_ == 1 || fabs((double)s.curve) < 0.001)
        {
            linear_ = true;
            grow_   = (end - level_) / (double)counter_;
        }
        else
        {
            linear_ = false;
            const double a1 = (end - level_) / (1.0 - exp((double)s.curve));
            a2_   = level_ + a1;
            b1_   = a1;
            grow_ = exp((double)s.curve / (double)counter_);
        }
    }

    /* Exactly one control period along the segment chain. */
    float stepControl()
    {
        if(done_) return (float)level_;

        if(linear_) level_ += grow_;
        else      { b1_ *= grow_; level_ = a2_ - b1_; }

        if(--counter_ <= 0)
        {
            /* No snap to the node level: SC only does that for shape_Hold,
             * and carrying the small shortfall into the next segment is part
             * of what we are reproducing. */
            ++segIndex_;
            armSegment();
        }
        return (float)level_;
    }

    EnvSeg seg_[SC_ENV_MAX_SEGS] = {};
    int    count_ = 0;
    int    segIndex_ = 0;
    int    counter_ = 1;
    double ctrlRate_ = 44100.0 / 64.0;
    double level_ = 0.0;
    double grow_ = 0.0, a2_ = 0.0, b1_ = 0.0;
    bool   linear_ = true, done_ = true;
    float  start_ = 0.0f;
    float  prev_ = 0.0f, cur_ = 0.0f;
    int    phase_ = kBlockSize;
};

/* Env.perc(attack, release, level, curve) — levels [0, level, 0], the same
 * curve on both segments. SC's default curve is -4; sc808 always names one. */
inline void envPerc(Env &_e, const float _attack, const float _release,
                    const float _level, const float _curve, const double _sr)
{
    const EnvSeg s[2] = { { _level, _attack, _curve }, { 0.0f, _release, _curve } };
    _e.set(0.0f, s, 2, _sr);
}

/* ---- SC's own filters -------------------------------------------------
 *
 * LPF/HPF/BPF are 2nd order and share a Direct Form II core:
 *     y0  = in + b1·y1 + b2·y2
 *     out = a0·(numerator over y0,y1,y2)
 * The numerator is what distinguishes them, so it is spelled out per class
 * rather than hidden behind a generic biquad — the sign of the "2·y1" term
 * is the whole difference between a lowpass and a highpass here.
 */

class LPF {
public:
    void set(const double _freq, const double _sr)
    {
        const double pfreq = _freq * (kTwoPi / _sr) * 0.5;
        const double C  = 1.0 / tan(pfreq);
        const double C2 = C * C;
        const double s2C = C * kSqrt2;
        a0_ = 1.0 / (1.0 + s2C + C2);
        b1_ = -2.0 * (1.0 - C2) * a0_;
        b2_ = -(1.0 - s2C + C2) * a0_;
    }
    void reset() { y1_ = y2_ = 0.0; }
    float process(const float _in)
    {
        const double y0 = (double)_in + b1_ * y1_ + b2_ * y2_;
        const double out = a0_ * (y0 + 2.0 * y1_ + y2_);
        y2_ = y1_; y1_ = y0;
        return (float)out;
    }
private:
    double y1_ = 0.0, y2_ = 0.0, a0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
};

class HPF {
public:
    void set(const double _freq, const double _sr)
    {
        const double pfreq = _freq * (kTwoPi / _sr) * 0.5;
        const double C  = tan(pfreq);
        const double C2 = C * C;
        const double s2C = C * kSqrt2;
        a0_ = 1.0 / (1.0 + s2C + C2);
        b1_ = 2.0 * (1.0 - C2) * a0_;
        b2_ = -(1.0 - s2C + C2) * a0_;
    }
    void reset() { y1_ = y2_ = 0.0; }
    float process(const float _in)
    {
        const double y0 = (double)_in + b1_ * y1_ + b2_ * y2_;
        const double out = a0_ * (y0 - 2.0 * y1_ + y2_);
        y2_ = y1_; y1_ = y0;
        return (float)out;
    }
private:
    double y1_ = 0.0, y2_ = 0.0, a0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
};

/* BPF's second argument is rq (reciprocal of Q), NOT bandwidth in Hz.
 * sc808 passes 1 or leaves it at the default 1. */
class BPF {
public:
    void set(const double _freq, const double _rq, const double _sr)
    {
        const double pfreq = _freq * (kTwoPi / _sr);
        const double pbw   = _rq * pfreq * 0.5;
        const double C = 1.0 / tan(pbw);
        const double D = 2.0 * cos(pfreq);
        a0_ = 1.0 / (1.0 + C);
        b1_ = C * D * a0_;
        b2_ = (1.0 - C) * a0_;
    }
    void reset() { y1_ = y2_ = 0.0; }
    float process(const float _in)
    {
        const double y0 = (double)_in + b1_ * y1_ + b2_ * y2_;
        const double out = a0_ * (y0 - y2_);
        y2_ = y1_; y1_ = y0;
        return (float)out;
    }
private:
    double y1_ = 0.0, y2_ = 0.0, a0_ = 0.0, b1_ = 0.0, b2_ = 0.0;
};

/* ---- the "B" suite: RBJ cookbook biquads ------------------------------
 *
 * BPeakEQ, BLowShelf, BHiShelf, BHiPass, BBandPass all share one difference
 * equation (BPerformFilterLoop in FilterUGens.cpp) and differ only in how the
 * five coefficients are computed, so here they are one class with named
 * setters. Note SC's sign convention: b1 and b2 are stored ALREADY NEGATED
 * relative to the textbook, so the recursion adds them.
 *
 * The dB-to-linear conversion is pow(10, db/40) — SC writes it as
 * pow(10., db * 0.025), which is the same thing and is why an "amplitude"
 * of a shelf is the square root of its power gain.
 */
class Biquad {
public:
    void reset() { y1_ = y2_ = 0.0; }

    /* BHiPass(freq, rq) */
    void setHiPass(const double _freq, const double _rq, const double _sr)
    {
        const double w0 = kTwoPi * _freq / _sr;
        const double cosw0 = cos(w0);
        const double i = 1.0 + cosw0;
        const double alpha = sin(w0) * 0.5 * _rq;
        const double rz = 1.0 / (1.0 + alpha);
        a0_ = i * 0.5 * rz;
        a1_ = -i * rz;
        a2_ = a0_;
        b1_ = cosw0 * 2.0 * rz;
        b2_ = (1.0 - alpha) * -rz;
    }

    /* BBandPass(freq, bw) — bw in OCTAVES, unlike BPF's rq.
     * 0.34657359027997 is ln(2)/2, from the cookbook's bandwidth form. */
    void setBandPass(const double _freq, const double _bw, const double _sr)
    {
        const double w0 = kTwoPi * _freq / _sr;
        const double sinw0 = sin(w0);
        const double alpha = sinw0 * sinh((0.34657359027997 * _bw * w0) / sinw0);
        const double rz = 1.0 / (1.0 + alpha);
        a0_ = alpha * rz;
        a1_ = 0.0;
        a2_ = -a0_;
        b1_ = cos(w0) * 2.0 * rz;
        b2_ = (1.0 - alpha) * -rz;
    }

    /* BPeakEQ(freq, rq, db) */
    void setPeakEQ(const double _freq, const double _rq, const double _db,
                   const double _sr)
    {
        const double a  = pow(10.0, _db * 0.025);
        const double w0 = kTwoPi * _freq / _sr;
        const double alpha = sin(w0) * 0.5 * _rq;
        const double rz = 1.0 / (1.0 + (alpha / a));
        b1_ = 2.0 * rz * cos(w0);
        a0_ = (1.0 + (alpha * a)) * rz;
        a1_ = -b1_;
        a2_ = (1.0 - (alpha * a)) * rz;
        b2_ = (1.0 - (alpha / a)) * -rz;
    }

    /* BLowShelf(freq, rs, db) */
    void setLowShelf(const double _freq, const double _rs, const double _db,
                     const double _sr)
    {
        const double a = pow(10.0, _db * 0.025);
        const double w0 = kTwoPi * _freq / _sr;
        const double cosw0 = cos(w0);
        const double alpha = sin(w0) * 0.5 * sqrt((a + (1.0 / a)) * (_rs - 1.0) + 2.0);
        const double i = (a + 1.0) * cosw0;
        const double j = (a - 1.0) * cosw0;
        const double k = 2.0 * sqrt(a) * alpha;
        const double rz = 1.0 / ((a + 1.0) + j + k);
        a0_ = a * ((a + 1.0) - j + k) * rz;
        a1_ = 2.0 * a * ((a - 1.0) - i) * rz;
        a2_ = a * ((a + 1.0) - j - k) * rz;
        b1_ = 2.0 * ((a - 1.0) + i) * rz;
        b2_ = ((a + 1.0) + j - k) * -rz;
    }

    /* BHiShelf(freq, rs, db) */
    void setHiShelf(const double _freq, const double _rs, const double _db,
                    const double _sr)
    {
        const double a = pow(10.0, _db * 0.025);
        const double w0 = kTwoPi * _freq / _sr;
        const double cosw0 = cos(w0);
        const double alpha = sin(w0) * 0.5 * sqrt((a + (1.0 / a)) * (_rs - 1.0) + 2.0);
        const double i = (a + 1.0) * cosw0;
        const double j = (a - 1.0) * cosw0;
        const double k = 2.0 * sqrt(a) * alpha;
        const double rz = 1.0 / ((a + 1.0) - j + k);
        a0_ = a * ((a + 1.0) + j + k) * rz;
        a1_ = -2.0 * a * ((a - 1.0) + i) * rz;
        a2_ = a * ((a + 1.0) + j - k) * rz;
        b1_ = -2.0 * ((a - 1.0) - i) * rz;
        b2_ = ((a + 1.0) - j - k) * -rz;
    }

    float process(const float _in)
    {
        const double y0 = (double)_in + b1_ * y1_ + b2_ * y2_;
        const double out = a0_ * y0 + a1_ * y1_ + a2_ * y2_;
        y2_ = y1_; y1_ = y0;
        return (float)out;
    }

private:
    double y1_ = 0.0, y2_ = 0.0;
    double a0_ = 1.0, a1_ = 0.0, a2_ = 0.0, b1_ = 0.0, b2_ = 0.0;
};

/*
 * BHiPass4 is two BHiPass in series — but NOT with the rq you asked for.
 * SCClassLibrary/Common/Audio/BEQSuite.sc:
 *
 *     BHiPass4 { *ar { arg in, freq = 1200.0, rq = 1.0, mul = 1.0, add = 0.0;
 *         rq = sqrt(rq);
 *         coefs = BHiPass.sc(nil, freq, rq);
 *         ^SOS.ar(SOS.ar(in, *coefs), *coefs ++ [mul, add]); } }
 *
 * The sqrt is the whole point: cascading two sections each of rq gives an
 * overall rq of rq^2, so each stage takes the square root to land on the rq
 * that was requested. Passing rq straight through to both stages costs about
 * 2 dB of resonance and was worth 20 dB of null on the open hat.
 */
class HiPass4 {
public:
    void set(const double _freq, const double _rq, const double _sr)
    {
        const double rq = sqrt(_rq);
        a_.setHiPass(_freq, rq, _sr);
        b_.setHiPass(_freq, rq, _sr);
    }
    void reset() { a_.reset(); b_.reset(); }
    float process(const float _in) { return b_.process(a_.process(_in)); }
private:
    Biquad a_, b_;
};

/* ---- oscillators ------------------------------------------------------
 *
 * All three emit the current sample and THEN advance, which is what SC's
 * per-sample loops do (`ZXP(out) = ...; phase += phaseinc;`).
 *
 * A rendered scsynth file looks at first like it disagrees: the first sample
 * of SinOsc.ar(440, pi/2) is cos(w) = 0.99804, not sin(pi/2) = 1.0. That is
 * not the loop — it is SC constructing a fresh synth per note and running one
 * extra sample through the whole graph at construction. Every UGen Ctor ends
 * by calling its own next(unit, 1). See SC_CTOR_PRIME below.
 *
 * The distinction is invisible at a constant frequency and very visible at a
 * swept one, because the construction step uses the envelope's INITIAL value
 * (470 Hz on the bass drum) while the first real block uses the value after
 * one control step (334 Hz).
 */

/*
 * SinOsc. SC reads an 8192-point sine wavetable with linear interpolation;
 * this is sinf(). The difference is the table's interpolation error, about
 * -100 dBFS at this table size — below anything the null test cares about,
 * and below the 16-bit floor the Move outputs at. Named here so nobody has
 * to rediscover it when the null lands at -95 dB instead of -140.
 */
class SinOsc {
public:
    void set(const double _freq, const double _sr) { inc_ = kTwoPi * _freq / _sr; }
    void reset(const double _phase) { phase_ = _phase; }
    float process()
    {
        const float v = sinf((float)phase_);
        phase_ += inc_;
        if(phase_ > kTwoPi) phase_ -= kTwoPi;
        return v;
    }
    /* Frequency-modulated form, for the voices whose pitch is an envelope. */
    float process(const double _freq, const double _sr)
    {
        inc_ = kTwoPi * _freq / _sr;
        return process();
    }
private:
    double phase_ = 0.0, inc_ = 0.0;
};

/*
 * LFTri. From LFUGens.cpp:
 *     freqMul = 4 / sampleRate;  phase starts wrapped into [0, 4)
 *     z = phase > 1 ? 2 - phase : phase;  phase += freq;
 *     if (phase >= 3) phase -= 4;
 * Output is bipolar -1..1 and NOT band-limited — the corners alias, in SC
 * too. Reproduced, not fixed.
 */
class LFTri {
public:
    void reset(const double _iphase)
    {
        phase_ = fmod(_iphase, 4.0);
        if(phase_ < 0.0) phase_ += 4.0;
    }
    float process(const double _freq, const double _sr)
    {
        const float z = (float)(phase_ > 1.0 ? 2.0 - phase_ : phase_);
        phase_ += 4.0 * _freq / _sr;
        if(phase_ >= 3.0) phase_ -= 4.0;
        return z;
    }
private:
    double phase_ = 0.0;
};

/*
 * LFPulse. From LFUGens.cpp:
 *     if (phase >= 1) { phase -= 1; z = duty <= 0.5 ? 1 : 0; }
 *     else            { z = phase < duty ? 1 : 0; }
 *     phase += freq/sampleRate;
 *
 * Two things that matter and are easy to get wrong:
 *   1. the output is UNIPOLAR, 0..1. sc808's hi-hats sum six of these, so the
 *      sum carries a large DC term that the filter chain removes. Making it
 *      bipolar "because a square wave is bipolar" changes every hat.
 *   2. the wrap branch forces one sample of the opposite polarity, which is
 *      what stops a duty of exactly 0 or 1 from producing silence.
 * Naive and aliasing, as in SC. On the 808's real hats the oscillators are
 * Schmitt-trigger squares whose harmonics run past Nyquist anyway.
 */
class LFPulse {
public:
    void reset(const double _iphase) { phase_ = _iphase; }
    float process(const double _freq, const double _duty, const double _sr)
    {
        float z;
        if(phase_ >= 1.0) { phase_ -= 1.0; z = _duty <= 0.5 ? 1.0f : 0.0f; }
        else              { z = phase_ < _duty ? 1.0f : 0.0f; }
        phase_ += _freq / _sr;
        return z;
    }
private:
    double phase_ = 0.0;
};

/* ---- Limiter ----------------------------------------------------------
 *
 * SC's Limiter is a lookahead limiter over three rotating buffers of
 * `dur` seconds each (default 0.01 s). Two consequences that a naive
 * "just clip it" port would silently lose, and that sc808's bass drum
 * depends on:
 *
 *   - it delays the signal by TWO buffers, ~20 ms at the default. The
 *     sc808 kick therefore arrives ~20 ms late relative to every other
 *     voice. That is a property of sc808, not a bug in this port; the
 *     engine compensates for it explicitly and says so where it does.
 *   - it emits silence until two buffers have filled, so the very start
 *     of the first note is gone.
 *
 * Gain is recomputed once per buffer from max(prev, current) peak and
 * ramped across the next buffer, so it never modulates within a buffer.
 */
#define SC_LIMITER_MAX 2048

class Limiter {
public:
    void set(const float _dur, const double _sr)
    {
        long n = (long)ceil((double)_dur * _sr);
        if(n < 1) n = 1;
        if(n > SC_LIMITER_MAX) n = SC_LIMITER_MAX;
        bufsize_ = n;
        slopeFactor_ = 1.0f / (float)bufsize_;
        reset();
    }

    void reset()
    {
        for(int i = 0; i < 3; ++i)
            for(long j = 0; j < bufsize_; ++j) buf_[i][j] = 0.0f;
        in_ = 0; mid_ = 1; out_ = 2;
        pos_ = 0; flips_ = 0;
        slope_ = 0.0f; level_ = 1.0f;
        prevMax_ = curMax_ = 0.0f;
    }

    /* Latency in samples, for callers that want to line the kick up with the
     * rest of the kit rather than inherit SC's 20 ms. */
    long latency() const { return bufsize_ * 2; }

    float process(const float _in, const float _amp)
    {
        buf_[in_][pos_] = _in;
        const float y = flips_ >= 2 ? level_ * buf_[out_][pos_] : 0.0f;
        level_ += slope_;

        const float a = fabsf(_in);
        if(a > curMax_) curMax_ = a;

        if(++pos_ >= bufsize_)
        {
            pos_ = 0;
            const float max2 = prevMax_ > curMax_ ? prevMax_ : curMax_;
            prevMax_ = curMax_;
            curMax_  = 0.0f;

            const float next = max2 > _amp ? _amp / max2 : 1.0f;
            slope_ = (next - level_) * slopeFactor_;

            const int t = out_; out_ = mid_; mid_ = in_; in_ = t;
            ++flips_;
        }
        return y;
    }

private:
    float buf_[3][SC_LIMITER_MAX] = {};
    long  bufsize_ = 441, pos_ = 0;
    int   in_ = 0, mid_ = 1, out_ = 2, flips_ = 0;
    float slope_ = 0.0f, level_ = 1.0f, slopeFactor_ = 1.0f / 441.0f;
    float prevMax_ = 0.0f, curMax_ = 0.0f;
};

} /* namespace sc */

#endif /* SC_UGENS_H */
