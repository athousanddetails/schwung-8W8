#!/usr/bin/env python3
"""
fit_trim.py — solve for kVoiceTrim and the master headroom.

The kit's balance is FITTED, not chosen. This drives tools/kit_check in a loop:
kit_check measures each lane's loudest 20 ms and reports what trim would put it
on its target (`trim_suggest <lane> <value>`), this rewrites the table in
sc808_engine.cpp, rebuilds, and goes again until the worst lane is inside a
twentieth of a dB.

Run from the repo root:   python3 tools/fit_trim.py

It EDITS src/dsp/sc808_engine.cpp in place. Commit before running it if you
want to be able to see what it changed.

Two passes, because they interact: balance first with the overall level held
still, then level and balance together but damped. The drive stage compresses,
so a naive step under-corrects — hence the >1 exponents.

GPL-3.0.
"""
import re, subprocess, sys

ENG = 'src/dsp/sc808_engine.cpp'

# Read the lane order out of the engine rather than repeating it here. A trim
# table written in the wrong order silences nothing and fails no test — it just
# makes the kit quietly wrong — and a hand-kept copy of this list got rs and cl
# the wrong way round within a minute of being written.
def lane_ids():
    m = re.search(r'kVoiceIds\[SC808_NUM_VOICES\] = \{(.*?)\}', open(ENG).read(), re.S)
    assert m, "kVoiceIds not found in " + ENG
    return re.findall(r'"(\w+)"', m.group(1))

LANES = lane_ids()

COMMENT = {"bd": "the reference: everything else is set against the kick",
           "cp": "the quietest voice in sc808 by a long way",
           "ch": "raw peak near 17 before the drive stage catches it",
           "rs": "a click with a crest factor of 11"}

def write(t):
    assert len(t) == len(LANES), f"{len(t)} trims for {len(LANES)} lanes"
    s = open(ENG).read()
    rows = "\n".join(f'    {v:.4f}f,   /* {l}{" — " + COMMENT[l] if l in COMMENT else ""} */'
                     for l, v in zip(LANES, t))
    s, n = re.subn(r'(constexpr float kVoiceTrim\[SC808_NUM_VOICES\] = \{).*?(\n\};)',
                   lambda m: m.group(1) + "\n" + rows + m.group(2), s, flags=re.S)
    assert n == 1, "kVoiceTrim table not found in " + ENG
    open(ENG, 'w').write(s)

def run():
    subprocess.run(["clang++","-std=c++14","-O2","-Isrc/dsp","-o","build-native/kit_check",
                    "tools/kit_check.cpp","src/dsp/sc808_engine.cpp"], check=True)
    out = subprocess.run(["./build-native/kit_check"], capture_output=True, text=True).stdout
    sug, worst, scale = {}, None, 1.0
    for line in out.splitlines():
        p = line.split()
        if line.startswith("trim_suggest"):   sug[p[1]] = float(p[2])
        if line.startswith("worst lane"):     worst = float(p[-2])
        if line.startswith("headroom_scale"): scale = float(p[1])
    missing = [l for l in LANES if l not in sug]
    assert not missing, "kit_check reported no trim for: " + " ".join(missing)
    return [sug[l] for l in LANES], worst, scale, out

cur = [1.0] * len(LANES)

# Balance first: the shaper compresses, so a naive step under-corrects and
# over-relaxation is safe while the overall level is held still.
for it in range(40):
    write(cur); nxt, worst, scale, out = run()
    if worst < 0.02: break
    cur = [c * (n / c) ** 1.7 for c, n in zip(cur, nxt)]
print(f"balance converged in {it} iters: worst {worst:.3f} dB")

# Then level, damped — moving all trims together shifts the balance a little,
# so alternate the two rather than pushing both at full step.
for it in range(60):
    write(cur); nxt, worst, scale, out = run()
    if worst < 0.05 and abs(scale - 1.0) < 0.005: break
    cur = [c * ((n / c) ** 1.2) * (scale ** 0.6) for c, n in zip(cur, nxt)]
print(f"level converged in {it} iters: worst {worst:.3f} dB, scale {scale:.4f}")

write(cur); _, _, _, out = run(); print(out)
