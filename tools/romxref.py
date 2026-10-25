#!/usr/bin/env python3
"""Map RAM addresses from one ROM to another (a regional release, a sequel on
the same engine) by matching the Thumb code that loads them.

  tools/romxref.py FROM.gba TO.gba ADDR... [--win 16] [--range 24]

For each ADDR, every 4-byte-aligned literal holding it in FROM is found, then
the `ldr rX, [pc, #n]` instructions that load it; code around each load
(--win bytes, skipping the load itself, within --range bytes of it) that
appears exactly once in TO points at the matching load there, whose literal
is the answer. Prints ADDR -> {candidate: votes}. Shorter windows and a wider
range find more (and noisier) matches; an empty result means the code
differs: look at the literal pools nearby instead (a pool in TO holding the
neighbours' mapped addresses), or watch RAM change (tools/ramdiff.py).
"""
import argparse, struct

ap = argparse.ArgumentParser()
ap.add_argument("src"); ap.add_argument("dst"); ap.add_argument("addr", nargs="+")
ap.add_argument("--win", type=int, default=16); ap.add_argument("--range", type=int, default=24)
a = ap.parse_args()
r1, r2 = open(a.src, "rb").read(), open(a.dst, "rb").read()


def loads(rom, lit):
    out = []
    for pc in range(max(0, lit - 1024), lit, 2):
        h = struct.unpack_from("<H", rom, pc)[0]
        if h >> 11 == 9 and ((pc + 4) & ~3) + (h & 0xFF) * 4 == lit:
            out.append(pc)
    return out


for s in a.addr:
    val = int(s, 0)
    pat, votes, i = struct.pack("<I", val), {}, r1.find(struct.pack("<I", val))
    while i >= 0:
        if i % 4 == 0:
            for u in loads(r1, i):
                for st in range(u - a.range, u + a.range, 2):
                    if st <= u < st + a.win:
                        continue
                    chunk = r1[st:st + a.win]
                    if r2.count(chunk) != 1:
                        continue
                    u2 = r2.find(chunk) + (u - st)
                    h = struct.unpack_from("<H", r2, u2)[0]
                    if h >> 11 == 9:
                        v = struct.unpack_from("<I", r2, ((u2 + 4) & ~3) + (h & 0xFF) * 4)[0]
                        votes[hex(v)] = votes.get(hex(v), 0) + 1
        i = r1.find(pat, i + 1)
    print(hex(val), "->", dict(sorted(votes.items(), key=lambda kv: -kv[1])))
