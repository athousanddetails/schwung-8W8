# 8W8 — design analysis

A TR-808 style drum machine for Schwung on Ableton Move, built to the same
pattern as [9W9](https://github.com/athousanddetails/schwung-9W9) (TR-909) and
6W6 (TR-606): synthesised voices, every continuous control a 0–127 pot,
per-voice Drive + Distortion type, a Master drive stage, pad-follow editing,
per-lane mutes, remote panel, Movy config.

This document is the pre-build analysis: what source material exists, what it
is actually worth, and what 8W8 should be built from.

---

## 1. The source material

### 1.1 sc808.scd — what it is

`reference/sc808/sc808.scd`, from Sonic Pi. 16 SynthDefs by Yoshinosuke
Horiuchi, adapted by Sam Aaron. **MIT licensed** — Sonic Pi's LICENSE.md puts
`etc/synthdefs/` under MIT with a named GPL-3.0 exception list, and sc808 is
not on it. MIT is GPL-3.0 compatible, so 8W8 can be GPL-3.0 like its siblings
and vendor sc808 unmodified, exactly as 6W6 vendors the MIT 606 repo.

Housekeeping note: the upstream file contains **every SynthDef twice** (lines
1–577 and 578–1152). Diffed: identical apart from whitespace and the header
comment. `reference/sc808/sc808.scd` is the deduplicated first copy.

The 16 voices are `bassdrum, snare, clap, tomlo, tommid, tomhi, congalo,
congamid, congahi, rimshot, claves, maracas, cowbell, closed_hihat,
open_hihat, cymbal`.

### 1.2 SC → C++ is mechanical, not hard

The whole UGen vocabulary sc808 uses is small and every one has a closed-form
implementation:

| UGen | Port |
|---|---|
| `SinOsc`, `LFTri`, `LFPulse` | trivial; `LFPulse` is **naive/aliasing** in SC too — reproduce it naively, the aliasing is part of the hat sound |
| `WhiteNoise` | SC's own LCG, or any uniform source |
| `EnvGen` + `Env` with `curve:` | one formula: `y = y0 + (y1−y0)·(1−e^{t·c})/(1−e^{c})` |
| `HPF`, `LPF`, `BPF` | SC's own 2-pole designs (specific coefficient formulas, not RBJ) |
| `BPeakEQ`, `BLowShelf`, `BHiShelf`, `BHiPass`, `BHiPass4`, `BBandPass` | the "B" suite is the RBJ Audio EQ Cookbook; `BHiPass4` is two cascaded `BHiPass` |
| `Limiter` | lookahead limiter — the one non-obvious port, used only in the kick |
| `DetectSilence` | becomes our per-lane gate, not a DSP block |
| `midicps` | `440·2^((m−69)/12)` |

Estimate: ~400 lines for an SC-compatible kernel, then 20–40 lines per voice
transcribed coefficient-for-coefficient. This is a **faithful translation
problem, and it is verifiable** — see §4.

### 1.3 But sc808 is a sound-alike, not a circuit model

This is the honest limitation. sc808's kick is `SinOsc` with a pitch envelope,
plus an `LFTri` sub and an HPF'd second sine for "punch", into a `Limiter`.
The real 808 kick is a **bridged-T network inside an op-amp feedback loop that
self-oscillates**, where the Decay knob sets loop gain. The difference shows
up exactly where people care:

- the **pitch sigh** (a real leakage effect, not a programmed envelope)
- the **attack punch** — a genuine centre-frequency jump of over an octave for
  ~6 ms, not a second oscillator layered on
- **no machine-gun effect** on fast repeats, because filter state carries over
- how **accent interacts with decay**, which is a circuit interaction

Its hats/cymbal are similar: right oscillators, hand-tuned EQ chains standing
in for the actual two-bandpass / three-VCA architecture.

So sc808 gets you a complete, good-sounding 808 quickly. It does not get you
the thing that makes an 808 an 808.

### 1.4 The two links you gave

- **Vult / VCV Drums** — a blog post about method ("build it, model it,
  improve it, optimize it", Wolfram SystemModeler → optimised code) and it is
  mostly about the **909**, not the 808. The author says outright the toms
  "differ the most" and were tuned to taste. No component values, no
  equations, no code. Useful as philosophy, not as a specification.
- **CCRMA seminar page** — an event listing for a 2014 DSP seminar. It names
  the researcher, which is the valuable part.

### 1.5 What that researcher actually published — this is the find

You said you couldn't find another faithful open-source 808. There isn't a
*repo*, but there are two **open-access papers** that are effectively a
specification, both by Kurt James Werner, Jonathan Abel and Julius Smith
(CCRMA, 2014), saved to `reference/papers/`:

**A. The bass drum** (DAFx-14) — a complete first-order circuit model:
- the full annotated schematic with Roland's part numbers and values
- pulse shaper as a passive low-shelf + a memoryless diode nonlinearity
  (`V+ ≥ 0 → V+`, `V+ < 0 → 0.71(e^{V+} − 1)`)
- the **bridged-T transfer functions** `H_bt1/2/3(s)` written out symbolically
  in R and C, so tuning "bends" fall out of changing a value
- the **feedback buffer** with the decay pot appearing directly in the
  coefficients as `VR₆·k`, `k ∈ [0,1]` — decay *is* loop gain, as in hardware
- the **attack frequency shift** via `R_effective` modulation
- the **pitch sigh** as leakage, with a fitted memoryless nonlinearity
  `i_C = −log(1 + e^{−α(V_comm−V₀)})·m/α`, and the fit values given:
  `α = 14.3150, V₀ = −0.5560, m = 1.4765·10⁻⁵`
- tone / level / output stage as a table of biquad coefficients
- **implementation notes**: bilinear transform, Transposed Direct Form II
  (chosen because the bridged-T coefficients are constantly time-varying), and
  one unit delay to break the delay-free feedback loop

Their results section explicitly demonstrates correct pitch sigh, correct
transient behaviour across accent levels, and **absence of machine-gunning**.

**B. The cymbal** (ICMC/SMC-14) — and therefore also CH, OH and cowbell, since
on real hardware all four share one HD14584 hex Schmitt-trigger inverter:
- the six oscillator frequencies: **205.3, 369.6, 304.4, 522.7 Hz** hardwired,
  plus **800 and 540 Hz** on internal trimpots (ranges 359.4–1149.9 and
  254.3–627.2)
- duty cycle **47.98%**, amplitude **5 V**
- the passive resistive mixing network that sums them
- two bandpass filters centred **~3440 Hz** and **~7100 Hz**
- three "swing-type" VCAs with fitted nonlinear transfer functions
- three Sallen-Key highpasses, then tone/level/output
- envelope generators solved as switched first-order ODEs with fitted
  constants (`V_on = 0.5899`, attack smoother `V_BE = 0.7258, τ = 1.0244·10⁻⁴`)

**Cross-check that matters:** sc808's hat oscillators are 203.52 / 366.31 /
301.77 / 518.19 / 811.16 / 538.75 Hz — within about 1% of Werner's measured
set. Horiuchi got the frequencies right. What he approximated is everything
around them.

The papers are CC-BY / open-access; implementing published equations with
citation is clean.

---

## 2. Proposed architecture — two engines

Take 9W9's precedent, which still ships `Engine: er-99` as a selectable
alternative to its circuit voices. Same idea, and it de-risks the build:

**Engine A — `sc808`.** Exact C++ transcription of the SynthDefs, null-tested
against SuperCollider. Gets a complete 15-voice kit playing on the Move early,
and is the fallback for any voice Engine B doesn't reach.

**Engine B — `circuit`.** Built from the papers:
- **Bridged-T voices** — BD, SD, LT/MT/HT, LC/MC/HC, RS, CL, and the cowbell.
  On real hardware *every one* of these is a bridged-T network; the paper
  notes the 808's snare, toms/congas and rimshot/clave all use the same
  topology as the kick, differing in component values. One well-built
  bridged-T voice class covers eleven instruments.
- **Metal chain** — the six Schmitt oscillators, passive mix, two bandpasses,
  three swing VCAs, three highpasses. Covers CH, OH, CY and (with the two
  tunable oscillators) the cowbell.
- **Noise voices** — maracas and clap stay close to sc808; they're noise +
  envelope + filters in hardware too, so there's little to gain.

Engine choice is a kit-level enum, `Engine: sc808 / circuit`, default
`circuit` once it's proven.

### 2.1 A faithful detail worth keeping

On a real 808 the low tom and low conga are **the same circuit** with a
switch — likewise mid and hi. sc808 gives six separate SynthDefs. Engine B
should implement three shared bridged-T voices with two parameter sets each,
which is both more faithful and cheaper. They still get six pads.

---

## 3. Move / Schwung fit

The roster maps onto the left 4×4 pad block cleanly — **15 drums + Master on
pad 16**, which is a better fit than 6W6 (8 voices, half the block) or 9W9
(11):

```
row 3 (92–95)   RS  CL  MA  CB
row 2 (84–87)   HT  LC  MC  HC
row 1 (76–79)   CH  OH  CY  [MASTER]
row 0 (68–71)   BD  SD  LT  MT
```

(Exact assignment to be settled — the constraint is that the bottom row reads
as the front-panel order and Master lands on the 16th pad, as in 9W9/6W6.)

Everything else is the established pattern and is essentially port-and-adjust
from 6W6, which is the cleaner of the two codebases:

- `scripts/gen_params.py` as the single source of truth generating
  `chain_params`, `ui_pages`, the pot table and `movy_config.json`. 6W6 added
  this because 9W9's hand-written second table was a standing drift hazard —
  keep it. Note its hard rule: **one Movy bank = exactly one page**.
- Pots resolved once at create time, no string search on the audio path.
- Per-voice Drive + 4 Distortion flavours (Diode / Clip / Fold / Crush) and a
  Master Drive/Distortion, lifted from `sd606_shape.h` so all three kits
  respond identically to the same knob. Option text sized for the grid's enum
  box (two lines of three characters).
- Choke: CH cuts OH, as on hardware, as a switch (Off / CH>OH / Mutual).
  Worth considering whether the 808's tom/conga channel sharing implies a
  choke group there too — on hardware they share a circuit, so they *do* cut
  each other. That's a faithful behaviour to offer as a switch.
- 2 ms fade for chokes and mutes, not a hard stop.
- State blob positional; appending safe, reordering breaks saved patches.
- Fitted defaults, not centred defaults, with `set_param(key, "default")`.
- v2 instance API, `-static-libstdc++`, glibc 2.35 gate.

### 3.1 Scale check

15 voices × ~6 pots ≈ 90 pots, 16 pages, 16 Movy banks. All array-driven, so
this is generator work, not new architecture. `module.json` stays under
Schwung's 8 KB cap because the JSON is served from the DSP via `get_param()`.

---

## 4. Verification — the part that makes "faithful" checkable

6W6 fitted its voices against hardware recordings and scored them. 8W8 has a
better option for Engine A: **SuperCollider is the ground truth and it is
executable.** Render each SynthDef with `scsynth` in NRT mode, render the same
voice from the C++ port, and null-test. A faithful translation should null to
near the noise floor for the deterministic voices, and match spectrally for
the noise-driven ones (seeded RNGs differ).

That turns "faithful translation" into a number in CI rather than a vibe.

For Engine B the reference is the papers' own figures (transient response,
instantaneous-frequency trace showing the pitch sigh, the machine-gun test) —
reproduce those plots and compare. Plus A/B against 808 recordings.

**Blocker to flag now:** this Mac has `clang++` but **no cmake, no Docker, and
no SuperCollider**. 6W6's `scripts/build.sh` says so in its header — the
cross-build runs on the VPS. So:
- offline renderers can be built here directly with `clang++`, no cmake, which
  is enough to listen and iterate;
- the ARM64 module build and the NRT null-test both need the VPS, and
  SuperCollider needs installing there (headless `scsynth` NRT is a small
  install and scriptable).

I'd push for the null test. Without it, Engine A is "sounds about right"
instead of "provably the same".

---

## 5. Risks and open questions

1. **CPU.** 6W6's metal voices ate 55% of the block budget and needed a
   coupled-form oscillator rewrite. 8W8's metal is cheaper (6 naive squares +
   biquads, not 47 sines), but Engine B's bridged-T has **time-varying
   coefficients**, and recomputing biquad coefficients per sample across 11
   voices is the real cost. Mitigation: recompute per-sample only during the
   ~10 ms attack transient, then drop to a 32-sample control rate, which is
   the trick 6W6 already proved. Needs a `bench` target and a measurement on
   the device before the design is locked.
2. **`Limiter.ar`** in sc808's kick is a lookahead limiter. A faithful Engine A
   needs a real one, not a clipper.
3. **sc808 syntax quirks.** Several arg lists are missing commas
   (`click = 0.11` then `decay = 2` with no separator). SC tolerates it; a
   transcriber must read intent, not parse. Worth compiling the original in SC
   once to confirm the parameter defaults that actually take effect.
4. **Licensing.** sc808 MIT → vendor unmodified under `src/vendor/sc808`,
   credit Horiuchi and Sam Aaron, 8W8 GPL-3.0 like its siblings. Papers cited,
   not copied. `THIRD_PARTY.md` as in 6W6.
5. **Trademark.** Same disclaimer as 9W9/6W6.

---

## 6. Suggested order of work

1. Repo skeleton from 6W6 (CMake, toolchain, Docker, build/deploy scripts,
   plugin wrapper, `gen_params.py`), retargeted to 8W8's roster. Nothing
   sounds yet, but the module loads.
2. SC-compatible DSP kernel + Engine A, voice by voice, with a native offline
   renderer for listening.
3. NRT null test against SuperCollider on the VPS. Gate Engine A on it.
4. On-device: pads, mutes, pad-follow, choke, drive/distortion, master,
   loadtest, bench. **First playable 808.**
5. Engine B — bridged-T voice class first (proves BD, then eleven voices come
   nearly free), then the metal chain.
6. Fit defaults, measure kit trims, remote panel, Movy, docs, release.

Steps 1–4 are well-understood port work with a known-good template. Step 5 is
the interesting one, and it's the one that makes 8W8 a real 808 rather than
another sample-alike.
