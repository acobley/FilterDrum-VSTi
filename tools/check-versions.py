#!/usr/bin/env python3
"""tools/check-versions.py - the version lives in THREE places; say if they disagree.

    CMakeLists.txt          PLUGIN_VERSION "M.S.R.B"   (names the .pkg and the tag)
    source/version.h        MAJOR / SUB / RELEASE / BUILD  (what a VST3 host shows)
    resource/au-info.plist  CFBundleVersion, CFBundleShortVersionString "M.S.R",
                            AudioComponents version  = M<<16 | S<<8 | R,
                            "AudioUnit Version"      = the same, as hex

The AU half matters more than it looks. An AU host caches a component by
its version: ship a changed Audio Unit under the old number and Logic, and
anything else built on the AU cache, may go on showing the old name and the
old validation result. The build number has no place in the AU version, so
a release that changes the AU should move RELEASE, not just BUILD.

Exit 0 if they agree, 1 (with every disagreement listed) if not.
"""
import plistlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    problems = []

    m = re.search(r'^set\(PLUGIN_VERSION\s+"([^"]+)"', (ROOT / "CMakeLists.txt").read_text(), re.M)
    if not m:
        print("check-versions: no PLUGIN_VERSION in CMakeLists.txt")
        return 1
    cmake = m.group(1)
    parts = cmake.split(".")
    if len(parts) != 4 or not all(p.isdigit() for p in parts):
        print(f"check-versions: PLUGIN_VERSION {cmake!r} is not M.S.R.B")
        return 1
    major, sub, rel, build = (int(p) for p in parts)

    vh = (ROOT / "source" / "version.h").read_text()
    for name, want in (("MAJOR_VERSION", major), ("SUB_VERSION", sub),
                       ("RELEASE_NUMBER", rel), ("BUILD_NUMBER", build)):
        for suffix, fmt in (("_INT", r"(\d+)"), ("_STR", r'"(\d+)"')):
            mm = re.search(rf"^#define\s+{name}{suffix}\s+{fmt}", vh, re.M)
            got = int(mm.group(1)) if mm else None
            if got != want:
                problems.append(f"source/version.h {name}{suffix} is {got}, CMakeLists says {want}")

    with open(ROOT / "resource" / "au-info.plist", "rb") as f:
        pl = plistlib.load(f)
    short = f"{major}.{sub}.{rel}"
    for key in ("CFBundleVersion", "CFBundleShortVersionString"):
        if pl.get(key) != short:
            problems.append(f"au-info.plist {key} is {pl.get(key)!r}, want {short!r}")
    au_int = (major << 16) | (sub << 8) | rel
    for i, comp in enumerate(pl.get("AudioComponents", [])):
        if comp.get("version") != au_int:
            problems.append(f"au-info.plist AudioComponents[{i}].version is "
                            f"{comp.get('version')}, want {au_int} (0x{au_int:08X} == {short})")
    hexv = pl.get("AudioUnit Version", "")
    try:
        ok = int(hexv, 16) == au_int
    except ValueError:
        ok = False
    if not ok:
        problems.append(f"au-info.plist 'AudioUnit Version' is {hexv!r}, want {au_int:08X}")

    if problems:
        print(f"check-versions: CMakeLists.txt says {cmake}, but:")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check-versions: {cmake} everywhere (AU {short}, 0x{au_int:08X})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
