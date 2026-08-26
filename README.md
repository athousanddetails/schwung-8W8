# 8W8 — Rhythm Composer for Ableton Move

A TR-808 style drum machine for [Schwung](https://github.com/charlesvestal/schwung)
on Ableton Move. Sixteen voices, all synthesised, no samples. Nine of them
are models of the 808's actual circuits, built from the service notes; the
rest of the kit is a transcription of Sonic Pi's sc808, verified
sample-for-sample against SuperCollider.

Sixteen drums fill Move's left 4×4 pad block exactly — where
[6W6](https://github.com/athousanddetails/schwung-6W6) uses half the block and
[9W9](https://github.com/athousanddetails/schwung-9W9) eleven pads. There is no
Master pad, because sixteen drums leave no room for one; Master is on the jog
with every other page.

## Voices

| Voice | Engine |
|---|---|
| Bass Drum | **Circuit** — a bridged-T network in an op-amp feedback loop, where Decay is loop gain and the pitch sighs because the circuit makes it. Or **sc808**, switchable |
| Snare | **Circuit** — two bridged-T shells at 173 and 336 Hz from the service notes' component values, so Tone balances two ringing shells and Snappy sets the noise's length as well as its level. Or **sc808**, switchable |
| Low / Mid / Hi Tom | **Circuit** — a bridged-T at Q 10.8 ringing in a feedback loop, plus the noise head the 808 feeds its toms from the pink-noise bus. Or **sc808**, switchable |
| Low / Mid / Hi Conga | The same channel with the switch the other way: struck clean, no head, all shell — which is what separates a conga from a tom on the hardware and now here. Or **sc808** |
| Rim Shot | A triangle and an 80% pulse through a fat +8 dB peak at 464 Hz — that peak is the "tock" |
| Claves | One sine, one envelope. The simplest voice in the kit, and its own pad |
| Maracas | Noise, a 5.6 kHz highpass, and a 27 ms ramp up that is the rattle |
| Hand Clap | **Circuit** — three noise bursts about 10 ms apart and a 330 ms tail, through the 874 Hz bandpass R342/R334/C128 make. Or **sc808**, switchable |
| Cowbell | Two pulse oscillators at 811 and 539 Hz — numbers 5 and 6 of the metal bank, exactly as the hardware wires them |
| Closed / Open Hat | The six Schmitt-trigger oscillators through two filter paths. **Free-running** by default, as on the hardware, so no two hits are the same |
| Cymbal | The same six through **three** parallel chains and four envelopes |

Every drum has **Tune, Decay, Drive, a Distortion type** (Diode / Hard Clip /
Wavefolder / Bitcrush) and **Level**, plus a **Master Drive / Distortion**
across the kit. Every continuous control is a **0–127 pot**. **Hat choke is a
switch**: Off, CH cuts OH (the hardware's shared metal source), or Mutual.

Defaults are not pot centre. Where sc808 is the engine they are sc808's own
declared arguments, which is what the null test verifies. Where a circuit
engine is the default they are the **circuit's** — the clap's Decay sits at
330 ms because that is R362 × C143 on the real board — so a fresh patch is
always the sound that was checked, whichever engine checked it.

## The circuit voices

Nine of the sixteen lanes have a circuit model as well as the sc808
transcription, and every one of them is switchable per patch, so nothing is
lost — the verified transcription is always one knob away.

The seven that do not yet have one are the rim shot, the claves, the maracas,
the cowbell, the two hats and the cymbal.

### The two kicks

The kick is the voice an 808 is judged on, so it gets both.

**Circuit** (default) is built from Werner, Abel and Smith's DAFx-14 analysis
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
- accent is a trigger **voltage** driving a diode, so a harder hit is a
  slightly different sound and not only a louder one

**sc808** is the transcription: a sine on a pitch envelope. A good kick drum,
and not a TR-808. Kept because it is the one the null test covers, and because
sometimes it is the one you want.

### The two snares

**Circuit** (default) is two bridged-T networks, derived from the component
values in Roland's service notes: R196/R197/C58/C59 give **173.334 Hz at
Q 17.4**, R195/R198/C60/C61 give **335.976 Hz at Q 10.7**, through the same
frequency and Q forms the bass drum paper uses. Three things follow that two
enveloped sines cannot do —

- **Tone is VR8**, the *balance* between the two shells, not a filter sweep
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

**Circuit** (default) is the board: Q70 turns the trigger into a short **burst
of pulses**, not one pulse — that is what a clap is, several hands not quite
together — and C143 bleeds off through R362 afterwards, which is a tail whose
time constant you can read straight off the schematic at **330 ms**. The noise
goes through IC22 (a BA662, the same VCA the snare uses) and IC21's
multiple-feedback bandpass, R342 15 k in, C128 = C129 = 4.7 nF, R334 100 k
around the op-amp:

    f0 = 1 / (2π C √(Rin Rf)) = 874.4 Hz     Q = ½ √(Rf / Rin) = 1.291

874 Hz is why an 808 clap sits where it does in a mix. So Decay is the tail,
Spread is the spacing of the burst, and both do what their labels say.

What is *not* derived: how many pulses and how far apart. That falls out of a
transistor's switching behaviour around the C.P. OFFSET trimmer, and nobody
has published the analysis. Three pulses about 10 ms apart is a measurement,
not a derivation, and it is a knob rather than a constant so it does not have
to be exactly right.

### Free-running metal

On the hardware the hats' and cymbal's six Schmitt-trigger oscillators never
stop — the envelopes gate them — so every hit catches the bank at a different
phase and no two 808 hats are quite the same. sc808 restarts them on every
note, because in SuperCollider every note is a new synth, and its hats are
bit-identical hit to hit. **Metal: Free / Retrig** on the Master page, Free by
default.

## Workflow on the Move

- **Pads (left 4×4)** play and select drums; the parameter page follows what
  you hit. Row 1: BD SD LT MT. Row 2: HT LC MC HC. Row 3: RS CL MA CP. Row 4:
  CB CH OH CY. All sixteen are drums, so **Master** is reached by the jog
  rather than by a pad. **Shift+Pad** selects silently (works during
  playback). **Mute+Pad** mutes that drum (`[M]` in the title bar).
- **Knobs 1–8** edit the visible page, drawn with Schwung's stock knob grid
  (host 0.12.1+): **jog** cycles pages, **Shift+Jog** jumps sections, **jog
  click** opens the section list, **Shift** reveals values / fine mode,
  **Mute+knob** resets a pot to its default.
- **Sequencing:** use Move's own sequencer — a drum track with a kit, muted
  (HiJack), track MIDI OUT on the slot's channel. Each drum is its own lane.
  Note map: drum rack (36–51, default) or General MIDI, switchable.
- Works with [Movy](https://github.com/DimaDake/schwung-movy) — a
  `movy_config.json` ships with the module.

## How it is verified

The claim about Engine A is that it is a **transcription** of sc808, not an
impression of it, and that claim is only worth anything if it is checked.

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
changes the clap's tail and Spread only moves its burst, and that a tom and a
conga at the same pitch, same decay and same accent are still not the same
sound. They also check that both engines on a lane land within 3 dB of each
other, because the two share one trim and a mismatch would make the Engine
switch change the mix.

`test/ui_chain.test.mjs` runs the on-device editor against the real
`param_pages` library, cross-checks its pad tables against the DSP's, and
checks that every attack/decay pair on a page draws as one envelope across
both knobs — the fault that started this round. `sc808_loadtest` dlopens the
shipped `dsp.so` exactly as Schwung's chain host does.

`./test/all.sh` runs all of it. Steps whose tooling is missing report **SKIP**
and say so in the summary, rather than passing quietly.

CPU, measured on the Move: a busy pattern is **6.3% of one core**, against
6W6's 22%. The worst single lane is the cymbal at 2.9%; all sixteen retriggered
every sixteenth — which no pattern does — is 22.9%.

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
  **Sam Aaron**, MIT. Vendored under `src/vendor/sc808`; every voice except
  the circuit kick is a transcription of one of them.
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
  architecture, pad gestures and the four distortion flavours, so all three
  kits feel identical under the hands.
- **Schwung** by Charles Vestal and contributors — the platform and the shared
  `param_pages` knob grid; **Movy** by DimaDake for the page model.

Built with AI assistance. GPL-3.0; see `LICENSE` and `THIRD_PARTY.md`.

## Contributing

**Contributions are open to anyone, any time — just submit a PR.** Voice
tweaks, new distortion flavours, UI improvements, Movy templates, docs, bug
reports: all welcome. If you touch a voice, run `./test/nulltest.sh`; if you
touch the kick, run `bd_check`. Please note in your PR which AI tools you
used, if any (same policy as Schwung upstream).

## Disclaimer

Not affiliated with, approved or endorsed by Ableton or Roland. TR-808 is a
trademark of Roland Corporation, referenced only to describe behaviour.
