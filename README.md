# 8W8 — Rhythm Composer for Ableton Move

A TR-808 style drum machine for [Schwung](https://github.com/charlesvestal/schwung)
on Ableton Move. Sixteen voices, all synthesised, no samples. Fifteen of them
are models of the 808's actual circuits, built from the service notes and the
published analyses; the sixteenth — the rim shot — is a transcription of Sonic
Pi's sc808, because two circuit rim shots were built and both lost to it by
ear. The rest of that transcription still ships and is still verified
sample-for-sample against SuperCollider; it is just no longer what you hear.

Sixteen drums fill Move's left 4×4 pad block exactly — where
[6W6](https://github.com/athousanddetails/schwung-6W6) uses half the block and
[9W9](https://github.com/athousanddetails/schwung-9W9) eleven pads. There is no
Master pad, because sixteen drums leave no room for one; Master is on the jog
with every other page.

![Bass Drum page on the Move](docs/img/device-bd.png)
![Hi Tom page, with the two sends](docs/img/device-ht.png)
![Maracas page, Attack and Decay drawn as one envelope](docs/img/device-ma.png)

The kick has no sends — it is dry by design. The maracas page shows the stock
knob grid spanning ONE envelope graphic across an adjacent Attack/Decay pair,
which is why those two are ordered the way they are.

## Voices

| Voice | Built from |
|---|---|
| Bass Drum | A bridged-T network in an op-amp feedback loop, where Decay is loop gain and the pitch sighs because the circuit makes it |
| Snare | Two bridged-T shells at 173 and 336 Hz from the service notes' component values; Snappy sets the noise's length as well as its level |
| Low / Mid / Hi Tom | A bridged-T at Q 10.8 ringing in a feedback loop, tuned and timed to hardware samples: F2/C3/G3, ring 0.39/0.23/0.18 s, the head a quiet texture on the strike |
| Low / Mid / Hi Conga | The same channel with the switch the other way: struck soft (it blooms at 2–5 ms where a tom clicks inside 1), no head, ringing twice as long at the same pitch, and tuned an octave-plus above its tom — G3/D4/A4, as measured |
| Rim Shot | **The one sc808 voice.** Two circuit rim shots were built from the schematic and both lost on hardware, the second badly. The transcription is the rim, tuned to a reference render so the tock lands on 1788 Hz |
| Claves | The clave network with the switch's 1k shunt: 2524 Hz at Q 14, ringing 30 ms as Roland's own model does |
| Maracas | Bus noise high-passed hard, 39 ms to silence, fitted to Roland's render with the kit switch flipped to MA |
| Hand Clap | Three noise bursts about 10 ms apart and a 330 ms tail (R362 × C143), through the 874 Hz bandpass R342/R334/C128 make. One knob: Decay, the tail |
| Cowbell | Oscillators 5 and 6 of the ONE shared free-running Schmitt bank, exactly as the hardware wires them |
| Closed / Open Hat | The shared bank through each hat's C-R ladder (1.5 nF open, 1.0 nF closed — the closed hat is high-passed higher, which is why it is the thinner voice), a swing VCA whose diode gates the tail dead |
| Cymbal | Werner's three-path structure voiced to a reference recording: two band passes, a linear-discharge shimmer, and a brushed floor under the crash |

Every drum has **Tune, Decay, Drive, a Distortion type, Level** and — every
one except the kick — **Rev** and **Dly** sends. Across the kit there is a
**Master Drive / Distortion**, a one-knob **Comp**, and **Velocity**.

**Seven distortion characters**, the same seven 9W9 and 6W6 offer so the knob
means the same thing on all three: Diode, Clip, SAT, BFZ, PDIST, Fold, Crush.
**Drive at 0 — the default — is a bit-exact bypass**: the 808 had no drive
stage, so a fresh patch has none, and the knob adds saturation without adding
level. Every curve's makeup is measured, not guessed; `tools/fx_probe` fails
the build if any of them moves the kit by more than 6 dB.

**Velocity** is one straight line with no threshold: a full-velocity hit is
the loudest the kit gets, and the **Velocity** knob on Master sets how far
below that a soft hit falls. It only ever carves down. On the eleven lanes
whose circuits take the strike as a trigger **voltage**, a hard hit is a
different sound and not just a louder one — so the line is split at the
unaccented point, the voltage carrying the top of the range and the lane's
gain the bottom.

**Reverb and Delay** are two send buses with a page each, reached by the jog.
Sends are post-fader. **The kick is dry** — no sends, by design: reverb on an
808 kick is mud, and the low end is what the send high-pass exists to keep out
of the wet path. Delay Time is a note division and follows the host tempo.

Every continuous control is a **0–127 pot**. **Hat choke is a switch**: Off,
CH cuts OH (the hardware's shared metal source), or Mutual. On the tom and
conga lanes **Decay is seconds of ring**, solved into loop gain at whatever
pitch Tune has chosen.

Defaults are the **circuit's** — the clap's Decay sits at 330 ms because that
is R362 × C143 on the real board — so a fresh patch is always the sound that
was checked.

## The circuit voices

Every lane was built, deployed and judged on hardware one at a time, and the
Engine switches came off once each one had an answer. The sc808 transcription
stays in the tree — the null test still verifies all of it, straight from the
header — but it is not on the panel any more, and the freed knob slot on every
instrument is what the send pair now occupies.

### The kick

The kick is the voice an 808 is judged on.

It is built from Werner, Abel and Smith's DAFx-14 analysis
of the real bass drum circuit. Decay is **loop gain**, not an envelope: the
network's own ringing is about 80 ms and everything past that is the feedback
buffer failing to lose the signal. That one fact is why it behaves like an
808 —

- the pitch **sighs** downward through the note and settles, because leakage
  shifts the network's effective resistance as the note dies
- the attack is a real **octave jump** in centre frequency and Q for a few
  milliseconds — the beater, not a second oscillator layered on
- fast repeats do not **machine-gun**: the resonator state is still there when
  the next pulse arrives, so no two hits are identical
- velocity is a trigger **voltage** driving a diode, so a harder hit is a
  slightly different sound and not only a louder one

sc808's kick, for contrast, is a sine on a pitch envelope. A good kick drum,
and not a TR-808 — which is why this one exists.

### The snare

Two bridged-T networks, derived from the component
values in Roland's service notes: R196/R197/C58/C59 give **173.334 Hz at
Q 17.4**, R195/R198/C60/C61 give **335.976 Hz at Q 10.7**, through the same
frequency and Q forms the bass drum paper uses. Three things follow that two
enveloped sines cannot do —

- the shell **balance** is VR8's, fixed at its centre — the Tone pot was cut
  on the field verdict "Tune is enough"
- the shells **decay at different rates** from their own components: the
  harmonic dies in about 65 ms while the fundamental rings for 200
- **Snappy is a voltage divider on the trigger** into the noise envelope
  generator, so turning it down makes the noise shorter as well as quieter

The noise runs through Werner's model of the swing-type VCA — half-wave
rectification times the envelope. What is *not* derived, and is marked in the
source: the noise high-pass corner and the envelope times, which are not in
the sources and take sc808's fitted values instead.

### Toms and congas, and why they used to sound the same

They *were* the same. sc808 gives the hi tom and the hi conga the same note,
the same two detune ratios and the same graph — a sine under a steep curve —
so the only thing separating them was how long they rang. Transposing one
does not fix that; it just moves it.

The hardware has three tom/conga **channels** and a switch, and it can never
play a tom and a conga together. 8W8 gives them a pad each, so ours have to be
further apart than the hardware's, not closer.

**Circuit** (default) is what the service notes show. One bridged-T network
per channel, its two resistors the same in all three — 4.7 k in the shunt arm
and 2.2 M in the series arm, which is a **Q of 10.82** — inside a feedback
loop, so **Decay is loop gain** here exactly as it is on the kick. The
TOM/CONGA switch grounds a node for conga and puts 1.5 k there for tom: it is
in the *trigger* path, so what it changes is how the network is struck. And
each channel takes **P.N.**, the machine's pink noise, alongside its trigger —
that is the drum head, the part of a tom that is a stick on a skin rather than
a shell ringing. The toms get it, the congas do not, and that is the
difference you actually hear.

What is *not* derived, and is marked in the source: the loop-gain map, the
pitch drop, and how much noise head a tom gets. Werner's papers do that work
for the bass drum and the cymbal; there is no equivalent for the toms, and
these are fitted rather than analysed.

### The clap that finally has a Decay knob

sc808's clap fires one burst, a second 26 ms later, and a long diffuse tail.
Two things followed from that, and both were reported as faults:

- **Decay did nothing.** In the SynthDef it sets the attack burst's envelope,
  whose curve is −160 — so steep that the audible part lasts `decay/160`,
  under two milliseconds across the whole pot. The part you hear is a second
  envelope hard-coded to six seconds, which no knob reaches.
- **Spread was odd.** One delayed burst over 5–100 ms is a flam.

The circuit is the board: Q70 turns the trigger into a short **burst of
pulses**, not one pulse — that is what a clap is, several hands not quite
together — and C143 bleeds off through R362 afterwards, which is a tail whose
time constant you can read straight off the schematic at **330 ms**. The noise
goes through IC22 (a BA662, the same VCA the snare uses) and IC21's
multiple-feedback bandpass, R342 15 k in, C128 = C129 = 4.7 nF, R334 100 k
around the op-amp:

    f0 = 1 / (2π C √(Rin Rf)) = 874.4 Hz     Q = ½ √(Rf / Rin) = 1.291

874 Hz is why an 808 clap sits where it does in a mix. Decay is that tail —
one knob, scaling both derived constants together, with its default sitting on
the hardware's own 330 ms.

Spread and Room existed briefly and were cut on the field verdict: Spread at
its extremes turned the burst into a flam, and Room against Decay was two
knobs for one audible thing. The burst spacing is fixed at the hardware's
~10 ms inside the voice, where it belongs.

What is *not* derived: how many pulses and how far apart. That falls out of a
transistor's switching behaviour around the C.P. OFFSET trimmer, and nobody
has published the analysis. Three pulses about 10 ms apart is a measurement,
not a derivation, and it is called out as such in the source so it does not have
to be exactly right.

### Free-running metal

On the hardware the hats' and cymbal's six Schmitt-trigger oscillators never
stop — the envelopes gate them — so every hit catches the bank at a different
phase and no two 808 hats are quite the same. There is ONE bank, ticked once
per sample and shared by the cowbell, both hats and the cymbal, exactly as the
hardware wires half an HD14584. sc808 restarted them on every note because in
SuperCollider every note is a new synth; with that engine gone, free-running
is simply what the module does, and the loadtest asserts consecutive hats are
different hits.

## Workflow on the Move

- **Pads (left 4×4)** play and select drums; the parameter page follows what
  you hit. Row 1: BD SD LT MT. Row 2: HT LC MC HC. Row 3: RS CL MA CP. Row 4:
  CB CH OH CY. All sixteen are drums, so **Master** is reached by the jog
  rather than by a pad. **Shift+Pad** selects silently (works during
  playback). **Mute+Pad** mutes that drum (`[M]` in the title bar).
- **Knobs 1–8** edit the visible page, drawn with Schwung's stock knob grid
  (host 0.12.1+): **jog** cycles pages, **Shift+Jog** jumps sections, **jog
  click** opens the section list, **Shift** reveals values / fine mode,
  **Mute+knob** resets a pot to its default. Master, **Reverb** and **Delay**
  are pages like any other, reached the same way.
- **Jog click while on Main locks the page** (`[L]` in the title bar). Locked,
  the pads still play and still record but the page stops following them, so
  the master knobs stay under your hands while you jam the kit. **Shift+Pad**
  still navigates — that gesture is an explicit "take me there". Click again
  to unlock.
- **Sequencing:** use Move's own sequencer — a drum track with a kit, muted
  (HiJack), track MIDI OUT on the slot's channel. Each drum is its own lane.
  Note map: drum rack (36–51, default) or General MIDI, switchable.
- Works with [Movy](https://github.com/DimaDake/schwung-movy) — a
  `movy_config.json` ships with the module.
- **Remote panel** in a browser, served by schwung-manager: the whole machine
  on one screen, every control at once, with the device's current page
  highlighted.

![8W8 remote panel](docs/img/remote-ui.png)

Top deck is the 808's own panel. Middle deck is Drive, Distortion and the two
send amounts per drum — none of which an 808 had, which is what the header
says. Bottom row is the send buses and Master.

## How it is verified

The claim about the sc808 transcription is that it IS a transcription, not an
impression of one, and that claim is only worth anything if it is checked. It
still matters with one voice left on the panel: the transcription is what the
circuit models were measured against on the way in, and the rim shot you hear
is one of them.

```bash
./test/nulltest.sh --probes
```

renders every voice twice — once through `scsynth` from the vendored
SynthDefs, once through `src/dsp/sc808_voices.h` — and subtracts them.

| | result |
|---|---|
| every deterministic voice | nulls between **−64 and −164 dB** |
| the four noise-driven voices | cannot null (different RNG stream), so they are measured by band and envelope error against a **control**: the same C++ voice rendered twice with different noise. They score **better than that control** — as close to SuperCollider as two takes of the same voice are to each other |
| every individual UGen | `--probes` nulls each one alone; worst is −59 dB, the filters are −149 to −243 |

Getting there found six real bugs, none of which sounded *wrong* and all of
which sounded *not quite it*. They are written up in the git history and in
the comments where they happened: `BHiPass4` applies `sqrt(rq)` per section;
`EnvGen.kr` segments run an integer number of control periods and the curve
rate is computed over that integer; a control-rate **frequency** is sampled
once per block and held while the same envelope multiplying audio is ramped;
the block ramp starts *at* the previous control value; SuperCollider runs one
sample through the whole graph at synth construction; and the bass drum never
reset its oscillator phases.

A circuit voice has nothing to null against, so each one gets a test that
asserts what its header claims instead: `tools/bd_check` for the kick,
`tools/cp_check` for the clap, `tools/tom_check` for the tom and conga
channel. The last two assert the reports that produced them — that Decay
changes the clap's tail, and that a tom and a conga at the same pitch, same
decay and same velocity are still not the same sound. `tom_check` also keeps
the sc808 tom as a **level reference**: the lane trims were fitted while both
existed, so a circuit tom that drifts far from it has quietly moved the kit
balance.

Three checks guard the work that has no reference to null against:

- **`tools/golden_check`** renders all sixteen lanes through the real engine
  at two velocities and compares them **bit for bit** against
  `tools/golden.txt`. The voices were tuned one at a time against reference
  recordings and signed off by ear; nothing else in the suite asserts that
  work, and a structural change to the engine around them fails silently —
  the kit still plays, it just plays something else. Every structural commit
  in this repo kept all 32 renders identical.
- **`tools/vel_check`** asserts velocity's *properties* rather than a curve:
  monotonic, no step where the old accent switch sat at 100, genuinely flat at
  depth 0, and never louder at full velocity than before the knob existed. It
  checks a trigger-voltage lane and a gain lane, because on this kit those are
  two different mechanisms and the join between them has to hold.
- **`tools/fx_probe`** measures what the drive stage promises: bypass at zero
  is bit-exact for all seven types, no curve moves the kit by more than 6 dB
  anywhere on its throw, Crush really holds samples, both send buses return
  exactly zero from silence, and the Comp closes the gap between loud and
  quiet passages without changing the loudness.

`test/ui_chain.test.mjs` runs the on-device editor against the real
`param_pages` library, cross-checks its pad tables against the DSP's, and
checks that every attack/decay pair on a page draws as one envelope across
both knobs — the fault that started this round. `sc808_loadtest` dlopens the
shipped `dsp.so` exactly as Schwung's chain host does.

`./test/all.sh` runs all of it. Steps whose tooling is missing report **SKIP**
and say so in the summary, rather than passing quietly.

CPU, measured on the Move: a busy pattern is **3.5% of one core**, against
6W6's 22%. The pathological all-sixteen-every-16th case is 11.9%. The circuit
voices are cheaper than the transcriptions they replaced — a bridged-T in a
loop is a handful of multiplies, and the sc808 graphs were not — and about a
point of that 3.5% is the two send buses, which tick every sample whether or
not anything is feeding them so that a tail survives its send being turned
down.

## Install

Requires Schwung **0.12.1 or newer**. Via the Schwung Module Store /
[schwung-manager](https://github.com/charlesvestal/schwung), or manually:
build, then copy `dist/8w8/` to
`/data/UserData/schwung/modules/sound_generators/8w8/` on the device.

## Building

Requires Docker (cross-compiles for the Move's ARM64, pinned to glibc 2.35):

```bash
./scripts/build.sh all            # builds build/dsp.so + dist/8w8-module.tar.gz
./scripts/deploy.sh move.local    # safe deploy (atomic rename, never over a live .so)
```

`scripts/build.sh` also builds `sc808_loadtest`, an on-device test that
dlopens the real `dsp.so` exactly as Schwung's chain host does and checks that
every pad sounds, that Master does not, that every generated parameter key
resolves, mutes, the hat choke and state round-trip — end to end.

The parameter surface has one source: `scripts/gen_params.py` generates the
pot table, `chain_params`, the page hierarchy and `movy_config.json` from a
single dict. Adding a control is one edit.

## Credits and provenance

8W8 stands on other people's work and says so:

- **[sc808](https://github.com/sonic-pi-net/sonic-pi/blob/dev/etc/synthdefs/designs/supercollider/sc808.scd)**
  — the 808 SynthDefs by **Yoshinosuke Horiuchi**, adapted for Sonic Pi by
  **Sam Aaron**, MIT. Vendored under `src/vendor/sc808`. **Exactly one voice
  you hear is one of these: the rim shot.** The other fifteen lanes are
  circuit models and are not transcriptions of anything. The rest of the
  transcription is still in the tree and still verified by the null test,
  because it is what the circuit models were measured against on the way in
  and it is what keeps `sc_ugens.h` honest.
- **Werner, Abel and Smith**, *A Physically-Informed, Circuit-Bendable,
  Digital Model of the Roland TR-808 Bass Drum Circuit* (DAFx-14) and *The
  TR-808 Cymbal* (ICMC|SMC 2014) — the circuit analysis the kick is built
  from. Cited, not copied; `src/dsp/sc808_bd_circuit.h` says exactly which
  parts are theirs and which are re-derived, and why.
- **SuperCollider** by James McCartney and contributors — `sc_ugens.h`
  reimplements the UGens sc808 uses, each one naming the source file its
  behaviour was read out of. Pinned to 3.11.2, and that matters.
- **[9W9](https://github.com/athousanddetails/schwung-9W9)** and
  **[6W6](https://github.com/athousanddetails/schwung-6W6)** — the module
  architecture, pad gestures, the Main-page lock, the seven distortion
  characters, the reverb/delay buses and the one-knob glue, so all three kits
  feel identical under the hands. The maths is theirs, copied rather than
  re-derived; where 8W8 departs (the drive pot's range, the makeup exponents)
  the source says why.
- **Schwung** by Charles Vestal and contributors — the platform and the shared
  `param_pages` knob grid; **Movy** by DimaDake for the page model.

Built with AI assistance. GPL-3.0; see `LICENSE` and `THIRD_PARTY.md`.

## Contributing

**Contributions are open to anyone, any time — just submit a PR.** Voice
tweaks, new distortion flavours, UI improvements, Movy templates, docs, bug
reports: all welcome. If you touch a voice, run `./test/nulltest.sh` **and**
`golden_check` — the latter is what proves you did not move the kit by
accident. If you touch the kick, run `bd_check`. Please note in your PR which AI tools you
used, if any (same policy as Schwung upstream).

## Disclaimer

Not affiliated with, approved or endorsed by Ableton or Roland. TR-808 is a
trademark of Roland Corporation, referenced only to describe behaviour.
