#!/usr/bin/env python3
"""GameShark / Action Replay "SharkPortSave" files (.sps, .gsv) to battery
saves GBA DS (mGBA) loads: tools/sharkport.py IN.sps [OUT.sav].

The container: a length-prefixed "SharkPortSave", the platform, then
length-prefixed title, date and notes, the payload's size, the payload (a
0x1C-byte game block, with the game's code, then the save) and a checksum.
Some files on GameFAQs have hand-edited headers (a renamed title whose date
length was zeroed, no checksum), so the save is taken as the 512, 8 KB,
32 KB, 64 KB or 128 KB right before the checksum (or at the very end) that
the game block sits in front of.

EEPROM saves (512 bytes or 8 KB) come in different byte orders: mGBA's .sav
has each 8-byte block reversed from the order the game sees. Give
--magic BYTES (what the game's own data starts with, in its order, such
as MALRPG for Mario & Luigi) and the save is put in mGBA's order whatever
order the file had (as is, its 4-byte halves swapped, or reversed).
Without --magic, the save is written as stored.
"""
import argparse
import struct
import sys

SIZES = (512, 8192, 32768, 65536, 131072)


def payload(d):
    """the save and its game block's code (4 chars)"""
    for size in sorted(SIZES, reverse=True):
        for end in (len(d) - 4, len(d)):
            start = end - size
            if start < 0x1C + 0x11:
                continue
            block = d[start - 0x1C:start]
            # the game block: the 12-char title, then the 4-char game code
            # (only the right end puts it there: files with no checksum
            # end at the save)
            code = block[12:16]
            if all(65 <= c <= 90 or 48 <= c <= 57 for c in code):
                return d[start:end], code.decode()
    return None, None


def to_mgba(save, magic):
    if len(save) not in (512, 8192) or not magic:
        return save
    m = magic.encode()
    rev = lambda b: b''.join(b[i:i + 8][::-1] for i in range(0, len(b), 8))
    swap = lambda b: b''.join(b[i + 4:i + 8] + b[i:i + 4] for i in range(0, len(b), 8))
    for plain in (save, swap(save), rev(save)):
        if plain.find(m) in range(0, 16):
            return rev(plain)
    sys.exit(f"the game's data ({magic}) isn't at the start in any byte order")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('sps')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--magic', help="what the game's save starts with, in the game's order (EEPROM saves)")
    a = ap.parse_args()
    d = open(a.sps, 'rb').read()
    if d[4:17] != b'SharkPortSave':
        sys.exit('not a SharkPortSave file')
    save, code = payload(d)
    if save is None:
        sys.exit('no save of a known size found')
    if len(save) == 65536 and a.magic and save[:8][::-1].find(a.magic.encode()) >= 0:
        save = save[:8192]  # (an 8 KB EEPROM save padded to 64 KB by the dumper)
    out = to_mgba(save, a.magic)
    path = a.out or a.sps.rsplit('.', 1)[0] + '.sav'
    open(path, 'wb').write(out)
    print(f'{path}: {len(out)} bytes, game {code}')


if __name__ == '__main__':
    main()
