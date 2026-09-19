#!/bin/bash
#------------------------------------------------------------------------
# tools/run-tests.sh - every suite, in one command.
#
# The DSP, the sequencer and the transport clock are free of Steinberg
# headers on purpose, so all three build and run with nothing but a
# compiler. Run this before cutting a release; it is the first line of
# the release sequence in installer/README.md.
#
# The installer's own guards have a separate runner - installer/
# test-guards.sh - because they test shell rather than C++. This calls
# it too when it is present, so one command covers both.
#------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX="${CXX:-g++}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail=0

run () {   # run <label> <output> <source...>
    local label="$1" out="$2"; shift 2
    printf '\n=== %s ===\n' "$label"
    if ! "$CXX" -std=c++17 -O2 -I"$ROOT/source" -o "$WORK/$out" "$@" 2>"$WORK/build.err"; then
        echo "  BUILD FAILED"
        head -20 "$WORK/build.err" | sed 's/^/  /'
        fail=1
        return
    fi
    if "$WORK/$out"; then :; else fail=1; fi
}

run "DSP"       dsptests \
    "$ROOT/tests/DspTests.cpp"       "$ROOT/source/FilterDrumDsp.cpp"
run "Sequencer" seqtests \
    "$ROOT/tests/SequencerTests.cpp" "$ROOT/source/FilterDrumSequencer.cpp" \
    "$ROOT/source/FilterDrumTransport.cpp"
run "Transport" trtests \
    "$ROOT/tests/TransportTests.cpp" "$ROOT/source/FilterDrumTransport.cpp"

if [ -x "$ROOT/installer/test-guards.sh" ]; then
    printf '\n=== Installer guards ===\n'
    "$ROOT/installer/test-guards.sh" || fail=1
fi

printf '\n--------------------\n'
if [ "$fail" = 0 ]; then
    echo "ALL SUITES PASSED"
else
    echo "THERE WERE FAILURES"
fi
exit "$fail"
