#!/bin/bash
#------------------------------------------------------------------------
# installer/test-build-release.sh
#
# Exercise build-release.sh's refusals by STUBBING the Apple tools and
# the build. Runs anywhere bash, git and python3 do. The real version,
# preset and plist files are copied in, so the version and preset checks
# run for real; only the compiler, the signer and the notary are fake.
#
# Every refusal here is one that would otherwise be discovered AFTER a
# clean build and a notarisation - a quarter of an hour, each time.
#------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
SCRIPT="$HERE/build-release.sh"
[ -x "$SCRIPT" ] || { echo "test-build-release: cannot execute $SCRIPT" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin"
export FAKE_HOME_DIR="$TMP/home/someone"

cat > "$TMP/bin/uname" <<'S'
#!/bin/bash
[ "${1:-}" = "-s" ] && echo "${FAKE_UNAME:-Darwin}" || /usr/bin/uname "$@"
S
cat > "$TMP/bin/security" <<'S'
#!/bin/bash
n_app="${FAKE_APP_IDS:-1}"; n_inst="${FAKE_INST_IDS:-1}"
for i in $(seq 1 "$n_app"); do
  echo "  $i) AAAA$i \"Developer ID Application: Test ($i)\""; done
if [ "${2:-}" != "-p" ]; then
  for i in $(seq 1 "$n_inst"); do
    echo "  $i) BBBB$i \"Developer ID Installer: Test ($i)\""; done
fi
S
cat > "$TMP/bin/xcrun" <<'S'
#!/bin/bash
[ "${FAKE_NOTARY:-1}" = "1" ]
S
cat > "$TMP/bin/cmake" <<'S'
#!/bin/bash
for kind in vst3 component; do
  d="build/VST3/Release/FilterDrum.$kind/Contents/MacOS"; mkdir -p "$d"
  echo "binary" > "$d/FilterDrum"
done
[ -n "${FAKE_LEAK:-}" ] && echo "$HOME/src/Leak.cpp" >> build/VST3/Release/FilterDrum.vst3/Contents/MacOS/FilterDrum
[ -n "${FAKE_SDK_LEAK:-}" ] && echo "$HOME/x/external/AudioUnitSDK/src/AUBase.cpp" >> build/VST3/Release/FilterDrum.component/Contents/MacOS/FilterDrum
exit 0
S
cat > "$TMP/bin/lipo" <<'S'
#!/bin/bash
echo "${FAKE_ARCHS:-x86_64 arm64}"
S
chmod +x "$TMP/bin"/*
export PATH="$TMP/bin:$PATH"

setup () {
    rm -rf "$TMP/proj"; mkdir -p "$TMP/proj"
    rsync -a --exclude build --exclude external --exclude .git \
          --exclude '*.pkg' --exclude .built-from "$ROOT/" "$TMP/proj/"
    cd "$TMP/proj" || exit 2
    printf '#!/bin/bash\nexit 0\n' > setup-xcode.sh
    printf '#!/bin/bash\n[ "${FAKE_TESTS:-1}" = 1 ] && echo "ALL SUITES PASSED" || { echo broken; exit 1; }\n' > tools/run-tests.sh
    cat > installer/build-installer.sh <<'S'
#!/bin/bash
here="$(cd "$(dirname "$0")" && pwd)"
v="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' "$here/../CMakeLists.txt")"
echo "build-installer $*" > "$here/.called-with"
[ "${FAKE_NO_PKG:-0}" = 1 ] || echo pkg > "$here/FilterDrum-$v.pkg"
S
    chmod +x setup-xcode.sh tools/run-tests.sh installer/build-installer.sh
    v="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' CMakeLists.txt)"
    [ -f "installer/release-notes-$v.md" ] || echo notes > "installer/release-notes-$v.md"
    git init -q . && git config user.email t@t && git config user.name T
    git add -A && git commit -qm "build commit"
    cd - >/dev/null || exit 2
}

pass=0; fail=0
case_ () {   # case_ <want-exit> <label> [args-and-VAR=value ...]
    local want="$1" label="$2"; shift 2
    local envs=() args=()
    for a in "$@"; do case "$a" in FAKE_*=*|HOME=*) envs+=("$a") ;; *) args+=("$a") ;; esac; done
    setup >/dev/null 2>&1
    [ -n "${PREP:-}" ] && ( cd "$TMP/proj" && eval "$PREP" )
    ( cd "$TMP/proj" && env HOME="$FAKE_HOME_DIR" "${envs[@]}" ./installer/build-release.sh "${args[@]}" ) \
        > "$TMP/out" 2>&1
    local got=$?
    if [ "$got" = "$want" ] && { [ -z "${EXPECT:-}" ] || grep -q -- "$EXPECT" "$TMP/out" "$TMP/proj/installer/.called-with" 2>/dev/null; }; then
        printf '  ok    %s\n' "$label"; pass=$((pass+1))
    else
        printf '  FAIL  %s (wanted exit %s, got %s%s)\n' "$label" "$want" "$got" "${EXPECT:+, looking for: $EXPECT}"
        tail -4 "$TMP/out" | sed 's/^/        /'
        fail=$((fail+1))
    fi
    unset PREP EXPECT
}

echo "BUILD-RELEASE GUARDS - refusals that would otherwise cost a notarisation"

EXPECT="--notarize project6-notary"
case_ 0 "a good tree builds, signed and notarised with the default profile"
EXPECT="--sign-app AAAA1 --sign-installer BBBB1"
case_ 0 "the identities are looked up and passed as hashes"
EXPECT="^build-installer $"
case_ 0 "--unsigned passes nothing to build-installer" --unsigned FAKE_APP_IDS=0 FAKE_ARCHS=arm64
EXPECT="needs macOS"
case_ 1 "refuses when not run on macOS"                   FAKE_UNAME=Linux

EXPECT="version numbers disagree"
PREP='sed -i "s/<integer>65537</<integer>65536</" resource/au-info.plist; git commit -qam x'
case_ 1 "refuses when the AU version disagrees with CMakeLists"

EXPECT="already exists"
PREP='git tag v$(sed -n "s/^set(PLUGIN_VERSION[[:space:]]*\"\([^\"]*\)\").*/\1/p" CMakeLists.txt)'
case_ 1 "refuses a version that is already tagged (released)"

EXPECT="no installer/release-notes"
PREP='git rm -q installer/release-notes-*.md; git commit -qm x'
case_ 1 "refuses when there are no release notes for this version"

EXPECT="uncommitted changes"
PREP='echo change >> README.md'
case_ 1 "refuses a dirty tree"
PREP='echo change >> README.md'
case_ 0 "... unless --allow-dirty"                         --allow-dirty

EXPECT="no \"Developer ID Application\""
case_ 1 "refuses with NO Developer ID Application"        FAKE_APP_IDS=0
EXPECT="more than one"
case_ 1 "refuses with TWO Developer ID Installers"        FAKE_INST_IDS=2
EXPECT="does not work"
case_ 1 "refuses when the notary profile does not answer" FAKE_NOTARY=0
EXPECT="tests failed"
case_ 1 "refuses when the tests fail"                     FAKE_TESTS=0
case_ 0 "... unless --skip-tests"                         FAKE_TESTS=0 --skip-tests

EXPECT="missing or stale"
PREP='rm installer/presets/*.aupreset; git commit -qam x'
case_ 1 "refuses when an .aupreset is missing"
EXPECT="missing or stale"
# Change a parameter value INSIDE the Comp chunk (byte 70 is in the doubles
# that follow the 48-byte header and the version/count ints). A byte
# appended past the chunk list would change nothing the AU sees, and the
# check rightly lets that through.
PREP='python3 -c "p=\"installer/presets/Pew.vstpreset\";b=bytearray(open(p,\"rb\").read());b[70]^=0x10;open(p,\"wb\").write(b)"; git commit -qam x'
case_ 1 "refuses when a parameter in a .vstpreset changed and its .aupreset did not"

EXPECT="absolute paths naming"
case_ 1 "refuses a home-directory path in the VST3"       FAKE_LEAK=1
EXPECT="AudioUnitSDK source path"
case_ 0 "the known AudioUnitSDK paths are reported, not fatal" FAKE_SDK_LEAK=1
EXPECT="advertises both"
case_ 1 "refuses a single-architecture signed build"      FAKE_ARCHS=arm64
EXPECT="is not there after"
case_ 1 "refuses when build-installer leaves no package"  FAKE_NO_PKG=1

PREP='echo old > installer/FilterDrum-0.9.pkg; echo stale > installer/.built-from'
case_ 0 "an earlier package and .built-from are removed first"
if [ -e "$TMP/proj/installer/FilterDrum-0.9.pkg" ] || [ -e "$TMP/proj/installer/.built-from" ]; then
    echo "  FAIL  ... but they are still there"; fail=$((fail+1))
fi

echo
echo "--------------------"
echo "$((pass+fail)) cases, $fail failures"
[ "$fail" = 0 ]
