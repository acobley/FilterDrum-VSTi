#!/bin/bash
#------------------------------------------------------------------------
# tools/test-make-presets.sh - exercise tools/make-presets.py's refusals
# against deliberately wrong presets. Runs anywhere python3 does.
#
# Each case builds a throwaway tree carrying the REAL identity files -
# FilterDrumIDs.h, the processor, the params header, au-info.plist - so
# the converter is tested against this plug-in's actual UID, state
# version and AU codes, and a preset crafted from installer/presets/
# Pew.vstpreset with one thing broken.
#------------------------------------------------------------------------
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="$ROOT/installer/presets/Pew.vstpreset"
[ -f "$SEED" ] || { echo "test-make-presets: no $SEED to build cases from" >&2; exit 2; }
command -v python3 >/dev/null || { echo "test-make-presets: needs python3" >&2; exit 2; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

setup () {
    rm -rf "$TMP/t"; mkdir -p "$TMP/t/source" "$TMP/t/resource" "$TMP/t/tools" "$TMP/t/installer/presets"
    cp "$ROOT/source/FilterDrumIDs.h" "$ROOT/source/FilterDrumProcessor.cpp" \
       "$ROOT/source/FilterDrumParams.h" "$TMP/t/source/"
    cp "$ROOT/resource/au-info.plist" "$TMP/t/resource/"
    cp "$ROOT/tools/make-presets.py" "$TMP/t/tools/"
    cp "$SEED" "$TMP/t/installer/presets/Pew.vstpreset"
}

# Rewrite the seed preset with one field changed.  $1 is a python
# expression over `b`, a bytearray of the whole file.
mutate () {
    python3 - "$TMP/t/installer/presets/Pew.vstpreset" "$1" <<'PY'
import sys, struct
p, expr = sys.argv[1], sys.argv[2]
b = bytearray(open(p, 'rb').read())
exec(expr)
open(p, 'wb').write(b)
PY
}

pass=0; fail=0
expect () {   # expect <want-exit> <label> [args...]
    local want="$1" label="$2"; shift 2
    python3 "$TMP/t/tools/make-presets.py" "$@" >"$TMP/out" 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then printf '  ok    %s\n' "$label"; pass=$((pass+1))
    else printf '  FAIL  %s (wanted exit %s, got %s)\n' "$label" "$want" "$got"
         sed 's/^/        /' "$TMP/out" | head -3; fail=$((fail+1)); fi
}

echo "PRESET CONVERTER"

setup
expect 0 "Pew converts"
python3 - "$TMP/t/installer/presets" <<'PY' && { echo "  ok    the result carries Controller State, and Processor State is the Comp chunk"; pass=$((pass+1)); } || { echo "  FAIL  the result is not what the AU wrapper needs"; fail=$((fail+1)); }
import sys, plistlib, struct
d = sys.argv[1]
a = plistlib.load(open(d + '/Pew.aupreset', 'rb'))
raw = open(d + '/Pew.vstpreset', 'rb').read()
assert 'Controller State' in a, 'no Controller State key - the wrapper would not sync the controller'
assert a['Processor State'] == raw[48:48 + 348]
assert a['version'] == 0
assert struct.pack('>I', a['subtype']) == b'FDrm'
PY

setup; python3 "$TMP/t/tools/make-presets.py" >/dev/null 2>&1
expect 0 "--check passes on a fresh conversion" --check

setup
expect 1 "--check fails when the .aupreset is MISSING" --check

setup; python3 "$TMP/t/tools/make-presets.py" >/dev/null 2>&1
mutate 'b[48+8] ^= 0xFF'
expect 1 "--check fails when the .vstpreset changed since - a STALE .aupreset" --check

setup; mutate 'b[8:40] = b"0123456789ABCDEF0123456789ABCDEF"'
expect 1 "refuses a preset from ANOTHER plug-in (foreign class ID)"

setup; mutate 'b[48:52] = struct.pack("<i", 99)'
expect 1 "refuses a state version NEWER than this build"

setup; mutate 'b[52:56] = struct.pack("<i", 50)'
expect 1 "refuses a Comp chunk whose length disagrees with its count"

setup; mutate 'b[0:4] = b"XXXX"'
expect 1 "refuses a file that is not a VST3 preset"

echo
echo "ADOPTING AN .aupreset SAVED BY AN AU HOST"

# Pew.vstpreset was written by a real VST3 host. Rebuilding it from its own
# Comp chunk must give the same file, byte for byte - that is what proves
# the layout the adopter writes is the one hosts write.
setup
python3 - "$TMP/t" <<'PY' && { echo "  ok    the adopter's .vstpreset layout is byte-identical to a host-written one"; pass=$((pass+1)); } || { echo "  FAIL  the adopter writes a different layout from a VST3 host"; fail=$((fail+1)); }
import sys, importlib.util
t = sys.argv[1]
spec = importlib.util.spec_from_file_location('mp', t + '/tools/make-presets.py')
mp = importlib.util.module_from_spec(spec); spec.loader.exec_module(mp)
raw = open(t + '/installer/presets/Pew.vstpreset', 'rb').read()
assert mp.vstpreset_bytes(mp.processor_uid(), raw[48:48 + 348]) == raw
PY

# An orphan .aupreset: convert Pew, then drop its .vstpreset and rename.
orphan () {
    setup; python3 "$TMP/t/tools/make-presets.py" >/dev/null 2>&1
    mv "$TMP/t/installer/presets/Pew.aupreset" "$TMP/t/installer/presets/Kick.aupreset"
    rm "$TMP/t/installer/presets/Pew.vstpreset"
}
orphan
expect 1 "--check fails on an .aupreset with no .vstpreset" --check
orphan
expect 0 "an orphan .aupreset is adopted"
python3 - "$TMP/t/installer/presets" <<'PY' && { echo "  ok    the adopted .vstpreset carries the AU preset's state exactly"; pass=$((pass+1)); } || { echo "  FAIL  the adopted .vstpreset is not the AU preset's state"; fail=$((fail+1)); }
import sys, plistlib
d = sys.argv[1]
a = plistlib.load(open(d + '/Kick.aupreset', 'rb'))
v = open(d + '/Kick.vstpreset', 'rb').read()
assert v[48:48 + len(a['Processor State'])] == a['Processor State']
assert a['name'] == 'Kick'
PY
orphan
python3 - "$TMP/t/installer/presets/Kick.aupreset" <<'PY'
import sys, plistlib
p = sys.argv[1]; a = plistlib.load(open(p, 'rb')); a['subtype'] = 0x46547261   # 'FTra'
open(p, 'wb').write(plistlib.dumps(a))
PY
expect 1 "refuses to adopt an .aupreset from ANOTHER Audio Unit"

echo
echo "--------------------"
echo "$((pass+fail)) cases, $fail failures"
[ "$fail" = 0 ]
