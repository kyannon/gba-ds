#!/usr/bin/env python3
"""Draws GBA DS's default overlays: a flat bezel around the picture, for
every screen of every device in docs/devices.csv and every scaling that
leaves room around the picture.

    tools/make_overlays.py [--out DIR] [--master DIR] [--master-only]

The design (the "master") is defined once, in GBA pixels around a 240x160
picture, by the constants below, and drawn at any size by bezel(): the
picture's hole, a dark grey bezel with slightly rounded corners, the GBA DS
logo's navy outside it, and the logo (black outline, dimmed letters) in the
bezel next to the hinge: below the picture on the top screen, above it on
the bottom screen.

Where the screen has less room than the design (Full Width, or Pixel
Perfect's sides on a 4:3 screen), the bezel is cut off by the screen's edge;
the logo shrinks to fit its band, or is left out where the band is too thin.

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
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBA_W, GBA_H = 240, 160

# ---------------------------------------------------------------- the design
# Sizes in GBA pixels (u: one GBA pixel at the picture's scale).
NAVY = (16, 32, 96, 255)       # #102060, the GBA DS logo's outline (tools/make_brand.py)
BEZEL = (26, 26, 26, 255)      # #1A1A1A, the bezel round the screen (darker than the idle logo screen's #303030)
LOGO_INK = (208, 208, 208)     # the logo's letters, a little dimmer than white
MARGIN = 12.0     # the bezel round the picture
RADIUS = 3.0      # its corners
SLIVER = 2.0      # less navy than this beside the bezel: the bezel goes to the edge
LOGO_H = 0.56     # the GBA DS logo's height (outline included), as a share of the bezel's band
MIN_TEXT = 9      # screen pixels: a smaller logo is left out
LOGO = os.path.join(ROOT, "assets", "brand", "gba-ds-logo.png")

_logo = None


def logo_image(height):
    """The GBA DS logo with a black outline and slightly dim letters."""
    global _logo
    if _logo is None:
        src = Image.open(LOGO).convert("RGBA")
        src = src.crop(src.getbbox())
        a = np.array(src).astype(np.float32)
        luma = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
        n = NAVY[0] * 0.299 + NAVY[1] * 0.587 + NAVY[2] * 0.114
        t = np.clip((luma - n) / (255 - n), 0, 1)  # 0: the outline, 1: the letters
        for c in range(3):
            a[..., c] = t * LOGO_INK[c]
        _logo = Image.fromarray(a.round().astype(np.uint8), "RGBA")
    w = max(1, round(_logo.width * height / _logo.height))
    return _logo.resize((w, max(1, round(height))), Image.LANCZOS)


def bezel(sw, sh, pic, bottom, ss=4):
    """The overlay for a screen of sw x sh whose picture is pic = (x, y, w, h)
    (screen pixels): RGBA, transparent inside the picture. The logo goes in
    the bezel next to the hinge: below the picture on the top screen, above
    it on the bottom one (bottom)."""
    px, py, pw, ph = pic
    u = min(pw / GBA_W, ph / GBA_H)  # one GBA pixel
    W, H = sw * ss, sh * ss
    X0, Y0, X1, Y1 = px * ss, py * ss, (px + pw) * ss, (py + ph) * ss
    U = u * ss
    img = Image.new("RGBA", (W, H), NAVY)
    d = ImageDraw.Draw(img)
    m = MARGIN * U
    # the bezel: the picture plus the margin, out to the edge where only a sliver of navy would be left
    bx0, by0, bx1, by1 = X0 - m, Y0 - m, X1 + m, Y1 + m
    if bx0 < SLIVER * U: bx0 = -RADIUS * U * 2
    if by0 < SLIVER * U: by0 = -RADIUS * U * 2
    if bx1 > W - SLIVER * U: bx1 = W + RADIUS * U * 2
    if by1 > H - SLIVER * U: by1 = H + RADIUS * U * 2
    d.rounded_rectangle((bx0, by0, bx1 - 1, by1 - 1), radius=RADIUS * U, fill=BEZEL)
    img = img.resize((sw, sh), Image.BOX) if ss > 1 else img
    # the logo, at the screen's own resolution
    t0, t1 = (max(0, by0 / ss), py) if bottom else (py + ph, min(sh, by1 / ss))
    band = t1 - t0
    h = min(LOGO_H * MARGIN * u, 0.8 * band)  # the design's size, smaller if the band is cut off
    if band > 0 and h >= MIN_TEXT:
        t = logo_image(h)
        img.alpha_composite(t, (round(px + pw / 2 - t.width / 2), round((t0 + t1) / 2 - t.height / 2)))
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
    ap.add_argument("--master-only", action="store_true", help="draw only the master images")
    args = ap.parse_args()

    # the master: the design at 16x, with room for all of it
    k = 16
    pw, ph = GBA_W * k, GBA_H * k
    pad = int((MARGIN + 8) * k)
    for bottom in (False, True):
        m = bezel(pw + 2 * pad, ph + 2 * pad, (pad, pad, pw, ph), bottom, ss=1)
        m.save(os.path.join(args.master, f"master-{'bottom' if bottom else 'top'}.png"), optimize=True)
    if args.master_only:
        return

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
