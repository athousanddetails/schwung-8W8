#!/usr/bin/env bash
#
# The null test: prove Engine A is a transcription of sc808 and not an
# impression of it.
#
# Renders every voice twice — once by scsynth from the vendored SynthDefs,
# once by src/dsp/sc808_voices.h — and subtracts them. A voice that nulls at
# -90 dB is the same signal; a voice that nulls at -25 dB is a different voice
# that happens to sound similar, which is exactly the failure this project
# exists to avoid.
#
#   ./test/nulltest.sh            voices only
#   ./test/nulltest.sh --probes   also one-UGen-at-a-time probes
#
# Runs on the build host, not the Mac: it needs Docker (for SuperCollider) and
# a C++ compiler. See scripts/build.sh for the same reasoning.
#
# WHY DOCKER FOR SUPERCOLLIDER. The reference is pinned to SC 3.11.2, and that
# is not cosmetic — 3.11's EnvGen computes its curve rate over the INTEGER
# control-period count, while the development branch reworked it to use a
# fractional duration with a residual carried between segments. The two
# produce audibly identical envelopes and numerically different ones, and
# sc_ugens.h implements 3.11.2. Moving the reference to a newer SC without
# updating Env::armSegment() will show up here as the envelopes and the swept
# voices falling from -150 dB to about -40 dB.
#
# GPL-3.0.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
IMAGE="sc808-sc"
PROBES=0
[ "${1:-}" = "--probes" ] && PROBES=1

cd "$ROOT"
mkdir -p sc/defs sc/out sc/mine sc/ctrl sc/pdefs sc/pout sc/pmine

# SuperCollider, headless. Built once and cached.
docker image inspect "$IMAGE" >/dev/null 2>&1 || \
    docker build -t "$IMAGE" -f scripts/Dockerfile.sc .

# sclang wants a Qt platform and refuses to run its WebEngine sandboxed as
# root; offscreen + a non-root uid is the whole of it.
sc() {
    docker run --rm \
        -e QT_QPA_PLATFORM=offscreen -e QTWEBENGINE_DISABLE_SANDBOX=1 \
        -e HOME=/tmp -e XDG_RUNTIME_DIR=/tmp \
        -u "$(id -u):$(id -g)" -v "$ROOT:/work" -w /work "$IMAGE" "$@"
}

# The vendored sc808.scd is written to be evaluated block by block in the SC
# IDE, so its top-level parens do not balance over the file as a whole and
# executeFile() rejects it. Stripping the lone paren lines leaves a valid
# sequence of SynthDef statements. Also retarget writeDefFile away from the
# original author's home directory.
echo "==> preparing synthdefs"
sed 's#/Users/sam/Development/sonic-pi/etc/synthdefs/compiled/#/work/sc/defs/#g' \
    src/vendor/sc808/sc808.scd | grep -v '^[()]$' > sc/sc808_defs.scd

# The engine is built WITHOUT SC808_NULLTEST; only these tools define it, and
# only they emulate SC's per-note construction sample. See sc_ugens.h.
CXXFLAGS="-std=c++14 -O2 -DSC808_NULLTEST -Isrc/dsp"

if [ "$PROBES" = "1" ]; then
    echo "==> probes: one UGen at a time"
    rm -f sc/pout/* sc/pdefs/*
    g++ $CXXFLAGS -o build-native/probes src/tools/probes.cpp
    ./build-native/probes sc/pmine >/dev/null
    sc sclang test/probes.scd >/dev/null 2>&1
    sc python3 test/nulltest.py --mono sc/pout sc/pmine
    echo
fi

echo "==> reference: scsynth NRT"
rm -f sc/out/* sc/defs/*
sc sclang test/nrt_render.scd >/dev/null 2>&1

echo "==> engine: sc808_voices.h"
mkdir -p build-native
g++ $CXXFLAGS -o build-native/nullref src/tools/nullref.cpp
./build-native/nullref sc/mine sc/ctrl >/dev/null

echo
echo "=== 8W8 Engine A vs SuperCollider ==="
sc python3 test/nulltest.py sc/out sc/mine

echo
echo "=== control: the same C++ voice, two different noise draws ==="
echo "    The noise voices cannot null against scsynth — different RNG"
echo "    stream. This is the floor of the measurement: whatever two"
echo "    innocent draws of the SAME code score, a real error must beat."
sc python3 test/nulltest.py sc/mine sc/ctrl | grep -E 'NOISE|worst band|worst env'
