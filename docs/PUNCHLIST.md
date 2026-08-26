# 8W8 field punch list — 2026-08-26, user's hardware test

Worked **one instrument at a time**, in this order, each deployed and
verdict-ed before the next. References on the Desktop: `toms808.wav`
(low/mid/hi, default preset settings), `rim808.wav`, `808 clean/`,
`808default.aupreset` + the AU harness.

1. **TOMS — DONE (verdict: better; not 1:1, revisit last).**
   - No click. The reference blooms to its peak at 6-10 ms; the direct
     strike bleed (kTOM_ClickThru) was wrong and goes.
   - Tune must NOT let one tom reach its neighbour. The hardware TUNING
     trimmer range is narrow; measure it from the AU and match.
   - Retune/redecay to the reference: ~93.6 / 139.6 / 188.5 Hz settling
     ~92 / 135.5 / 183, decay to 1% = 0.386 / 0.255 / 0.246 s.
2. **CONGAS.** "Congas = toms" — not distinct enough. Flip the AU kit
   switch (channel instrument ids) to render REAL congas as reference.
   Same no-click rule applies.
3. **RIM SHOT.** Circuit version is WORSE than the old sc808 one.
   Reference: `rim808.wav`. Fix against it or default the lane back to
   sc808 until the circuit is right.
4. **COWBELL.** Worse than before. Fix against AU n56 (543+812 Hz,
   0.43 s) — the sc808 one was closer in character.
5. **HATS.** "Disgusting." Both CH and OH. References: 808 clean CH/OH
   samples + AU. The circuit ladder/VCA needs real work against them.
6. **SNARE.** User reports a Tone control that does nothing and was to
   be removed. sd_tone IS gone from the DSP — find what they are seeing
   (possibly the panel cache, possibly BD Tone) and kill it properly.
7. **KICK.** Good. Wants LONGER Decay available (extend the top of the
   pot's range).
8. **CYMBAL.** Not called out this round beyond the metal disgust —
   re-verdict after hats.

Standing rules: no kit-wide passes; deploy after each instrument;
`fit_trim` refits only when a voice's level actually changed and that is
flagged when it happens.
