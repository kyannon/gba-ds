#!/usr/bin/env python3
"""Draws GBA DS's default overlays: a flat bezel in the style of the purple
(indigo) Game Boy Advance around the picture, for every screen of every
device in docs/devices.csv and every scaling that leaves room around the
picture.

    tools/make_overlays.py [--out DIR] [--master DIR]

The design (the "master") is defined once, in GBA pixels around a 240x160
picture, by the constants below, and drawn at any size by bezel(): the
picture's hole, a thin grey outline round it, a black bezel with slightly
rounded corners, purple outside it, and the text in the black above and
below the picture:

    top screen:    "kyannon" above the picture, the GBA DS logo below it
    bottom screen: the GBA DS logo above the picture, "kyannon" below it

Where the screen has less room than the design (Full Width, or Pixel
Perfect's sides on a 4:3 screen), the bezel is cut off by the screen's edge;
the text shrinks to fit its band, or is left out where the band is too thin.

Outputs:

    assets/overlays/master-top.png, master-bottom.png
        the design at 16x (a 3840x2560 picture), for reference and editing
    assets/overlays/default/<SW>x<SH>-<top|bottom>-<PW>x<PH>+<PX>+<PY>.png
        one file per screen size, screen and picture rectangle (where the
        game or map picture goes, transparent there); the app picks the one
        that matches (game.c draw_default_overlay)
    assets/overlays/default/README.txt
        which device, screen, scaling and screen shapes use each file

The picture rectangles are worked out as the app does (ui.c
ui_layout_pictures and fit_rect, with the known devices' pixels per inch for
"Match screen sizes and aspect ratios").

Needs Pillow and numpy.
"""
import argparse
import csv
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBA_W, GBA_H = 240, 160

# ---------------------------------------------------------------- the design
# Sizes in GBA pixels (u: one GBA pixel at the picture's scale).
PURPLE = (94, 63, 181, 255)    # #5E3FB5, the indigo GBA's shell, flat
BLACK = (0, 0, 0, 255)
GREY = (140, 140, 140, 255)    # #8C8C8C, the screen's outline
WHITE = (255, 255, 255, 255)
OUTLINE = 1.25    # grey outline round the picture
MARGIN = 12.0     # black bezel round the picture (outline included)
RADIUS = 3.0      # black bezel's corners
OUTLINE_RADIUS = 0.75
SLIVER = 2.0      # less purple than this beside the bezel: the black goes to the edge
NAME_H = 0.42     # "kyannon": ascender height, as a share of the text band
LOGO_H = 0.62     # the GBA DS logo's height (outline included), as a share of the band
MIN_TEXT = 9      # screen pixels: smaller text is left out
FONT = os.path.join(ROOT, "assets", "fonts", "Jost-700-BoldItalic.ttf")
LOGO = os.path.join(ROOT, "assets", "brand", "gba-ds-logo.png")


def name_image(height):
    """'kyannon' in white, its ascender `height` px tall, trimmed."""
    f = ImageFont.truetype(FONT, 1000)
    b = f.getbbox("k")
    size = max(1, round(1000 * height / (b[3] - b[1])))
    f = ImageFont.truetype(FONT, size)
    l, t, r, btm = f.getbbox("kyannon")
    img = Image.new("RGBA", (r - l + 4, btm - t + 4), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((2 - l, 2 - t), "kyannon", font=f, fill=WHITE)
    img = img.crop(img.getbbox())
    # align on the ascender: the image's top is the k's top; its descender (y) hangs below
    return img, (b[3] - b[1]) * size / 1000.0


_logo = None


def logo_image(height):
    global _logo
    if _logo is None:
        _logo = Image.open(LOGO).convert("RGBA")
        _logo = _logo.crop(_logo.getbbox())
    w = max(1, round(_logo.width * height / _logo.height))
    return _logo.resize((w, max(1, round(height))), Image.LANCZOS)


def bezel(sw, sh, pic, bottom, ss=4):
    """The overlay for a screen of sw x sh whose picture is pic = (x, y, w, h)
    (screen pixels): RGBA, transparent inside the picture. bottom: the bottom
    screen (the logo above the picture, the name below)."""
    px, py, pw, ph = pic
    u = min(pw / GBA_W, ph / GBA_H)  # one GBA pixel
    W, H = sw * ss, sh * ss
    X0, Y0, X1, Y1 = px * ss, py * ss, (px + pw) * ss, (py + ph) * ss
    U = u * ss
    img = Image.new("RGBA", (W, H), PURPLE)
    d = ImageDraw.Draw(img)
    m = MARGIN * U
    # black: the picture plus the margin, out to the edge where only a sliver of purple would be left
    bx0, by0, bx1, by1 = X0 - m, Y0 - m, X1 + m, Y1 + m
    if bx0 < SLIVER * U: bx0 = -RADIUS * U * 2
    if by0 < SLIVER * U: by0 = -RADIUS * U * 2
    if bx1 > W - SLIVER * U: bx1 = W + RADIUS * U * 2
    if by1 > H - SLIVER * U: by1 = H + RADIUS * U * 2
    d.rounded_rectangle((bx0, by0, bx1 - 1, by1 - 1), radius=RADIUS * U, fill=BLACK)
    o = OUTLINE * U
    d.rounded_rectangle((X0 - o, Y0 - o, X1 + o - 1, Y1 + o - 1), radius=OUTLINE_RADIUS * U, fill=GREY)
    img = img.resize((sw, sh), Image.BOX) if ss > 1 else img
    # the text, at the screen's own resolution, in the black above and below
    d = ImageDraw.Draw(img)
    full = (MARGIN - OUTLINE) * u  # the design's band
    o1 = OUTLINE * u
    bands = {
        "above": (max(0, by0 / ss), py - o1),
        "below": (py + ph + o1, min(sh, by1 / ss)),
    }
    texts = {"above": "logo" if bottom else "name", "below": "name" if bottom else "logo"}
    for where, (t0, t1) in bands.items():
        band = t1 - t0
        if band <= 0:
            continue
        kind = texts[where]
        share = LOGO_H if kind == "logo" else NAME_H
        h = min(share * full, 0.8 * band)  # the design's size, smaller if the band is cut off
        if h < MIN_TEXT:
            continue
        if kind == "logo":
            t = logo_image(h)
            top = (t0 + t1) / 2 - t.height / 2
        else:
            t, asc = name_image(h)
            top = (t0 + t1) / 2 - asc / 2  # centred on the letters' x-to-ascender block
        x = px + pw / 2 - t.width / 2
        img.alpha_composite(t, (round(x), round(top)))
    # the picture: fully transparent, exactly
    a = np.array(img)
    a[py:py + ph, px:px + pw] = 0
    return Image.fromarray(a, "RGBA")


# ------------------------------------------------------------ the app's geometry
def f32(v):
    return float(np.float32(v))


def c_div(a, b):
    """C's integer division (towards zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def fit_rect_c(area, w, h, mode):
    ax, ay, aw, ah = area
    if mode == "stretch":
        return area
    s = min(aw / w, ah / h)
    if mode == "integer" and s >= 1.0:
        s = float(int(s))
    if mode == "zoom":
        s = ah / h
    ow, oh = int(w * s), int(h * s)
    return (ax + c_div(aw - ow, 2), ay + c_div(ah - oh, 2), ow, oh)


def ppi(w, h, inches):
    return f32(f32(math.hypot(w, h)) / f32(inches))


def pic_areas(dev, match):
    """ui.c ui_layout_pictures: the picture area of [top, bottom]."""
    scr = [(dev["tw"], dev["th"]), (dev["bw"], dev["bh"])]
    areas = [(0, 0, w, h) for w, h in scr]
    a0, a1 = scr[0][0] / scr[0][1], scr[1][0] / scr[1][1]
    differ = abs(a0 - a1) > 0.02 * max(a0, a1)
    if match in ("aspect", "size") and differ:
        wide = 0 if a0 > a1 else 1
        nar = 1 - wide
        WW, WH = scr[wide]
        NW, NH = scr[nar]
        a = NW / NH
        w, h = int(WH * a + 0.5), WH
        if match == "size":
            pw = ppi(*scr[wide], dev["tin" if wide == 0 else "bin"])
            pn = ppi(*scr[nar], dev["tin" if nar == 0 else "bin"])
            w = int(f32(f32(f32(NW * pw) / pn) + 0.5))
            h = int(f32(f32(f32(NH * pw) / pn) + 0.5))
        if w > WW:
            h, w = c_div(h * WW, w), WW
        if h > WH:
            w, h = c_div(w * WH, h), WH
        y = (WH - h if wide == 0 else 0) if match == "size" else c_div(WH - h, 2)
        areas[wide] = (c_div(WW - w, 2), y, w, h)
    return areas, differ


def devices():
    out = []
    with open(os.path.join(ROOT, "docs", "devices.csv"), newline="") as f:
        for r in csv.DictReader(f):
            def res(s):
                w, h = (int(v) for v in s.lower().split("x"))
                return max(w, h), min(w, h)  # (the app sees every screen landscape)
            tw, th = res(r["Top Screen Resolution"])
            bw, bh = res(r["Bottom Screen Resolution"])
            out.append({"name": f'{r["Manufacturer"]} {r["Device"]}', "tw": tw, "th": th,
                        "tin": float(r["Top Screen Size (in)"]), "bw": bw, "bh": bh,
                        "bin": float(r["Bottom Screen Size (in)"])})
    return out


SCALES = [("integer", "Pixel Perfect"), ("fit", "Full Width"), ("zoom", "Zoom"), ("stretch", "Stretch")]
MATCHES = [("keep", "Keep device aspect ratios"), ("aspect", "Match screen aspect ratios"),
           ("size", "Match screen sizes and aspect ratios")]


def file_name(sw, sh, which, r):
    return f"{sw}x{sh}-{which}-{r[2]}x{r[3]}+{r[0]}+{r[1]}.png"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "assets", "overlays", "default"))
    ap.add_argument("--master", default=os.path.join(ROOT, "assets", "overlays"))
    args = ap.parse_args()

    # the master: the design at 16x, with room for all of it
    k = 16
    pw, ph = GBA_W * k, GBA_H * k
    pad = int((MARGIN + 8) * k)
    for bottom in (False, True):
        m = bezel(pw + 2 * pad, ph + 2 * pad, (pad, pad, pw, ph), bottom, ss=1)
        m.save(os.path.join(args.master, f"master-{'bottom' if bottom else 'top'}.png"), optimize=True)

    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.endswith(".png"):
            os.remove(os.path.join(args.out, f))
    uses = {}
    for dev in devices():
        for match, mname in MATCHES:
            areas, differ = pic_areas(dev, match)
            if match != "keep" and not differ:
                continue
            for i, which in enumerate(("top", "bottom")):
                sw, sh = (dev["tw"], dev["th"]) if i == 0 else (dev["bw"], dev["bh"])
                if match != "keep" and areas[i] == (0, 0, sw, sh):
                    continue  # (the narrower screen: as with Keep, whose file it uses)
                for scale, sname in SCALES:
                    r = fit_rect_c(areas[i], GBA_W, GBA_H, scale)
                    if r[0] <= 0 and r[1] <= 0 and r[0] + r[2] >= sw and r[1] + r[3] >= sh:
                        continue  # the picture fills the screen: nothing to draw
                    # (zoom wider than the screen: only the band above and below, if any)
                    x0, y0 = max(r[0], 0), max(r[1], 0)
                    x1, y1 = min(r[0] + r[2], sw), min(r[1] + r[3], sh)
                    rr = (x0, y0, x1 - x0, y1 - y0)
                    if rr == (0, 0, sw, sh):
                        continue
                    name = file_name(sw, sh, which, r)
                    if name not in uses:
                        uses[name] = (sw, sh, rr, i == 1, [])
                    uses[name][4].append(f'{dev["name"]}, {which} screen, {sname}' + (f", {mname}" if match != "keep" else ""))
    for name, (sw, sh, rr, bottom, who) in sorted(uses.items()):
        ss = max(2, min(4, 4096 // sw))
        bezel(sw, sh, rr, bottom, ss).save(os.path.join(args.out, name), optimize=True)
    with open(os.path.join(args.out, "README.txt"), "w") as f:
        f.write("GBA DS's default overlays: a bezel round the picture, for each screen of the devices GBA DS knows\n"
                "(docs/devices.csv). Made by tools/make_overlays.py; don't edit them here (your own overlays go in\n"
                "overlays/, and win over these). A file's name is the screen's size, which screen, and where the\n"
                "picture goes: <screen width>x<height>-<top|bottom>-<picture width>x<height>+<x>+<y>.png\n\n")
        for name, (_, _, _, _, who) in sorted(uses.items()):
            f.write(name + "\n" + "".join(f"  {w}\n" for w in who))
    print(f"{len(uses)} overlays in {args.out}")


if __name__ == "__main__":
    main()
