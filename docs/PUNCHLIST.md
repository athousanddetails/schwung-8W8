# 8W8 field punch list — updated 2026-08-28

Worked **one instrument at a time**, each deployed and verdict-ed before
the next. References on the Desktop: `toms808.wav`, `rim808.wav`,
`cb808.wav`, `808ch.wav`, `808oh.wav`, `808cy.wav`, `808 clean/`,
`808default.aupreset` + the AU harness.

## Voice verdicts — ALL IN

1. **TOMS — DONE** ("better; not 1:1" — revisit someday, low priority).
2. **CONGAS — DONE** ("Congas and Toms OK. i like them").
3. **RIM SHOT — DONE.** The sc808 algorithm IS the rim (player's call
   after two circuit builds lost on hardware). No Engine switch on the
   lane; tuned to rim808.wav (1788 Hz tock), Decay in real seconds.
4. **HAND CLAP — DONE** ("fucking good").
5. **COWBELL — DONE** (reverted version approved; `git revert 4e7af8f`).
6. **CLOSED HAT — DONE** ("CH is good now! well done!").
7. **OPEN HAT — DONE** ("OK NOW WE TALKING!").
8. **CYMBAL — DONE** ("ok it's good! maybe one day we improve it").
   Linear EG1 discharge, two-resonator shimmer (beating pair ~3300 Hz),
   BP1's own crash band, brushed floors. Commit f101329.
9. **SNARE — believed done.** sd_tone is gone from params and DSP; the
   "Tone does nothing" report predates the panel regeneration and was
   most likely the cached panel. Confirm once on hardware that no Tone
   control shows on SD.
10. **KICK — one open item.** "Make Decay longer if possible": bd_decay
    is still 0.1–8.0 s, unchanged since the first commit. Extend the top
    of the pot's range and verify the tail actually uses it.
11. **MARACAS / CLAVES — small default-tuning touchups** if the player
    ever calls them out; nothing reported since the retune.

## Structural work — next phase

- **Single-engine plan**: every lane's verdict is in — remove the 15
  `*_engine` switches (rim already has none) so circuit is THE engine.
  The sc808 transcription files STAY for the null test. The
  engines-agree checks in tom_check/loadtest need replacing, not
  deleting. Freed knob slot per lane.
- After the switch removal: refit trims if any lane's level path
  changed, full suite, deploy, hardware pass over the whole kit.

Standing rules: no kit-wide passes on VOICING; deploy after each
instrument; `fit_trim` refits only when a voice's level actually
changed and that is flagged when it happens.
