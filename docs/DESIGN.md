# 8W8 — design notes

What was built, what it is built from, what was learned building it, and what
is left. The pre-build analysis that started this is in the git history; this
is the version that reflects the code.

---

## 1. The two engines

**Engine A — sc808.** Every voice except the kick is a transcription of one of
the sixteen SynthDefs in Sonic Pi's `sc808.scd` (MIT, by Yoshinosuke Horiuchi,
adapted by Sam Aaron). Vendored under `src/vendor/sc808`, transcribed in
`src/dsp/sc808_voices.h`, and **verified sample-for-sample against
SuperCollider** by `test/nulltest.sh`.

**Engine B — the circuit.** So far, the bass drum: `sc808_bd_circuit.h`, built
from Werner, Abel and Smith's DAFx-14 analysis of the real 808 bass drum. It
is the default kick.

The split is deliberate and it is the same shape 9W9 uses, where the original
ER-99 engine is still selectable. Engine A gets a complete, verified kit
quickly; Engine B replaces the voices where sc808 is audibly not an 808.

### Why the kick needed replacing

sc808's kick is a `SinOsc` on a pitch envelope. That is a perfectly good kick
drum and it is not a TR-808, and it was obvious the moment anyone heard it.

The real thing is a bridged-T network in an op-amp feedback loop. **Decay is
loop gain, not an envelope** — the network's own ringing is about 80 ms, and
everything past that is the feedback buffer failing to lose the signal. That
one fact produces all four of the behaviours an envelope-based imitation never
quite gets: the pitch sigh, a genuine octave jump in centre frequency at the
attack, no machine-gunning on fast repeats, and accent as a trigger voltage
into a diode rather than a gain.

`tools/bd_check` asserts all of them.

---

## 2. What the null test actually proved

```
worst null (deterministic voices)    -63.7 dB
worst band error (noise voices)       7.98 dB   (control: 11.15)
worst envelope error (noise)          5.44 dB   (control:  7.54)
```

Every deterministic voice nulls between −64 and −164 dB against scsynth. The
four noise-driven voices cannot null — different RNG stream — so they are
measured against a **control**: the same C++ voice rendered twice with
different noise. They score better than that control, which is the strongest
statement available for a stochastic voice.

`--probes` nulls each UGen alone: filters at −149 to −243 dB, oscillators at
−86 to −240, envelopes at −147 to −158.

### The six bugs it found

None of these sounded *wrong*. All of them sounded *not quite it*, which is
exactly the failure a null test exists to catch and ears do not.

1. **`BHiPass4` applies `sqrt(rq)` per section.** Two cascaded sections each
   at `rq` give an overall `rq²`, so SC takes the square root first. Worth
   20 dB on the open hat.
2. **`EnvGen.kr` segments run an integer number of control periods**, floored,
   and in 3.11.2 the curve rate is computed over that integer. Treating the
   duration as fractional held every segment 1.45 ms too long.
3. **A control-rate *frequency* is sampled once per block and held**, while
   the same envelope multiplying audio is ramped. Interpolating it integrated
   into a complete phase decorrelation of the bass drum over one second.
4. **The block ramp starts *at* the previous control value**, not one step in.
5. **SC runs one sample through the whole graph at synth construction.** Every
   UGen Ctor ends with `next(unit, 1)`, so filters see their first input twice
   and a swept oscillator takes a phase step at the envelope's *initial*
   value. Modelled in the harness only, behind `SC808_NULLTEST`, and compiled
   out of the module — 8W8's voices are persistent and doing this on every hit
   would be a bug.
6. **The bass drum never reset its oscillator phases** to `pi/2`.

### Version pinning is load-bearing

The reference is SuperCollider **3.11.2**, and that is not cosmetic. 3.11's
`EnvGen` computes its curve rate over the integer control-period count; the
development branch reworked it to a fractional duration with a residual
carried between segments. Moving the reference to a newer SC without updating
`Env::armSegment()` shows up as the envelopes and swept voices dropping from
−150 dB to about −40.

---

## 3. Verification, by piece

| Piece | How it is checked |
|---|---|
| Engine A voices | `test/nulltest.sh` — render twice, subtract |
| Individual UGens | `test/nulltest.sh --probes` |
| Circuit kick | `tools/bd_check` — every behaviour the paper asserts |
| Kit balance | `tools/kit_check` — fits trims and headroom, reports the residual |
| On-device editor | `test/ui_chain.test.mjs` against the real `param_pages` |
| Editor ↔ DSP agreement | same file — cross-checks the pad tables |
| Remote panel | same file — every key it touches must exist in `chain_params` |
| The shipped `.so` | `sc808_loadtest`, dlopened exactly as the chain host does |
| CPU | `sc808_bench`, on the device |

The two cross-checks were confirmed to fail when deliberately broken. A test
that cannot fail is not a test.

### Measured on the Move

```
worst single lane   cymbal, 41x realtime (2.4% of a core)
busy pattern        18x realtime (5.6% of a core)
all 15 every 16th   5.4x realtime (18.6% of a core)  [pathological]
```

6W6 ships at 22% of a core. 8W8 is well inside that.

---

## 4. Decisions worth remembering

**The roster is 15 + Master**, filling Move's left 4×4 pad block exactly. Rim
shot and claves share a lane with a mode switch — because they share a
*channel* on the hardware, where the panel has one RS/CL selector and a
pattern cannot contain both. Merging the pair the machine itself merges is
what frees the pad Master needs.

**Kit balance is fitted, not chosen**, and two earlier metrics are recorded in
`kit_check.cpp` as failures worth not repeating:

- *Peak* is wrong. The closed hat's raw peak is 17, a single-sample filter
  transient at the onset, and trimming for it makes the hat inaudible.
- *RMS over a fixed window* is wrong more subtly. A 70 ms rim shot measured
  over 150 ms reads quiet because it is **short**, and fitting to that asked
  for 16.7× on a voice with a crest factor of 11 — which drove it through the
  diode stage and out the other side as a square wave.
- The loudest 20 ms is immune to both.

**Trim goes before the drive stage.** With lanes arriving 33 dB apart, a
post-drive trim leaves Drive 64 meaning something different on every pad.

**Headroom is fitted against the worst realistic case, not the representative
one.** Fitting a four-voice downbeat left the two-bar pattern clipping,
because a real pattern also carries the previous bar's tails.

**Defaults are sc808's own declared arguments**, converted to pot positions by
the generator, so a fresh patch is the sound the null test verified. Three
documented exceptions: the high conga's base note (sc808 gives it the same
note as the low conga, plainly a slip), and the kick's Attack and Decay, which
mean different things to the two engines.

---

## 5. What is left

**Engine B, the rest of it.** On the hardware, the snare, all three toms, all
three congas, the rim shot, the claves and the cowbell are *all* bridged-T
networks — the same topology as the kick with different component values. One
well-built bridged-T voice class already exists; extending it across eleven
instruments is mostly parameter work.

The metal voices are the other half: the cymbal paper gives the six
Schmitt-trigger oscillators (205.3, 369.6, 304.4, 522.7 measured, plus 800 and
540 on trimpots), the passive mixing network, two bandpasses at ~3440 and
~7100 Hz, three swing-type VCAs with fitted nonlinearities and three
Sallen-Key highpasses.

**Free-running oscillators.** On a real 808 the six Schmitt oscillators never
stop — the envelopes gate them. sc808 restarts them per note because each note
is a new synth, which is why its hats are identical hit to hit. Free-running
them is a small change and a very audible one, and it belongs in Engine B
because it is a deviation from the transcription.

**The clap** is the widest gap between sc808 and hardware in Engine A: the real
808 fires three fast bursts before the tail, sc808 uses one immediate and one
delayed 26 ms. It is also the voice whose null is weakest.

**On-device UI verification.** The editor is tested offline against the real
library, but nobody has driven it on the Move yet.
