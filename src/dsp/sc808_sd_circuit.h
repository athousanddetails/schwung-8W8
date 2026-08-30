/*
 * sc808_sd_circuit.h — the TR-808 snare drum, as a circuit.
 *
 * The second Engine B voice, and the second one there is real evidence for.
 *
 * WHAT AN 808 SNARE ACTUALLY IS
 *
 * Two bridged-T networks and a noise path. The service notes describe the
 * design directly: "two bridged T-networks for fundamental waveforms and
 * harmonic waveforms. The output ratio of the two can be changed by VR8 to
 * tailor sound characteristics." VR8 is the panel's TONE — so Tone on an 808
 * snare is not a filter at all, it is the balance between two ringing
 * networks. The noise runs through a swing-type VCA and a high-pass and is
 * summed in.
 *
 * Three things follow that sc808's two-sines-and-a-crossfade does not do:
 *
 *   - TONE moves the balance between a 173 Hz shell and a 336 Hz one, so it
 *     changes which drum you are hearing rather than how bright it is
 *   - the two shells decay at DIFFERENT rates, because their Q comes from
 *     their own component values — the harmonic dies in about 70 ms while the
 *     fundamental rings for 220 — which is most of what makes an 808 snare's
 *     tail sound the way it does
 *   - SNAPPY is a voltage divider on the trigger going into the noise
 *     envelope generator, not a mix control. Turning it down triggers that
 *     envelope more weakly, so the noise gets both quieter AND shorter
 *
 * PROVENANCE
 *
 * The component values and the frequencies they give are from Kurt James
 * Werner's analysis of the TR-808 snare circuit against Roland's service
 * notes, alongside his ChucK emulation:
 *   R196 680, R197 820k, C58 0.056u, C59 0.027u  ->  173.334 Hz
 *   R195 2.2k, R198 1M,  C60 0.0068u, C61 0.015u ->  335.976 Hz
 * (These are the revised values; Roland's originals give 249.6 and 499.0.)
 * The frequency and Q forms are the same ones the bass drum paper uses, so
 * the shells here are derived exactly as the kick's network is.
 *
 * Werner also models the swing-type VCA — which the service notes describe as
 * producing a "metalic sound" with "many high harmonic components" — as
 * half-wave rectification multiplied by the envelope. That is what the noise
 * path does below.
 *
 * WHAT IS NOT DERIVED, and is marked where it happens: the noise high-pass
 * corner, and the envelope times. Those stages' component values are not in
 * the sources used here, so they take sc808's fitted values instead. That is
 * a weaker footing than the kick, where every block came from the paper.
 *
 * GPL-3.0.
 */
#ifndef SC808_SD_CIRCUIT_H
#define SC808_SD_CIRCUIT_H

#include <math.h>

#include "sc_ugens.h"
#include "sc808_circuit_common.h"

namespace sc808 {

/* Service-note component values for the two bridged-T networks. */
static const double kSD_R196 = 680.0,  kSD_R197 = 820.0e3;   /* fundamental */
static const double kSD_C58  = 0.056e-6, kSD_C59 = 0.027e-6;
static const double kSD_R195 = 2.2e3,  kSD_R198 = 1.0e6;     /* harmonic    */
static const double kSD_C60  = 0.0068e-6, kSD_C61 = 0.015e-6;

class CircuitSnare {
public:
    void init(const double _sampleRate)
    {
        sr_ = _sampleRate >= 8000.0 ? _sampleRate : 44100.0;

        /* The two shells, straight out of the component values. */
        f1_ = bridgedTFreq(kSD_R196, kSD_R197, kSD_C58, kSD_C59);   /* 173.3 */
        f2_ = bridgedTFreq(kSD_R195, kSD_R198, kSD_C60, kSD_C61);   /* 336.0 */
        q1_ = bridgedTQ(kSD_R196, kSD_R197);                        /*  17.4 */
        q2_ = bridgedTQ(kSD_R195, kSD_R198);                        /*  10.7 */

        /* A bridged-T's gain at its own centre frequency is 1 + R2/(2 R1),
         * which is the same as 1 + 2Q^2 — so a high-Q shell is also a very
         * loud one, 604x on the fundamental. The resonators below are
         * normalised to unity peak gain (see BridgedT), so that goes back on
         * the input side, and the two shells keep their real RATIO to each
         * other, which is what matters for the Tone control. */
        g1_ = 1.0 + 2.0 * q1_ * q1_;
        g2_ = 1.0 + 2.0 * q2_ * q2_;

        shaper_.init(sr_);
        rng_.seed(0x808D51Eu);
        reset();
    }

    void reset()
    {
        bt1_.reset(); bt2_.reset();
        shaper_.reset();
        noiseHp_.set(kNoiseHpHz, sr_);
        noiseHp_.reset();
        outHp_.set(30.0, sr_);
        outHp_.reset();
        gateSamples_ = 0;
        noiseEnv_ = 0.0;
        active_ = false;
        quiet_ = 0;
    }

    bool active() const { return active_; }

    /*
     * ratio    tuning multiplier on both shells (Tune)
     * decay01  scales both shells' Q — the hardware has no decay control on
     *          this voice, so this is 8W8's addition and it says so
     * tone01   VR8: the balance between the fundamental and harmonic shells
     * snappy01 the voltage divider feeding the noise envelope generator
     * accentV  trigger voltage, 4..14 V as on the hardware
     *
     * Nothing is reset here. Same reason as the kick: a snare hit while the
     * previous one is still ringing adds to what is in the networks.
     */
    void trigger(const double _ratio, const float _decay01, const float _tone01,
                 const float _snappy01, const float _accentV)
    {
        const double r = _ratio > 0.05 ? (_ratio < 8.0 ? _ratio : 8.0) : 0.05;
        const double dq = 0.35 + 1.15 * (double)sc::clampf(_decay01, 0.0f, 1.0f);

        bt1_.set(f1_ * r, q1_ * dq, sr_);
        bt2_.set(f2_ * r, q2_ * dq, sr_);

        /* VR8. Equal-power, so the middle of the pot is not a dip. */
        const double t = (double)sc::clampf(_tone01, 0.0f, 1.0f);
        mix1_ = cos(t * kCircPi * 0.5);
        mix2_ = sin(t * kCircPi * 0.5);

        accentV_ = _accentV < 1.0f ? 1.0f : (_accentV > 20.0f ? 20.0f : _accentV);

        /*
         * Snappy divides the trigger before it reaches the noise envelope
         * generator, so it sets that envelope's STARTING level. Because the
         * envelope decays at a fixed rate to the same floor, a lower start is
         * also a shorter noise burst — which is the behaviour a mix control
         * cannot reproduce and the reason this is not one.
         */
        noiseEnv_ = (double)sc::clampf(_snappy01, 0.0f, 1.0f) * accentV_ * 0.25;
        /*
         * THE DECAY POT REACHES THE NOISE TOO, and it did not used to.
         *
         * Decay only scaled the shells' Q, so with Snappy up the fixed 75 ms
         * noise tail outlasted them and swamped the pot: measured, the note
         * ran 0.25 s to 0.30 s across the whole throw at Snappy 127 — a range
         * of 1.18x, against 3.7x with Snappy down. The knob looked broken
         * because on the half of the voice you could hear, it was.
         *
         * The noise tau now follows the same law the shells' Q does,
         * normalised so it is exactly 1.0 at the pot's shipped default — the
         * snare that was signed off does not move, and only the positions
         * either side of it change. golden_check is what holds that.
         */
        const double dnorm = dq / (0.35 + 1.15 * kDecayDefault01);
        noiseDecay_ = exp(-1.0 / (kNoiseTau * dnorm * sr_));

        gateSamples_ = (int)(sr_ * 0.001);          /* the CPU's 1 ms pulse */
        active_ = true;
        quiet_ = 0;
    }

    float process()
    {
        if(!active_) return 0.0f;

        double gate = 0.0;
        if(gateSamples_ > 0) { gate = accentV_; --gateSamples_; }
        const double vplus = shaper_.process(gate);

        /* The two shells. Their forward gains are restored here and their
         * ratio is real, so the fundamental genuinely dominates. */
        const double s1 = bt1_.process(vplus * g1_);
        const double s2 = bt2_.process(vplus * g2_);
        double shells = opampClip(s1 * mix1_ + s2 * mix2_, 40.0);

        /*
         * Noise through the swing-type VCA. Werner models that stage as
         * half-wave rectification times the envelope; the rectification's DC
         * term is what the high-pass immediately downstream is there to
         * remove, so the two belong together.
         */
        noiseEnv_ *= noiseDecay_;
        const double n = (double)rng_.frand2();
        const double vca = (n > 0.0 ? n : 0.0) * noiseEnv_;
        const double noise = (double)noiseHp_.process((float)vca);

        double out = (shells * kShellScale + noise * kNoiseScale);
        out = (double)outHp_.process((float)out);      /* output DC blocker */

        if(!(out > -50.0 && out < 50.0)) { reset(); return 0.0f; }

        if(out > 3.2e-5 || out < -3.2e-5) quiet_ = 0;
        else if(++quiet_ > 400) active_ = false;
        return (float)out;
    }

private:
    /* NOT derived from the circuit — the service notes' values for the noise
     * stage are not in the sources this file is built from. sc808's fitted
     * high-pass corner (its `hpf` default, MIDI note 93) stands in, and the
     * noise decay is fitted by ear against an 808 snare's tail. Marked
     * because the rest of this file is not like this. */
    static constexpr double kNoiseHpHz = 1760.0;
    static constexpr double kNoiseTau  = 0.075;

    /*
     * The Decay pot's shipped default POSITION, 108/127 — sd_decay is
     * (0.1, 8.0) EXP defaulting to 4.2 s in gen_params.py, which lands there.
     * It is here only to normalise the noise tail so the default hit is
     * unchanged; if the pot's default ever moves, golden_check fails and this
     * is the number to follow.
     *
     * Spelled as the FLOAT division the engine actually performs — it passes
     * pot/127.0f — so dnorm lands on exactly 1.0 at the default rather than
     * one ulp away from it. Written as a double, the default snare shifted by
     * 1e-7 and golden_check caught it.
     */
    static constexpr double kDecayDefault01 = (double)(108.0f / 127.0f);

    /*
     * Level staging, set so an unaccented hit at the engine's default pot
     * positions peaks at 2.09 — which is exactly where the sc808 snare lands
     * at ITS defaults.
     *
     * The two engines share one per-lane trim, so they have to agree about
     * what a snare comes out at or switching engines moves the whole kit
     * balance by 12 dB. Matching is done HERE rather than by scaling the
     * sc808 voice, because that voice is what the null test renders and
     * putting a gain in it would break the one measurement this project's
     * fidelity claim rests on.
     *
     * The ratio between the two numbers is the shell/noise balance and is
     * fitted by ear: shells alone reach about three quarters of the total,
     * with the noise on top at the default Snappy.
     */
    static constexpr double kShellScale = 0.1491;
    static constexpr double kNoiseScale = 1.4908;

    double sr_ = 44100.0;
    double f1_ = 173.334, f2_ = 335.976, q1_ = 17.36, q2_ = 10.66;
    double g1_ = 604.0, g2_ = 228.0;
    double mix1_ = 0.7, mix2_ = 0.7;
    double accentV_ = 8.0;
    double noiseEnv_ = 0.0, noiseDecay_ = 0.0;

    BridgedT    bt1_, bt2_;
    PulseShaper shaper_;
    sc::HPF     noiseHp_, outHp_;
    sc::RGen    rng_;

    int gateSamples_ = 0;
    int quiet_ = 0;
    bool active_ = false;
};

} /* namespace sc808 */

#endif /* SC808_SD_CIRCUIT_H */
