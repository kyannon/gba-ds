#!/usr/bin/env python3
"""Makes src/logo_masks.c: the shapes of title logos that are painted into a
full-screen title picture rather than drawn on a layer of their own.

    GBADS_LOGO_DEBUG=1 build/host/harness THPS2.gba out logo.txt   # 'logo logo'
    (writes out/logo.png.fg.png: the exact title frame GBA DS renders)
    tools/make_logo_masks.py THPS2=THPS2-frame.png [SK8=SK8-frame.png] [THPS3=...]

Masks not named on the command line are kept as they are in
src/logo_masks.c, so one can be redone without the other games' ROMs.

Only the outline (which pixels are logo) goes into the source; the pixels
themselves are taken from the ROM when the logo is extracted, but for any a
mask draws itself (Dragon Ball Z's rebuilt Z: patchAt / patchRgb, from
tools/dbz-z.json, made by tools/dbz_fit_z.py). Needs Pillow, numpy and scipy.
"""
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H, W = 160, 240


def fit_ellipse(rim, columns):
    """Axis-aligned ellipse through the top-most and bottom-most rim pixel of
    each column (least squares)."""
    pts = []
    for xr in columns:
        for x in xr:
            col = np.nonzero(rim[:, x])[0]
            if len(col):
                pts.append((x, col.min()))
                pts.append((x, col.max()))
    p = np.array(pts, float)
    a, b, c, d = np.linalg.lstsq(np.c_[p[:, 0] ** 2, p[:, 1] ** 2, p[:, 0], p[:, 1]], np.ones(len(p)), rcond=None)[0]
    cx, cy = -c / (2 * a), -d / (2 * b)
    k = 1 + c * c / (4 * a) + d * d / (4 * b)
    return cx, cy, np.sqrt(k / a), np.sqrt(k / b)


def largest(m):
    lab, n = ndimage.label(m)
    if not n:
        return m
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return lab == (1 + int(np.argmax(sizes)))


def thps2(a):
    """Tony Hawk's Pro Skater 2 (USA/Europe, France, Germany): the oval badge
    with its white rim, plus the red 2 and its emblem sticking out right."""
    mx, mn = a.max(axis=2), a.min(axis=2)
    ys, xs = np.mgrid[0:H, 0:W]
    rim = (mn > 170) & (mx - mn < 60) & (ys > 35) & (ys < 125)
    cx, cy, ax, by = fit_ellipse(rim, [range(28, 165)])
    m = ((xs - cx) / (ax + 0.8)) ** 2 + ((ys - cy) / (by + 0.8)) ** 2 <= 1
    red = (a[..., 0] > 170) & (a[..., 1] < 100) & (a[..., 2] < 120) & (xs > cx + 40) & (ys > cy - by - 12) & (ys < cy + by + 6)
    red = largest(ndimage.binary_dilation(red, iterations=1))
    yy, xx = np.nonzero(red)
    em = (mn > 140) & (xs >= xx.min() - 3) & (xs <= xx.max()) & (ys >= yy.min() - 3) & (ys <= yy.min() + 16)
    red = ndimage.binary_dilation(ndimage.binary_closing(red | em, iterations=1), iterations=1)
    return largest(ndimage.binary_fill_holes(m | red))


def sk8(a):
    """SK8 (Japan): the oval badge, the lettering that breaks out of it and the
    yellow starburst behind."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(axis=2), a.min(axis=2)
    ys, xs = np.mgrid[0:H, 0:W]
    rim = (mn > 150) & (mx - mn < 50) & (ys > 45) & (ys < 115) & (xs > 15) & (xs < 225)
    cx, cy, ax, by = fit_ellipse(rim, [range(30, 90), range(170, 212)])
    ell = ((xs - cx) / (ax + 0.8)) ** 2 + ((ys - cy) / (by + 0.8)) ** 2 <= 1
    near = (ys >= cy - by - 10) & (ys <= cy + by + 2)
    logo = ((r > 190) & (g > 150) & (b < 100)) | ((r > 150) & (g < 70) & (b < 80)) | ((mx < 45) & near) | ((mn > 190) & near)
    lab, _ = ndimage.label(logo | ell, structure=np.ones((3, 3)))
    keep = np.unique(lab[ell])
    m = np.isin(lab, keep[keep > 0])
    m = ndimage.binary_opening(m, iterations=1) | ell
    return largest(ndimage.binary_fill_holes(m))


def thps3(a):
    """Tony Hawk's Pro Skater 3 (USA/Europe, France, Germany: the same
    picture): everything on the band between the black bars that isn't the
    saturated blue behind the badge, holes filled."""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    ys, xs = np.mgrid[0:H, 0:W]
    band = (ys >= 24) & (ys < 136)
    blue = (b - r > 45) & (b > g)
    m = largest(band & ~blue)
    return ndimage.binary_fill_holes(ndimage.binary_closing(m, iterations=1))


def dbz_render(polys):
    """Polygons in frame pixels to a bool mask, even-odd, 4x4 supersampled."""
    from PIL import ImageDraw
    im = np.zeros((H * 4, W * 4), bool)
    for p in polys:
        layer = Image.new('L', (W * 4, H * 4), 0)
        ImageDraw.Draw(layer).polygon([(x * 4, y * 4) for x, y in p], fill=255)
        im ^= np.asarray(layer) > 0
    return im.reshape(H, 4, W, 4).mean(axis=(1, 3)) >= 0.5


def dbz(a):
    """Dragon Ball Z: The Legacy of Goku (USA, Europe): DRAGON BALL Z and THE
    LEGACY OF GOKU, without the blue behind them or Goku, whose hair is
    painted over the Z's lower left (a solid black mass, unlike the letters'
    thin outlines) and who's below it. Where his hair was, the Z is drawn
    again (the owner's exception to DBZ-1): its shape from the owner's SVG
    of the logo, fitted onto the game's Z (tools/dbz_fit_z.py, saved in
    tools/dbz-z.json), in the game's own red and outline black.
    Returns the mask and the drawn pixels (0xRRGGBB, -1 elsewhere)."""
    import json
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    ys, xs = np.mgrid[0:H, 0:W]
    mx = a.max(axis=2)
    band = ys < 100
    dark = mx < 80
    blue = (b > r + 30) & (b > g + 10) & ~dark
    core = band & ~blue & ~dark
    hair = ndimage.binary_dilation(ndimage.binary_opening(dark & band, iterations=2) & (xs >= 148) & (ys >= 45), iterations=1)
    skin = (r > 180) & (g > 110) & (b > 80) & (r - b < 140) & (xs >= 148) & (ys >= 50)
    goku = hair | ((xs >= 150) & (ys >= 68)) | ndimage.binary_dilation(skin, iterations=1)
    core &= ~goku
    outline = dark & ndimage.binary_dilation(core, iterations=2) & ~goku
    m = ndimage.binary_fill_holes(core | outline) & ~blue & ~goku & band
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    m = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 6])
    # the Z again where Goku covered it
    z = json.load(open(os.path.join(ROOT, "tools", "dbz-z.json")))
    zo, zr = dbz_render(z["outline"]), dbz_render(z["red"])
    seen = zo & m
    pick = lambda sel: tuple(int(v) for v in np.median(a[sel], axis=0))
    red = pick(zr & seen & (r > 170) & (g < 90))
    black = pick(zo & ~zr & seen & (mx < 80))
    near = np.abs(a - np.array(red)).sum(axis=2) < 90
    hair3 = ndimage.binary_dilation(ndimage.binary_opening(dark & band, iterations=2) & (xs >= 148) & (ys >= 45), iterations=3)
    under = (hair3 | ((xs >= 150) & (ys >= 62))) & (xs >= 150) & (ys >= 40)
    fill = (zo & ~m & (ys >= 40) & (xs >= 150)) | (zr & m & ~near & (ys >= 45) & (xs >= 150)) | (zo & under)
    m = (m & ~(under & ~zo)) | fill
    patch = np.full((H, W), -1, np.int64)
    patch[fill & zr] = red[0] << 16 | red[1] << 8 | red[2]
    patch[fill & ~zr] = black[0] << 16 | black[1] << 8 | black[2]
    return m, patch

def gs2(a, japan=False):
    """Golden Sun: The Lost Age (every release; its title only comes with a
    save, so the logo profile patches the save check): the emblem painted
    over a picture of the lighthouse. The gold (redder than blue) and the
    dark outline beside it; the subtitle's red with its pinkish edge; the
    Europeans' TM, white in a black outline (filled in); Japan's subtitle,
    white with a dark shadow over the mountains. Nothing from the copyright
    line down."""
    def comps(m, mn):
        lab, n = ndimage.label(m)
        if not n:
            return m
        s = ndimage.sum(m, lab, range(1, n + 1))
        return np.isin(lab, [i + 1 for i, v in enumerate(s) if v >= mn])
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    warm = comps(R > B + 24, 4)
    near = ndimage.distance_transform_edt(~warm) <= 5
    m = warm | ((a.max(axis=2) < 0x50) & near)
    red = comps((R > G + 40) & (R > B + 16), 3)
    m |= red
    m |= (ndimage.distance_transform_edt(~red) <= 1) & (R >= B + 8) & (R > G + 8)
    if japan:
        x0, y0, x1, y1 = 50, 82, 132, 98
        box = a[y0:y1, x0:x1]
        m[y0:y1, x0:x1] |= comps((box.min(axis=2) > 0xC8) | (box.max(axis=2) < 0x60), 3)
    else:
        x0, y0, x1, y1 = 206, 36, 240, 56
        m[y0:y1, x0:x1] |= ndimage.binary_fill_holes(a[y0:y1, x0:x1].max(axis=2) < 0x50)
    m[118:] = False
    return m


MASKS = (("THPS2", thps2), ("SK8", sk8), ("THPS3", thps3), ("DBZ", dbz),
         ("GS2E", gs2), ("GS2D", gs2), ("GS2F", gs2), ("GS2I", gs2), ("GS2S", gs2),
         ("GS2J", lambda a: gs2(a, True)))


def runs(mask):
    ys, xs = np.nonzero(mask)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    bits = mask[y0:y1 + 1, x0:x1 + 1].flatten()
    out, cur, n = [], False, 0
    for v in bits:
        if bool(v) != cur:
            while n > 65535:
                out += [65535, 0]
                n -= 65535
            out.append(n)
            cur, n = bool(v), 0
        n += 1
    out.append(n)
    return int(x0), int(y0), int(x1 - x0 + 1), int(y1 - y0 + 1), out


def existing_blocks(path):
    """The masks already in src/logo_masks.c, by name: their source lines."""
    blocks = {}
    if not os.path.exists(path):
        return blocks
    text = open(path).read()
    for name, _ in MASKS:
        start = text.find(f"static const uint16_t runs{name}[")
        end = text.find(f"const LogoMask logoMask{name} =")
        if start >= 0 and end >= 0:
            end = text.index("\n", end) + 1
            blocks[name] = text[start:end].rstrip("\n").split("\n")
    return blocks


def main():
    given = dict(arg.split("=", 1) for arg in sys.argv[1:] if "=" in arg)
    if not given or len(given) != len(sys.argv) - 1 or any(n not in dict(MASKS) for n in given):
        sys.exit(__doc__)
    out = os.path.join(ROOT, "src", "logo_masks.c")
    old = existing_blocks(out)
    lines = ["/* Generated by tools/make_logo_masks.py - do not edit. */", '#include "logo_masks.h"', ""]
    for name, fn in MASKS:
        if name in given:
            a = np.asarray(Image.open(given[name]).convert("RGB")).astype(int)
            got = fn(a)
            mask, patch = got if isinstance(got, tuple) else (got, None)
            x, y, w, h, r = runs(mask)
            block = [f"static const uint16_t runs{name}[{len(r)}] = {{"]
            for i in range(0, len(r), 16):
                block.append("\t" + ", ".join(str(v) for v in r[i:i + 16]) + ",")
            block.append("};")
            extra = ""
            if patch is not None:
                py, px = np.nonzero(patch >= 0)
                at = [int(yy) * W + int(xx) for yy, xx in zip(py, px)]
                rgb = [int(patch[yy, xx]) for yy, xx in zip(py, px)]
                block.append(f"static const uint16_t patchAt{name}[{len(at)}] = {{")
                for i in range(0, len(at), 16):
                    block.append("\t" + ", ".join(str(v) for v in at[i:i + 16]) + ",")
                block.append("};")
                block.append(f"static const uint32_t patchRgb{name}[{len(rgb)}] = {{")
                for i in range(0, len(rgb), 8):
                    block.append("\t" + ", ".join(f"0x{v:06X}" for v in rgb[i:i + 8]) + ",")
                block.append("};")
                extra = f", patchAt{name}, patchRgb{name}, {len(at)}"
                print(f"{name}: {len(at)} pixels drawn")
            block.append(f"const LogoMask logoMask{name} = {{ {x}, {y}, {w}, {h}, runs{name}, {len(r)}{extra} }};")
            print(f"{name}: {w}x{h} at {x},{y}, {len(r)} runs")
        elif name in old:
            block = old[name]
            print(f"{name}: kept")
        else:
            sys.exit(f"{name}: no frame given and none in {out}")
        lines += block + [""]
    with open(out, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
