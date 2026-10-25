#!/usr/bin/env python3
"""List the sprites on screen in a state: tools/oam.py ROM STATE.ss
(position, tile, palette, priority, shape/size), for writing SpriteRules
(a profile's hud.objs, captureHide...). Runs build/host/harness."""
import os, re, subprocess, sys, tempfile

rom, state = sys.argv[1], os.path.abspath(sys.argv[2])
d = tempfile.mkdtemp()
os.symlink(state, os.path.join(d, "s.ss"))
here = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run([os.path.join(here, "..", "build", "host", "harness"), rom, d],
                     input="load s\nwait 1\npeek 0x07000000 0x400\n", capture_output=True, text=True).stdout
b = bytes(int(x, 16) for m in re.finditer(r"^0700[0-9A-F]{4}:((?: [0-9A-F]{2})+)", out, re.M) for x in m.group(1).split())
for i in range(len(b) // 8):
    a0, a1, a2 = [b[i * 8 + k * 2] | b[i * 8 + k * 2 + 1] << 8 for k in range(3)]
    y, x = a0 & 0xFF, a1 & 0x1FF
    if (a0 & 0x300) == 0x200 or y == 160:
        continue  # hidden
    x, y = x - 512 if x >= 256 else x, y - 256 if y >= 160 else y
    print(f"{i:3} x {x:4} y {y:4} tile {a2 & 0x3FF:4} pal {a2 >> 12:2} prio {(a2 >> 10) & 3} shape {a0 >> 14} size {a1 >> 14}")
