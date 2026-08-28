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
skips=0
step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
verdict() { if [ "$1" -eq 0 ]; then echo "   PASS"; else echo "   FAIL"; fails=$((fails+1)); fi; }
# A step that could not run is NOT a step that passed. Counted separately and
# reported at the end, because a green run on a machine missing half the
# toolchain used to look exactly like a green run on the build host.
skip()    { echo "   SKIP ($1)"; skips=$((skips+1)); }
have()    { command -v "$1" >/dev/null 2>&1; }

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
# Warning-free is only half of it: the build has to have SUCCEEDED and to have
# produced the artefacts. Grepping the log for "warning" said PASS on a host
# with no Docker, where the build printed "docker: command not found" and
# produced nothing at all.
if ! have docker; then
  skip "no docker — scripts/build.sh needs it, see the header"
  built=1
else
  NATIVE=1 ./scripts/build.sh all >build-native/build.log 2>&1
  rc=$?
  n=$(grep -ciE '\bwarning\b|\berror\b' build-native/build.log || true)
  if [ "$rc" -ne 0 ]; then
    echo "   build.sh exited $rc"; tail -5 build-native/build.log; verdict 1; built=1
  elif [ ! -f build-native/dsp.so ]; then
    echo "   build.sh succeeded but produced no build-native/dsp.so"; verdict 1; built=1
  elif [ "$n" -ne 0 ]; then
    grep -iE '\bwarning\b|\berror\b' build-native/build.log | head -10; verdict 1; built=1
  else
    verdict 0; built=0
  fi
fi

step "loadtest: the shipped .so, dlopened as the chain host does"
# `verdict $?` after a `tail` reads TAIL's status, which is always 0 — this
# step reported PASS with the binary missing entirely.
if [ ! -x build-native/sc808_loadtest ] || [ ! -f build-native/dsp.so ]; then
  skip "not built — needs the build step above"
else
  ./build-native/sc808_loadtest ./build-native/dsp.so >build-native/loadtest.log 2>&1
  l=$?
  tail -1 build-native/loadtest.log
  verdict $l
fi

step "golden render — every lane still makes the sound it was signed off on"
# The voices were tuned one at a time against the player's own references and
# each was approved by ear on the device. Nothing else in this suite asserts
# that work; it lives in filter corners and envelope constants. This renders
# every lane through the real engine and compares the samples bit for bit, so
# structural work on the engine AROUND the voices cannot quietly move them.
$CXX $FLAGS -o build-native/golden_check tools/golden_check.cpp src/dsp/sc808_engine.cpp
./build-native/golden_check >build-native/golden.log 2>&1
g=$?
tail -1 build-native/golden.log
[ $g -ne 0 ] && head -20 build-native/golden.log
verdict $g

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

step "circuit hand clap"
$CXX $FLAGS -o build-native/cp_check tools/cp_check.cpp
./build-native/cp_check >build-native/cp.log 2>&1
c=$?
tail -1 build-native/cp.log
verdict $c

step "circuit tom / conga"
$CXX $FLAGS -o build-native/tom_check tools/tom_check.cpp
./build-native/tom_check >build-native/tom.log 2>&1
t=$?
grep -E 'engines land|not the same signal' build-native/tom.log
verdict $t

step "editor and remote panel"
if have node; then
  node test/ui_chain.test.mjs >build-native/ui.log 2>&1
  u=$?
  tail -1 build-native/ui.log
  grep -q '^SKIP' build-native/ui.log && sed -n '/^SKIP/,$p' build-native/ui.log | head -4
  verdict $u
else
  skip "no node"
fi

step "null test: Engine A against SuperCollider"
if ! have docker; then
  skip "no docker — scsynth runs in a container, see test/nulltest.sh"
else
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
fi

printf '\n\033[1m%s\033[0m (%d failing step%s' \
  "$([ $fails -eq 0 ] && echo ALL PASS || echo FAILED)" "$fails" \
  "$([ $fails -eq 1 ] || echo s)"
if [ $skips -gt 0 ]; then
  printf ', %d SKIPPED — this run did NOT verify everything' "$skips"
fi
printf ')\n'
exit $((fails ? 1 : 0))
