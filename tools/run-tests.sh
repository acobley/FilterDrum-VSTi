#!/bin/bash
#------------------------------------------------------------------------
# tools/run-tests.sh - every suite, in one command.
#
# The DSP, the sequencer and the transport clock are free of Steinberg
# headers on purpose, so all three build and run with nothing but a
# compiler. Run this before cutting a release; it is the first line of
# the release sequence in installer/README.md,
# and build-release.sh runs it for you.
#
# The installer's own guards, the release scripts and the preset converter
# have separate runners - installer/test-*.sh, tools/test-make-presets.sh -
# because they test shell and Python rather than C++. This calls them all.
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

printf '\n=== versions ===\n'
python3 "$ROOT/tools/check-versions.py" || fail=1

for t in tools/test-make-presets installer/test-guards installer/test-build-release installer/test-publish; do
    script="$ROOT/$t.sh"
    if [ -f "$script" ] && [ ! -x "$script" ]; then
        # The execute bit is easy to lose - cp, a restore from backup and an
        # editor rewriting in place have all done it in this repo alone. Say
        # so rather than silently skipping the test.
        printf '\n=== %s ===\n  NOT EXECUTABLE: %s\n' "$t" "$script"
        echo "  chmod +x it; a skipped test looks exactly like a passing one."
        fail=1
    elif [ -x "$script" ]; then
        printf '\n=== %s ===\n' "$t"
        "$script" || fail=1
    fi
done

printf '\n--------------------\n'
if [ "$fail" = 0 ]; then
    echo "ALL SUITES PASSED"
else
    echo "THERE WERE FAILURES"
fi
exit "$fail"
