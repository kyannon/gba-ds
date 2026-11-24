#!/usr/bin/env python3
"""Xvfb's framebuffer dumps (.xwd, 24-bit in 32) to PNG: xwd2png.py FILE.xwd..."""
import struct, sys
from PIL import Image
for path in sys.argv[1:]:
    d = open(path, 'rb').read()
    hs, = struct.unpack('>I', d[:4])
    f = struct.unpack('>25I', d[:100])
    w, h, bpl, ncol = f[4], f[5], f[12], f[19]
    off = hs + ncol * 12
    im = Image.frombuffer('RGBA', (w, h), d[off:off + bpl * h], 'raw', 'BGRA', bpl, 1).convert('RGB')
    im.save(path[:-4] + '.png')
