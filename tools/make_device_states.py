#!/usr/bin/env python3
"""Make the save states baked into a final dev test zip.

    tools/make_device_states.py [--games slug,slug,...] [--manifest FILE] [--out DIR] [--fixtures DIR]

Reads tools/device-states.json (per game: each release's No-Intro name, slots
1-9 -> fixture state + what to check), copies each fixture from
gba-ds-tests/states/<game>/ to OUT/<release name>.slotN.state (the name the
app looks for beside a ROM of that name; a name "NAME + PATCH" is NAME's game
with that patch switched on in Extras > Patches, whose states are its own) and writes OUT/TEST-STATES.txt, the
table of what each slot is for. scripts/build-rocknix.sh puts OUT in the zip's
states/ folder when GBADS_DEVICE_STATES=OUT (dev versions only).

Checks, and stops on a failure: every release name is in src/dumps_data.h, every
fixture exists, slots are 1-9, no two games write the same file. Without
--games every game in the manifest is made.
"""
import argparse, json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def dump_names():
    names = set()
    for line in open(os.path.join(ROOT, 'src', 'dumps_data.h'), encoding='utf-8'):
        m = re.match(r'\s*\{\s*"[A-Z0-9]{4}",\s*0x[0-9a-f]+,\s*(?:true|false),\s*(?:true|false),\s*"([^"]+)"\s*\}', line)
        if m:
            names.add(m.group(1))
    return names

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--games', help='comma-separated game slugs (default: all in the manifest)')
    ap.add_argument('--manifest', default=os.path.join(ROOT, 'tools', 'device-states.json'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'device-states'))
    ap.add_argument('--fixtures', default=os.environ.get('GBADS_TESTS') or os.path.join(ROOT, '..', 'gba-ds-tests'))
    a = ap.parse_args()
    man = json.load(open(a.manifest, encoding='utf-8'))['games']
    want = a.games.split(',') if a.games else list(man)
    names = dump_names()
    bad = 0
    for g in want:
        if g not in man:
            print(f'{g}: not in tools/device-states.json'); bad += 1
    if bad:
        return 1
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    table, seen = [], set()
    for g in want:
        for rom, slots in man[g].items():
            # "NAME + PATCH": the states of NAME with that patch switched on in
            # Extras > Patches (a patched game's own states, paths.c game_paths_patched)
            base = rom.split(' + ')[0]
            if base not in names:
                print(f'{g}: "{base}" is not a name in src/dumps_data.h'); bad += 1
            for slot, (fx, note) in slots.items():
                if not re.fullmatch(r'[1-9]', slot):
                    print(f'{g}: {rom}: slot {slot} (use 1-9)'); bad += 1; continue
                src = os.path.join(a.fixtures, 'states', g, fx)
                if not os.path.isfile(src):
                    print(f'{g}: missing fixture {src}'); bad += 1; continue
                dst = f'{rom}.slot{slot}.state'
                if dst in seen:
                    print(f'{dst} twice'); bad += 1; continue
                seen.add(dst)
                shutil.copyfile(src, os.path.join(a.out, dst))
                table.append((rom, int(slot), note))
    if bad:
        return 1
    with open(os.path.join(a.out, 'TEST-STATES.txt'), 'w', encoding='utf-8') as f:
        f.write('Test states for this build. Each file is <game name>.slotN.state: it shows in the\n'
                'pause menu\'s Load State slots when your ROM is named as the game is (No-Intro\n'
                'names, as below, without .gba / .zip). If your ROM has another name, rename the\n'
                'state files to match it. A name with " + PATCH" is that game with the patch\n'
                'switched on in Extras > Patches (its states kept apart, as by default).\n\n'
                'Unzipping over an install replaces these slot files for these games only.\n'
                '"After slot 1" means: load slot 1, wait for the second screen to settle, then\n'
                'load the state named.\n')
        last = None
        for rom, slot, note in sorted(table):
            if rom != last:
                f.write(f'\n{rom}\n')
                last = rom
            f.write(f'  slot {slot}: {note}\n')
    print(f'{len(table)} states for {len(want)} games in {a.out}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
