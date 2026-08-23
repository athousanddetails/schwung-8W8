#!/usr/bin/env python3
"""Single source of truth for 8W8's parameter surface.

Emits src/dsp/sc808_params.h, which carries THREE things that must never
drift apart:

  * chain_params   — types/ranges/options the Shadow UI needs (JSON)
  * ui_pages       — the page hierarchy (JSON; served under "ui_pages" so
                     ui_chain.js loads it, see the comment in ui_chain.js)
  * the pot table  — key -> real engineering range + curve + default

and src/movy_config.json from the same dict.

9W9 kept the pot table hand-written in a second header and it was a standing
drift hazard; 6W6 fixed that with this generator and 8W8 inherits it. Adding a
control is one edit here.

module.json is capped at 8 KB by Schwung's loader, so the JSON payloads are
served dynamically from the DSP via get_param().
"""
import json, pathlib

LIN, EXP = 0, 1

# Every continuous control is a 0..127 pot, exactly like a hardware panel.
# Nobody dials an 808 in milliseconds. The DSP maps each pot to its real range
# with a musical curve, so the UI only ever shows a pot position.
#
# DEFAULTS ARE sc808's OWN ARGUMENTS, converted to pot positions. Pot centre
# is NOT the default here and is not meant to be: the default is "the sound
# sc808 makes", which is the thing 8W8's null test verifies. The one
# exception is called out at the high conga.


def P(key, label, lo, hi, curve, default):
    return dict(kind="pot", key=key, name=label, min=lo, max=hi,
                curve=curve, default=default)


def E(key, label, options, default=0):
    return dict(kind="enum", key=key, name=label, options=options,
                default=default)


def pot_for(value, lo, hi, curve):
    """The pot position whose mapped value is `value`.

    Defaults are written as the ENGINEERING value sc808 declares — decay=4.2
    seconds, click=0.11 — and converted here. Writing pot numbers by hand and
    hoping they land on the right seconds is how a kit drifts away from the
    thing it is supposed to reproduce.
    """
    import math
    value = max(lo, min(hi, value))
    if curve == EXP and lo > 0:
        t = math.log(value / lo) / math.log(hi / lo)
    else:
        t = (value - lo) / (hi - lo)
    return int(round(t * 127))


def PV(key, label, lo, hi, curve, value):
    """Like P, but the default is given as an engineering value."""
    return P(key, label, lo, hi, curve, pot_for(value, lo, hi, curve))


# Post-voice drive stage. The 808's own nonlinearities live inside the voices
# where the circuit puts them; this is the panel's Drive/Distortion, which the
# hardware never had. Same four flavours as 9W9 and 6W6 so a player who knows
# what Fold at 90 does on the 606 knows what it does here.
#
# Option text is sized for the stock grid's enum box: TWO LINES OF THREE
# CHARACTERS (font5x3.mjs enumSquareLines). These read DIO/DE, CLI/P, FOL/D,
# CRU/SH.
DIST = ["Diode", "Clip", "Fold", "Crush"]


def DRIVE(v):  return P(f"{v}_drive", "Drive", 0.2, 8.0, EXP, 55)   # pot 55 == 1.0
def DTYPE(v):  return E(f"{v}_dist_type", "Distortion", DIST)
def LEVEL(v):  return P(f"{v}_level", "Level", 0.0, 2.0, LIN, 64)   # pot 64 == 1.0

# Pitch as a SEMITONE OFFSET around the voice's own base note, so pot 64 is
# always "as sc808 has it" whatever that voice is tuned to. +/- an octave.
def TUNE(v):   return P(f"{v}_tune", "Tune", -12.0, 12.0, LIN, 64)

# The metal voices have no note — they are six fixed oscillators — so their
# Tune is a RATIO on the whole bank, unity at pot centre.
def RATIO(v):  return P(f"{v}_tune", "Tune", 0.5, 2.0, EXP, 64)


# ---- Pages. One per voice, in TR-808 front-panel order. --------------------
#
# 15 voices and a Master, which fills Move's left 4x4 pad block exactly. That
# is a better fit than 6W6 (8 voices, half the block) or 9W9 (11), and it is
# why the roster is what it is.
#
# sc808 ships SIXTEEN SynthDefs; rim shot and claves share one lane here with
# a mode switch, because they share one CHANNEL on the hardware — the 808's
# front panel has a single RS/CL selector and you cannot have both in a
# pattern. Merging the pair the machine itself merges costs nothing and buys
# the pad that Master needs.
PAGES = [
    ("bd", "Bass Drum", [
        # note 34, click 0.11, decay 2 s. `punch` is fixed at 2 in the
        # SynthDef and exposed here as Tone — it is the level of the second,
        # faster sine through the 350 Hz highpass, i.e. the beater.
        # A SEMITONE OFFSET, like every other Tune pot — the engine adds it
        # to the lane's base note. Declaring it as an absolute MIDI note
        # instead put the kick at midicps(34+34) = 415 Hz.
        TUNE("bd"),
        PV("bd_decay",  "Decay",   0.1,  8.0, EXP,  2.0),
        # The one pot whose default is NOT sc808's argument, and deliberately.
        # To the circuit engine this is the attack-time frequency and Q jump,
        # which on the hardware is not a control at all — it simply happens,
        # every hit. Since Circuit is the default engine, the default pot is
        # what the circuit does. The sc808 engine reads the same pot as its
        # `click` pre-level, whose own default would be pot 14.
        P("bd_attack", "Attack", 0.0, 1.0, LIN, 100),
        PV("bd_tone",   "Tone",    0.0,  6.0, LIN,  2.0),
        # Which kick. "Circuit" is the bridged-T model from Werner et al.'s
        # analysis of the real 808 — a resonator in an op-amp feedback loop,
        # where Decay is LOOP GAIN and the pitch sighs because the circuit
        # makes it. "sc808" is the transcription the null test verifies: a
        # sine on a pitch envelope, which is a fine kick drum and is not an
        # 808's. Circuit is the default because the kick is the voice this
        # whole project is judged on.
        #
        # Decay, Attack and Tone mean different things to the two engines and
        # each maps the same pot POSITION its own way — see sc808_engine.cpp.
        E("bd_engine", "Engine", ["Circ", "sc808"]),
        DRIVE("bd"), DTYPE("bd"), LEVEL("bd"),
    ]),
    ("sd", "Snare", [
        # note 65, detune -11, mix 0.7, lpf 121, decay 4.2 s.
        # Snappy is sc808's `mix`: 0 is all shell, 1 is all wires.
        TUNE("sd"),   # semitone offset, see bd_tune
        PV("sd_decay",  "Decay",   0.1,  8.0, EXP,  4.2),
        PV("sd_snappy", "Snappy",  0.0,  1.0, LIN,  0.7),
        PV("sd_tone",   "Tone",  109.0,133.0, LIN,121.0),   # noise lowpass, MIDI
        DRIVE("sd"), DTYPE("sd"), LEVEL("sd"),
    ]),
    ("lt", "Low Tom",   [TUNE("lt"), PV("lt_decay", "Decay", 1.0, 40.0, EXP, 20.0),
                         DRIVE("lt"), DTYPE("lt"), LEVEL("lt")]),
    ("mt", "Mid Tom",   [TUNE("mt"), PV("mt_decay", "Decay", 1.0, 40.0, EXP, 16.0),
                         DRIVE("mt"), DTYPE("mt"), LEVEL("mt")]),
    ("ht", "Hi Tom",    [TUNE("ht"), PV("ht_decay", "Decay", 1.0, 40.0, EXP, 11.0),
                         DRIVE("ht"), DTYPE("ht"), LEVEL("ht")]),
    ("lc", "Low Conga", [TUNE("lc"), PV("lc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         DRIVE("lc"), DTYPE("lc"), LEVEL("lc")]),
    ("mc", "Mid Conga", [TUNE("mc"), PV("mc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         DRIVE("mc"), DTYPE("mc"), LEVEL("mc")]),
    ("hc", "Hi Conga",  [TUNE("hc"), PV("hc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         DRIVE("hc"), DTYPE("hc"), LEVEL("hc")]),
    ("rs", "Rim / Clave", [
        # sc808: rim shot decay 0.07 s, claves 0.1 s. One lane, one switch,
        # as on the panel.
        TUNE("rs"),
        PV("rs_decay", "Decay", 0.01, 0.5, EXP, 0.07),
        E("rs_mode", "Mode", ["Rim", "Clave"]),          # RIM / CLA in the box
        DRIVE("rs"), DTYPE("rs"), LEVEL("rs"),
    ]),
    ("ma", "Maracas", [
        # hpf 113, click 0.027, decay 0.07. `click` here is a real DURATION,
        # not a level — the 27 ms ramp from 0.3 up to 1 that is the rattle.
        TUNE("ma"),
        PV("ma_decay",  "Decay",  0.01, 0.5, EXP, 0.07),
        PV("ma_attack", "Attack", 0.0,  0.1, LIN, 0.027),
        DRIVE("ma"), DTYPE("ma"), LEVEL("ma"),
    ]),
    ("cp", "Hand Clap", [
        # hpf 71, lpf 84, click 0.5, decay 0.3, rev 1. Spread is sc808's
        # fixed 26 ms gap before the second burst.
        TUNE("cp"),
        PV("cp_decay",  "Decay",  0.05, 1.5,  EXP, 0.3),
        PV("cp_spread", "Spread", 0.005, 0.1, EXP, 0.026),
        PV("cp_room",   "Room",   0.0,  2.0,  LIN, 1.0),
        DRIVE("cp"), DTYPE("cp"), LEVEL("cp"),
    ]),
    ("cb", "Cowbell", [
        # Two oscillators at 811.4 and 538.7 Hz — numbers 5 and 6 of the metal
        # bank, exactly as the hardware wires them — so Tune is a ratio.
        RATIO("cb"),
        PV("cb_decay", "Decay", 0.5, 20.0, EXP, 9.5),
        DRIVE("cb"), DTYPE("cb"), LEVEL("cb"),
    ]),
    ("ch", "Closed Hat", [
        RATIO("ch"),
        PV("ch_decay", "Decay", 0.02, 1.5, EXP, 0.42),
        DRIVE("ch"), DTYPE("ch"), LEVEL("ch"),
        # Lives here as well as on Master: this is where you are standing when
        # you want it. The 808 shares one metal source between CH and OH, so
        # closed cutting open is the hardware behaviour; here it is a switch.
        E("hh_choke", "Choke", ["Off", "CH>OH", "Mutual"], 1),   # CH>/OH
    ]),
    ("oh", "Open Hat", [
        RATIO("oh"),
        PV("oh_decay", "Decay", 0.05, 4.0, EXP, 0.5),
        DRIVE("oh"), DTYPE("oh"), LEVEL("oh"),
    ]),
    ("cy", "Cymbal", [
        RATIO("cy"),
        PV("cy_decay", "Decay", 0.2, 10.0, EXP, 2.0),
        # sc808 multiplies `tone` by 0.008 before use, so this is really "how
        # much low band" and even at maximum it is a whisper next to the
        # 7 kHz band. That is correct: an 808 cymbal is mostly 7 kHz.
        PV("cy_tone",  "Tone",  0.0, 1.0, LIN, 0.25),
        DRIVE("cy"), DTYPE("cy"), LEVEL("cy"),
    ]),
]

GLOBALS = [
    E("master_dist", "Master Dist", ["Off"] + DIST),
    P("master_drive", "Master Drive", 0.2, 8.0, EXP, 55),
    # The absolute level lives in the per-lane trims (see kit_check), not
    # here, so Volume sits high with room in both directions rather than
    # being the thing that stops the kit clipping.
    P("volume", "Volume", 0.0, 1.0, LIN, 100),
    P("accent", "Accent", 1.0, 4.0, LIN, 42),               # 2.0x on accents
    E("hh_choke", "Choke", ["Off", "CH>OH", "Mutual"], 1),
    E("note_map", "Note Map", ["Rack 36", "GM"]),            # RAC/36 in the box
]

# ---------------------------------------------------------------------------


def viz_for(p):
    """Honest viz declarations for the 0.12.x param-pages renderer.

    Levels draw as faders. The bass drum's "Attack" is a CLICK LEVEL, not an
    envelope time — declare viz:false so the detector cannot pair it with
    Decay into a fake AD envelope. 9W9 learned this the hard way. The
    maracas' Attack genuinely IS a time, so it is left alone.
    """
    k = p["key"]
    if k.endswith("_level") or k == "volume":
        return {"kind": "fader"}
    if k == "bd_attack":
        return False
    return None


def chain_param(p):
    if p["kind"] == "enum":
        d = {"key": p["key"], "name": p["name"], "type": "enum",
             "options": p["options"]}
    else:
        d = {"key": p["key"], "name": p["name"], "type": "int",
             "min": 0, "max": 127}
    # The sc808 default, so a reset gesture (stock Mute+knob, Movy, the web
    # panel's double-click) lands on the verified sound and not on a guessed 64.
    d["default"] = p["default"]
    v = viz_for(p)
    if v is not None:
        d["viz"] = v
    return d


cp, levels, root, pots, enums, seen = [], {}, [], [], [], set()


def register(p):
    if p["key"] in seen:
        return
    seen.add(p["key"])
    cp.append(chain_param(p))
    (pots if p["kind"] == "pot" else enums).append(p)


for pid, label, params in PAGES:
    for p in params:
        register(p)
    if len(params) > 8:
        raise SystemExit(f"page {pid} has {len(params)} params — max 8 knobs")
    levels[pid] = {"name": label,
                   "knobs": [p["key"] for p in params],
                   "params": [{"key": p["key"], "name": p["name"]}
                              for p in params]}
    root.append({"level": pid, "label": label})

for p in GLOBALS:
    register(p)
root += [{"key": p["key"], "name": p["name"]} for p in GLOBALS]
levels["root"] = {"name": "8W8",
                  "knobs": [p["key"] for p in GLOBALS[:4]],
                  "params": root}

# Two plugin-level keys that live on NO page but must be in chain_params: the
# remote-UI manager seeds and periodically re-reads exactly the keys listed
# here, and a key it does not know about never reaches the browser. ui_focus
# is the lane the on-device editor is showing (0-14, 15 = master); mutes is
# the per-lane mute mask, which needs 15 bits.
cp.append({"key": "ui_focus", "name": "Focus", "type": "int",
           "min": 0, "max": 15, "default": 0})
cp.append({"key": "mutes", "name": "Mutes", "type": "int",
           "min": 0, "max": 32767, "default": 0})

cpj = json.dumps(cp, separators=(",", ":"))
uhj = json.dumps({"levels": levels}, separators=(",", ":"))


def cstr(s):
    q, b = chr(34), chr(92)
    return "\n".join(
        f'    "{s[k:k+100].replace(b, b*2).replace(q, b+q)}"'
        for k in range(0, len(s), 100))


pot_rows = "\n".join(
    f'    {{ "{p["key"]}", {p["min"]:>10.4f}f, {p["max"]:>10.4f}f, '
    f'{"SC808_EXP" if p["curve"] == EXP else "SC808_LIN"}, {p["default"]:>3} }},'
    for p in pots)
enum_rows = "\n".join(
    f'    {{ "{p["key"]}", {len(p["options"]):>2}, {p["default"]:>2} }},'
    for p in enums)

root_dir = pathlib.Path(__file__).resolve().parent.parent
(root_dir / "src/dsp/sc808_params.h").write_text(f"""\
/* Generated by scripts/gen_params.py — DO NOT EDIT BY HAND.
 *
 * module.json is capped at 8 KB by Schwung's loader, so chain_params and the
 * page hierarchy are served dynamically from the DSP via get_param().
 *
 * The pot and enum tables below define storage order for the state blob.
 * Appending is safe; reordering breaks every saved patch.
 */
#ifndef SC808_PARAMS_H
#define SC808_PARAMS_H

typedef enum {{ SC808_LIN = 0, SC808_EXP = 1 }} sc808_curve_t;

typedef struct {{
    const char    *key;
    float          min;
    float          max;
    sc808_curve_t  curve;
    int            def;      /* default POT position, 0..127 */
}} sc808_pot_t;

typedef struct {{
    const char *key;
    int         count;      /* number of options */
    int         def;
}} sc808_enum_t;

#define SC808_NUM_POTS  {len(pots)}
#define SC808_NUM_ENUMS {len(enums)}

static const sc808_pot_t g_sc808_pots[SC808_NUM_POTS] = {{
{pot_rows}
}};

static const sc808_enum_t g_sc808_enums[SC808_NUM_ENUMS] = {{
{enum_rows}
}};

#define SC808_CHAIN_PARAMS_LEN {len(cpj)}
static const char sc808_chain_params_json[] =
{cstr(cpj)};

#define SC808_UI_PAGES_LEN {len(uhj)}
static const char sc808_ui_pages_json[] =
{cstr(uhj)};

#endif /* SC808_PARAMS_H */
""")

# ---- movy_config.json: same source, Movy's shape. --------------------------
# HARD RULE (cost a debugging session on Tablor): a Movy bank is EXACTLY ONE
# PAGE. buildConfigPages keys bankGroups per BANK but the UI indexes per PAGE,
# so a multi-row bank shifts every following page's label. One row per bank.
SHORT = {"Tune": "TUNE", "Decay": "DECAY", "Attack": "ATTK", "Tone": "TONE",
         "Drive": "DRIVE", "Distortion": "DIST", "Level": "LEVEL",
         "Snappy": "SNAPY", "Mode": "MODE", "Spread": "SPRD", "Room": "ROOM",
         "Choke": "CHOKE", "Engine": "ENGIN",
         "Master Dist": "MDIST", "Master Drive": "MDRV",
         "Volume": "VOL", "Accent": "ACNT", "Note Map": "NMAP"}
MOVY_NAME = {"bd": "Kick", "sd": "Snare", "lt": "Lo Tom", "mt": "Mid Tom",
             "ht": "Hi Tom", "lc": "Lo Cnga", "mc": "Md Cnga", "hc": "Hi Cnga",
             "rs": "Rim/Cl", "ma": "Maracas", "cp": "Clap", "cb": "Cowbell",
             "ch": "Cl Hat", "oh": "Op Hat", "cy": "Cymbal"}


def movy_slot(p):
    d = {"key": p["key"], "short": SHORT[p["name"]], "full": p["name"]}
    if p["kind"] == "enum":
        d["type"] = "enum"
        d["options"] = list(p["options"])   # already sized for a 32 px cell
    else:
        d["type"] = "int"; d["min"] = 0; d["max"] = 127
    return d


banks = []
for pid, label, params in PAGES:
    row = [movy_slot(p) for p in params] + [None] * (8 - len(params))
    banks.append({"name": MOVY_NAME[pid], "rows": [row]})
banks.append({"name": "Master", "global": True,
              "rows": [[movy_slot(p) for p in GLOBALS] + [None] * (8 - len(GLOBALS))]})
movy = {"id": "8w8", "name": "8W8",
        "drum": {"padCount": 16, "padNoteStart": 36, "rawMidi": False},
        "banks": banks}
(root_dir / "src/movy_config.json").write_text(json.dumps(movy, indent=2) + "\n")

print(f"chain_params {len(cpj)}B  ui_pages {len(uhj)}B  movy banks={len(banks)}  "
      f"pages={len(levels)}  pots={len(pots)}  enums={len(enums)}  "
      f"params={len(cp)}")
