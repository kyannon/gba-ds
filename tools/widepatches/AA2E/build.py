#!/usr/bin/env python3
"""Super Mario Advance 2: Super Mario World (USA, AA2E) widescreen patch for GBA DS.

    build.py ROM [--out-dir widescreen/AA2E] [--patched OUT.gba] [--list]

ROM: the USA cartridge dump (CRC32 5206880a). Writes
"Super Mario Advance 2 (16-9).ips": the game's fixes below and the GBA DS
widescreen marker (tools/widescreen.py). Needs nothing but Python.

What the game already does (so the patch is small; docs/games/super-mario-advance-2.md, SMA-8):
- every level layer is a 512x512 map (BGxCNT size 3) streamed as on the SNES, whose
  screen was 256 wide, so the scenery is there well past the GBA's 240 columns;
- sprites spawn 0x120 pixels right of the camera (0x30 left), as on the SNES, and
  their tiles are drawn while their x is within [-32, 271] (most routines: x + 0x20
  against 0x12F), so the right margin (to x 261) already gets them.

What it changes:
- the left edge of those sprite tile checks, 32 pixels further left (x + A against
  L becomes x + A + 0x20 against L + 0x20; the right edge stays): a sprite leaving
  the screen to the left stays drawn until it's past the 22-pixel margin instead of
  vanishing in it. The checks are found in the code by their shape (find_culls); the
  ROM check below makes sure it's the dump they were found in, and the count is fixed;
- two of the despawn distances (the SubOffScreen tables at 0x080FF7D0, SMW's
  $01AC11 / $02FEC5 family): the one whose right edge is 256 (now 288) and the one
  whose left edge is -16 (now -48), so those sprites aren't removed inside a margin.
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import widescreen  # noqa: E402

ROM_CRC = 0x5206880A
B = 0x08000000
MARKER_AT = 0x3FFFE0          # (file offset) the last 32 bytes; zeros from 0x300670 to the end
NAME = "Super Mario Advance 2"
CULL_EDITS = 161              # halfwords find_culls changes in this dump
LIMITS = (0x11F, 0x12F, 0x13F, 0x14F, 0x15F)
# (address, old, new) signed halfwords in the despawn tables
DESPAWN = [(0x080FF7D4, 256, 288), (0x080FF7E2, -16, -48)]


def find_culls(rom):
    """The sprite cull checks that hide a sprite or tile a few pixels past the left
    edge, and the edits moving that edge 32 pixels left. A check is x + A compared
    unsigned with a limit L (L - A is the right edge), A 0x10 or 0x20:
        adds rN, #A ... ldr rY, =L ... cmp rX, rY ; bhi|bls
    or with x in a register's top half:
        movs rA, #0x80 ; lsls rA, rA, #13|14 ; adds rd, .., rA ... ldr rY, =L << 16 ; cmp ; bhi|bls
    with the added value reaching rX through shifts and moves only."""
    H = lambda a: struct.unpack("<H", rom[a - B:a - B + 2])[0]

    def loads(lit):
        return [(pc, (H(pc) >> 8) & 7) for pc in range(lit - 1024, lit, 2)
                if H(pc) & 0xF800 == 0x4800 and ((pc + 4) & ~3) + (H(pc) & 0xFF) * 4 == lit]

    edits = []
    for i in range(0, 0x200000, 4):
        v = struct.unpack("<I", rom[i:i + 4])[0]
        hi = v > 0xFFFF
        lim = v >> 16 if hi else v
        if lim not in LIMITS or (hi and v & 0xFFFF):
            continue
        lit = B + i
        ls = loads(lit)
        if len(ls) != 1:
            continue
        pc, ry = ls[0]
        c, br = H(pc + 2), H(pc + 4)
        if c & 0xFFC0 != 0x4280 or (c >> 3) & 7 != ry or br >> 8 not in (0xD8, 0xD9):
            continue
        rx = c & 7

        def flows(start, reg):
            for a in range(start + 2, pc + 2, 2):
                h = H(a)
                if a == pc:
                    continue
                if h < 0x1800 or h & 0xFFC0 == 0x1C00:        # shifts by #imm, mov (adds #0)
                    rd, rs = h & 7, (h >> 3) & 7
                    if rs == reg:
                        reg = rd
                    elif rd == reg:
                        return False
                elif h & 0xF800 in (0x2000, 0x3000, 0x3800):
                    if (h >> 8) & 7 == reg:
                        return False
                elif h & 0xF800 in (0x6800, 0x7800, 0x8800, 0x5800) or h & 0xFE00 in (0x1800, 0x1A00, 0x1C00, 0x1E00, 0x5A00, 0x5C00, 0x5E00):
                    if h & 7 == reg:
                        return False
                elif h & 0xFC00 == 0x4000:                     # ALU rd, rs (cmp/cmn/tst don't write)
                    if h & 7 == reg and (h >> 6) & 0xF not in (0x8, 0xA, 0xB):
                        return False
                elif h & 0xF000 in (0x8000, 0x7000, 0x6000, 0x9000) or h & 0xFE00 == 0x5000:
                    pass                                       # stores
                elif h & 0xFF00 == 0x4600:                     # mov with high registers
                    if (h & 7) | ((h >> 4) & 8) == reg:
                        return False
                else:
                    return False
            return reg == rx

        found = None
        for k in range(1, 11):
            a = pc - 2 * k
            h = H(a)
            if h & 0xF8FF in (0x3010, 0x3020) and flows(a, (h >> 8) & 7):
                found = [(a, h, h + 0x20)]
                break
            ra = h & 7
            if h & 0xFFC0 in (0x0340, 0x0380) and (h >> 3) & 7 == ra and H(a - 2) == 0x2000 | ra << 8 | 0x80:
                ok = False
                for b2 in range(a + 2, pc, 2):
                    g = H(b2)
                    if g & 0xFE00 == 0x1800 and ra in ((g >> 3) & 7, (g >> 6) & 7):
                        ok = flows(b2, g & 7)
                        break
                if not ok:
                    break
                if h & 0xFFC0 == 0x0380:                      # 0x80 << 14 -> 0x80 << 15
                    found = [(a, h, (h & ~0x07C0) | 15 << 6)]
                else:                                         # 0x80 << 13 -> 0xC0 << 14
                    found = [(a - 2, H(a - 2), 0x2000 | ra << 8 | 0xC0), (a, h, (h & ~0x07C0) | 14 << 6)]
                break
        if not found:
            continue
        edits += found
        edits.append((lit + 2, lim, lim + 0x20) if hi else (lit, lim, lim + 0x20))
    return edits


def build(rom):
    if zlib.crc32(rom) != ROM_CRC:
        sys.exit("not the USA cartridge dump of Super Mario Advance 2 (CRC32 5206880a)")
    if any(rom[MARKER_AT:MARKER_AT + 32]):
        sys.exit("the marker's place isn't empty")
    out = bytearray(rom)
    edits = find_culls(rom)
    if len(edits) != CULL_EDITS:
        sys.exit(f"found {len(edits)} cull edits, expected {CULL_EDITS}")
    for a, old, new in edits:
        o = a - B
        assert struct.unpack("<H", out[o:o + 2])[0] == old
        out[o:o + 2] = struct.pack("<H", new)
    for a, old, new in DESPAWN:
        o = a - B
        if struct.unpack("<h", out[o:o + 2])[0] != old:
            sys.exit(f"unexpected despawn value at 0x{a:08X}")
        out[o:o + 2] = struct.pack("<h", new)
    return bytes(out), edits


def ips_from(old, new):
    recs, i = [], 0
    while i < len(new):
        if new[i] == old[i]:
            i += 1
            continue
        j = i
        while j < len(new) and (new[j] != old[j] or (j + 1 < len(new) and new[j + 1] != old[j + 1])):
            j += 1
        recs.append((i, new[i:j], False))
        i = j
    return widescreen.ips_write(recs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "widescreen", "AA2E"))
    ap.add_argument("--patched", help="also write the patched ROM (16:9) here")
    ap.add_argument("--list", action="store_true", help="print each edit")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    new, edits = build(rom)
    if a.list:
        for at, old, nw in edits:
            print(f"0x{at:08X}: {old:04X} -> {nw:04X}")
    fixes = ips_from(rom, new)
    os.makedirs(a.out_dir, exist_ok=True)
    for ratio in ("16:9",):
        recs = widescreen.ips_records(fixes)
        recs.append((MARKER_AT, widescreen.marker(*widescreen.parse_ratio(ratio)), False))
        path = os.path.join(a.out_dir, f"{NAME} ({ratio.replace(':', '-')}).ips")
        open(path, "wb").write(widescreen.ips_write(recs))
        print(f"{path}: {ratio}, {'%d x %d' % widescreen.picture(*widescreen.parse_ratio(ratio))}")
    if a.patched:
        p = bytearray(new)
        p[MARKER_AT:MARKER_AT + 32] = widescreen.marker(16, 9)
        open(a.patched, "wb").write(p)
    print(f"{len(edits)} cull edits, {len(DESPAWN)} despawn distances")


if __name__ == "__main__":
    main()
