#!/usr/bin/env python3
"""GBA DS widescreen patches (docs/WIDESCREEN.md).

A GBA DS widescreen patch is an ordinary IPS patch that, besides whatever it
changes in the game (scenery loaded further out, sprites not culled at the
screen's edge), writes a 32-byte marker somewhere in the ROM that the game
never reads:

    0x00  "GBADS-WIDESCREEN"   16 bytes
    0x10  version              1
    0x11  ratio numerator      16
    0x12  ratio denominator    9
    0x13  flags                0
    0x14  picture width        u16 little-endian, 284
    0x16  picture height       u16 little-endian, 160
    0x18  reserved             8 bytes of 0

on a 16-byte boundary. GBA DS finds it in the patched ROM and draws the
picture that size, at that shape.

    widescreen.py size 16:9                    the picture for a ratio
    widescreen.py marker 16:9                  the marker's bytes, as hex
    widescreen.py ips OFFSET 16:9 -o OUT.ips [--with FIX.ips]
                                               a patch writing the marker at
                                               OFFSET (in the ROM file), with
                                               another IPS's records first
    widescreen.py check FILE...                a ROM's or a patch's marker
"""
import argparse
import struct
import sys

MAGIC = b"GBADS-WIDESCREEN"
GBA_W, GBA_H = 240, 160
MARGIN_MAX = 64  # emu.h WIDE_MARGIN_MAX


def picture(num, den):
    """The picture for a ratio: one axis widened, the other the GBA's own."""
    if num * GBA_H >= den * GBA_W:  # wider than 3:2
        w = round(GBA_H * num / den)
        w -= (w - GBA_W) % 2
        h = GBA_H
    else:
        h = round(GBA_W * den / num)
        h -= (h - GBA_H) % 2
        w = GBA_W
    if (w - GBA_W) // 2 > MARGIN_MAX or (h - GBA_H) // 2 > MARGIN_MAX:
        raise ValueError(f"{num}:{den} needs margins wider than {MARGIN_MAX} pixels")
    return w, h


def parse_ratio(s):
    a, _, b = s.partition(":")
    num, den = int(a), int(b)
    if not (0 < num < 256 and 0 < den < 256):
        raise ValueError(f"bad ratio {s}")
    return num, den


def marker(num, den):
    w, h = picture(num, den)
    return MAGIC + struct.pack("<BBBBHH8x", 1, num, den, 0, w, h)


def ips_records(data):
    if not data.startswith(b"PATCH"):
        raise ValueError("not an IPS patch")
    i, out = 5, []
    while True:
        off = data[i:i + 3]
        if off == b"EOF":
            return out
        o = int.from_bytes(off, "big")
        n = int.from_bytes(data[i + 3:i + 5], "big")
        i += 5
        if n == 0:
            rn = int.from_bytes(data[i:i + 2], "big")
            out.append((o, data[i + 2:i + 3] * rn, True))
            i += 3
        else:
            out.append((o, data[i:i + n], False))
            i += n


def ips_write(records):
    out = bytearray(b"PATCH")
    for o, d, rle in records:
        if o == 0x454F46:
            raise ValueError("IPS can't write at 0x454F46 ('EOF'): move the marker")
        if o > 0xFFFFFF:
            raise ValueError("IPS can't reach past 16 MB: put the marker lower")
        out += o.to_bytes(3, "big")
        if rle:
            out += b"\0\0" + len(d).to_bytes(2, "big") + d[:1]
        else:
            out += len(d).to_bytes(2, "big") + d
    return bytes(out + b"EOF")


def find(data):
    """The marker in a ROM or in a patch's literal bytes (IPS, BPS)."""
    at = data.find(MAGIC)
    while at >= 0:
        b = data[at:at + 32]
        if len(b) == 32:
            ver, num, den, _flags, w, h = struct.unpack("<BBBBHH", b[16:24])
            if ver == 1 and num and den:
                return at, num, den, w, h
        at = data.find(MAGIC, at + 1)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("size"); p.add_argument("ratio")
    p = sub.add_parser("marker"); p.add_argument("ratio")
    p = sub.add_parser("ips"); p.add_argument("offset"); p.add_argument("ratio")
    p.add_argument("-o", "--out", required=True); p.add_argument("--with", dest="fix")
    p = sub.add_parser("check"); p.add_argument("files", nargs="+")
    a = ap.parse_args()
    if a.cmd == "size":
        print("%d x %d" % picture(*parse_ratio(a.ratio)))
    elif a.cmd == "marker":
        print(marker(*parse_ratio(a.ratio)).hex())
    elif a.cmd == "ips":
        off = int(a.offset, 0)
        if off % 16:
            sys.exit("the marker goes on a 16-byte boundary")
        recs = ips_records(open(a.fix, "rb").read()) if a.fix else []
        recs.append((off, marker(*parse_ratio(a.ratio)), False))
        open(a.out, "wb").write(ips_write(recs))
        print(f"{a.out}: {a.ratio}, {'%d x %d' % picture(*parse_ratio(a.ratio))}, marker at 0x{off:X}")
    elif a.cmd == "check":
        bad = 0
        for f in a.files:
            r = find(open(f, "rb").read())
            if r:
                print(f"{f}: {r[1]}:{r[2]}, {r[3]} x {r[4]} (marker at byte {r[0]})")
            else:
                print(f"{f}: no widescreen marker")
                bad = 1
        sys.exit(bad)


if __name__ == "__main__":
    main()
