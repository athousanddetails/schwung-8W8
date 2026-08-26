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
3. **RIM SHOT — DONE.** Two circuit builds, both worse than sc808 on
   hardware. Resolution the player chose: the sc808 rim IS the rim, no
   Engine switch on that lane, tuned to `rim808.wav` (note 92 -> 91.62,
   putting the tock on the reference's 1788 Hz) and Decay in real
   seconds. The circuit rim code is deleted, not left dormant.
4. **HAND CLAP — deployed, awaiting verdict.** Envelope rebuilt from the
   schematic's two RCs: C144x82k = 38.5 ms main decay, C143x330k = 330 ms
   floor (they were conflated before). Teeth at 0/10/20 ms, main hit
   opening after the last tooth, matching the reference's 5 ms envelope
   table to within the stochastic floor. cp_check embeds that table as the
   oracle.
5. **COWBELL.** Worse than before. Fix against AU n56 (543+812 Hz,
   0.43 s) — the sc808 one was closer in character.
6. **HATS.** "Disgusting." Both CH and OH. References: 808 clean CH/OH
   samples + AU. The circuit ladder/VCA needs real work against them.
7. **SNARE.** User reports a Tone control that does nothing and was to
   be removed. sd_tone IS gone from the DSP — find what they are seeing
   (possibly the panel cache, possibly BD Tone) and kill it properly.
8. **KICK.** Good. Wants LONGER Decay available (extend the top of the
   pot's range).
9. **CYMBAL.** Not called out this round beyond the metal disgust —
   re-verdict after hats.

Standing rules: no kit-wide passes; deploy after each instrument;
`fit_trim` refits only when a voice's level actually changed and that is
flagged when it happens.
