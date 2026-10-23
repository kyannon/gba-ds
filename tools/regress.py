#!/usr/bin/env python3
"""GBA DS regression tests.

Runs the cases in tests/regress/*.json against the fixtures repository
(kyannon/gba-ds-tests, private: ROMs, save states and approved screenshots)
and reports which of the games' rules (docs/games/*.md) still hold.

    tools/regress.py                   run every case
    tools/regress.py fire-emblem       one game's cases (or a case id, or a glob: 'fe-us-*')
    tools/regress.py --update CASE...  approve the current pictures as the goldens
    tools/regress.py --list            list the cases and the rules they check
    tools/regress.py --uncovered       the rules in docs/games/ no case checks yet
    tools/regress.py --prune           delete goldens no case uses any more

Options: --fixtures DIR (default $GBADS_TESTS, else ../gba-ds-tests),
--bin build/host/gba-ds, --harness build/host/harness, --report DIR
(default build/regress: index.html with each failure's pictures), -j N.

Every run is deterministic: GBADS_SYNC=1 makes map captures and logo
extraction run inline, at the same frames each time, so goldens are
compared exactly (a case can allow a tolerance). See docs/TESTING.md.
"""
import argparse, fnmatch, glob, html, json, os, re, shutil, subprocess, sys, tempfile, time
from concurrent.futures import ThreadPoolExecutor

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("regress.py needs Pillow (pip install --break-system-packages pillow)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCREEN_W, SCREEN_H = 1024, 768  # each screen in the test window (GBADS_WINDOW=1024x1536)
SCREENS = {"game": 0, "second": 1}  # default layout: the game on top


def load_cases(pattern_list):
    cases = []
    for path in sorted(glob.glob(os.path.join(ROOT, "tests", "regress", "*.json"))):
        slug = os.path.splitext(os.path.basename(path))[0]
        spec = json.load(open(path))
        for c in spec["cases"]:
            c = dict(c)
            c["slug"] = slug
            c["game"] = spec["game"]
            cases.append(c)
    if not pattern_list:
        return cases
    out = []
    for c in cases:
        if any(p == c["slug"] or fnmatch.fnmatch(c["id"], p) for p in pattern_list):
            out.append(c)
    return out


def hexrgb(s):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def pixels(img):
    """The image's pixels (Pillow 12+ renamed getdata)."""
    return img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()


def near(a, b, tol):
    return all(abs(x - y) <= tol for x, y in zip(a[:3], b[:3]))


class Run:
    """One case's run: its screenshots (each split into the two screens) and log."""

    def __init__(self, case, workdir):
        self.case = case
        self.dir = workdir
        self.shots = {}
        self.log = ""
        self.error = None

    def screen(self, shot, name):
        img = self.shots[shot]
        y = SCREENS[name] * SCREEN_H
        return img.crop((0, y, SCREEN_W, y + SCREEN_H))


def run_app(case, args, workdir):
    r = Run(case, workdir)
    rom = os.path.join(args.fixtures, "roms", case["rom"] + ".zip")
    if not os.path.exists(rom):
        r.error = "missing ROM fixture roms/%s.zip" % case["rom"]
        return r
    statedir = os.path.join(workdir, "state")
    os.makedirs(statedir)
    # the ROM in a folder of the case's own: the game writes its battery save
    # beside it, so nothing lands in the fixtures and every run starts clean
    romdir = os.path.join(workdir, "rom")
    os.makedirs(romdir)
    os.symlink(rom, os.path.join(romdir, case["rom"] + ".zip"))
    if case.get("save"):
        sav = os.path.join(args.fixtures, "saves", case["save"])
        if not os.path.exists(sav):
            r.error = "missing save fixture saves/%s" % case["save"]
            return r
        shutil.copy(sav, os.path.join(romdir, case["rom"] + ".sav"))
    rom = os.path.join(romdir, case["rom"] + ".zip")
    shots = case.get("shots", {"end": case.get("frames", 300)})
    lines = []
    for frame, keys in case.get("input", []):
        if keys.startswith("LOAD:"):  # another state, from the fixtures' states/
            st = os.path.join(args.fixtures, "states", keys[5:])
            if not os.path.exists(st):
                r.error = "missing state fixture states/%s" % keys[5:]
                return r
            keys = "LOAD:" + st
        lines.append((frame, keys))
    for name, frame in shots.items():
        lines.append((frame, "SHOT:" + os.path.join(workdir, name + ".bmp")))
    last = max(f for f, _ in lines) if lines else 1
    lines.append((last + 1, "QUIT"))
    lines.sort(key=lambda x: (x[0], x[1].startswith("SHOT")))
    script = os.path.join(workdir, "input.txt")
    with open(script, "w") as f:
        for frame, what in lines:
            f.write("%d %s\n" % (frame, what))
    cmd = [args.bin, "--state-dir=" + statedir, "--input-script=" + script]
    for k, v in case.get("config", {}).items():
        cmd.append("--%s=%s" % (k, v))
    if case.get("state"):
        st = os.path.join(args.fixtures, "states", case["state"])
        if not os.path.exists(st):
            r.error = "missing state fixture states/%s" % case["state"]
            return r
        cmd.append("--load-state=" + st)
    cmd.append(rom)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
               GBADS_WINDOW="%dx%d" % (SCREEN_W, SCREEN_H * 2), GBADS_SYNC="1", GBADS_DEBUG="1")
    try:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace", timeout=args.timeout)
        r.log = p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        r.error = "timed out after %d s" % args.timeout
        return r
    for name in shots:
        path = os.path.join(workdir, name + ".bmp")
        if not os.path.exists(path):
            r.error = "no screenshot %s (the app exited early?)" % name
            return r
        r.shots[name] = Image.open(path).convert("RGB")
    return r


def run_logo(case, args, workdir):
    """A logo case: extract the release's logo as the app does, keep it as shot 'logo'."""
    r = Run(case, workdir)
    rom = os.path.join(args.fixtures, "roms", case["rom"] + ".zip")
    if not os.path.exists(rom):
        r.error = "missing ROM fixture roms/%s.zip" % case["rom"]
        return r
    os.makedirs(os.path.join(workdir, "rom"))
    os.symlink(rom, os.path.join(workdir, "rom", case["rom"] + ".zip"))
    rom = os.path.join(workdir, "rom", case["rom"] + ".zip")
    script = os.path.join(workdir, "logo.txt")
    open(script, "w").write("logo logo\n")
    p = subprocess.run([args.harness, rom, workdir, script], capture_output=True, text=True, errors="replace",
                       timeout=args.timeout)
    r.log = p.stdout + p.stderr
    path = os.path.join(workdir, "logo.png")
    if "logo: ok" not in r.log or not os.path.exists(path):
        r.error = "logo extraction failed"
        return r
    r.shots["logo"] = Image.open(path).convert("RGBA")
    return r


# ------------------------------------------------------------------ checks

def check(run, chk):
    """True / False, and a description of what was seen."""
    t = chk["type"]
    if t == "log":
        ok = re.search(chk["match"], run.log, re.M) is not None
        return ok, "log %s /%s/" % ("has" if ok else "lacks", chk["match"])
    if t == "no_log":
        m = re.search(chk["match"], run.log, re.M)
        return m is None, "log has %r" % m.group(0) if m else "log lacks /%s/" % chk["match"]
    tol = chk.get("tol", 12)
    if t in ("same", "differs"):
        a, b = chk["a"], chk["b"]
        ia = run.screen(a["shot"], a.get("screen", "second")).crop(tuple(a["rect"]))
        ib = run.screen(b["shot"], b.get("screen", "second")).crop(tuple(b["rect"]))
        if ia.size != ib.size:
            return False, "rects differ in size"
        diff = ImageChops.difference(ia, ib)
        changed = sum(1 for p in pixels(diff) if max(p) > tol) / (ia.size[0] * ia.size[1])
        limit = chk.get("frac", 0.01)
        if t == "same":
            return changed <= limit, "%.2f%% of the pixels differ (allowed %.2f%%)" % (changed * 100, limit * 100)
        return changed > limit, "%.2f%% of the pixels differ (need over %.2f%%)" % (changed * 100, limit * 100)
    img = run.screen(chk["shot"], chk.get("screen", "second"))
    if t in ("pixels", "not_pixels"):
        hits, seen = 0, []
        for x, y, c in chk["probes"]:
            px = img.getpixel((x, y))
            hit = near(px, hexrgb(c), tol)
            hits += hit
            seen.append("(%d,%d)=%02X%02X%02X%s" % (x, y, px[0], px[1], px[2], "" if hit else "≠" + c))
        need = chk.get("min", len(chk["probes"]))
        if t == "pixels":
            return hits >= need, "%d/%d probes match (need %d): %s" % (hits, len(chk["probes"]), need, " ".join(seen))
        return hits == 0, "%d/%d probes match (need 0): %s" % (hits, len(chk["probes"]), " ".join(seen))
    if t in ("uniform", "not_uniform"):
        x0, y0, x1, y1 = chk["rect"]
        region = img.crop((x0, y0, x1, y1))
        want = hexrgb(chk["color"])
        px = list(pixels(region))
        frac = sum(1 for p in px if near(p, want, tol)) / max(1, len(px))
        need = chk.get("frac", 0.99)
        if t == "uniform":
            return frac >= need, "%.1f%% of %s is %s (need %.1f%%)" % (frac * 100, chk["rect"], chk["color"], need * 100)
        return frac < need, "%.1f%% of %s is %s (need under %.1f%%)" % (frac * 100, chk["rect"], chk["color"], need * 100)
    return False, "unknown check type %s" % t


def golden_path(args, case, shot):
    return os.path.join(args.fixtures, "golden", case["slug"], case["id"], shot + ".png")


def compare_golden(run, args, shot):
    case = run.case
    path = golden_path(args, case, shot)
    if not os.path.exists(path):
        return None, "no golden yet (approve with --update %s)" % case["id"]
    want = Image.open(path).convert(run.shots[shot].mode)
    have = run.shots[shot]
    if want.size != have.size:
        return False, "size %s, golden %s" % (have.size, want.size)
    diff = ImageChops.difference(have, want)
    tol = case.get("golden_tol", 0)
    n = sum(1 for p in pixels(diff) if max(p) > tol)
    frac = n / (have.size[0] * have.size[1])
    limit = case.get("golden_frac", 0.0)
    return frac <= limit, "%d pixels differ (%.3f%%, allowed %.3f%%)" % (n, frac * 100, limit * 100)


def evaluate(case, args):
    workdir = tempfile.mkdtemp(prefix=case["id"] + "-", dir=args.work)
    t0 = time.time()
    run = run_logo(case, args, workdir) if case.get("kind") == "logo" else run_app(case, args, workdir)
    res = {"case": case, "dir": workdir, "secs": 0, "checks": [], "goldens": [], "error": run.error}
    if not run.error:
        for chk in case.get("checks", []):
            try:
                ok, what = check(run, chk)
            except Exception as e:  # a bad check (shot name, rect) is a failure, not a crash
                ok, what = False, "check failed to run: %s" % e
            res["checks"].append((chk.get("rule", "?"), chk.get("what", chk["type"]), ok, what))
        for shot in case.get("golden", list(run.shots)):
            if args.update:
                dst = golden_path(args, case, shot)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                run.shots[shot].save(dst)
                res["goldens"].append((shot, True, "approved"))
            else:
                ok, what = compare_golden(run, args, shot)
                res["goldens"].append((shot, ok, what))
                if ok is False:
                    want = Image.open(golden_path(args, case, shot)).convert(run.shots[shot].mode)
                    run.shots[shot].save(os.path.join(workdir, shot + ".png"))
                    want.save(os.path.join(workdir, shot + ".golden.png"))
                    d = ImageChops.difference(run.shots[shot].convert("RGB"), want.convert("RGB"))
                    d.point(lambda v: 255 if v else 0).save(os.path.join(workdir, shot + ".diff.png"))
        for shot, img in run.shots.items():
            p = os.path.join(workdir, shot + ".png")
            if not os.path.exists(p):
                img.save(p)
    res["log"] = run.log
    res["secs"] = time.time() - t0
    res["ok"] = not res["error"] and all(c[2] for c in res["checks"]) and all(g[1] is not False for g in res["goldens"])
    res["missing"] = any(g[1] is None for g in res["goldens"])
    return res


def write_report(results, outdir):
    os.makedirs(outdir, exist_ok=True)
    rows = []
    for r in results:
        c = r["case"]
        cdir = os.path.join(outdir, c["id"])
        if os.path.exists(cdir):
            shutil.rmtree(cdir)
        shutil.copytree(r["dir"], cdir, ignore=shutil.ignore_patterns("*.bmp", "state", "rom", "*.txt"))
        open(os.path.join(cdir, "gba-ds.log"), "w").write(r.get("log", ""))
        status = "pass" if r["ok"] else "FAIL"
        items = []
        if r["error"]:
            items.append("<li class=bad>%s</li>" % html.escape(r["error"]))
        for rule, what, ok, seen in r["checks"]:
            items.append("<li class=%s><b>%s</b> %s: %s</li>" % ("ok" if ok else "bad", html.escape(rule), html.escape(what), html.escape(seen)))
        pics = []
        for shot, ok, seen in r["goldens"]:
            cls = "ok" if ok else ("miss" if ok is None else "bad")
            items.append("<li class=%s>golden <b>%s</b>: %s</li>" % (cls, html.escape(shot), html.escape(seen)))
            for suffix in ("", ".golden", ".diff"):
                f = shot + suffix + ".png"
                if os.path.exists(os.path.join(cdir, f)):
                    pics.append("<figure><img src='%s/%s'><figcaption>%s%s</figcaption></figure>" % (c["id"], f, shot, suffix))
        rows.append("<section class=%s><h2>%s <small>%s · %s · %.1f s</small></h2><p>%s</p><ul>%s</ul>%s</section>" % (
            status.lower(), html.escape(c["id"]), html.escape(c["game"]), status, r["secs"],
            html.escape(c.get("desc", "")), "".join(items), "".join(pics)))
    fails = sum(1 for r in results if not r["ok"])
    page = """<!doctype html><meta charset=utf-8><title>GBA DS regression</title>
<style>body{font:14px system-ui;margin:16px;background:#fff;color:#111}section{border-top:1px solid #ccc;padding:8px 0}
.fail h2{color:#b00}.ok{color:#060}.bad{color:#b00}.miss{color:#a60}img{max-width:256px;image-rendering:pixelated;border:1px solid #ccc}
figure{display:inline-block;margin:4px}small{font-weight:normal;color:#666}</style>
<h1>GBA DS regression: %d of %d cases pass</h1>%s""" % (len(results) - fails, len(results), "".join(
        sorted(rows, key=lambda s: not s.startswith("<section class=fail"))))
    open(os.path.join(outdir, "index.html"), "w").write(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("only", nargs="*", help="game slugs, case ids or globs")
    ap.add_argument("--fixtures", default=os.environ.get("GBADS_TESTS", os.path.join(os.path.dirname(ROOT), "gba-ds-tests")))
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "host", "gba-ds"))
    ap.add_argument("--harness", default=os.path.join(ROOT, "build", "host", "harness"))
    ap.add_argument("--report", default=os.path.join(ROOT, "build", "regress"))
    ap.add_argument("--update", action="store_true", help="save the current pictures as the goldens")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--uncovered", action="store_true")
    ap.add_argument("--prune", action="store_true", help="delete goldens no case uses")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--timeout", type=int, default=300)
    args = ap.parse_args()
    cases = load_cases(args.only)
    if args.prune:
        want = set()
        for c in load_cases([]):
            shots = ["logo"] if c.get("kind") == "logo" else c.get("golden", list(c.get("shots", {"end": 0})))
            want |= {golden_path(args, c, s) for s in shots}
        gone = [f for f in glob.glob(os.path.join(args.fixtures, "golden", "*", "*", "*.png")) if f not in want]
        for f in gone:
            os.remove(f)
            print("removed", os.path.relpath(f, args.fixtures))
        for d in glob.glob(os.path.join(args.fixtures, "golden", "*", "*")):
            if os.path.isdir(d) and not os.listdir(d):
                os.rmdir(d)
        print("%d unused goldens removed" % len(gone))
        return
    if args.uncovered:
        covered = set()
        for c in load_cases([]):
            covered |= set(c.get("rules", [])) | {ch.get("rule") for ch in c.get("checks", [])}
        n, hand = 0, []
        for doc in sorted(glob.glob(os.path.join(ROOT, "docs", "games", "*.md"))):
            for m in re.finditer(r"^\*\*([A-Z0-9]+-\d+)\. ([^*]+)\*\*(.*)$", open(doc).read(), re.M):
                if "retired" in m.group(3)[:40]:
                    continue
                by = re.search(r"\*By hand: ([^*]+)\*", m.group(3))
                if by:
                    hand.append((m.group(1), m.group(2).rstrip("."), by.group(1)))
                elif m.group(1) not in covered:
                    print("%-8s %-28s %s" % (m.group(1), m.group(2).rstrip("."), os.path.basename(doc)))
                    n += 1
        print("%d rules without a case" % n)
        if hand:
            print("\nChecked by hand (before a release):")
            for rid, name, why in hand:
                print("%-8s %-28s %s" % (rid, name, why))
        return
    if not cases:
        sys.exit("no cases match %s" % " ".join(args.only))
    if args.list:
        for c in cases:
            rules = sorted({ch.get("rule", "?") for ch in c.get("checks", [])} | set(c.get("rules", [])))
            print("%-36s %-5s %s  [%s]" % (c["id"], c["rom"], c.get("desc", ""), " ".join(rules)))
        return
    if args.update and not args.only:
        sys.exit("--update needs the cases to approve (a game, case ids or a glob)")
    for need in ((args.bin, "make"), (args.harness, "make tools")):
        if not os.path.exists(need[0]):
            sys.exit("%s not found: run %s" % need)
    if not os.path.isdir(os.path.join(args.fixtures, "roms")):
        sys.exit("fixtures not found at %s (clone kyannon/gba-ds-tests there, or pass --fixtures / set GBADS_TESTS)" % args.fixtures)
    args.work = tempfile.mkdtemp(prefix="gbads-regress-")
    with ThreadPoolExecutor(max_workers=args.j) as pool:
        results = list(pool.map(lambda c: evaluate(c, args), cases))
    write_report(results, args.report)
    fails = [r for r in results if not r["ok"]]
    for r in results:
        c = r["case"]
        mark = "ok  " if r["ok"] else "FAIL"
        print("%s %-36s %5.1fs" % (mark, c["id"], r["secs"]))
        if r["error"]:
            print("       %s" % r["error"])
        for rule, what, ok, seen in r["checks"]:
            if not ok:
                print("       %s %s: %s" % (rule, what, seen))
        for shot, ok, seen in r["goldens"]:
            if ok is not True:
                print("       golden %s: %s" % (shot, seen))
    shutil.rmtree(args.work, ignore_errors=True)
    print("\n%d of %d cases pass%s. Report: %s" % (len(results) - len(fails), len(results),
          " (goldens approved)" if args.update else "", os.path.join(args.report, "index.html")))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
