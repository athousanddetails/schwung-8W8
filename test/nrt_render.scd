/*
 * nrt_render.scd — render every sc808 voice offline, as ground truth.
 *
 * This is the reference half of the null test. It loads the preprocessed
 * sc808 SynthDefs (see nulltest.sh — the vendored .scd has unbalanced
 * top-level parens because it is written to be evaluated block by block in
 * the IDE, so the lone paren lines are stripped first), then renders each
 * voice on its own with scsynth in non-realtime mode.
 *
 * Two settings are load-bearing and must match sc_ugens.h:
 *   sampleRate 44100 — the Move's rate
 *   blockSize  64    — every envelope in sc808 is EnvGen.kr, so the control
 *                      block size is audible in the output, and sc_ugens.h
 *                      reproduces exactly this blockiness.
 *
 * Output is stereo because every SynthDef ends in Pan2; at pan 0 both
 * channels carry the mono signal scaled by sqrt(1/2), which nulltest.py
 * undoes before comparing.
 *
 * Run via test/nulltest.sh, not by hand.
 */
(
var defDir  = "/work/sc/defs";
var outDir  = "/work/sc/out";
var seconds = 3.0;

// One file per voice, so a failure is attributable to a voice and not a mix.
var voices = [
    "bassdrum", "snare", "clap", "tomlo", "tommid", "tomhi",
    "congalo", "congamid", "congahi", "rimshot", "claves",
    "maracas", "cowbell", "closed_hihat", "open_hihat", "cymbal"
];

"loading sc808 synthdefs...".postln;
thisProcess.interpreter.executeFile("/work/sc/sc808_defs.scd");

fork {
    0.5.wait;                      // let writeDefFile finish flushing
    voices.do { |name|
        var def   = "sonic-pi-sc808_" ++ name;
        var out   = outDir +/+ name ++ ".wav";
        var osc   = "/tmp/" ++ name ++ ".osc";
        var score = Score([
            [0.0,     ['/d_loadDir', defDir]],
            [0.0,     ['/s_new', def, 1000, 0, 0]],
            [seconds, ['/c_set', 0, 0]]     // a no-op that pins the end time
        ]);

        ("rendering " ++ name).postln;
        score.recordNRT(
            osc, out, nil, 44100, "WAV", "float",
            ServerOptions.new
                .numOutputBusChannels_(2)
                .blockSize_(64)
                .verbosity_(-2),
            duration: seconds        // keyword: the positional slot before
                                     // `duration` is completionString, and a
                                     // number there reaches scsynth's argv
        );
        1.0.wait;                  // recordNRT forks a scsynth; let it finish
    };
    "done".postln;
    1.0.wait;
    0.exit;
};
)
