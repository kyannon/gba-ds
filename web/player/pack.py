#!/usr/bin/env python3
"""Packs GBA DS's web player (web/player) two ways:

  OUT/gba-ds-web-player-VERSION.html   everything in one file (opens from disk)
  OUT/gba-ds-web-player-VERSION.zip    a folder to put on a web server

  pack.py OUT [--build ZIP_OR_WASM]

--build puts a GBA DS build in both, so the player starts with it: a dev zip
(gba-ds-*-linux-aarch64.zip, its gba-ds/web/gba-ds.wasm) or a bare
gba-ds.wasm (then dist/gba-ds.cfg and VERSION go with it). Only what the
player uses is kept (web/gba-ds.wasm, web/VERSION, gba-ds.cfg,
gamecontrollerdb.txt). The names then end in "-with-BUILDVERSION".
Run by scripts/build-web-player.sh.
"""
import argparse, base64, io, os, re, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
RUNTIME = os.path.join(ROOT, "tools", "statesaver", "gbads-runtime.js")
FONT = os.path.join(ROOT, "assets", "fonts", "Jost-700-BoldItalic.ttf")
LOGO = os.path.join(ROOT, "assets", "brand", "gba-ds-logo.png")
KEEP = re.compile(r"^(web/gba-ds\.wasm|web/VERSION|gba-ds\.cfg|gamecontrollerdb\.txt)$")


def read(p, mode="r"):
    with open(p, mode) as f:
        return f.read()


def logo_png():
    """The logo at the bar's height (22 px, twice over for sharp screens)."""
    try:
        from PIL import Image
        im = Image.open(LOGO)
        h = 44
        im = im.resize((round(im.width * h / im.height), h), Image.LANCZOS)
        b = io.BytesIO()
        im.save(b, "PNG", optimize=True)
        return b.getvalue()
    except ImportError:
        return read(LOGO, "rb")


def build_zip(src):
    """The player's own small zip of a build, and its version."""
    files = {}
    if src.endswith(".wasm"):
        files["web/gba-ds.wasm"] = read(src, "rb")
        files["web/VERSION"] = read(os.path.join(ROOT, "VERSION"), "rb")
        files["gba-ds.cfg"] = read(os.path.join(ROOT, "dist", "gba-ds.cfg"), "rb")
        files["gamecontrollerdb.txt"] = read(os.path.join(ROOT, "dist", "gamecontrollerdb.txt"), "rb")
    else:
        with zipfile.ZipFile(src) as z:
            wasm = [n for n in z.namelist() if re.search(r"(^|/)web/gba-ds\.wasm$", n)]
            if not wasm:
                sys.exit(src + " has no web build (gba-ds/web/gba-ds.wasm)")
            root = wasm[0][: -len("web/gba-ds.wasm")]
            for n in z.namelist():
                if n.startswith(root) and KEEP.match(n[len(root):]):
                    files[n[len(root):]] = z.read(n)
    version = files.get("web/VERSION", b"unknown").decode().strip()
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for n, b in sorted(files.items()):
            z.writestr("gba-ds/" + n, b)
    return out.getvalue(), version


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--build")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    version = read(os.path.join(ROOT, "VERSION")).strip()
    bundle, bver = build_zip(a.build) if a.build else (None, None)
    name = "gba-ds-web-player-v" + version + ("-with-" + bver if bundle else "")

    html = read(os.path.join(HERE, "index.html")).replace('data-version="dev"', 'data-version="' + version + '"')
    css = read(os.path.join(HERE, "player.css"))
    srcs = {n: read(os.path.join(HERE, n + ".js")) for n in ("zip", "player", "worker", "worklet")}
    srcs["runtime"] = read(RUNTIME)
    for n, t in srcs.items():
        if "</script" in t.lower():
            sys.exit(n + ".js has </script in it: it can't be inlined")
    logo = logo_png()
    lic = "\n<!-- GBA DS web player " + version + ". GBA DS: github.com/kyannon/gba-ds (its LICENSE); mGBA: MPL-2.0; Jost: SIL OFL 1.1. -->\n"

    # ---- one file
    one = html
    one = one.replace('<link rel="stylesheet" href="player.css">', "<style>\n" + css.replace(
        'url("jost-bold-italic.ttf")', 'url("data:font/ttf;base64,' + base64.b64encode(read(FONT, "rb")).decode() + '")') + "</style>")
    one = one.replace('src="logo.png"', 'src="data:image/png;base64,' + base64.b64encode(logo).decode() + '"')
    inline = "".join('<script type="text/plain" id="src-' + n + '">\n' + srcs[n] + "</script>\n" for n in ("runtime", "worker", "worklet"))
    if bundle:
        inline += '<script type="application/octet-stream" id="bundled-build">' + base64.b64encode(bundle).decode() + "</script>\n"
    one = one.replace('<script src="zip.js"></script>\n<script src="player.js"></script>',
                      inline + "<script>\n" + srcs["zip"] + "</script>\n<script>\n" + srcs["player"] + "</script>")
    one = one.replace("</body>", lic + "</body>")
    with open(os.path.join(a.out, name + ".html"), "w") as f:
        f.write(one)

    # ---- a folder, zipped
    with zipfile.ZipFile(os.path.join(a.out, name + ".zip"), "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        d = name + "/"
        z.writestr(d + "index.html", html.replace("</body>", lic + "</body>"))
        z.writestr(d + "player.css", css)
        for n in ("zip", "player", "worker", "worklet"):
            z.writestr(d + n + ".js", srcs[n])
        z.writestr(d + "gbads-runtime.js", srcs["runtime"])
        z.writestr(d + "logo.png", logo)
        z.write(FONT, d + "jost-bold-italic.ttf")
        z.write(os.path.join(ROOT, "assets", "fonts", "Jost-OFL.txt"), d + "licenses/Jost-OFL.txt")
        z.write(os.path.join(ROOT, "LICENSE"), d + "licenses/GBA-DS-LICENSE.txt")
        mlic = os.path.join(ROOT, "deps", "mgba", "LICENSE")
        if os.path.exists(mlic):
            z.write(mlic, d + "licenses/mGBA-LICENSE.txt")
        if bundle:
            z.writestr(d + "build/gba-ds-web.zip", bundle)
        z.writestr(d + "README.txt", README.format(version=version, bundled=(
            "build/gba-ds-web.zip is GBA DS " + bver + ": the player starts with it.\n" if bundle else
            "No GBA DS build comes with it: drop a dev build (.zip) on the page,\nor put one at build/gba-ds-web.zip (scripts/build-web-player.sh --build).\n")))
    for n in (name + ".html", name + ".zip"):
        print(os.path.join(a.out, n), os.path.getsize(os.path.join(a.out, n)))


README = """GBA DS web player {version}
==========================

GBA DS in a web browser, for the screens of any dual-screen device it knows.

Put this folder on any web server (it's static files: no server code) and
open index.html. It needs to be served over http(s): a page opened from disk
can't start the files beside it, so for that use the one-file player
(gba-ds-web-player-*.html) instead.

{bundled}
Drop on the page: a GBA DS dev build (.zip), a game (.gba or zipped), a
battery save (.sav) or a state (.ss), or a bug report zip. Saves, states and
settings stay in the browser (IndexedDB); nothing is sent anywhere.

Needs a browser with WebAssembly, WebGL2, OffscreenCanvas and AudioWorklet:
Firefox 105, Chrome 69 or Safari 17 and later.
"""

if __name__ == "__main__":
    main()
