#!/usr/bin/env bash
#
# Everything that can be checked without a Move plugged in.
#
#   ./test/all.sh            the fast set
#   ./test/all.sh --probes   also the per-UGen null probes (slower)
#
# What is NOT here, and cannot be: sc808_loadtest and sc808_bench are
# cross-compiled for aarch64 and run ON the device. scripts/build.sh builds
# them; deploy and run them there.
#
# Runs on the build host — it needs Docker for SuperCollider, a C++ compiler
# and node. See scripts/build.sh for why the Mac is not that host.
#
# GPL-3.0.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
cd "$ROOT"
mkdir -p build-native

CXX="${CXX:-g++}"
FLAGS="-std=c++14 -O2 -Isrc/dsp"
fails=0
step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
verdict() { if [ "$1" -eq 0 ]; then echo "   PASS"; else echo "   FAIL"; fails=$((fails+1)); fi; }

step "generator is in sync with the checked-in header"
# gen_params.py is the single source of truth for the pot table, chain_params,
# the page hierarchy and movy_config.json. If someone edits the generated
# header by hand, everything downstream silently disagrees with it.
cp src/dsp/sc808_params.h build-native/params.before
cp src/movy_config.json   build-native/movy.before
python3 scripts/gen_params.py >/dev/null
if diff -q build-native/params.before src/dsp/sc808_params.h >/dev/null &&
   diff -q build-native/movy.before   src/movy_config.json   >/dev/null
then verdict 0; else
  echo "   sc808_params.h or movy_config.json differ from what gen_params.py emits"
  verdict 1
fi

step "the module builds clean, natively"
NATIVE=1 ./scripts/build.sh all >build-native/build.log 2>&1
n=$(grep -ciE '\bwarning\b|\berror\b' build-native/build.log || true)
if [ "$n" -eq 0 ]; then verdict 0; else
  grep -iE '\bwarning\b|\berror\b' build-native/build.log | head -10; verdict 1; fi

step "loadtest: the shipped .so, dlopened as the chain host does"
./build-native/sc808_loadtest ./build-native/dsp.so >build-native/loadtest.log 2>&1
tail -1 build-native/loadtest.log
verdict $?

step "kit balance and spectral placement"
$CXX $FLAGS -o build-native/kit_check tools/kit_check.cpp src/dsp/sc808_engine.cpp
./build-native/kit_check >build-native/kit.log 2>&1
k=$?
grep -E 'worst lane|lanes land|wrong part|busy:' build-native/kit.log
verdict $k

step "circuit bass drum, against the paper's claims"
$CXX $FLAGS -o build-native/bd_check tools/bd_check.cpp
./build-native/bd_check >build-native/bd.log 2>&1
b=$?
tail -1 build-native/bd.log
verdict $b

step "editor and remote panel"
if command -v node >/dev/null; then
  node test/ui_chain.test.mjs >build-native/ui.log 2>&1
  u=$?
  tail -1 build-native/ui.log
  grep -q '^SKIP' build-native/ui.log && sed -n '/^SKIP/,$p' build-native/ui.log | head -4
  verdict $u
else
  echo "   SKIP (no node)"
fi

step "null test: Engine A against SuperCollider"
if [ "${1:-}" = "--probes" ]; then ./test/nulltest.sh --probes > build-native/null.log 2>&1
else                               ./test/nulltest.sh           > build-native/null.log 2>&1; fi
grep -E 'worst null|worst band|worst envelope' build-native/null.log
# The gate: every deterministic voice must still null well below anything
# audible. -50 dB is 0.3% residual; the current worst is -63.7.
# "worst null (deterministic voices)    -63.7 dB" -> field 5, not 6. Getting
# this wrong made the gate compare an empty string and fail every run.
worst=$(grep 'worst null' build-native/null.log | head -1 | awk '{print $5}')
if [ -n "$worst" ] && awk "BEGIN{exit !($worst < -50)}"; then verdict 0; else
  echo "   worst deterministic null is $worst dB — Engine A has drifted"; verdict 1; fi

printf '\n\033[1m%s\033[0m (%d failing step%s)\n' \
  "$([ $fails -eq 0 ] && echo ALL PASS || echo FAILED)" "$fails" \
  "$([ $fails -eq 1 ] || echo s)"
exit $((fails ? 1 : 0))
