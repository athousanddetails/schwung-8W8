/*
 * probes.scd — one UGen at a time, as ground truth.
 *
 * When a whole voice fails to null there is no way to tell from the number
 * whether the oscillator, the envelope or one filter in a chain of six is at
 * fault. These probes render each primitive on its own, driven by a
 * DETERMINISTIC source, so the comparison points at a single line of
 * sc_ugens.h.
 *
 * Everything is mono and un-panned: no Pan2, so no sqrt(1/2) to undo.
 * Filters are excited by LFPulse rather than noise because noise cannot null.
 *
 * The envelope probes multiply DC.ar(1), which is exactly how an EnvGen.kr
 * reaches an audio signal in the real voices — including the linear ramp the
 * binary op applies across each control block. That ramp is the thing being
 * tested.
 *
 * Run via test/nulltest.sh --probes.
 */
(
var outDir  = "/work/sc/pout";
var defDir  = "/work/sc/pdefs";
var seconds = 0.5;
var probes;

probes = [
    // --- oscillators ---
    ["pulse",     { LFPulse.ar(203.52) }],
    ["pulse_duty",{ LFPulse.ar(512.8, 0, 0.8) }],
    ["tri",       { LFTri.ar(1827.4, 1) }],
    ["sine",      { SinOsc.ar(440, pi/2) }],
    ["bank",      { LFPulse.ar(203.52) + LFPulse.ar(366.31) + LFPulse.ar(301.77)
                  + LFPulse.ar(518.19) + LFPulse.ar(811.16) + LFPulse.ar(538.75) }],

    // --- envelopes, as they actually reach the audio path ---
    ["env_perc",  { DC.ar(1) * EnvGen.kr(Env.perc(0.005, 0.42, 1, -30)) }],
    ["env_jump",  { DC.ar(1) * EnvGen.kr(Env.new([0.11, 1, 0], [0, 0.3], -7)) }],
    ["env_lin",   { DC.ar(1) * EnvGen.kr(Env.new([0.3, 1, 0], [0.027, 0.07], -250)) }],

    // --- a control-rate frequency driving an oscillator ---
    // The bass drum and the toms are swept sines, and a frequency input is
    // sampled once per control block and HELD, not ramped. This isolates
    // that convention from the limiter and the rest of the kick.
    ["sweep_sin",  { SinOsc.ar(EnvGen.kr(Env([470, 63.5, 47], [0.05, 0.6], -14)), pi/2) }],
    ["sweep_tri",  { LFTri.ar(EnvGen.kr(Env([470, 63.5, 47], [0.05, 0.6], -14)), pi/2) }],

    // --- SC's own filters, on a deterministic input ---
    ["lpf",       { LPF.ar(LFPulse.ar(203.52), 3000) }],
    ["hpf",       { HPF.ar(LFPulse.ar(203.52), 8998) }],
    ["bpf",       { BPF.ar(LFPulse.ar(203.52), 8898, 1) }],

    // --- the RBJ "B" suite ---
    ["bpeakeq",   { BPeakEQ.ar(LFPulse.ar(203.52), 9700, 0.8, 0.7) }],
    ["bbandpass", { BBandPass.ar(LFPulse.ar(203.52), 8900, 0.8) }],
    ["bhipass",   { BHiPass.ar(LFPulse.ar(203.52), 9000, 0.3) }],
    ["blowshelf", { BLowShelf.ar(LFPulse.ar(203.52), 990, 2, -3) }],
    ["bhishelf",  { BHiShelf.ar(LFPulse.ar(203.52), 9400, 1, 5) }],
    ["bhipass4",  { BHiPass4.ar(LFPulse.ar(203.52), 8100, 0.7) }],

    // --- the lookahead limiter, on something it must actually pull down ---
    ["limiter",   { Limiter.ar(SinOsc.ar(220, pi/2) * 3, 0.5) }]
];

"writing probe synthdefs...".postln;
probes.do { |p|
    SynthDef("probe_" ++ p[0], { |out_bus = 0|
        Out.ar(out_bus, p[1].value)
    }).writeDefFile(defDir);
};

fork {
    0.5.wait;
    probes.do { |p|
        var name  = p[0];
        var score = Score([
            [0.0,     ['/d_loadDir', defDir]],
            [0.0,     ['/s_new', "probe_" ++ name, 1000, 0, 0]],
            [seconds, ['/c_set', 0, 0]]
        ]);
        ("probe " ++ name).postln;
        score.recordNRT(
            "/tmp/probe_" ++ name ++ ".osc",
            outDir +/+ name ++ ".wav",
            nil, 44100, "WAV", "float",
            ServerOptions.new
                .numOutputBusChannels_(1)     // mono: no Pan2 in the chain
                .blockSize_(64)
                .verbosity_(-2),
            duration: seconds
        );
        0.8.wait;
    };
    "done".postln;
    1.0.wait;
    0.exit;
};
)
