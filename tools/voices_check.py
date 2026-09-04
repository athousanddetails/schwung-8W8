#!/usr/bin/env python3
"""voices_check.py — Schwung's voices contract, asserted against the plugin.

WHY THIS EXISTS. The contract is INERT until a device runs Schwung 0.13: the
declaration is read by shared/param_pages/voices.mjs, which today's host does
not have. So nothing on hardware will tell you it is wrong — not a crash, not
a log line, not a pad in the wrong place. 9W9 shipped a focus table naming two
levels its generator never emitted and four of eleven voices silently never
followed the pad, for months.

This reads the two hierarchy blobs straight out of the GENERATED header and
checks them against src/dsp/sc808_plugin.cpp's own note routers, which stay the
authority. A map that drifts from the router fails here.

    python3 tools/voices_check.py

GPL-3.0.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HDR = (ROOT / "src/dsp/sc808_params.h").read_text()
PLUG = (ROOT / "src/dsp/sc808_plugin.cpp").read_text()
ENG = (ROOT / "src/dsp/sc808_engine.cpp").read_text()

fails = []


def check(ok, what, detail=""):
    print(f"{'ok  ' if ok else 'FAIL'}: {what}{' — ' + detail if detail else ''}")
    if not ok:
        fails.append(what)


def blob(sym):
    """The C string literal `sym` in the generated header, back as a value."""
    m = re.search(re.escape(sym) + r"\[\]\s*=\s*(.*?);", HDR, re.S)
    if not m:
        sys.exit(f"{sym} not found in sc808_params.h")
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    return json.loads("".join(parts).replace('\\"', '"').replace("\\\\", "\\"))


# ---- the orders the plugin and engine actually use -------------------------

VOICE_IDS = re.search(r"kVoiceIds\[SC808_NUM_VOICES\]\s*=\s*\{(.*?)\}", ENG, re.S)
VOICE_IDS = re.findall(r'"(\w+)"', VOICE_IDS.group(1))

LEVEL_OF = re.search(r"kLevelOf\[SC808_NUM_VOICES\]\s*=\s*\{(.*?)\}", PLUG, re.S)
LEVEL_OF = re.findall(r'"(\w+)"', LEVEL_OF.group(1))

# The GM switch, as the router actually spells it: a run of `case N:` labels
# followed by `return SC808_XX;`. First case of a run is the canonical note.
GM = {}
for cases, voice in re.findall(r"((?:case\s+\d+:\s*)+)return\s+SC808_(\w+);", PLUG):
    ns = [int(n) for n in re.findall(r"case\s+(\d+):", cases)]
    GM.setdefault(voice.lower(), []).extend(ns)

DRUMRACK_BASE = int(re.search(r"_note\s*>=\s*(\d+)\s*&&\s*_note\s*<=\s*\d+\)\s*"
                              r"return\s*\(int\)\(_note\s*-\s*\d+\)", PLUG).group(1))

NOT_VOICES = {"rev", "dly", "root"}

print("Schwung voices contract — 8W8\n")

# ---- 0. the table that has already cost 9W9 four voices --------------------
drum = blob("sc808_ui_pages_json")
levels = drum["levels"]

check(LEVEL_OF == VOICE_IDS,
      "kLevelOf is the engine's own voice ids, in trigger order",
      f"{len(LEVEL_OF)} entries")
missing = [i for i in LEVEL_OF if i not in levels]
check(not missing,
      "every kLevelOf id resolves against a declared level",
      "all 16 resolve" if not missing else f"missing: {missing}")

# ---- the two maps ----------------------------------------------------------
for name, sym in (("drum rack", "sc808_ui_pages_json"),
                  ("GM", "sc808_ui_pages_gm_json")):
    print(f"\n  {name}")
    h = blob(sym)
    lv = h["levels"]

    check(h.get("pad_layout") == "drums", f"[{name}] pad_layout is \"drums\"",
          repr(h.get("pad_layout")))
    check(h.get("focus_param") == "ui_focus_level",
          f"[{name}] focus_param names the key the plugin publishes",
          repr(h.get("focus_param")))

    voiced = {i: e["note"] for i, e in lv.items() if "note" in e}
    check(sorted(voiced) == sorted(VOICE_IDS),
          f"[{name}] exactly the engine's voices carry a note",
          f"{len(voiced)} voices")

    silent = [i for i in NOT_VOICES if i in lv and "note" in lv[i]]
    check(not silent,
          f"[{name}] no note on a page that makes no sound",
          "rev, dly and root are pages, not voices"
          if not silent else f"note on {silent}")

    dupes = {n for n in voiced.values() if list(voiced.values()).count(n) > 1}
    check(not dupes, f"[{name}] no two voices claim the same note",
          "" if not dupes else f"duplicated: {sorted(dupes)}")

    roles = [i for i in voiced if "role" not in lv[i]]
    check(not roles, f"[{name}] every voice carries a role hint",
          "" if not roles else f"missing on {roles}")

# ---- drum rack: must BE the router, not merely resemble it -----------------
print("\n  against the plugin's routers")
want = {vid: DRUMRACK_BASE + i for i, vid in enumerate(VOICE_IDS)}
got = {i: e["note"] for i, e in blob("sc808_ui_pages_json")["levels"].items()
       if "note" in e}
off = sorted(set(want) | set(got))
off = [k for k in off if got.get(k) != want.get(k)]
check(got == want,
      f"drum-rack notes are drumrack_to_voice()'s own: {DRUMRACK_BASE}+index",
      f"{min(want.values())}..{max(want.values())}" if got == want else
      "; ".join(f"{k}: declared {got.get(k)}, router {want.get(k)}" for k in off))

gmdecl = {i: e["note"] for i, e in blob("sc808_ui_pages_gm_json")["levels"].items()
          if "note" in e}
wrong = [f"{v}={n} (router: {GM.get(v)})"
         for v, n in gmdecl.items() if n not in GM.get(v, [])]
check(not wrong, "every declared GM note routes to that voice in note_to_voice()",
      "" if not wrong else "; ".join(wrong))

# The anchors anyone reading a GM map checks first. 8W8 has no ride.
ANCHORS = {"bd": 36, "sd": 38, "ch": 42, "oh": 46, "cy": 49}
bad = {v: (gmdecl.get(v), n) for v, n in ANCHORS.items() if gmdecl.get(v) != n}
check(not bad, "the GM anchors are real GM",
      "36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash"
      if not bad else str(bad))

print("\n" + ("ALL PASS" if not fails else f"FAILED ({len(fails)})"))
sys.exit(1 if fails else 0)
