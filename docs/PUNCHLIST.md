# 8W8 punch list — updated 2026-08-28

## Voice verdicts — ALL IN

Worked **one instrument at a time**, each deployed and verdict-ed before
the next, against the player's own Desktop references.

1. **TOMS** — "better; not 1:1". Revisit someday, low priority.
2. **CONGAS** — "Congas and Toms OK. i like them."
3. **RIM SHOT** — the sc808 algorithm IS the rim, after two circuit
   builds lost on hardware. The one transcription voice still in the
   audio path.
4. **HAND CLAP** — "fucking good."
5. **COWBELL** — the reverted version was the approved one.
6. **CLOSED HAT** — "CH is good now! well done!"
7. **OPEN HAT** — "OK NOW WE TALKING!"
8. **CYMBAL** — "ok it's good! maybe one day we improve it."
9. **SNARE / KICK** — no outstanding complaint. The kick's Decay top
   was queried and the player's answer was "leave it, it's good if it's
   8s", so 0.1–8.0 s stands.

`tools/golden_check` freezes all sixteen: every lane rendered through
the real engine at two velocities, compared bit for bit. Everything
below was done without moving a single sample of it.

## Structural work — DONE

- **Circuit is the engine.** Fifteen Engine enums and the master Metal
  enum are gone. sc808_voices.h stays for the null test, which reaches
  it through `src/tools/nullref.cpp` rather than the engine.
- **Seven distortion characters** — 9W9's curves under 8W8's own
  contract (linear 0..10, bit-exact bypass at 0). Makeup exponents
  measured, not guessed.
- **Velocity replaces Accent.** One line, no threshold; full velocity
  reproduces the old accented hit bit for bit. Split at the unaccented
  point so the eleven trigger-voltage lanes carry the bottom of the
  range as gain.
- **Main-page jog lock** and **voice-qualified LFO picker names**, with
  a generator assertion that no two picker entries collide.
- **Reverb + Delay sends** on every lane but the kick, their own pages,
  and the **one-knob Comp** on Master.

## Open

- **Toms 1:1** — the player's "maybe one day". Lowest priority.
- **Maracas / claves** default tuning: nothing reported since the
  retune; revisit only if the ear calls it.
- The null test and the native build steps SKIP without Docker on the
  Mac. They run on the VPS.

Standing rules: no kit-wide passes on VOICING; deploy after each
change; `fit_trim` refits only when a voice's level actually changed
and that is flagged when it happens.
