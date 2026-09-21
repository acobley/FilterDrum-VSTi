#!/usr/bin/env python3
"""Turn each installer/presets/*.vstpreset into the matching .aupreset.

    tools/make-presets.py            write the .aupreset files
    tools/make-presets.py --check    exit 1 if any is missing or stale

WHY THIS CAN BE EXACT. Both formats carry the same bytes: the ones
FilterDrumProcessor::getState wrote. A .vstpreset keeps them as its "Comp"
chunk; Steinberg's AU wrapper keeps them in the plist under
"Processor State" (auwrapper.mm, SaveState). So the conversion re-wraps
the bytes and derives nothing - the two files cannot disagree about what a
preset sounds like.

ForTran's tools/make-presets.* goes the other way, .aupreset -> .vstpreset,
because its patches are authored in Logic. FilterDrum's was saved from a
VST3 host, hence this direction.

WHAT GOES IN THE PLIST, and why no more:

  * "Processor State" - the Comp chunk, verbatim.
  * "Controller State" - PRESENT BUT EMPTY. FilterDrumController::getState
    writes nothing, which is exactly what a host-saved preset carries. It
    must be present all the same: the wrapper's restoreState only calls
    setComponentState on the controller, and syncs the host's parameter
    values, `if (controllerData)`. Leave the key out and the sound changes
    while the controller - what the host and the panel show - keeps the
    old values, and can push them straight back.
  * type / subtype / manufacturer / version 0 / name. The wrapper refuses a
    preset whose version is not 0 or whose subtype or manufacturer is not
    its own; that check is what kept ForTran's presets out of FilterDrum.

NOT "data" and NOT "element-name", which a host-saved preset also has.
Those are AUBase's, read by AUBase::RestoreState - and the wrapper's
restoreState never calls it. Fabricating a blob nothing reads would be a
second copy of the state that can only ever be wrong.

WHAT IT REFUSES, because a preset that loads silently wrong is worse than
one that does not load:

  * a class ID that is not FilterDrum's processor. A host ignores a
    .vstpreset whose UID does not match, so this is always a mistake - most
    likely a preset saved from another plug-in dropped in the wrong folder;
  * a state version newer than this build's, which setState would refuse;
  * a Comp chunk whose length does not match the count it declares.

A preset with FEWER parameters than this build is fine and reported: the
processor defaults whatever the stream does not mention.
"""

import os
import plistlib
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PRESETS = os.path.join(ROOT, 'installer', 'presets')


def fail(msg):
    sys.stderr.write('make-presets: %s\n' % msg)
    sys.exit(1)


def read(rel):
    path = os.path.join(ROOT, rel)
    try:
        return open(path, encoding='utf-8').read()
    except OSError as e:
        fail('cannot read %s: %s' % (rel, e))


# --- identity, from the source ----------------------------------------------
def processor_uid():
    m = re.search(r'kFilterDrumProcessorUID\s*\(\s*0x([0-9A-Fa-f]{8})\s*,\s*0x([0-9A-Fa-f]{8})'
                  r'\s*,\s*0x([0-9A-Fa-f]{8})\s*,\s*0x([0-9A-Fa-f]{8})\s*\)',
                  read('source/FilterDrumIDs.h'))
    if not m:
        fail('could not find kFilterDrumProcessorUID in source/FilterDrumIDs.h')
    return ''.join(m.groups()).upper()


def state_version():
    m = re.search(r'kStateVersion\s*=\s*(\d+)\s*;', read('source/FilterDrumProcessor.cpp'))
    if not m:
        fail('could not find kStateVersion in source/FilterDrumProcessor.cpp')
    return int(m.group(1))


def param_count():
    m = re.search(r'static_assert\s*\(\s*kNumParams\s*==\s*(\d+)', read('source/FilterDrumParams.h'))
    if not m:
        fail('could not find the kNumParams static_assert in source/FilterDrumParams.h')
    return int(m.group(1))


def au_identity():
    path = os.path.join(ROOT, 'resource', 'au-info.plist')
    try:
        p = plistlib.load(open(path, 'rb'))
    except Exception as e:
        fail('cannot read resource/au-info.plist: %s' % e)
    comps = p.get('AudioComponents') or []
    if len(comps) != 1:
        fail('expected exactly one AudioComponents entry in au-info.plist, found %d' % len(comps))
    c = comps[0]

    def code(key):
        v = c.get(key, '')
        if not isinstance(v, str) or len(v) != 4:
            fail('au-info.plist %s is %r - expected four characters' % (key, v))
        return struct.unpack('>I', v.encode('ascii'))[0]

    return code('type'), code('subtype'), code('manufacturer')


# --- the .vstpreset -----------------------------------------------------------
def read_vstpreset(path):
    raw = open(path, 'rb').read()
    name = os.path.basename(path)
    if len(raw) < 48 or raw[:4] != b'VST3':
        fail('%s is not a VST3 preset file' % name)
    cid = raw[8:40].decode('ascii', 'replace').upper()
    (list_off,) = struct.unpack('<q', raw[40:48])
    if not (48 <= list_off <= len(raw) - 8) or raw[list_off:list_off + 4] != b'List':
        fail('%s has no chunk list where its header says' % name)
    (n,) = struct.unpack('<i', raw[list_off + 4:list_off + 8])
    chunks = {}
    for i in range(n):
        e = list_off + 8 + i * 20
        if e + 20 > len(raw):
            fail('%s: chunk list runs off the end of the file' % name)
        cid4 = raw[e:e + 4].decode('ascii', 'replace')
        off, size = struct.unpack('<qq', raw[e + 4:e + 20])
        if off < 0 or size < 0 or off + size > len(raw):
            fail('%s: chunk %s points outside the file' % (name, cid4))
        chunks[cid4] = raw[off:off + size]
    return cid, chunks


def check_state(name, comp, want_version, want_params):
    if len(comp) < 8:
        fail('%s: the Comp chunk is %d bytes - too short to be a FilterDrum state' % (name, len(comp)))
    version, count = struct.unpack('<ii', comp[:8])
    if version < 1 or version > want_version:
        fail('%s: state version %d, but this build writes and reads version %d. '
             'A newer preset than the build is refused by setState.' % (name, version, want_version))
    if count < 0:
        fail('%s: negative parameter count %d' % (name, count))
    body = 8 + 8 * count
    # The trailing bypass flag is optional by design - see setState.
    if len(comp) not in (body, body + 4):
        fail('%s: declares %d parameters (%d or %d bytes) but the Comp chunk is %d bytes'
             % (name, count, body, body + 4, len(comp)))
    notes = []
    if version < want_version:
        notes.append('state version %d, older than this build\'s %d' % (version, want_version))
    if count < want_params:
        notes.append('%d of %d parameters - the newest %d will arrive at their defaults'
                     % (count, want_params, want_params - count))
    elif count > want_params:
        notes.append('%d parameters, more than this build\'s %d - the extra are ignored'
                     % (count, want_params))
    return count, notes


# --- the .aupreset ------------------------------------------------------------
def aupreset_bytes(preset_name, comp, cont, au_type, au_subtype, au_manu):
    return plistlib.dumps({
        'Controller State': cont,
        'Processor State': comp,
        'manufacturer': au_manu,
        'name': preset_name,
        'subtype': au_subtype,
        'type': au_type,
        'version': 0,
    }, fmt=plistlib.FMT_XML, sort_keys=True)


def main():
    check_only = '--check' in sys.argv[1:]
    unknown = [a for a in sys.argv[1:] if a != '--check']
    if unknown:
        fail('unknown argument %s' % unknown[0])

    if not os.path.isdir(PRESETS):
        print('make-presets: no installer/presets folder - nothing to do')
        return 0

    uid = processor_uid()
    want_version = state_version()
    want_params = param_count()
    au_type, au_subtype, au_manu = au_identity()

    vst = sorted(f for f in os.listdir(PRESETS) if f.endswith('.vstpreset'))
    if not vst:
        print('make-presets: no .vstpreset files in installer/presets - nothing to do')
        return 0

    stale = []
    for f in vst:
        stem = f[:-len('.vstpreset')]
        cid, chunks = read_vstpreset(os.path.join(PRESETS, f))
        if cid != uid:
            fail('%s belongs to class %s, not FilterDrum\'s processor %s. '
                 'A host would ignore it; it was probably saved from another plug-in.'
                 % (f, cid, uid))
        if 'Comp' not in chunks:
            fail('%s has no Comp chunk, so there is no state to convert' % f)
        count, notes = check_state(f, chunks['Comp'], want_version, want_params)

        data = aupreset_bytes(stem, chunks['Comp'], chunks.get('Cont', b''),
                              au_type, au_subtype, au_manu)
        target = os.path.join(PRESETS, stem + '.aupreset')

        # Round trip: what was written must read back as the same state.
        back = plistlib.loads(data)
        if back['Processor State'] != chunks['Comp'] or back['subtype'] != au_subtype:
            fail('%s: the .aupreset did not read back as written' % f)

        if check_only:
            try:
                on_disk = open(target, 'rb').read()
            except OSError:
                on_disk = None
            if on_disk != data:
                stale.append(stem + '.aupreset' + (' (missing)' if on_disk is None else ' (stale)'))
            continue

        with open(target, 'wb') as out:
            out.write(data)
        line = '  %-28s %d parameters' % (stem + '.aupreset', count)
        print(line + (('  - ' + '; '.join(notes)) if notes else ''))

    orphans = sorted(f for f in os.listdir(PRESETS)
                     if f.endswith('.aupreset') and not os.path.exists(
                         os.path.join(PRESETS, f[:-len('.aupreset')] + '.vstpreset')))
    for o in orphans:
        sys.stderr.write('make-presets: note - %s has no .vstpreset beside it, so the VST3 '
                         'will not get that preset\n' % o)

    if check_only:
        if stale:
            sys.stderr.write('make-presets: out of date - run tools/make-presets.py:\n')
            for s in stale:
                sys.stderr.write('    %s\n' % s)
            return 1
        print('make-presets: %d .aupreset file(s) match their .vstpreset' % len(vst))
    return 0


if __name__ == '__main__':
    sys.exit(main())
