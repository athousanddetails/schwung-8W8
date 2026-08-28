# Third-party code and provenance

8W8 is GPL-3.0. What it is built from, and under what terms:

## sc808 — the rim shot, and the reference the rest was measured against

`src/vendor/sc808/sc808.scd`, vendored unmodified apart from removing the
duplicated second copy of every SynthDef that ships upstream.

- **Source:** [Sonic Pi](https://github.com/sonic-pi-net/sonic-pi),
  `etc/synthdefs/designs/supercollider/sc808.scd`
- **Licence:** MIT. Sonic Pi's `LICENSE.md` places `etc/synthdefs/` under the
  MIT licence with a named list of GPL-3.0 exceptions; sc808.scd is not on
  that list. MIT is GPL-3.0 compatible, so it can be combined here.
- **Authorship:** the 808 SynthDefs are by **Yoshinosuke Horiuchi**, released
  free (see the header comment in the file), adapted for Sonic Pi by
  **Sam Aaron**.

Every voice in `src/dsp/sc808_voices.h` is a transcription of one of these
SynthDefs, verified sample-for-sample by `test/nulltest.sh`.

WHAT IS ACTUALLY HEARD, as of the circuit-only build: **the rim shot, and
only the rim shot.** Two circuit rim shots were built from the schematic and
both lost to this transcription on hardware, so it is the shipped voice on
that lane. The other fifteen lanes are circuit models built from the service
notes and the published analyses — they are not transcriptions of anything.

The rest of the transcription is still in the tree, still compiled, and still
verified by the null test, because it is what the circuit models were measured
against on the way in and it is the only thing that keeps `sc_ugens.h` honest.
The obligation to credit it is unchanged either way.

## SuperCollider — the UGen semantics

`src/dsp/sc_ugens.h` implements the UGens sc808 uses. The coefficient
formulas, envelope segment rules and oscillator phase conventions were read
out of SuperCollider's own server plugin sources (`FilterUGens.cpp`,
`LFUGens.cpp`, `NoiseUGens.cpp`, `BEQSuite.sc`) and each one names its origin
in a comment. No SuperCollider code is copied; the behaviour is reimplemented
and then verified against scsynth.

SuperCollider is GPL-3.0-or-later, Copyright (c) James McCartney and
contributors.

**Pinned to SC 3.11.2.** The reference renders come from that version, and it
matters: 3.11's `EnvGen` computes its curve rate over the integer control
period count, where the development branch reworked it to a fractional
duration with a residual carried between segments.

## TR-808 circuit analysis — for Engine B

Not code, and nothing is copied from them; they are the published analyses
Engine B's circuit voices are built from, and they are cited rather than
vendored.

- Werner, Abel and Smith, *A Physically-Informed, Circuit-Bendable, Digital
  Model of the Roland TR-808 Bass Drum Circuit*, DAFx-14.
  <https://dafx14.fau.de/papers/dafx14_kurt_james_werner_a_physically_informed,_ci.pdf>
- Werner, Abel and Smith, *The TR-808 Cymbal: a Physically-Informed,
  Circuit-Bendable, Digital Model*, ICMC|SMC 2014 (CC BY 3.0).
  <https://pureadmin.qub.ac.uk/ws/portalfiles/portal/125044847/tr_808_cymbal_a_physically_informed_circuit_bendable_digital.pdf>

## Schwung, 9W9 and 6W6

- **Schwung** by Charles Vestal and contributors — the platform, the plugin
  ABI (`src/host/plugin_api_v1.h`) and the shared `param_pages` knob grid.
- **[9W9](https://github.com/athousanddetails/schwung-9W9)** and
  **6W6** — the module architecture, pad gestures, per-lane mutes and the four
  distortion flavours, so all three kits feel the same under the hands.
- **[Movy](https://github.com/DimaDake/schwung-movy)** by DimaDake — the page
  model and config format.

## Disclaimer

Not affiliated with, approved or endorsed by Ableton or Roland. TR-808 is a
trademark of Roland Corporation, referenced only to describe behaviour.
