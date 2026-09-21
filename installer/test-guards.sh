#!/bin/bash
#------------------------------------------------------------------------
# installer/test-guards.sh
#
# Exercise build-installer.sh's two guards against DELIBERATELY BROKEN
# TREES. Runs anywhere bash and find do - it never calls pkgbuild,
# codesign or anything else Apple-only, so it works on the Linux side of
# a remote session where the rest of the installer cannot.
#
# WHY THIS EXISTS. Both guards were written wrong the first time and
# neither error was visible by reading them:
#
#   * the pkg-ref scan was line-based, so it matched nothing against
#     elements written across several lines - and then reported that
#     nothing was missing, which is a guard that passes everything;
#   * a guard with nothing to check has to FAIL, not pass, or a
#     malformed archive sails through it.
#
# THE GUARDS ARE SLICED OUT OF build-installer.sh, not retyped here, so
# this tests what actually ships. The slice is anchored on marker
# comments and the script exits if it cannot find them - a test that
# silently examines the wrong lines is worse than no test.
#------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/build-installer.sh"
[ -r "$SRC" ] || { echo "test-guards: cannot read $SRC" >&2; exit 2; }

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

# --- slice a guard out, between two anchors, or die ---------------------
slice () {   # slice <first-line-anchor> <last-line-anchor> <out>
    awk -v a="$1" -v b="$2" '
        index($0, a) { on = 1 }
        on           { print }
        on && index($0, b) { exit }
    ' "$SRC" > "$3"
    if ! grep -qF "$1" "$3" || ! grep -qF "$2" "$3"; then
        echo "test-guards: could not find the guard between" >&2
        echo "   $1" >&2
        echo "   $2" >&2
        echo "in build-installer.sh. The script changed; fix this slice rather" >&2
        echo "than the picture it gives you." >&2
        exit 2
    fi
}

slice 'escapes=""' 'no symlink in the payload points outside it' "$WORKDIR/guard-symlink.sh"
slice 'refs=$(tr'  'every pkg-ref resolves'                      "$WORKDIR/guard-pkgref.sh"
sed -i 's/^    //' "$WORKDIR/guard-pkgref.sh" 2>/dev/null || \
    sed -e 's/^    //' -i '' "$WORKDIR/guard-pkgref.sh"

pass=0; fail=0
expect () {   # expect <want-exit> <label> <guard>
    bash "$WORKDIR/$3" >"$WORKDIR/out" 2>&1
    local got=$?
    if [ "$got" = "$1" ]; then
        printf '  ok    %s\n' "$2"; pass=$((pass+1))
    else
        printf '  FAIL  %s (wanted exit %s, got %s)\n' "$2" "$1" "$got"
        sed 's/^/        /' "$WORKDIR/out" | head -3
        fail=$((fail+1))
    fi
}

export WORK="$WORKDIR/w"

echo "SYMLINK GUARD - the one that stops a dead Audio Unit shipping"
rm -rf "$WORK"; mkdir -p "$WORK/root-vst3/a" "$WORK/root-au"
ln -s ../a/real "$WORK/root-vst3/rel"
expect 0 "a relative symlink is allowed" guard-symlink.sh

mkdir -p "$WORK/root-au/FilterDrum.component/Contents/Resources"
ln -s /Users/someone/DXi-DEv/FilterDrum-VSTi/build/VST3/Release/FilterDrum.vst3 \
      "$WORK/root-au/FilterDrum.component/Contents/Resources/plugin.vst3"
expect 1 "an ABSOLUTE symlink is caught - THE defect this skill exists for" guard-symlink.sh

rm -rf "$WORK"; mkdir -p "$WORK/root-vst3" "$WORK/root-au"
ln -s ./nowhere "$WORK/root-vst3/dangling"
expect 0 "a dangling RELATIVE link is not flagged - not this guard's job" guard-symlink.sh

rm -rf "$WORK"; mkdir -p "$WORK/root-vst3" "$WORK/root-au"; touch "$WORK/root-vst3/f"
expect 0 "a payload with no symlinks passes" guard-symlink.sh

rm -rf "$WORK"; mkdir -p "$WORK/root-vst3" "$WORK/root-au" "$WORK/root-presets/AE Cobley/FilterDrum"
ln -s "/Users/someone/DXi-DEv/FilterDrum-VSTi/installer/presets/Pew.aupreset" \
      "$WORK/root-presets/AE Cobley/FilterDrum/Pew.aupreset"
expect 1 "an absolute symlink in the PRESETS payload is caught too - path with spaces" guard-symlink.sh

echo
echo "PKG-REF GUARD - the one that stops a pagecontroller error reaching a user"
mk () { rm -rf "$WORK"; mkdir -p "$WORK/expanded"; cat > "$WORK/expanded/Distribution"; }

mk <<'XML'
<installer-gui-script minSpecVersion="2">
  <pkg-ref id="audio.filterdrum.vst3.pkg"
           version="1.0.0.1"
           onConclusion="none">FilterDrum-VST3.pkg</pkg-ref>
  <pkg-ref id="audio.filterdrum.audiounit.pkg"
           version="1.0.0.1"
           onConclusion="none">FilterDrum-AU.pkg</pkg-ref>
</installer-gui-script>
XML
touch "$WORK/expanded/FilterDrum-VST3.pkg" "$WORK/expanded/FilterDrum-AU.pkg"
expect 0 "multi-line pkg-refs that all resolve - the case that broke it" guard-pkgref.sh

rm -f "$WORK/expanded/FilterDrum-AU.pkg"
expect 1 "a pkg-ref naming a package that is NOT embedded" guard-pkgref.sh

mk <<'XML'
<installer-gui-script minSpecVersion="2">
  <title>FilterDrum</title>
</installer-gui-script>
XML
expect 1 "no pkg-ref at all is a FAILURE, not a pass" guard-pkgref.sh

mk <<'XML'
<installer-gui-script minSpecVersion="2">
  <pkg-ref id="x" version="1">#FilterDrum-VST3.pkg</pkg-ref>
</installer-gui-script>
XML
touch "$WORK/expanded/FilterDrum-VST3.pkg"
expect 0 "the leading-# spelling resolves" guard-pkgref.sh

echo
echo "--------------------"
echo "$((pass+fail)) cases, $fail failures"
[ "$fail" = 0 ]
