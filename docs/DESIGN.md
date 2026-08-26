# 8W8 — design notes

What was built, what it is built from, what was learned building it, and what
is left. The pre-build analysis that started this is in the git history; this
is the version that reflects the code.

---

## 1. The two engines

**Engine A — sc808.** Every voice except the kick and the snare is a
transcription of one of the sixteen SynthDefs in Sonic Pi's `sc808.scd` (MIT, by Yoshinosuke Horiuchi,
adapted by Sam Aaron). Vendored under `src/vendor/sc808`, transcribed in
`src/dsp/sc808_voices.h`, and **verified sample-for-sample against
SuperCollider** by `test/nulltest.sh`.

**Engine B — the circuit.** Nine of the sixteen lanes now have one, all
default and all switchable:

| Voice | File | Built from |
|---|---|---|
| Bass drum | `sc808_bd_circuit.h` | Werner, Abel and Smith, DAFx-14 |
| Snare | `sc808_sd_circuit.h` | service notes' component values, via Werner's snare analysis |
| Tom / conga ×6 | `sc808_tom_circuit.h` | service notes, voicing board — Q derived, the rest fitted |
| Hand clap | `sc808_cp_circuit.h` | service notes, voicing board — bandpass and tail derived |

The shared blocks live once, in `sc808_circuit_common.h`: the pulse shaper,
the op-amp clip, the bridged-T, and now the multiple-feedback bandpass.

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
| Circuit snare | nothing of its own yet — see "what is left" |
| Circuit clap | `tools/cp_check` — Decay moves the tail, Spread only moves the burst |
| Circuit tom/conga | `tools/tom_check` — tom ≠ conga at matched pitch; both engines within 3 dB |
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
worst single lane   cymbal, 35.1x realtime (2.9% of a core)
busy pattern        15.9x realtime (6.3% of a core)
all 16 every 16th    4.4x realtime (22.9% of a core)  [pathological]
```

6W6 ships at 22% of a core. 8W8 is well inside that — and note that the busy
pattern did not get *more* expensive when nine lanes moved to circuit
models. A bridged-T in a loop is two biquad-ish updates and a clip; sc808's
voices are envelopes and oscillators. The pathological case rose from 19.7%
because there are sixteen lanes now instead of fifteen.

---

## 4. Decisions worth remembering

**The roster is 16 drums and no Master pad**, filling Move's left 4×4 block
exactly. Rim shot and claves used to share a lane with a mode switch, because
they share a *channel* on the hardware — one RS/CL selector, and a pattern
cannot contain both. They now have a pad each, which is a deliberate
departure: sc808 ships them as two SynthDefs, a pattern that wants both is a
normal thing to want, and it makes the roster sixteen. The cost is the Master
pad, which moves to the jog with every other page.

**Where a circuit engine is the default, the DEFAULT POT IS THE CIRCUIT'S.**
The clap's Decay sits at 330 ms because that is R362 × C143. This is the one
place 8W8 knowingly departs from "defaults are sc808's arguments", and it is
the right way round: a fresh patch should be the sound that was verified, and
for those lanes the thing that was verified is the circuit.

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

**Headroom is fitted to the actual two-bar demo pattern**, shared between
`render` and `kit_check` so they cannot disagree, targeted at −1 dBFS. Two
cheaper proxies were both wrong: a four-voice downbeat left the pattern
clipping at +0.7 dBFS, because a real pattern carries the previous bar's tails
underneath it; a six-voice accented hit went the other way and left it 8.6 dB
quiet, because no pattern fires six accented voices on one sixteenth. A busy
simultaneous hit does clip from here, which is the same trade 6W6 makes.

**Defaults are sc808's own declared arguments**, converted to pot positions by
the generator, so a fresh patch is the sound the null test verified. Three
documented exceptions: the high conga's base note (sc808 gives it the same
note as the low conga, plainly a slip), and the kick's Attack and Decay, which
mean different things to the two engines.

---

## 5. What is left

**One engine, eventually.** 8W8 is to settle on Circuit alone, the way 9W9
settled on one engine, and the per-lane `Engine` switch is to go — that frees
a knob slot on every instrument page, and those slots are wanted.

This is deliberately the LAST step, not the next one. Both engines stay while
the circuit voices are being judged against the transcription on hardware, and
seven lanes do not have a circuit model at all yet: rim shot, claves, maracas,
cowbell, both hats and the cymbal. Nine have one.

Two things not to lose when the switch goes. The sc808 transcription stays in
the tree regardless — it is what `test/nulltest.sh` verifies against
SuperCollider, and that null test is the strongest correctness claim this
project has. And `tools/tom_check` and the loadtest currently assert that the
two engines on a lane agree about level, which is a real check on the fitted
output scales; it needs replacing rather than deleting.

**Engine B, the rest of it.** The rim shot and the claves are bridged-T
networks on the hardware too, the same topology as everything else here, and
they do not have a circuit engine yet. The evidence problem that used to block
the toms is smaller than it looked: the service notes have the component
values, and the two that matter for a bridged-T — the shunt and series
resistors — are legible. What they do not have is the analysis. Q comes
straight out of the values; loop-gain maps, pitch drops and noise levels do
not, and every one of those in `sc808_tom_circuit.h` is fitted and says so in
the header. Any new voice has to be that explicit about which half is which.

The metal voices are the bigger piece: the cymbal paper gives the six
Schmitt-trigger oscillators (205.3, 369.6, 304.4, 522.7 measured, plus 800 and
540 on trimpots), the passive mixing network, two bandpasses at ~3440 and
~7100 Hz, three swing-type VCAs with fitted nonlinearities and three
Sallen-Key highpasses. That is a real rebuild, not a parameter fit.

**Presets.** There are none. Sixteen lanes with five controls each is a lot of
knobs to arrive at from defaults.

**On-device UI verification.** The editor is tested offline against the real
library, and the DSP has now been benchmarked and loadtested on the Move, but
nobody has driven the *interface* there yet.

---

## 6. Traps worth not falling into twice

**calloc means no constructors.** `sc808_create` allocates the whole engine
with `calloc`, so every voice object is zero bytes and C++ default member
initialisers never run. `PulseShaper`'s `dc_ = 0.045` became `0.0`, which
makes it return exactly zero for any input. The toms still sounded, because
their noise head drives the resonator on its own, and only the *congas* went
silent — so the search started at the congas, which were fine. Every voice's
`init()` must set everything it needs, explicitly.

**A short initialiser list zero-fills its tail, silently.** Adding a sixteenth
voice left `kVoiceTrim` with fifteen entries; the cymbal read `0.0f` and
vanished. `tools/kit_check` caught it as a `-inf`, which is what that check is
for, but a compile error is cheaper — `sc808_engine.cpp` now `static_assert`s
that no trim is zero, and `tools/fit_trim.py` reads the lane order out of
`kVoiceIds` rather than keeping its own copy.

**And a third, about tests.** Two loadtest assertions failed after this round
without either voice changing by a single sample — confirmed by rendering the
hat from this engine and from the previous commit's and getting byte-identical
output. `quiesce()` takes lanes out of the mix but does not *end* their notes,
so once the circuit toms and clap started ringing for over a second, tests
began inheriting state from whatever ran before them. Anything measuring an
absolute number now takes a fresh instance. The Retrig threshold had also been
set from a figure four orders of magnitude off the real one and only ever
passed on that inherited state; it is now written down as a measurement.
