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
    widescreen.py index [--check]              widescreen/index.txt, the list
                                               the app offers downloads from
                                               (WS-6), made again from the
                                               folder (--check: says if it's
                                               out of date, changes nothing)

The repository's widescreen/ folder holds GBA DS's own patches, never put
in a release: widescreen/CODE/NAME (16-9).ips, and widescreen/CODE/dumps.txt
listing the ROM files they fit (CRC32s, one a line; without it, any ROM
with the code).
"""
import hashlib
import os
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


ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
INDEX_HEAD = ("# GBA DS widescreen patches (docs/WIDESCREEN.md): made by tools/widescreen.py index, don't edit.\n"
              "# code<TAB>ratio<TAB>file under widescreen/<TAB>SHA-256<TAB>the ROM files it fits (CRC32s, or -)\n")


def index_text(folder):
    lines = []
    for code in sorted(os.listdir(folder)):
        d = os.path.join(folder, code)
        if not os.path.isdir(d):
            continue
        if len(code) != 4:
            raise ValueError(f"widescreen/{code}: folders are named by the game's 4-letter code")
        crcs = []
        dumps = os.path.join(d, "dumps.txt")
        if os.path.exists(dumps):
            for line in open(dumps):
                line = line.split("#")[0].strip()
                if line:
                    crcs.append("%08x" % int(line, 16))
        for name in sorted(os.listdir(d)):
            if os.path.splitext(name)[1].lower() not in (".ips", ".ups", ".bps"):
                continue
            if "\t" in name or "\n" in name:
                raise ValueError(f"{name}: no tabs or line breaks in a name")
            data = open(os.path.join(d, name), "rb").read()
            m = find(data)
            if not m:
                raise ValueError(f"widescreen/{code}/{name}: no widescreen marker in it (a UPS hides it: use IPS or BPS)")
            lines.append("\t".join([code, f"{m[1]}:{m[2]}", f"{code}/{name}", hashlib.sha256(data).hexdigest(), ",".join(crcs) or "-"]))
    return INDEX_HEAD + "".join(l + "\n" for l in lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("size"); p.add_argument("ratio")
    p = sub.add_parser("marker"); p.add_argument("ratio")
    p = sub.add_parser("ips"); p.add_argument("offset"); p.add_argument("ratio")
    p.add_argument("-o", "--out", required=True); p.add_argument("--with", dest="fix")
    p = sub.add_parser("check"); p.add_argument("files", nargs="+")
    p = sub.add_parser("index"); p.add_argument("--check", action="store_true")
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
    elif a.cmd == "index":
        folder = os.path.join(ROOT, "widescreen")
        path = os.path.join(folder, "index.txt")
        text = index_text(folder)
        old = open(path).read() if os.path.exists(path) else ""
        if a.check:
            if old != text:
                sys.exit("widescreen/index.txt is out of date: run tools/widescreen.py index")
            print("widescreen/index.txt is up to date (%d patches)" % (text.count("\n") - 2))
        else:
            open(path, "w").write(text)
            print("widescreen/index.txt: %d patches" % (text.count("\n") - 2))


if __name__ == "__main__":
    main()
