#!/usr/bin/env python3
"""Fits the Z of a Dragon Ball Z logo SVG onto Dragon Ball Z: The Legacy of
Goku's title frame, for the logo mask's rebuilt Z (DBZ-1; the title paints
Goku's hair over the Z's lower left).

    GBADS_LOGO_DEBUG=1 build/host/harness ALGE.gba out logo.txt  # 'logo logo'
    tools/dbz_fit_z.py LOGO.svg out/logo.png.fg.png tools/dbz-z.json

The SVG is the owner's (Oct 31, 2026); only the fitted outline of its Z
(its black shape and red fill, in the title frame's pixels) goes into the
JSON, which tools/make_logo_masks.py reads. Its first path is the Z's black
shape (with the two holes), the first subpath of its first #E30613 path the
Z's red. Fitted by scale, height, shift and slant to the game's own red Z
where Goku doesn't cover it. Needs Pillow, numpy and scipy.
"""
import json
import re
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage, optimize

H, W = 160, 240


def parse(d):
    """SVG path data (M L H V C Z, absolute and relative) to polygons (curves sampled)."""
    toks = re.findall(r'[MmLlHhVvCcZz]|-?\d*\.?\d+', d)
    i, cmd, x, y, sx, sy = 0, None, 0.0, 0.0, 0.0, 0.0
    subs, cur = [], []

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v
    while i < len(toks):
        t = toks[i]
        if re.match(r'[A-Za-z]', t):
            cmd = t
            i += 1
            if cmd in 'Zz':
                if cur:
                    subs.append(cur)
                    cur = []
                x, y = sx, sy
                continue
        rel, c = cmd.islower(), cmd.upper()
        if c == 'M':
            nx, ny = num(), num()
            if rel:
                nx, ny = nx + x, ny + y
            if cur:
                subs.append(cur)
            cur = [(nx, ny)]
            x, y = sx, sy = nx, ny
            cmd = 'l' if rel else 'L'
        elif c == 'L':
            nx, ny = num(), num()
            if rel:
                nx, ny = nx + x, ny + y
            cur.append((nx, ny))
            x, y = nx, ny
        elif c == 'H':
            nx = num()
            x = x + nx if rel else nx
            cur.append((x, y))
        elif c == 'V':
            ny = num()
            y = y + ny if rel else ny
            cur.append((x, y))
        elif c == 'C':
            p = [num() for _ in range(6)]
            if rel:
                p = [p[k] + (x if k % 2 == 0 else y) for k in range(6)]
            p0, p1, p2, p3 = np.array([x, y]), np.array(p[0:2]), np.array(p[2:4]), np.array(p[4:6])
            for tt in np.linspace(0, 1, 9)[1:]:
                q = (1 - tt) ** 3 * p0 + 3 * (1 - tt) ** 2 * tt * p1 + 3 * (1 - tt) * tt ** 2 * p2 + tt ** 3 * p3
                cur.append((float(q[0]), float(q[1])))
            x, y = p[4], p[5]
    if cur:
        subs.append(cur)
    return subs


def render(polys, w=W, h=H):
    """Polygons in frame pixels to a bool mask, even-odd, 4x4 supersampled."""
    im = np.zeros((h * 4, w * 4), bool)
    for p in polys:
        layer = Image.new('L', (w * 4, h * 4), 0)
        ImageDraw.Draw(layer).polygon([(x * 4, y * 4) for x, y in p], fill=255)
        im ^= np.asarray(layer) > 0
    return im.reshape(h, 4, w, 4).mean(axis=(1, 3)) >= 0.5


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    svg, frame, out = sys.argv[1:]
    paths = [a for a in re.findall(r'<path ([^>]*)/>', open(svg).read()) if 'com.sun.star' in a]
    shapes = []
    for a in paths:
        f = re.search(r'fill="([^"]*)"', a)
        shapes.append((f.group(1) if f else '#000', parse(re.search(r'(?:^| )d="([^"]*)"', a).group(1))))
    outline = shapes[0][1]
    red = [s for s in shapes if s[0] == '#E30613'][0][1][0]
    a = np.asarray(Image.open(frame).convert('RGB')).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    ys, xs = np.mgrid[0:H, 0:W]
    zone = (xs >= 162) & (ys < 80)  # (right of BALL's last L)
    game = zone & (r > 170) & (g < 90) & (b < 90)
    dark = a.max(axis=2) < 80
    hair = ndimage.binary_dilation(ndimage.binary_opening(dark & (ys < 100), iterations=2) & (xs >= 148) & (ys >= 45), iterations=2)
    skin = (r > 180) & (g > 110) & (b > 80) & (r - b < 140) & (xs >= 148) & (ys >= 50)
    seen = zone & ~(hair | ((xs >= 150) & (ys >= 68)) | ndimage.binary_dilation(skin, iterations=2))
    pts = np.array(red)
    x0, y0 = pts[:, 0].min(), pts[:, 1].min()

    def place(polys, p):
        s, ky, tx, ty, k = p
        return [[(tx + (x - x0) * s + k * (y - y0) * s * ky, ty + (y - y0) * s * ky) for x, y in q] for q in polys]

    def cost(p):
        return int(((render(place([red], p)) != game) & seen).sum())
    gy, gx = np.nonzero(game & seen)
    s0 = (gx.max() - gx.min()) / (pts[:, 0].max() - x0)
    best = None
    for s in np.linspace(s0 * 0.85, s0 * 1.15, 7):
        for ky in np.linspace(0.85, 1.25, 9):
            for k in np.linspace(-0.3, 0.3, 7):
                for tx in range(int(gx.min()) - 6, int(gx.min()) + 7, 2):
                    for ty in range(int(gy.min()) - 3, int(gy.min()) + 4):
                        c = cost([s, ky, tx, ty, k])
                        if best is None or c < best[0]:
                            best = (c, [s, ky, tx, ty, k])
    res = optimize.minimize(cost, best[1], method='Nelder-Mead', options={'maxiter': 600})
    p = list(res.x) if res.fun <= best[0] else best[1]
    print(f"fit: scale {p[0]:.6f}, height {p[1]:.4f}, at {p[2]:.2f},{p[3]:.2f}, slant {p[4]:.4f}; "
          f"{cost(p)} of {int((game & seen).sum())} red pixels differ")
    rnd = lambda polys: [[[round(x, 2), round(y, 2)] for x, y in q] for q in polys]
    json.dump({"outline": rnd(place(outline, p)), "red": rnd(place([red], p))}, open(out, 'w'))


if __name__ == '__main__':
    main()
