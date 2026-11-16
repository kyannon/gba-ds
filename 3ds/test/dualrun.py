#!/usr/bin/env python3
"""Plays one input script in GBA DS on libmgba and on armGBA, headless, and compares what both screens show.

    3ds/test/dualrun.py GAME SCRIPT OUTDIR [--runs mgba,armgba,armgba-thr] [--rom-dir DIR] [--state STATE]

GAME is a game code (AMKE: the ROM is found in --rom-dir by its header) or a ROM path. SCRIPT is a .play file
(below). Three runs by default, each with its own state and save folders under OUTDIR:

    mgba        build/host/gba-ds          the reference (scripts/setup-host.sh)
    armgba      build/sim-perf/gba-ds      the app on armGBA (3ds/sim/app.mk), as the 3DS runs it
    armgba-thr  the same with armGBA's render thread (GBADS_RENDER_THREAD=1)
    armgba-3ds  drawn as the 3DS draws (two threads, a frame late, the HUD split one frame in five), in the
                armgba column when that isn't run: --runs mgba,armgba-3ds

armgba-thr must match armgba exactly, every screenshot of both screens: armGBA's core is deterministic, however
it draws. A difference there is a bug (in armGBA, or in how GBA DS uses it). armgba against mgba may differ
where the two emulators time things differently; the report says where and how much.

OUTDIR gets report.json, report.md, sheet-NN.png (mgba | armgba | where they differ, both screens; the
third column red where armgba-thr differs from armgba) and each run's log.txt (GBADS_DEBUG=1: map captures,
second-screen switches) and shots.

A .play script: one command per line, in order, each starting where the last ended (frames at 60 a second).

    wait N                 nothing pressed for N frames
    press BTNS [N]         hold for N frames (default 4), then let go for 8
    hold BTNS N            hold for N frames, let go for 1
    mash BTNS COUNT [GAP]  press COUNT times, GAP frames apart (default 30)
    shot NAME              both screens (BMP, converted to NAME.png)
    tap X Y                touch at X, Y (fractions of the whole window: the bottom screen is Y 0.5-1)
    swipe X0 Y0 X1 Y1      a swipe (the quick bar: swipe 0.5 0.99 0.5 0.85)
    load FILE              load a save state (the second screen kept)
    # ...                  a comment
BTNS: A B L R START SELECT UP DOWN LEFT RIGHT joined with + (A+UP); HOME is GBA DS's menu button (the 3DS's
ZL), R2 fast-forward. The window is the 3DS's: 400x240 over 320x240.
"""
import argparse, json, os, shutil, signal, subprocess, sys, time, zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APPS = {"mgba": "build/host/gba-ds", "armgba": "build/sim-perf/gba-ds", "armgba-thr": "build/sim-perf/gba-ds",
        "armgba-thr2": "build/sim-thr2/gba-ds", "armgba-3ds": "build/sim-perf/gba-ds"}
WINDOW, SCREENS = "400x480", "400x240,320x240"
BIOS = None


def compile_play(path):
    """The .play file as the app's input script lines; returns (lines, shot names, last tick)."""
    t, out, shots = 30, [], []
    for n, raw in enumerate(open(path), 1):
        w = raw.split("#", 1)[0].split()
        if not w:
            continue
        c, a = w[0].lower(), w[1:]
        try:
            if c == "wait":
                t += int(a[0])
            elif c in ("press", "hold"):
                k = int(a[1]) if len(a) > 1 else 4
                out += [f"{t} {a[0]}", f"{t + k} -"]
                t += k + (8 if c == "press" else 1)
            elif c == "mash":
                gap = int(a[2]) if len(a) > 2 else 30
                for _ in range(int(a[1])):
                    out += [f"{t} {a[0]}", f"{t + 4} -"]
                    t += max(gap, 6)
            elif c == "shot":
                name = os.path.basename(a[0])
                out.append(f"{t} SHOT:{name}.bmp")
                shots.append(name)
                t += 2
            elif c == "tap":
                out.append(f"{t} TAP:{float(a[0])},{float(a[1])}")
                t += 12
            elif c == "swipe":
                out.append(f"{t} SWIPE:{','.join(str(float(x)) for x in a[:4])}")
                t += 20
            elif c == "load":
                out.append(f"{t} LOAD:{os.path.abspath(a[0])}")
                t += 4
            else:
                raise ValueError(f"unknown command {c}")
        except (IndexError, ValueError) as e:
            sys.exit(f"{path}:{n}: {e}")
    out.append(f"{t + 2} QUIT")
    return out, shots, t


def find_rom(game, rom_dir):
    if os.path.exists(game):
        return os.path.abspath(game)
    cache = os.path.join(REPO, "build", "romindex.json")
    idx = json.load(open(cache)) if os.path.exists(cache) else {}
    if game not in idx:
        idx = {}
        for f in sorted(os.listdir(rom_dir)):
            p = os.path.join(rom_dir, f)
            try:
                if f.lower().endswith(".zip"):
                    z = zipfile.ZipFile(p)
                    names = [x for x in z.namelist() if x.lower().endswith(".gba")]
                    hdr = z.open(names[0]).read(0xC0) if names else b""
                elif f.lower().endswith(".gba"):
                    hdr = open(p, "rb").read(0xC0)
                else:
                    continue
                if len(hdr) >= 0xB0:
                    idx.setdefault(hdr[0xAC:0xB0].decode("latin1"), []).append(f)
            except Exception:
                pass
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        json.dump(idx, open(cache, "w"))
    if game not in idx:
        sys.exit(f"no ROM with game code {game} in {rom_dir}")
    # The plainest name: no Rev / Beta / Proto / Virtual Console where there is a choice.
    pick = sorted(idx[game], key=lambda f: (any(k in f for k in ("Beta", "Proto", "Demo", "Sample", "Virtual")), "Rev" in f, len(f)))[0]
    return os.path.join(rom_dir, pick)


def run_app(name, rom, script_lines, outdir, last_tick, state):
    d = os.path.join(outdir, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "states"))
    os.makedirs(os.path.join(d, "saves"))
    # The ROM copied in, so nothing the run writes lands beside the player's games.
    local = os.path.join(d, os.path.basename(rom))
    shutil.copy(rom, local)
    with open(os.path.join(d, "script.txt"), "w") as f:
        f.write("\n".join(script_lines) + "\n")
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software",
               GBADS_SYNC="1", GBADS_NOPACE="1", GBADS_DRAW_SHOTS_ONLY="1", GBADS_DEBUG="1",
               GBADS_WINDOW=WINDOW, GBADS_SCREENS=SCREENS)
    if name == "armgba-thr":
        env["GBADS_RENDER_THREAD"] = "1"
    if name == "armgba-thr2":
        env["GBADS_RENDER_THREAD"] = "2"
    if name == "armgba-3ds":
        # drawn as the 3DS draws: two threads, a frame late, the HUD split's picture one frame in five
        env.update(GBADS_RENDER_THREAD="2", GBADS_RENDER_LATE="1", GBADS_SECOND_EVERY="5")
    # The same BIOS on both emulators (else mGBA's stand-in and armGBA's time its calls differently and the
    # runs drift apart), and the pause menu's clock fixed.
    if BIOS:
        env["GBADS_BIOS"] = BIOS
    env["GBADS_CLOCK"] = "12:00"
    args = [os.path.join(REPO, APPS[name]), f"--state-dir={d}/states", f"--save_dir={d}/saves",
            "--quick_bar_hint=0", "--overlays=off", "--input-script=script.txt"]
    if state:
        args.append(f"--load-state={os.path.abspath(state)}")
    args.append(local)
    t0 = time.time()
    with open(os.path.join(d, "log.txt"), "w") as log:
        try:
            p = subprocess.run(args, cwd=d, env=env, stdout=log, stderr=subprocess.STDOUT,
                               timeout=120 + last_tick / 60 * 2)
            code = p.returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    secs = time.time() - t0
    os.remove(local)
    if isinstance(code, int) and code < 0:
        code = f"signal {signal.Signals(-code).name}"
    return d, code, secs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game")
    ap.add_argument("script")
    ap.add_argument("outdir")
    ap.add_argument("--runs", default="mgba,armgba,armgba-thr")
    ap.add_argument("--rom-dir", default=os.environ.get("GBADS_ROM_DIR", "/mnt/storage/Games/roms/gba"))
    ap.add_argument("--state", help="a save state to start from (the app's --load-state)")
    ap.add_argument("--bios", default=os.environ.get("GBADS_BIOS", os.path.join(REPO, "build", "gba_bios.bin")),
                    help="a GBA BIOS both emulators boot (default build/gba_bios.bin, if there; never committed)")
    o = ap.parse_args()
    o.outdir = os.path.abspath(o.outdir)
    global BIOS
    BIOS = os.path.abspath(o.bios) if o.bios and os.path.exists(o.bios) else None
    from PIL import Image, ImageChops

    rom = find_rom(o.game, o.rom_dir)
    lines, shots, last = compile_play(o.script)
    os.makedirs(o.outdir, exist_ok=True)
    runs = o.runs.split(",")
    rep = {"game": o.game, "rom": os.path.basename(rom), "frames": last, "bios": "file" if BIOS else "each emulator's own",
           "runs": {}, "shots": []}
    for r in runs:
        d, code, secs = run_app(r, rom, lines, o.outdir, last, o.state)
        log = open(os.path.join(d, "log.txt"), errors="replace").read().splitlines()
        missing = [s for s in shots if not os.path.exists(os.path.join(d, s + ".bmp"))]
        for s in shots:
            b = os.path.join(d, s + ".bmp")
            if os.path.exists(b):
                Image.open(b).convert("RGB").save(os.path.join(d, s + ".png"))
                os.remove(b)
        keep = ("capture", "map", "hud", "snap", "logo", "second", "error", "fail", "warn", "abort", "assert", "could not", "cannot")
        rep["runs"][r] = {"exit": code, "seconds": round(secs, 1), "missing_shots": missing,
                          "log_notes": [l for l in log if any(k in l.lower() for k in keep)][-60:]}
        print(f"{r}: exit {code}, {secs:.0f} s, {len(shots) - len(missing)}/{len(shots)} shots", flush=True)

    def load(r, s):
        p = os.path.join(o.outdir, r, s + ".png")
        return Image.open(p).convert("RGB") if os.path.exists(p) else None

    def differ(a, b):
        if a is None or b is None:
            return None
        d = ImageChops.difference(a, b)
        diff = d.convert("L").point(lambda v: 255 if v else 0)
        # Visible: a channel off by more than 40 of 255 (not a shade of rounding).
        r, g, bl = d.split()
        vis = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(lambda v: 255 if v > 40 else 0)
        w, h = a.size
        count = lambda m, box: m.crop(box).histogram()[255]
        top, bottom = (0, 0, w, h // 2), (0, h // 2, w, h)
        return {"top": count(diff, top), "bottom": count(diff, bottom), "top_visible": count(vis, top),
                "bottom_visible": count(vis, bottom), "mask": diff}

    sheets, row_imgs = [], []
    for s in shots:
        a, m, t = load("mgba", s), load("armgba", s) or load("armgba-3ds", s), load("armgba-thr", s) or load("armgba-thr2", s)
        am, mt = differ(a, m), differ(m, t)
        entry = {"shot": s}
        if am:
            entry["mgba_vs_armgba"] = {k: v for k, v in am.items() if k != "mask"}
        if mt:
            entry["armgba_vs_thr"] = {"top": mt["top"], "bottom": mt["bottom"]}
        rep["shots"].append(entry)
        base = m or a or t
        if base is None:
            continue
        w, h = base.size
        row = Image.new("RGB", (w * 3 + 20, h), "white")
        if a:
            row.paste(a, (0, 0))
        if m:
            row.paste(m, (w + 10, 0))
        hl = (m or a).convert("L").convert("RGB").point(lambda v: v // 3)
        if am:
            hl.paste((255, 255, 0), mask=am["mask"])
        if mt:
            hl.paste((255, 0, 0), mask=mt["mask"])
        row.paste(hl, (2 * w + 20, 0))
        row_imgs.append((s, row))
    for k in range(0, len(row_imgs), 4):
        group = row_imgs[k:k + 4]
        w, h = group[0][1].size
        sheet = Image.new("RGB", (w, (h + 6) * len(group)), "white")
        for j, (_, im) in enumerate(group):
            sheet.paste(im, (0, j * (h + 6)))
        p = os.path.join(o.outdir, f"sheet-{k // 4 + 1:02d}.png")
        sheet.save(p)
        sheets.append((p, [n for n, _ in group]))
    rep["sheets"] = [{"file": os.path.basename(p), "shots": n} for p, n in sheets]
    nondet = [e["shot"] for e in rep["shots"] if e.get("armgba_vs_thr") and (e["armgba_vs_thr"]["top"] or e["armgba_vs_thr"]["bottom"])]
    rep["nondeterministic_shots"] = nondet
    json.dump(rep, open(os.path.join(o.outdir, "report.json"), "w"), indent=1)
    with open(os.path.join(o.outdir, "report.md"), "w") as f:
        f.write(f"# {o.game}: {rep['rom']}, {last} frames\n\n")
        for r, v in rep["runs"].items():
            f.write(f"- {r}: exit {v['exit']}, {v['seconds']} s" + (f", missing shots {v['missing_shots']}" if v["missing_shots"] else "") + "\n")
        f.write("\n| shot | mgba vs armgba: px differing top / bottom (visibly) | armgba vs armgba-thr |\n|---|---|---|\n")
        for e in rep["shots"]:
            am, mt = e.get("mgba_vs_armgba"), e.get("armgba_vs_thr")
            f.write(f"| {e['shot']} | {f'{am['top']} ({am['top_visible']}) / {am['bottom']} ({am['bottom_visible']})' if am else '-'} | {f'{mt['top']} / {mt['bottom']}' if mt else '-'} |\n")
        f.write("\nSheets: " + ", ".join(os.path.basename(p) for p, _ in sheets) + " (mgba | armgba | yellow: differ, red: armgba-thr differs)\n")
    print(open(os.path.join(o.outdir, "report.md")).read())
    if nondet:
        print("NOT DETERMINISTIC (armgba vs armgba-thr):", ", ".join(nondet))


if __name__ == "__main__":
    main()
