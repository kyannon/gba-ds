#!/usr/bin/env python3
"""Gen 3 (FireRed / LeafGreen / Ruby / Sapphire / Emerald) battery saves as test material.

  saves.py info SAVE                 what a save holds: slots, trainer, play time, party
  saves.py clean IN OUT              a .sav, or a GameShark / Action Replay .sps / .gsv, as a 128 KB .sav
                                     with both save slots valid (a 64 KB file, as old emulators wrote,
                                     has no second slot: it is made an older copy of the first, or the
                                     game says "the save file is corrupted" before loading it)
  saves.py frlg-jp IN OUT            an international FireRed / LeafGreen save the Japanese ROMs accept
  saves.py state ROM SAVE OUT.ss     boot ROM with SAVE (the harness, build/host/harness) through the
                                     title and the "Continue" screens to the overworld, save a state

What works across releases (checked with the owner's saves, Nov 19, 2026): see "Real saves" in
docs/games/pokemon.md. The save is 14 sections of 4 KB in each of two slots (A: 0-13, B: 14-27); a
section's footer has its id, checksum (the sum of its first 3968 bytes' 32-bit words, folded to 16 bits;
sizes per section id), the signature 0x08012025 and the save counter, the newer slot being the
higher counter.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
from gen3 import ORDERS  # noqa: E402

SIZES = [3884, 3968, 3968, 3968, 3848, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 3968, 2000]
HARNESS = os.path.join(HERE, '..', '..', 'build', 'host', 'harness')


def checksum(sec, size):
    s = sum(struct.unpack_from('<I', sec, i)[0] for i in range(0, size & ~3, 4)) & 0xFFFFFFFF
    return ((s >> 16) + (s & 0xFFFF)) & 0xFFFF


def slots(sav):
    out = []
    for base in (0, 14):
        secs = {}
        for i in range(14):
            sec = sav[(base + i) * 4096:(base + i + 1) * 4096]
            sid, cs, sig, idx = struct.unpack_from('<HHII', sec, 0xFF4)
            if sig == 0x08012025 and sid < 14:
                secs[sid] = (sec, checksum(sec, SIZES[sid]) == cs, idx, base + i)
        out.append(dict(base=base, secs=secs, idx=max((v[2] for v in secs.values()), default=-1),
                        good=len(secs) == 14 and all(v[1] for v in secs.values())))
    return out


def latest(sav):
    s = slots(sav)
    good = [x for x in s if x['good']]
    return (max(good, key=lambda x: x['idx']) if good else None), s


def text(b):
    t = []
    for c in b:
        if c == 0xFF:
            break
        t.append(' ' if c == 0 else chr(65 + c - 0xBB) if 0xBB <= c <= 0xD4 else chr(97 + c - 0xD5) if 0xD5 <= c <= 0xEE
                 else str(c - 0xA1) if 0xA1 <= c <= 0xAA else '?')
    return ''.join(t)


def mon(rec):
    pid, otid = struct.unpack_from('<II', rec, 0)
    key = pid ^ otid
    data = bytes(x for w in struct.unpack_from('<12I', rec, 32) for x in struct.pack('<I', w ^ key))
    order = ORDERS[pid % 24]
    g = data[order.index('G') * 12:][:12]
    species, item, exp = struct.unpack_from('<HHI', g)
    return dict(species=species, level=rec[84], hp=struct.unpack_from('<H', rec, 86)[0], lang=rec[18],
                nick=text(rec[8:18]), ot=text(rec[20:27]))


def info(path):
    sav = open(path, 'rb').read()
    sl, allsl = latest(sav)
    print(path, len(sav), 'bytes')
    for s in allsl:
        print('  slot', 'AB'[s['base'] // 14], 'counter', s['idx'], 'valid' if s['good'] else 'NOT valid (%d sections)' % len(s['secs']))
    if not sl:
        return
    s0, s1 = sl['secs'][0][0], sl['secs'][1][0]
    tid, sid = struct.unpack_from('<HH', s0, 0xA)
    h, m = struct.unpack_from('<HB', s0, 0xE)
    frlg = struct.unpack_from('<I', s0, 0xAC)[0] == 1
    print('  trainer %s, id %d, %d:%02d played' % (text(s0[0:7]), tid, h, m), '(FireRed / LeafGreen layout)' if frlg else '')
    off = (0x34, 0x38) if frlg else (0x234, 0x238)
    for i in range(min(struct.unpack_from('<I', s1, off[0])[0], 6)):
        p = mon(s1[off[1] + i * 100:off[1] + (i + 1) * 100])
        print('    #%d Lv%d %s (ot %s, language %d)' % (p['species'], p['level'], p['nick'], p['ot'], p['lang']))
    if frlg:
        items = [struct.unpack_from('<H', s1, 0x3B8 + i * 4)[0] for i in range(30)]
        items = [i for i in items if i]
        print('  key items (item ids; the Town Map is 361, row %s):' % (items.index(361) if 361 in items else 'none'), items)


def clean(src, dst):
    d = open(src, 'rb').read()
    if src.lower().endswith(('.sps', '.gsv')):
        import sharkport
        d, code = sharkport.payload(d)
        if d is None:
            sys.exit('no save found in ' + src)
        print('game block', code)
    d = bytearray(d.ljust(131072, b'\xff')[:131072])
    sl, allsl = latest(bytes(d))
    if not sl:
        sys.exit('no valid save slot in ' + src)
    other = allsl[1 - sl['base'] // 14]
    if not other['good']:
        ob, sb = other['base'], sl['base']
        d[ob * 4096:(ob + 14) * 4096] = d[sb * 4096:(sb + 14) * 4096]
        for i in range(14):
            struct.pack_into('<I', d, (ob + i) * 4096 + 0xFFC, sl['idx'] - 1)
        print('the other slot rebuilt as an older copy')
    open(dst, 'wb').write(d)


def frlg_jp(src, dst):
    """The Japanese FireRed / LeafGreen ROMs treat an international save as no save at all unless the last
    80 bytes of section 4's data (a table of 12-byte records after offset 3768) are zero, as they are in a
    Japanese save. Found by swapping sections, then ranges, of an international save into a Japanese one."""
    d = bytearray(open(src, 'rb').read())
    for base in (0, 14):
        for i in range(14):
            o = (base + i) * 4096
            sid, _, sig, _ = struct.unpack_from('<HHII', d, o + 0xFF4)
            if sig == 0x08012025 and sid == 4:
                d[o + 3768:o + 3848] = b'\0' * 80
                struct.pack_into('<H', d, o + 0xFF6, checksum(bytes(d[o:o + 4096]), 3848))
    open(dst, 'wb').write(d)


def state(rom, sav, out, pre=500, gap=90):
    """Boots ROM with SAV and presses A, counting the presses by what's on screen: the Continue menu and
    FireRed / LeafGreen's "Previously on your quest" panel are blue / purple, the overworld is not."""
    from PIL import Image
    menu = [(79, 83, 143), (115, 107, 132), (66, 90, 107), (140, 148, 255)]
    work = out + '.work'
    os.makedirs(work, exist_ok=True)

    def run(presses, every, name):
        script = ['wait %d' % pre]
        for i in range(presses):
            script += ['tap A 1', 'wait %d' % gap]
            if (i + 1) % every == 0:
                script.append('shot s%02d' % (i + 1))
        script += ['wait 60', 'save final']
        open(work + '/' + name, 'w').write('\n'.join(script) + '\n')
        env = dict(os.environ, HARNESS_SAVE=work + '/battery.sav')
        open(work + '/battery.sav', 'wb').write(open(sav, 'rb').read())
        subprocess.run([HARNESS, rom, work, work + '/' + name], env=env, capture_output=True, check=True)

    def is_menu(p):
        b = Image.open(p).convert('RGB').tobytes()
        px = list(zip(b[0::3], b[1::3], b[2::3]))
        return sum(1 for q in px if any(all(abs(q[i] - m[i]) < 7 for i in range(3)) for m in menu)) / len(px) >= 0.10

    run(16, 1, 'probe.txt')
    flags = [is_menu('%s/s%02d.png' % (work, i)) for i in range(1, 17)]
    k = max([i for i, f in enumerate(flags) if f], default=-1) + 2
    if k < 3:
        sys.exit('the game never showed its Continue menu: the save was not accepted')
    run(k, k, 'final.txt')
    os.replace(work + '/final.ss', out)
    print('%d presses; state in %s (shot: %s/s%02d.png)' % (k, out, work, k))


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == 'info':
        info(a[1])
    elif len(a) == 3 and a[0] == 'clean':
        clean(a[1], a[2])
    elif len(a) == 3 and a[0] == 'frlg-jp':
        frlg_jp(a[1], a[2])
    elif len(a) == 4 and a[0] == 'state':
        state(a[1], a[2], a[3])
    else:
        sys.exit(__doc__)
