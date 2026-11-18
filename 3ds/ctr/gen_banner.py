#!/usr/bin/env python3
"""The CIA's banner: banner.png (the logo on the app's blue, 256x128) and
banner.wav (a short two-note chime). Run in this folder; needs Pillow."""
import math
import struct
import wave

from PIL import Image

logo = Image.open('../../assets/brand/gba-ds-logo-1024.png').convert('RGBA')
banner = Image.new('RGBA', (256, 128), (0x10, 0x20, 0x60, 255))
w = 216
h = round(logo.height * w / logo.width)
banner.alpha_composite(logo.resize((w, h), Image.LANCZOS), ((256 - w) // 2, (128 - h) // 2))
banner.save('banner.png')

Image.open('../../assets/brand/gba-ds-icon.png').convert('RGB').resize((48, 48), Image.LANCZOS).save('icon.png')

rate = 32728
out = bytearray()
for i in range(int(rate * 1.2)):
    t = i / rate
    v = 0.0
    for start, freq in ((0.0, 659.25), (0.18, 987.77)):
        if t >= start:
            d = t - start
            v += math.sin(2 * math.pi * freq * d) * math.exp(-d * 5.0) * 0.35
    s = int(max(-1.0, min(1.0, v)) * 32767)
    out += struct.pack('<hh', s, s)
with wave.open('banner.wav', 'wb') as f:
    f.setnchannels(2)
    f.setsampwidth(2)
    f.setframerate(rate)
    f.writeframes(bytes(out))
