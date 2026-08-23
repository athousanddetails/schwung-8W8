# 8W8 — Rhythm Composer for Ableton Move

A TR-808 style drum machine for [Schwung](https://github.com/charlesvestal/schwung)
on Ableton Move. Fifteen voices, all synthesised, no samples. The kick is a
model of the 808's actual circuit; the rest of the kit is a transcription of
Sonic Pi's sc808 that is verified sample-for-sample against SuperCollider.

Fifteen drums and Master fill Move's left 4×4 pad block exactly — where
[6W6](https://github.com/athousanddetails/schwung-6W6) uses half the block and
[9W9](https://github.com/athousanddetails/schwung-9W9) eleven pads.

## Voices

| Voice | Engine |
|---|---|
| Bass Drum | **Circuit** — a bridged-T network in an op-amp feedback loop, where Decay is loop gain and the pitch sighs because the circuit makes it. Or **sc808**, switchable |
| Snare | Two detuned shell oscillators crossfaded against bandpassed noise; Snappy is the crossfade |
| Low / Mid / Hi Tom | A sine on a falling pitch envelope under a very steep amplitude curve |
| Low / Mid / Hi Conga | The same circuit at a different tuning — as on the hardware, where tom and conga share a channel |
| Rim / Clave | One lane, one switch, because the 808's front panel has one RS/CL selector |
| Maracas | Noise, a 5.6 kHz highpass, and a 27 ms ramp up that is the rattle |
| Hand Clap | An immediate noise burst, a second one 26 ms later, and a long diffuse tail |
| Cowbell | Two pulse oscillators at 811 and 539 Hz — numbers 5 and 6 of the metal bank, exactly as the hardware wires them |
| Closed / Open Hat | The six Schmitt-trigger oscillators through two filter paths |
| Cymbal | The same six through **three** parallel chains and four envelopes |

Every drum has **Tune, Decay, Drive, a Distortion type** (Diode / Hard Clip /
Wavefolder / Bitcrush) and **Level**, plus a **Master Drive / Distortion**
across the kit. Every continuous control is a **0–127 pot**. **Hat choke is a
switch**: Off, CH cuts OH (the hardware's shared metal source), or Mutual.

Defaults are not pot centre. They are sc808's own declared arguments, which is
what the null test verifies — so a fresh patch is the sound that was checked.

## The two kicks

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

## Workflow on the Move

- **Pads (left 4×4)** play and select drums; the parameter page follows what
  you hit. Row 1: BD SD LT MT. Row 2: HT LC MC HC. Row 3: RS MA CP CB. Row 4:
  CH OH CY, and pad 16 opens **Master**. **Shift+Pad** selects silently (works
  during playback). **Mute+Pad** mutes that drum (`[M]` in the title bar).
- **Knobs 1–8** edit the visible page, drawn with Schwung's stock knob grid
  (host 0.12.1+): **jog** cycles pages, **Shift+Jog** jumps sections, **jog
  click** opens the section list, **Shift** reveals values / fine mode,
  **Mute+knob** resets a pot to its default.
- **Sequencing:** use Move's own sequencer — a drum track with a kit, muted
  (HiJack), track MIDI OUT on the slot's channel. Each drum is its own lane.
  Note map: drum rack (36–50, default) or General MIDI, switchable.
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

The circuit kick has nothing to null against, so it gets
`tools/bd_check` instead — every behaviour the paper asserts about the real
circuit, asserted as a test. `test/ui_chain.test.mjs` runs the on-device
editor against the real `param_pages` library and cross-checks its pad tables
against the DSP's. `sc808_loadtest` dlopens the shipped `dsp.so` exactly as
Schwung's chain host does.

CPU on the device: a busy pattern is **5.6% of one core**.

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
