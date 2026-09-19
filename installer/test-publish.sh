#!/bin/bash
#------------------------------------------------------------------------
# installer/test-publish.sh
#
# Exercise publish-release.sh's refusals by STUBBING the tools it calls.
# Runs anywhere bash and git do - it never calls stapler, spctl, shasum or
# gh for real, so it works on Linux where publish-release.sh itself cannot.
#
# WHY STUBS. Every guard in that script is about a condition you cannot
# conveniently produce on demand: a package that is signed but not
# stapled, one Gatekeeper refuses, a build from a dirty tree. Reading the
# checks proves nothing; a stub makes each condition a one-line
# environment variable and the check either fires or it does not.
#
# Publishing is the one irreversible step in the whole release - a tag
# and an asset other people can fetch - so its refusals are worth more
# than the usual amount of testing.
#------------------------------------------------------------------------
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/publish-release.sh"
[ -x "$SCRIPT" ] || { echo "test-publish: cannot execute $SCRIPT" >&2; exit 2; }
command -v git >/dev/null || { echo "test-publish: needs git" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin"

cat > "$TMP/bin/uname" <<'EOF'
#!/bin/bash
[ "${1:-}" = "-s" ] && echo "${FAKE_UNAME:-Darwin}" || /usr/bin/uname "$@"
EOF
cat > "$TMP/bin/xcrun" <<'EOF'
#!/bin/bash
[ "${FAKE_STAPLED:-1}" = "1" ] || { echo "The validate action failed" >&2; exit 65; }
EOF
cat > "$TMP/bin/spctl" <<'EOF'
#!/bin/bash
if [ "${FAKE_GATEKEEPER:-1}" = "1" ]; then
    echo "pkg: accepted"; echo "source=Notarized Developer ID"
else
    echo "pkg: rejected"; echo "source=no usable signature"
fi
EOF
cat > "$TMP/bin/shasum" <<'EOF'
#!/bin/bash
echo "${FAKE_SHA:-aaaabbbbccccddddeeeeffff00001111222233334444555566667777888899990}  pkg"
EOF
cat > "$TMP/bin/gh" <<'EOF'
#!/bin/bash
[ "$1" = "auth" ] && exit 0
echo "gh $*"
EOF
chmod +x "$TMP/bin"/*
export PATH="$TMP/bin:$PATH"

setup () {
    rm -rf "$TMP/proj"; mkdir -p "$TMP/proj/installer"
    cd "$TMP/proj" || exit 2
    printf 'set(PLUGIN_VERSION      "1.0.0.1")\n' > CMakeLists.txt
    cp "$SCRIPT" installer/ && chmod +x installer/publish-release.sh
    printf 'notes\n\nSHA-256: <paste from: shasum -a 256 x>\n' \
        > installer/release-notes-1.0.0.1.md
    : > installer/FilterDrum-1.0.0.1.pkg
    git init -q . && git config user.email t@t && git config user.name T
    git add -A && git commit -qm "build commit"
    git remote add origin https://github.com/acobley/FilterDrum-VSTi.git
    { echo "commit=$(git rev-parse HEAD)"; echo "version=1.0.0.1"; echo "dirty=no"; } \
        > installer/.built-from
    cd - >/dev/null || exit 2
}

pass=0; fail=0
case_ () {   # case_ <want-exit> <label> [VAR=value ...]
    local want="$1" label="$2"; shift 2
    setup >/dev/null 2>&1
    [ -n "${PREP:-}" ] && eval "$PREP"
    ( cd "$TMP/proj" && env "$@" ./installer/publish-release.sh --dry-run ) \
        > "$TMP/out" 2>&1
    local got=$?
    if [ "$got" = "$want" ]; then
        printf '  ok    %s\n' "$label"; pass=$((pass+1))
    else
        printf '  FAIL  %s (wanted exit %s, got %s)\n' "$label" "$want" "$got"
        grep -m2 . "$TMP/out" | sed 's/^/        /'
        fail=$((fail+1))
    fi
    unset PREP
}

echo "PUBLISH GUARDS - the refusals that stand between a bad build and a tag"
case_ 0 "a good package publishes (dry run)"
case_ 1 "refuses when not run on macOS"                        FAKE_UNAME=Linux
case_ 1 "refuses a package that is NOT stapled"                FAKE_STAPLED=0
case_ 1 "refuses a package Gatekeeper rejects"                 FAKE_GATEKEEPER=0

PREP='rm -f "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when .built-from is missing"

PREP='sed -i s/dirty=no/dirty=yes/ "$TMP/proj/installer/.built-from"'
case_ 1 "refuses a package built from a DIRTY tree"

PREP='sed -i s/version=1.0.0.1/version=0.9.0.0/ "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when .built-from names a different version"

PREP='sed -i "s/^commit=.*/commit=0000000000000000000000000000000000000000/" "$TMP/proj/installer/.built-from"'
case_ 1 "refuses when the build commit is not in the repo"

PREP='printf "notes\n\nSHA-256: 1111111111111111111111111111111111111111111111111111111111111111\n" > "$TMP/proj/installer/release-notes-1.0.0.1.md"'
case_ 1 "refuses notes carrying a DIFFERENT checksum"

PREP='rm -f "$TMP/proj/installer/FilterDrum-1.0.0.1.pkg"'
case_ 1 "refuses when there is no package"

echo
echo "--------------------"
echo "$((pass+fail)) cases, $fail failures"
[ "$fail" = 0 ]
