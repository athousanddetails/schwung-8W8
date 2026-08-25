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
# SIXTEEN voices, which is exactly what Move's left 4x4 pad block holds — one
# per sc808 SynthDef. 6W6 uses half the block and 9W9 eleven pads.
#
# The rim shot and the claves get a pad each even though the hardware runs
# them off one channel behind an RS/CL selector. The cost is the Master pad:
# with sixteen drums there is no sixteenth pad left for it, so Master is
# reached by the jog like every other page.
PAGES = [
    ("bd", "Bass Drum", [
        # note 34, click 0.11, decay 2 s. `punch` is fixed at 2 in the
        # SynthDef and exposed here as Tone — it is the level of the second,
        # faster sine through the 350 Hz highpass, i.e. the beater.
        # A SEMITONE OFFSET, like every other Tune pot — the engine adds it
        # to the lane's base note. Declaring it as an absolute MIDI note
        # instead put the kick at midicps(34+34) = 415 Hz.
        TUNE("bd"),
        # Attack BEFORE Decay, and that ordering is load-bearing: the knob grid
        # spans one AD envelope graphic across an adjacent attack/decay pair,
        # and it reads the row left to right. Decay-then-attack draws the graph
        # squashed onto a single slot with an empty knob beside it. Guarded by
        # test/ui_chain.test.mjs.
        #
        # The one pot whose default is NOT sc808's argument, and deliberately.
        # To the circuit engine this is the attack-time frequency and Q jump,
        # which on the hardware is not a control at all — it simply happens,
        # every hit. Since Circuit is the default engine, the default pot is
        # what the circuit does. The sc808 engine reads the same pot as its
        # `click` pre-level, whose own default would be pot 14.
        P("bd_attack", "Attack", 0.0, 1.0, LIN, 100),
        PV("bd_decay",  "Decay",   0.1,  8.0, EXP,  2.0),
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
        # Which snare. "Circ" is two bridged-T networks at 173 and 336 Hz,
        # derived from the service notes' component values, with a swing-type
        # VCA on the noise — so Tone balances two ringing shells rather than
        # moving a filter, and Snappy divides the trigger into the noise
        # envelope rather than mixing a level. "sc808" is the transcription.
        E("sd_engine", "Engine", ["Circ", "sc808"]),
        DRIVE("sd"), DTYPE("sd"), LEVEL("sd"),
    ]),
    # The six tom / conga lanes. Each gets an Engine switch of its own, like
    # the kick and the snare — on the hardware there is ONE tom/conga switch
    # for the whole machine, but there tom and conga are the same channel and
    # cannot sound together, while here they are six pads that can.
    #
    # Decay reads as SECONDS on sc808 and as LOOP GAIN on the circuit, from
    # the same knob position; see the circuit kick for why that is deliberate.
    ("lt", "Low Tom",   [TUNE("lt"), PV("lt_decay", "Decay", 1.0, 40.0, EXP, 20.0),
                         E("lt_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("lt"), DTYPE("lt"), LEVEL("lt")]),
    ("mt", "Mid Tom",   [TUNE("mt"), PV("mt_decay", "Decay", 1.0, 40.0, EXP, 16.0),
                         E("mt_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("mt"), DTYPE("mt"), LEVEL("mt")]),
    ("ht", "Hi Tom",    [TUNE("ht"), PV("ht_decay", "Decay", 1.0, 40.0, EXP, 11.0),
                         E("ht_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("ht"), DTYPE("ht"), LEVEL("ht")]),
    ("lc", "Low Conga", [TUNE("lc"), PV("lc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         E("lc_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("lc"), DTYPE("lc"), LEVEL("lc")]),
    ("mc", "Mid Conga", [TUNE("mc"), PV("mc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         E("mc_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("mc"), DTYPE("mc"), LEVEL("mc")]),
    ("hc", "Hi Conga",  [TUNE("hc"), PV("hc_decay", "Decay", 1.0, 40.0, EXP, 18.0),
                         E("hc_engine", "Engine", ["Circ", "sc808"]),
                         DRIVE("hc"), DTYPE("hc"), LEVEL("hc")]),
    ("rs", "Rim Shot", [
        TUNE("rs"),
        PV("rs_decay", "Decay", 0.01, 0.5, EXP, 0.07),
        DRIVE("rs"), DTYPE("rs"), LEVEL("rs"),
    ]),
    ("cl", "Claves", [
        # A pad of their own. On the hardware the claves share the rim shot's
        # channel behind an RS/CL selector, and 8W8 ran them as one lane with
        # a Mode switch for that reason — but sc808 ships them as two
        # SynthDefs, wanting both in one pattern is normal, and sixteen voices
        # is exactly what the pad block holds.
        TUNE("cl"),
        PV("cl_decay", "Decay", 0.01, 0.5, EXP, 0.10),
        DRIVE("cl"), DTYPE("cl"), LEVEL("cl"),
    ]),
    ("ma", "Maracas", [
        # hpf 113, click 0.027, decay 0.07. `click` here is a real DURATION,
        # not a level — the 27 ms ramp from 0.3 up to 1 that is the rattle —
        # so unlike the kick's Attack it is a genuine envelope time.
        #
        # ATTACK BEFORE DECAY, and that ordering is the whole of it: the
        # stock grid detects an adjacent attack/decay pair and draws one AD
        # envelope spanning both cells. It only reads left to right, so with
        # Decay first the graph came out backwards and lopsided, sitting over
        # Decay with Attack blank beside it.
        TUNE("ma"),
        PV("ma_attack", "Attack", 0.0,  0.1, LIN, 0.027),
        PV("ma_decay",  "Decay",  0.01, 0.5, EXP, 0.07),
        DRIVE("ma"), DTYPE("ma"), LEVEL("ma"),
    ]),
    ("cp", "Hand Clap", [
        # sc808's args: hpf 71, lpf 84, click 0.5, decay 0.3, rev 1.
        #
        # The defaults below are the CIRCUIT's, because Circuit is the default
        # engine — the same rule the kick and the snare follow.
        #
        # Decay 0.33 is R362 x C143 off the schematic, so the knob at its
        # default IS the hardware's tail. Spread 0.010 is the burst spacing;
        # its old 5..100 ms range is what made it "odd", because 100 ms of gap
        # between two bursts is a flam and not a clap. 3..30 ms is a clap that
        # is tight at one end and a roomful of people at the other.
        TUNE("cp"),
        PV("cp_decay",  "Decay",  0.05,  1.5,  EXP, 0.33),
        PV("cp_spread", "Spread", 0.003, 0.03, EXP, 0.010),
        PV("cp_room",   "Room",   0.0,   2.0,  LIN, 1.0),
        E("cp_engine", "Engine", ["Circ", "sc808"]),
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
    # How the hats' and cymbal's six oscillators behave between hits.
    #
    # "Free" is the hardware: the HD14584's oscillators never stop, the
    # envelopes gate them, and every hit catches the bank at a different
    # phase — which is why no two 808 hats are quite the same. "Retrig" is
    # sc808, which restarts them every note because in SuperCollider every
    # note is a new synth, and which is what the null test verifies.
    E("metal_run", "Metal", ["Free", "Retrig"]),             # FRE/E, RET/RIG
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
           "min": 0, "max": 16, "default": 0})       # 16 == Master
cp.append({"key": "mutes", "name": "Mutes", "type": "int",
           "min": 0, "max": 65535, "default": 0})    # sixteen lanes, sixteen bits

cpj = json.dumps(cp, separators=(",", ":"))
uhj = json.dumps({"levels": levels}, separators=(",", ":"))

# ---- the host's parameter channel ------------------------------------------
#
# module.json is capped at 8 KB by Schwung's loader, which is why these two
# payloads are served from the DSP instead. The DSP's ceiling is different and
# much higher: shadow_constants.h sets
#
#     #define SHADOW_PARAM_VALUE_LEN 65536  /* 64KB for large ui_hierarchy and state */
#
# and get_param() in sc808_plugin.cpp REFUSES to truncate — it returns -1
# rather than hand back half a JSON document. So overrunning this does not
# corrupt anything; it makes the entire parameter surface vanish, and the
# editor comes up with nothing on it. That is a bad failure to discover on a
# device, so it is caught here instead.
#
# The guard is at half the ceiling. 8W8 uses about 8 KB with 94 parameters
# over 16 pages, so there is room for roughly another hundred before this is
# a real constraint.
HOST_PARAM_MAX = 65536
for name, payload in (("chain_params", cpj), ("ui_pages", uhj)):
    if len(payload) > HOST_PARAM_MAX // 2:
        raise SystemExit(
            f"{name} is {len(payload)} B — over half the host's "
            f"{HOST_PARAM_MAX} B parameter buffer (SHADOW_PARAM_VALUE_LEN). "
            f"get_param refuses to truncate, so the editor would come up empty.")


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
         "Snappy": "SNAPY", "Spread": "SPRD", "Room": "ROOM",
         "Choke": "CHOKE", "Engine": "ENGIN", "Metal": "METAL",
         "Master Dist": "MDIST", "Master Drive": "MDRV",
         "Volume": "VOL", "Accent": "ACNT", "Note Map": "NMAP"}
MOVY_NAME = {"bd": "Kick", "sd": "Snare", "lt": "Lo Tom", "mt": "Mid Tom",
             "ht": "Hi Tom", "lc": "Lo Cnga", "mc": "Md Cnga", "hc": "Hi Cnga",
             "rs": "Rim", "cl": "Claves", "ma": "Maracas", "cp": "Clap",
             "cb": "Cowbell",
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

print(f"chain_params {len(cpj)}B  ui_pages {len(uhj)}B  "
      f"(host buffer {HOST_PARAM_MAX}B)  movy banks={len(banks)}  "
      f"pages={len(levels)}  pots={len(pots)}  enums={len(enums)}  "
      f"params={len(cp)}")
