#!/usr/bin/env python3
"""GBA DS's pixel font: Jost's forms (round o c e d b, a single-storey a and g,
pointed A M N V W v w y, a short ascender) drawn on a pixel grid, one pixel
thick, proportional, with lower case; two sizes. The quick bar draws its text
with it at the GBA's own resolution (game.c, cv_label / cv_message).

This file is the source of the glyphs. `make_pixfont.py` writes src/pixfont.c;
`--preview FILE.png` writes a proof sheet. Edit the pictures below ('X' is a
pixel) and run it again.

Rows of a glyph are given from its top; the KIND says where the top is:
  C   a capital or digit (cap rows)        A   an ascender (cap rows)
  X   x-height rows                        D   x-height rows + the descender
  AD  cap rows + the descender (j)         P   free: rows as given, kind 'P<top>'
"""
import os, sys

SIZES = {
    # name: cap, x-height, descender, space, slant (pixels up the cap height per pixel across: 0 = upright)
    'small': dict(cap=6, xh=4, desc=2, space=3, slant=0.0),
    'large': dict(cap=7, xh=5, desc=2, space=4, slant=0.0),
}

L = {}  # large
S = {}  # small


def g(table, ch, kind, pic):
    rows = [r for r in pic.split('\n')]
    w = {len(r) for r in rows}
    assert len(w) == 1, (ch, rows)
    table[ch] = (kind, rows)


# ---------------------------------------------------------------- large: cap 7, x-height 5, descender 2
for ch, kind, pic in [
    ('A', 'C', "..X..\n.X.X.\n.X.X.\nX...X\nXXXXX\nX...X\nX...X"),
    ('B', 'C', "XXXX.\nX...X\nX...X\nXXXX.\nX...X\nX...X\nXXXX."),
    ('C', 'C', ".XXXX.\nX....X\nX.....\nX.....\nX.....\nX....X\n.XXXX."),
    ('D', 'C', "XXXX..\nX...X.\nX....X\nX....X\nX....X\nX...X.\nXXXX.."),
    ('E', 'C', "XXXX\nX...\nX...\nXXX.\nX...\nX...\nXXXX"),
    ('F', 'C', "XXXX\nX...\nX...\nXXX.\nX...\nX...\nX..."),
    ('G', 'C', ".XXXX.\nX....X\nX.....\nX..XXX\nX....X\nX....X\n.XXXX."),
    ('H', 'C', "X...X\nX...X\nX...X\nXXXXX\nX...X\nX...X\nX...X"),
    ('I', 'C', "X\nX\nX\nX\nX\nX\nX"),
    ('J', 'C', "...X\n...X\n...X\n...X\nX..X\nX..X\n.XX."),
    ('K', 'C', "X...X\nX..X.\nX.X..\nXX...\nX.X..\nX..X.\nX...X"),
    ('L', 'C', "X...\nX...\nX...\nX...\nX...\nX...\nXXXX"),
    ('M', 'C', "X.....X\nXX...XX\nX.X.X.X\nX..X..X\nX.....X\nX.....X\nX.....X"),
    ('N', 'C', "X....X\nXX...X\nX.X..X\nX..X.X\nX...XX\nX....X\nX....X"),
    ('O', 'C', ".XXXX.\nX....X\nX....X\nX....X\nX....X\nX....X\n.XXXX."),
    ('P', 'C', "XXXX.\nX...X\nX...X\nXXXX.\nX....\nX....\nX...."),
    ('Q', 'C', ".XXXX.\nX....X\nX....X\nX....X\nX..X.X\nX...X.\n.XXX.X"),
    ('R', 'C', "XXXX.\nX...X\nX...X\nXXXX.\nX.X..\nX..X.\nX...X"),
    ('S', 'C', ".XXXX\nX....\nX....\n.XXX.\n....X\n....X\nXXXX."),
    ('T', 'C', "XXXXX\n..X..\n..X..\n..X..\n..X..\n..X..\n..X.."),
    ('U', 'C', "X...X\nX...X\nX...X\nX...X\nX...X\nX...X\n.XXX."),
    ('V', 'C', "X...X\nX...X\nX...X\n.X.X.\n.X.X.\n.X.X.\n..X.."),
    ('W', 'C', "X.....X\nX.....X\nX..X..X\nX..X..X\n.X.X.X.\n.X.X.X.\n..X.X.."),
    ('X', 'C', "X...X\n.X.X.\n.X.X.\n..X..\n.X.X.\n.X.X.\nX...X"),
    ('Y', 'C', "X...X\nX...X\n.X.X.\n..X..\n..X..\n..X..\n..X.."),
    ('Z', 'C', "XXXXX\n....X\n...X.\n..X..\n.X...\nX....\nXXXXX"),
    ('0', 'C', ".XXX.\nX...X\nX...X\nX...X\nX...X\nX...X\n.XXX."),
    ('1', 'C', ".X\nXX\n.X\n.X\n.X\n.X\n.X"),
    ('2', 'C', ".XXX.\nX...X\n....X\n...X.\n..X..\n.X...\nXXXXX"),
    ('3', 'C', "XXXX.\n....X\n....X\n.XXX.\n....X\n....X\nXXXX."),
    ('4', 'C', "...X.\n..XX.\n.X.X.\nX..X.\nXXXXX\n...X.\n...X."),
    ('5', 'C', "XXXXX\nX....\nX....\nXXXX.\n....X\nX...X\n.XXX."),
    ('6', 'C', ".XXX.\nX....\nX....\nXXXX.\nX...X\nX...X\n.XXX."),
    ('7', 'C', "XXXXX\n....X\n...X.\n...X.\n..X..\n..X..\n..X.."),
    ('8', 'C', ".XXX.\nX...X\nX...X\n.XXX.\nX...X\nX...X\n.XXX."),
    ('9', 'C', ".XXX.\nX...X\nX...X\n.XXXX\n....X\n....X\n.XXX."),
    # lower case
    ('a', 'X', ".XXXX\nX...X\nX...X\nX...X\n.XXXX"),
    ('b', 'A', "X....\nX....\nXXXX.\nX...X\nX...X\nX...X\nXXXX."),
    ('c', 'X', ".XXX\nX...\nX...\nX...\n.XXX"),
    ('d', 'A', "....X\n....X\n.XXXX\nX...X\nX...X\nX...X\n.XXXX"),
    ('e', 'X', ".XXX.\nX...X\nXXXXX\nX....\n.XXXX"),
    ('f', 'A', ".XX\nX..\nXXX\nX..\nX..\nX..\nX.."),
    ('g', 'D', ".XXXX\nX...X\nX...X\nX...X\n.XXXX\n....X\nXXXX."),
    ('h', 'A', "X....\nX....\nXXXX.\nX...X\nX...X\nX...X\nX...X"),
    ('i', 'A', "X\n.\nX\nX\nX\nX\nX"),
    ('j', 'AD', ".X\n..\n.X\n.X\n.X\n.X\n.X\n.X\nX."),
    ('k', 'A', "X...\nX...\nX..X\nX.X.\nXX..\nX.X.\nX..X"),
    ('l', 'A', "X\nX\nX\nX\nX\nX\nX"),
    ('m', 'X', "XXXXXX.\nX..X..X\nX..X..X\nX..X..X\nX..X..X"),
    ('n', 'X', "XXXX.\nX...X\nX...X\nX...X\nX...X"),
    ('o', 'X', ".XXX.\nX...X\nX...X\nX...X\n.XXX."),
    ('p', 'D', "XXXX.\nX...X\nX...X\nX...X\nXXXX.\nX....\nX...."),
    ('q', 'D', ".XXXX\nX...X\nX...X\nX...X\n.XXXX\n....X\n....X"),
    ('r', 'X', "X.XX\nXX..\nX...\nX...\nX..."),
    ('s', 'X', ".XXX\nX...\n.XX.\n...X\nXXX."),
    ('t', 'A', "...\nX..\nXXX\nX..\nX..\nX..\n.XX"),
    ('u', 'X', "X...X\nX...X\nX...X\nX...X\n.XXXX"),
    ('v', 'X', "X...X\nX...X\n.X.X.\n.X.X.\n..X.."),
    ('w', 'X', "X.....X\nX..X..X\nX..X..X\n.X.X.X.\n..X.X.."),
    ('x', 'X', "X...X\n.X.X.\n..X..\n.X.X.\nX...X"),
    ('y', 'D', "X...X\nX...X\n.X.X.\n..X..\n..X..\n.X...\nX...."),
    ('z', 'X', "XXXX\n...X\n..X.\n.X..\nXXXX"),
    # punctuation
    ('.', 'P6', "X"),
    (',', 'P6', "X\nX"),
    (':', 'P3', "X\n.\n.\nX"),
    (';', 'P3', "X\n.\n.\nX\nX"),
    ('!', 'P0', "X\nX\nX\nX\nX\n.\nX"),
    ('?', 'P0', ".XXX.\nX...X\n....X\n...X.\n..X..\n.....\n..X.."),
    ("'", 'P0', "X\nX"),
    ('"', 'P0', "X.X\nX.X"),
    ('-', 'P4', "XXX"),
    ('+', 'P3', ".X.\nXXX\n.X."),
    ('=', 'P3', "XXXXX\n.....\nXXXXX"),
    ('/', 'P0', "..X\n..X\n.X.\n.X.\n.X.\nX..\nX.."),
    ('(', 'P0', ".X\nX.\nX.\nX.\nX.\nX.\n.X"),
    (')', 'P0', "X.\n.X\n.X\n.X\n.X\n.X\nX."),
    ('%', 'P0', "XX..X\nXX.X.\n...X.\n..X..\n.X...\n.X.XX\nX..XX"),
    ('_', 'P8', "XXXXX"),
    (' ', 'P0', ""),
]:
    if ch == ' ':
        continue
    g(L, ch, kind, pic)

# ---------------------------------------------------------------- small: cap 6, x-height 4, descender 2
for ch, kind, pic in [
    ('A', 'C', "..X..\n.X.X.\nX...X\nXXXXX\nX...X\nX...X"),
    ('B', 'C', "XXX.\nX..X\nXXX.\nX..X\nX..X\nXXX."),
    ('C', 'C', ".XXX.\nX...X\nX....\nX....\nX...X\n.XXX."),
    ('D', 'C', "XXX..\nX..X.\nX...X\nX...X\nX..X.\nXXX.."),
    ('E', 'C', "XXXX\nX...\nXXX.\nX...\nX...\nXXXX"),
    ('F', 'C', "XXXX\nX...\nXXX.\nX...\nX...\nX..."),
    ('G', 'C', ".XXX.\nX...X\nX....\nX.XXX\nX...X\n.XXX."),
    ('H', 'C', "X...X\nX...X\nXXXXX\nX...X\nX...X\nX...X"),
    ('I', 'C', "X\nX\nX\nX\nX\nX"),
    ('J', 'C', "...X\n...X\n...X\n...X\nX..X\n.XX."),
    ('K', 'C', "X..X\nX.X.\nXX..\nX.X.\nX..X\nX..X"),
    ('L', 'C', "X...\nX...\nX...\nX...\nX...\nXXXX"),
    ('M', 'C', "X...X\nXX.XX\nX.X.X\nX...X\nX...X\nX...X"),
    ('N', 'C', "X...X\nXX..X\nX.X.X\nX..XX\nX...X\nX...X"),
    ('O', 'C', ".XXX.\nX...X\nX...X\nX...X\nX...X\n.XXX."),
    ('P', 'C', "XXX.\nX..X\nX..X\nXXX.\nX...\nX..."),
    ('Q', 'C', ".XXX.\nX...X\nX...X\nX.X.X\nX..X.\n.XX.X"),
    ('R', 'C', "XXX.\nX..X\nXXX.\nX.X.\nX..X\nX..X"),
    ('S', 'C', ".XXX\nX...\n.XX.\n...X\n...X\nXXX."),
    ('T', 'C', "XXXXX\n..X..\n..X..\n..X..\n..X..\n..X.."),
    ('U', 'C', "X...X\nX...X\nX...X\nX...X\nX...X\n.XXX."),
    ('V', 'C', "X...X\nX...X\nX...X\n.X.X.\n.X.X.\n..X.."),
    ('W', 'C', "X.....X\nX.....X\nX..X..X\n.X.X.X.\n.X.X.X.\n..X.X.."),
    ('X', 'C', "X...X\n.X.X.\n..X..\n..X..\n.X.X.\nX...X"),
    ('Y', 'C', "X...X\n.X.X.\n..X..\n..X..\n..X..\n..X.."),
    ('Z', 'C', "XXXXX\n...X.\n..X..\n.X...\nX....\nXXXXX"),
    ('0', 'C', ".XX.\nX..X\nX..X\nX..X\nX..X\n.XX."),
    ('1', 'C', ".X\nXX\n.X\n.X\n.X\n.X"),
    ('2', 'C', ".XX.\nX..X\n...X\n..X.\n.X..\nXXXX"),
    ('3', 'C', "XXX.\n...X\n.XX.\n...X\n...X\nXXX."),
    ('4', 'C', "..X.\n.XX.\nX.X.\nXXXX\n..X.\n..X."),
    ('5', 'C', "XXXX\nX...\nXXX.\n...X\nX..X\n.XX."),
    ('6', 'C', ".XX.\nX...\nXXX.\nX..X\nX..X\n.XX."),
    ('7', 'C', "XXXX\n...X\n..X.\n..X.\n.X..\n.X.."),
    ('8', 'C', ".XX.\nX..X\n.XX.\nX..X\nX..X\n.XX."),
    ('9', 'C', ".XX.\nX..X\nX..X\n.XXX\n...X\n.XX."),
    ('a', 'X', ".XXX\nX..X\nX..X\n.XXX"),
    ('b', 'A', "X...\nX...\nXXX.\nX..X\nX..X\nXXX."),
    ('c', 'X', ".XX\nX..\nX..\n.XX"),
    ('d', 'A', "...X\n...X\n.XXX\nX..X\nX..X\n.XXX"),
    ('e', 'X', ".XX.\nXXXX\nX...\n.XXX"),
    ('f', 'A', ".XX\nX..\nXXX\nX..\nX..\nX.."),
    ('g', 'D', ".XXX\nX..X\nX..X\n.XXX\n...X\nXXX."),
    ('h', 'A', "X...\nX...\nXXX.\nX..X\nX..X\nX..X"),
    ('i', 'A', "X\n.\nX\nX\nX\nX"),
    ('j', 'AD', ".X\n..\n.X\n.X\n.X\n.X\n.X\nX."),
    ('k', 'A', "X...\nX...\nX.X.\nXX..\nX.X.\nX..X"),
    ('l', 'A', "X\nX\nX\nX\nX\nX"),
    ('m', 'X', "XXXX.\nX.X.X\nX.X.X\nX.X.X"),
    ('n', 'X', "XXX.\nX..X\nX..X\nX..X"),
    ('o', 'X', ".XX.\nX..X\nX..X\n.XX."),
    ('p', 'D', "XXX.\nX..X\nX..X\nXXX.\nX...\nX..."),
    ('q', 'D', ".XXX\nX..X\nX..X\n.XXX\n...X\n...X"),
    ('r', 'X', "X.XX\nXX..\nX...\nX..."),
    ('s', 'X', ".XX\nX..\n..X\nXX."),
    ('t', 'A', "...\n.X.\nXXX\n.X.\n.X.\n..X"),
    ('u', 'X', "X..X\nX..X\nX..X\n.XXX"),
    ('v', 'X', "X...X\nX...X\n.X.X.\n..X.."),
    ('w', 'X', "X...X\nX.X.X\nX.X.X\n.X.X."),
    ('x', 'X', "X.X\n.X.\n.X.\nX.X"),
    ('y', 'D', "X...X\nX...X\n.X.X.\n..X..\n.X...\nX...."),
    ('z', 'X', "XXXX\n..X.\n.X..\nXXXX"),
    ('.', 'P5', "X"),
    (',', 'P5', "X\nX"),
    (':', 'P3', "X\n.\nX"),
    (';', 'P3', "X\n.\nX\nX"),
    ('!', 'P0', "X\nX\nX\nX\n.\nX"),
    ('?', 'P0', ".XX.\nX..X\n..X.\n.X..\n....\n.X.."),
    ("'", 'P0', "X\nX"),
    ('"', 'P0', "X.X\nX.X"),
    ('-', 'P3', "XXX"),
    ('+', 'P2', ".X.\nXXX\n.X."),
    ('=', 'P2', "XXXX\n....\nXXXX"),
    ('/', 'P0', "..X\n..X\n.X.\n.X.\nX..\nX.."),
    ('(', 'P0', ".X\nX.\nX.\nX.\nX.\n.X"),
    (')', 'P0', "X.\n.X\n.X\n.X\n.X\nX."),
    ('%', 'P0', "XX..X\nXX.X.\n...X.\n..X..\n.X.XX\nX..XX"),
    ('_', 'P7', "XXXX"),
]:
    g(S, ch, kind, pic)

if os.environ.get('PF_SLANT'):  # to look at a slanted set: PF_SLANT=0.2
    for _p in SIZES.values():
        _p['slant'] = float(os.environ['PF_SLANT'])
TABLES = {'small': S, 'large': L}


def place(name, ch, kind, rows):
    """-> (list of full-height row strings, height)"""
    p = SIZES[name]
    cap, xh, desc = p['cap'], p['xh'], p['desc']
    h = cap + desc
    w = len(rows[0])
    n = len(rows)
    if kind in ('C', 'A'):
        top, need = 0, cap
    elif kind == 'X':
        top, need = cap - xh, xh
    elif kind == 'D':
        top, need = cap - xh, xh + desc
    elif kind == 'AD':
        top, need = 0, cap + desc
    else:
        top, need = int(kind[1:]), n
    assert n == need, (name, ch, kind, n, need)
    full = ['.' * w] * h
    for i, r in enumerate(rows):
        full[top + i] = r
    return full


def slanted(name, full):
    sl = SIZES[name]['slant']
    if not sl:
        return full
    cap = SIZES[name]['cap']
    out = []
    for r, row in enumerate(full):
        hidx = (cap - 1) - r  # pixels above the baseline row (negative below)
        dx = int(sl * max(hidx, 0) + 0.5)
        out.append('.' * dx + row)
    w = max(len(r) for r in out)
    return [r + '.' * (w - len(r)) for r in out]


def build(name):
    p = SIZES[name]
    h = p['cap'] + p['desc']
    glyphs = {}
    for ch, (kind, rows) in TABLES[name].items():
        full = slanted(name, place(name, ch, kind, rows))
        # trim empty columns on the right (a slant leaves them) but never to nothing
        w = max(len(r.rstrip('.')) for r in full)
        w = max(w, 1)
        glyphs[ch] = [r[:w].ljust(w, '.') for r in full]
    return h, glyphs


def emit(path):
    out = ['/* GENERATED by tools/make_pixfont.py: do not edit (the glyphs are drawn there). */',
           '#include "pixfont.h"', '']
    for name in ('small', 'large'):
        p = SIZES[name]
        h, glyphs = build(name)
        out.append(f'const PixFont pixfont_{name} = {{')
        out.append(f'\t.cap = {p["cap"]}, .xh = {p["xh"]}, .desc = {p["desc"]}, .height = {h}, .space = {p["space"]},')
        out.append('\t.g = {')
        for code in range(32, 127):
            ch = chr(code)
            if ch not in glyphs:
                out.append(f"\t\t[{code - 32}] = {{ 0, {{ 0 }} }}, /* {ch!r}: none */")
                continue
            rows = glyphs[ch]
            w = len(rows[0])
            assert w <= 8, (ch, w)
            vals = [int(r.replace('.', '0').replace('X', '1'), 2) for r in rows]
            out.append(f"\t\t[{code - 32}] = {{ {w}, {{ {', '.join('0x%02x' % v for v in vals)} }} }}, /* {ch!r} */")
        out.append('\t},')
        out.append('};')
        out.append('')
    open(path, 'w').write('\n'.join(out))


def preview(path, scale=6):
    from PIL import Image
    samples = [
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789 .,:;!?'\"-+=/()%_",
        "State saved. No quick state yet.", "Tap again to close GBA DS.", "14:29  57.1 FPS  98%",
        "This state was saved with Super Mario World Color Restoration v0.91: it may not work",
        "Controls  GBA  SNES  Auto  Hawk's Pro Skater",
    ]
    sheets = []
    for name in ('large', 'small'):
        p = SIZES[name]
        h, glyphs = build(name)
        W = 250
        im = Image.new('RGB', (W, (h + 3) * len(samples) + 2), (28, 24, 48))
        for i, text in enumerate(samples):
            x, y = 2, 2 + i * (h + 3)
            for ch in text:
                if ch == ' ':
                    x += p['space']
                    continue
                rows = glyphs.get(ch)
                if not rows:
                    x += p['space']
                    continue
                for yy, r in enumerate(rows):
                    for xx, c in enumerate(r):
                        if c == 'X' and x + xx < W:
                            im.putpixel((x + xx, y + yy), (236, 236, 244))
                x += len(rows[0]) + 1
        sheets.append(im.resize((im.width * scale, im.height * scale), Image.NEAREST))
    total = Image.new('RGB', (sheets[0].width, sum(s.height for s in sheets) + 10), (0, 0, 0))
    y = 0
    for s in sheets:
        total.paste(s, (0, y))
        y += s.height + 10
    total.save(path)


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--preview':
        preview(sys.argv[2])
    else:
        emit(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'pixfont.c'))
