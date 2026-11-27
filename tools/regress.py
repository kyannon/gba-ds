#!/usr/bin/env python3
"""GBA DS regression tests.

Runs the cases in tests/regress/*.json against the fixtures repository
(kyannon/gba-ds-tests, private: ROMs, save states and approved screenshots)
and reports which of the games' rules (docs/games/*.md) still hold.

    tools/regress.py                   run every case
    tools/regress.py fire-emblem       one game's cases (or a case id, or a glob: 'fe-us-*')
    tools/regress.py --update CASE...  approve the current pictures as the goldens
    tools/regress.py --list            list the cases and the rules they check
    tools/regress.py --changed [BASE]  only the games a change touches (since BASE: by
                                       default where this branch left origin/dev, plus
                                       uncommitted changes); shared code runs them all
    tools/regress.py --uncovered       the rules in docs/games/ no case checks yet, and
                                       the releases tested only on their logo
    tools/regress.py --prune           delete goldens no case uses any more

Options: --fixtures DIR (default $GBADS_TESTS, else ../gba-ds-tests),
--bin build/host/gba-ds, --harness build/host/harness, --report DIR
(default build/regress: index.html with each failure's pictures), -j N
(default: the CPU count + 1), --shard I/N (the Ith of N even shares of
the cases, for running on several machines), --json FILE (each case's results, for tools/romhack.py), --any-rom (run
cases on fixture ROMs that aren't the tested dumps: tools/romhack.py's patched
ROMs), --paced (at 60 frames a second, drawing
every frame, as players run it; by default the app runs as fast as it
can and draws only the frames a screenshot or a touch needs).

Every run is deterministic: GBADS_SYNC=1 makes map captures and logo
extraction run inline, at the same frames each time, so goldens are
compared exactly (a case can allow a tolerance). See docs/TESTING.md.
"""
import argparse, fnmatch, glob, html, json, os, re, shutil, struct, subprocess, sys, tempfile, time, zipfile
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
            c["rulesFile"] = spec.get("rules")
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
        self.ra_states = None  # a "retroarch" case's stand-in state folder
        self.driver = None  # a "driver" case's (ok, what): the module alone against the app

    def screen(self, shot, name):
        img = self.shots[shot]
        y = SCREENS[name] * SCREEN_H
        return img.crop((0, y, SCREEN_W, y + SCREEN_H))


_testedDumps = {}
ANY_ROM = False  # --any-rom (tools/romhack.py: a patched ROM in the fixtures on purpose)


def dump_error(case, rom):
    """An error if the fixture ROM isn't the dump its rules file's Dumps row
    marks tested (a patched or other dump in the fixtures); None if it is, or
    the rules have no Dumps row yet (tools/make_dumps.py writes them)."""
    rules = case.get("rulesFile")
    if not rules or ANY_ROM:
        return None
    if rules not in _testedDumps:
        tested = None
        for line in open(os.path.join(ROOT, rules), encoding="utf-8"):
            if line.startswith("| Dumps |") or line.startswith("| Mods |"):
                tested = tested if tested is not None else {}
                for part in line.split(" · "):
                    m = re.search(r"`([A-Z0-9]{4})`", part)
                    if m:
                        tested.setdefault(m.group(1), set()).update(re.findall(r"`([0-9a-f]{8})` \(tested\)", part))
        _testedDumps[rules] = tested
    tested = _testedDumps[rules]
    if tested is None:
        return None
    with zipfile.ZipFile(rom) as z:
        crcs = ["%08x" % i.CRC for i in z.infolist() if i.filename.lower().endswith(".gba")]
    if crcs and crcs[0] in tested.get(case["rom"][:4], set()):  # (CODE+name: a mod, or another dump of it)
        return None
    return "fixture roms/%s.zip (CRC32 %s) isn't the tested dump in %s's Dumps row (a patched ROM? tools/make_dumps.py)" % (
        case["rom"], crcs[0] if crcs else "?", rules)


_releaseNames = None


def release_name(code):
    """The release's No-Intro name as GBA DS names its patches folder
    (profile_release_name: the tested dump's, else the first official one's,
    the title with its region and languages only), or None."""
    global _releaseNames
    if _releaseNames is None:
        names = {}  # (built whole before it's shared: cases run on several threads)
        text = open(os.path.join(ROOT, "src", "dumps_data.h"), encoding="utf-8").read()
        for m in re.finditer(r'\{ "(\w{4})", 0x[0-9a-f]{8}, (true|false), false, "((?:[^"\\]|\\.)*)" \}', text):
            c, tested, n = m.group(1), m.group(2) == "true", m.group(3)
            if c not in names or (tested and not names[c][1]):
                names[c] = (n, tested)
        _releaseNames = names
    if code not in _releaseNames:
        return None
    n = _releaseNames[code][0]
    p = n.find("(")
    if p < 0:
        return n
    e = n.index(")", p)
    keep = e + 1
    if n[e + 1:e + 3] == " (":
        e2 = n.find(")", e + 2)
        if e2 > e + 3 and re.fullmatch(r"[A-Za-z,]+", n[e + 3:e2]):
            keep = e2 + 1
    return n[:keep]


def header_name(romzip, code):
    """A game the dumps don't list: its patches folder as paths.c patch_folder names it, from
    the ROM header's title: "TITLE (CODE)" (the GBA DS widescreen test ROM's "GBADS WIDE (ZWSE)")."""
    try:
        with zipfile.ZipFile(romzip) as z:
            gba = [i for i in z.infolist() if i.filename.lower().endswith(".gba")]
            head = z.read(gba[0])[0xA0:0xAC] if gba else b""
    except (OSError, zipfile.BadZipFile):
        return None
    title = head.split(b"\0")[0].decode("latin-1").rstrip(" ")
    name = "%s (%s)" % (title or "GBA", code)
    return re.sub(r'[:/\\*?"<>|]', "_", name)


def patches_on(case):
    """A case's "patch_on": one file name or a list, in the order applied."""
    on = case.get("patch_on") or []
    return [on] if isinstance(on, str) else list(on)


def run_app(case, args, workdir):
    r = Run(case, workdir)
    rom = os.path.join(args.fixtures, "roms", case["rom"] + ".zip")
    if not os.path.exists(rom):
        r.error = "missing ROM fixture roms/%s.zip" % case["rom"]
        return r
    r.error = dump_error(case, rom)
    if r.error:
        return r
    statedir = os.path.join(workdir, "state")
    os.makedirs(statedir)
    # "state_files" {a name in the state folder: a fixture under states/, or "text:..." for its content}:
    # what an earlier play left there (the auto state and its ".colours" note, GB-6: the resume logic)
    for name, spec in case.get("state_files", {}).items():
        dst = os.path.join(statedir, name)
        if spec.startswith("text:"):
            with open(dst, "w") as f:
                f.write(spec[5:])
        else:
            src = os.path.join(args.fixtures, "states", spec)
            if not os.path.exists(src):
                r.error = "missing state fixture states/%s" % spec
                return r
            shutil.copy(src, dst)  # (a copy: GBA DS saves over it on leaving)
    # the ROM in a folder of the case's own: the game writes its battery save
    # beside it, so nothing lands in the fixtures and every run starts clean
    romdir = os.path.join(workdir, "rom")
    os.makedirs(romdir)
    os.symlink(rom, os.path.join(romdir, case["rom"] + ".zip"))
    # "rom_files" {a file name in the game list's folder: a ROM fixture's name}: other ROMs listed beside it
    for name, other in case.get("rom_files", {}).items():
        src = os.path.join(args.fixtures, "roms", other + ".zip")
        if not os.path.exists(src):
            r.error = "missing ROM fixture roms/%s.zip" % other
            return r
        r.error = dump_error(dict(case, rom=other), src)
        if r.error:
            return r
        os.symlink(src, os.path.join(romdir, name))
    if case.get("patch"):  # a patch file beside the ROM (GEN-9), from the fixtures' patches/
        pf = os.path.join(args.fixtures, "patches", case["patch"])
        if not os.path.exists(pf):
            r.error = "missing patch fixture patches/%s" % case["patch"]
            return r
        os.symlink(pf, os.path.join(romdir, case["rom"] + os.path.splitext(pf)[1]))
    # patches in the game's own folder (GEN-9): "patch_folder" lists the
    # fixtures' patches/ files put there, "patch_on" the one switched on (or
    # a list: the ones on, in the order they're applied)
    patchdir = os.path.join(workdir, "patches")
    if case.get("patch_folder") or case.get("patch_on") or "patch_separate" in case:
        name = release_name(case["rom"][:4]) or header_name(rom, case["rom"][:4])
        if not name:
            r.error = "no release name for %s (src/dumps_data.h)" % case["rom"][:4]
            return r
        folder = os.path.join(patchdir, name)
        os.makedirs(folder, exist_ok=True)
        for f in case.get("patch_folder", []) + patches_on(case):
            pf = os.path.join(args.fixtures, "patches", f)
            if not os.path.exists(pf):
                r.error = "missing patch fixture patches/%s" % f
                return r
            if not os.path.exists(os.path.join(folder, f)):
                os.symlink(pf, os.path.join(folder, f))
        if case.get("patch_on") or "patch_separate" in case:
            # ("patch_separate": false: the separate battery save and states switches off)
            sep = 1 if case.get("patch_separate", True) else 0
            with open(os.path.join(statedir, case["rom"] + ".patches"), "w") as f:
                f.write("".join("on %s\n" % p for p in patches_on(case)) + "separate_save %d\nseparate_states %d\n" % (sep, sep))
    # GEN-11: "logo_folder" {name there: fixture under logos/}, the folder
    # given as --logo_folder (named as a scraper would, to test the lookup);
    # "own_logo": a fixture under logos/, put as <asset_dir>/logos/CODE.<ext>
    extra = []
    if case.get("logo_folder"):
        lf = os.path.join(workdir, "scraped")
        os.makedirs(lf)
        for name, f in case["logo_folder"].items():
            src = os.path.join(args.fixtures, "logos", f)
            if not os.path.exists(src):
                r.error = "missing logo fixture logos/%s" % f
                return r
            os.symlink(src, os.path.join(lf, name))
        extra.append("--logo_folder=" + lf)
    if case.get("own_logo"):
        src = os.path.join(args.fixtures, "logos", case["own_logo"])
        if not os.path.exists(src):
            r.error = "missing logo fixture logos/%s" % case["own_logo"]
            return r
        ad = os.path.join(workdir, "assets")
        os.makedirs(os.path.join(ad, "logos"))
        os.symlink(src, os.path.join(ad, "logos", case["rom"][:4] + os.path.splitext(src)[1]))
        extra.append("--asset_dir=" + ad)
    # GEN-13: "retroarch" {"cfg": retroarch.cfg's text ("{states}": its state
    # folder; default: savestate_directory = that folder), "states": {a name in
    # that folder ("mGBA/AWRE.state3"): a fixture under states/, the last one
    # listed the newest}}: a stand-in RetroArch, slots shared with it
    if "retroarch" in case:
        ra = case["retroarch"]
        sd = os.path.join(workdir, "retroarch", "states")
        os.makedirs(sd)
        cfgfile = os.path.join(workdir, "retroarch", "retroarch.cfg")
        with open(cfgfile, "w") as f:
            f.write(ra.get("cfg", 'savestate_directory = "{states}"\n').replace("{states}", sd))
        names = list(ra.get("states", {}).items())
        for i, (name, fx) in enumerate(names):
            src = os.path.join(args.fixtures, "states", fx)
            if not os.path.exists(src):
                r.error = "missing state fixture states/%s" % fx
                return r
            dst = os.path.join(sd, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(src, dst)  # (a copy: GBA DS may save over it)
            # (in order, a minute apart, from noon UTC on Jan 1, 2020: slot
            # labels show the same date on any machine, in any time zone)
            t = 1577880000 + 60 * i
            os.utime(dst, (t, t))
        r.ra_states = sd
        extra += ["--retroarch_cfg=" + cfgfile, "--state_folder=retroarch"]
    # GEN-15: "bios": a fixture under bios/, put as <asset_dir>/bios/gba_bios.bin
    # (the player's own GBA BIOS; the case's "config" {"gba_bios": 1} switches it on)
    if case.get("bios"):
        src = os.path.join(args.fixtures, "bios", case["bios"])
        if not os.path.exists(src):
            r.error = "missing bios fixture bios/%s" % case["bios"]
            return r
        ad = os.path.join(workdir, "assets")
        os.makedirs(os.path.join(ad, "bios"), exist_ok=True)
        os.symlink(src, os.path.join(ad, "bios", "gba_bios.bin"))
        if "--asset_dir=" + ad not in extra:
            extra.append("--asset_dir=" + ad)
    # GEN-17: "cheat_files" {name there: fixture under cheats/}, put in
    # <asset_dir>/cheats/ (a file is matched to its ROM by name, and the case's
    # ROM is named by its code: "BFZE (USA) (Code Breaker).cht")
    if case.get("cheat_files"):
        ad = os.path.join(workdir, "assets")
        os.makedirs(os.path.join(ad, "cheats"), exist_ok=True)
        for name, f in case["cheat_files"].items():
            src = os.path.join(args.fixtures, "cheats", f)
            if not os.path.exists(src):
                r.error = "missing cheat fixture cheats/%s" % f
                return r
            shutil.copy(src, os.path.join(ad, "cheats", name))
        if "--asset_dir=" + ad not in extra:
            extra.append("--asset_dir=" + ad)
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
    # (the start-up hint is timed in real seconds: off, so the pictures are exact;
    # overlays off, so the goldens are the games' pictures alone: a case can turn them on)
    cmd = [args.bin, "--state-dir=" + statedir, "--input-script=" + script, "--quick_bar_hint=0", "--overlays=off", "--patch_dir=" + patchdir] + extra
    for k, v in case.get("config", {}).items():
        cmd.append("--%s=%s" % (k, v))
    if case.get("state"):
        st = os.path.join(args.fixtures, "states", case["state"])
        if not os.path.exists(st):
            r.error = "missing state fixture states/%s" % case["state"]
            return r
        cmd.append("--load-state=" + st)
    if case.get("kind") == "library":  # the game list, with the case's ROM in it
        cmd.append("--rom_dir=" + romdir)
    else:
        cmd.append(rom)
    # SDL's own software renderer, the same arithmetic on any machine: through
    # OpenGL (Mesa's llvmpipe, where a machine has it) blended pictures (text,
    # its shadow, a dimmed map) came out a shade apart from one CPU or Mesa to
    # another, so goldens made on a session's container failed on GitHub's runners
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software",
               GBADS_WINDOW="%dx%d" % (SCREEN_W, SCREEN_H * 2), GBADS_SYNC="1", GBADS_DEBUG="1",
               GBADS_RTC_FIXED="1794934800",
               # the cartridge clock above is 17:00 UTC; mGBA shows it in the machine's
               # time zone, so a game that draws its clock (Boktai's, BT-8) read 12:00 on
               # a session's container (New York) and 17:00 on GitHub's UTC runners: the
               # zone is pinned (POSIX "EST5": five hours behind, no DST, no tzdata needed)
               TZ="EST5",
               # the first-run language question (issue #92, GEN-18) is never asked unless a case
               # asks for it (a release version's build would ask it in every fresh state folder)
               GBADS_LANG_ASK="0")
    if not args.paced:
        # (as fast as it goes, drawing only the frames a screenshot or a
        # touch needs: the same pictures, in under half the time)
        env.update(GBADS_NOPACE="1", GBADS_DRAW_SHOTS_ONLY="1")
    # WS-6: the widescreen patches' list and files from the repo's own widescreen/ folder, served
    # here, for a case with "widescreen_list": true; for every other case an address nothing
    # answers, so no case reaches GitHub (its list would change what a Patches menu shows)
    env["GBADS_WIDE_URL"] = widescreen_server() if case.get("widescreen_list") else "http://127.0.0.1:9/"
    env.update({k: str(v) for k, v in case.get("env", {}).items()})  # (a case's own: GBADS_TOGGLE_WAIT...)
    # "driver": true (or --driver): the app traces its calls into the second-screen module, and
    # tools/second_driver.c replays them with the module alone (discussion #75): see driver_check
    driver = (case.get("driver") or args.driver) and case.get("kind") != "library"
    tracedir = os.path.join(workdir, "trace")
    if driver:
        os.makedirs(tracedir)
        env["GBADS_MODULE_TRACE"] = tracedir
    try:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace", timeout=args.timeout)
        r.log = p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        r.error = "timed out after %d s" % args.timeout
        return r
    for name in shots:
        path = os.path.join(workdir, name + ".bmp")
        if not os.path.exists(path):
            tail = [l for l in r.log.strip().splitlines() if l.strip()][-2:]
            r.error = "no screenshot %s (the app exited early, status %s%s)" % (
                name, p.returncode, ": " + " / ".join(tail) if tail else "")
            return r
        r.shots[name] = Image.open(path).convert("RGB")
    if driver:
        env.pop("GBADS_MODULE_TRACE", None)
        sav = os.path.join(args.fixtures, "saves", case["save"]) if case.get("save") else None
        r.driver = driver_check(args, rom, tracedir, os.path.join(workdir, "driver"), env, sav)
    return r


def driver_check(args, rom, tracedir, outdir, env, save=None):
    """The module alone against the app: second-driver replays the app's trace; its own trace and
    every picture it writes (NAME.second: what the second screen shows, NAME.game: the game's
    picture) must be the app's, byte for byte. (ok, what)"""
    if not os.path.exists(args.driver_bin):
        return False, "no %s (make tools)" % os.path.relpath(args.driver_bin, ROOT)
    try:
        p = subprocess.run([args.driver_bin, rom, tracedir, outdir] + ([save] if save else []), env=env, capture_output=True, text=True,
                           errors="replace", timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return False, "second-driver timed out"
    if p.returncode != 0:
        tail = [l for l in (p.stdout + p.stderr).strip().splitlines() if l.strip()][-2:]
        return False, "second-driver failed: " + " / ".join(tail)
    names = sorted(n for n in os.listdir(tracedir) if n.endswith((".second", ".game")))
    if not names:
        return False, "the app's trace has no screenshots"
    kinds = ["off", "fight", "HUD", "map", "logo"]
    for n in ["trace.txt"] + names:
        a, b = os.path.join(tracedir, n), os.path.join(outdir, n)
        if not os.path.exists(b):
            return False, "second-driver wrote no %s" % n
        da, db = open(a, "rb").read(), open(b, "rb").read()
        if da == db:
            continue
        if n == "trace.txt":
            la, lb = da.decode(errors="replace").splitlines(), db.decode(errors="replace").splitlines()
            i = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            return False, "the traces part at line %d: app %r, driver %r" % (
                i + 1, la[i] if i < len(la) else "(end)", lb[i] if i < len(lb) else "(end)")
        if n.endswith(".second"):
            ha, hb = struct.unpack("<4i", da[:16]), struct.unpack("<4i", db[:16])
            if ha != hb:
                desc = lambda h: "%s%s %dx%d" % (kinds[h[0]] if 0 <= h[0] < 5 else h[0], ", dimmed" if h[1] else "", h[2], h[3])
                return False, "%s: the app shows %s, the driver %s" % (n, desc(ha), desc(hb))
            off = 16
        else:
            off = 8
        diff = sum(1 for i in range(off, min(len(da), len(db)), 4) if da[i:i + 4] != db[i:i + 4])
        return False, "%s: %d pixels differ" % (n, diff)
    return True, "%d pictures and the trace (%d lines) match" % (len(names), open(os.path.join(tracedir, "trace.txt")).read().count("\n"))


def run_logo(case, args, workdir):
    """A logo case: extract the release's logo as the app does, keep it as shot 'logo'."""
    r = Run(case, workdir)
    rom = os.path.join(args.fixtures, "roms", case["rom"] + ".zip")
    if not os.path.exists(rom):
        r.error = "missing ROM fixture roms/%s.zip" % case["rom"]
        return r
    r.error = dump_error(case, rom)
    if r.error:
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


_wideServer = None
_wideLock = __import__("threading").Lock()


def widescreen_server():
    """http://127.0.0.1:PORT/ serving the repo's widescreen/ folder (started once)."""
    global _wideServer
    with _wideLock:
        return _widescreen_server()


def _widescreen_server():
    global _wideServer
    if _wideServer is None:
        import functools, http.server, threading
        class Quiet(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *a):
                pass
        handler = functools.partial(Quiet, directory=os.path.join(ROOT, "widescreen"))
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        _wideServer = "http://127.0.0.1:%d/" % srv.server_address[1]
    return _wideServer


# ------------------------------------------------------------------ checks

def check(run, chk):
    """True / False, and a description of what was seen."""
    t = chk["type"]
    if t == "log":
        ok = re.search(chk["match"], run.log, re.M) is not None
        return ok, "log %s /%s/" % ("has" if ok else "lacks", chk["match"])
    if t in ("file", "no_file"):  # a file in a "retroarch" case's state folder: there, starting with "starts"
        path = os.path.join(run.ra_states or run.dir, chk["path"])
        if t == "no_file":
            return not os.path.exists(path), "%s %s" % (chk["path"], "is there" if os.path.exists(path) else "isn't there")
        if not os.path.exists(path):
            return False, "no %s" % chk["path"]
        want = chk.get("starts", "").encode("latin-1")
        with open(path, "rb") as f:
            head = f.read(max(len(want), 8))
        return head.startswith(want), "%s starts %r" % (chk["path"], head)
    if t == "image_size":  # a picture the app saved (a BMP or PNG: the resume picture, GB-13) has this size
        path = os.path.join(run.ra_states or run.dir, chk["path"])
        if not os.path.exists(path):
            return False, "no %s" % chk["path"]
        size = Image.open(path).size
        return list(size) == list(chk["size"]), "%s is %dx%d (need %dx%d)" % (chk["path"], size[0], size[1], chk["size"][0], chk["size"][1])
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


# a failing case's log lines worth printing with it
LOG_KEYS = ("renderer: ", "ROM: ", "patches: ", "patch ", "load state: ", "game list: ", "osd: ", "logo: ")


def compare_golden(run, args, shot):
    case = run.case
    path = golden_path(args, case, shot)
    if not os.path.exists(path):
        return None, "no golden yet (approve with --update %s)" % case["id"]
    want = Image.open(path).convert(run.shots[shot].mode)
    have = run.shots[shot]
    if want.size != have.size:
        return False, "size %s, golden %s" % (have.size, want.size)
    # rects [x0, y0, x1, y1] left out (the app's version number...); a fifth item, a shot's name
    # (or a list of them), keeps the rect to those shots
    masks = [b for b in case.get("golden_mask", [])
             if len(b) == 4 or shot in ([b[4]] if isinstance(b[4], str) else b[4])]
    if masks:
        want, have = want.copy(), have.copy()
        for box in masks:
            for im in (want, have):
                im.paste((0,) * len(im.getbands()), tuple(box[:4]))
    diff = ImageChops.difference(have, want)
    tol = case.get("golden_tol", 0)
    n = sum(1 for p in pixels(diff) if max(p) > tol)
    frac = n / (have.size[0] * have.size[1])
    limit = case.get("golden_frac", 0.0)
    where = ""
    if n:  # (where they differ: what a log can say of a picture it can't show, as on GitHub)
        box = diff.convert("L").point(lambda v: 255 if v > tol else 0).getbbox()
        if box: where = ", in x %d-%d, y %d-%d" % (box[0], box[2] - 1, box[1], box[3] - 1)
    return frac <= limit, "%d pixels differ (%.3f%%, allowed %.3f%%%s)" % (n, frac * 100, limit * 100, where)


def evaluate(case, args):
    workdir = tempfile.mkdtemp(prefix=case["id"] + "-", dir=args.work)
    t0 = time.time()
    run = run_logo(case, args, workdir) if case.get("kind") == "logo" else run_app(case, args, workdir)
    res = {"case": case, "dir": workdir, "secs": 0, "checks": [], "goldens": [], "error": run.error}
    if not run.error:
        if run.driver:
            res["checks"].append(("MODULE", "the module alone (second-driver) matches the app", run.driver[0], run.driver[1]))
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


def toggle_lint(cases):
    """GEN-14: a case that turns the toggle on needs its game's rules file to say what the toggle does
    (a Toggle row that isn't `none`), and every Toggle row says `pages`, `views` or `none`."""
    bad, seen = [], {}
    for c in cases:
        slug = c["slug"]
        if slug not in seen:
            f = os.path.join(ROOT, "docs", "games", slug + ".md")
            row = None
            if os.path.exists(f):
                for line in open(f, encoding="utf-8"):
                    if line.startswith("| Toggle |"):
                        row = line.split("|")[2].strip()
                        break
            seen[slug] = row
            if row is not None and not row.lstrip("`").startswith(("pages", "views", "none")):
                bad.append("%s.md: the Toggle row starts with `pages`, `views` or `none`, not %r" % (slug, row[:30]))
        row = seen[slug]
        if (c.get("config") or {}).get("toggle") == "1" and (row is None or row.lstrip("`").startswith("none")):
            bad.append("%s: turns the toggle on, but %s.md has no Toggle row saying what it does" % (c["id"], slug))
    return sorted(set(bad))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("only", nargs="*", help="game slugs, case ids or globs")
    ap.add_argument("--fixtures", default=os.environ.get("GBADS_TESTS", os.path.join(os.path.dirname(ROOT), "gba-ds-tests")))
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "host", "gba-ds"))
    ap.add_argument("--harness", default=os.path.join(ROOT, "build", "host", "harness"))
    ap.add_argument("--driver", action="store_true",
                    help="every case also checks the module alone against the app (as \"driver\": true cases do)")
    ap.add_argument("--driver-bin", default=os.path.join(ROOT, "build", "host", "second-driver"))
    ap.add_argument("--report", default=os.path.join(ROOT, "build", "regress"))
    ap.add_argument("--update", action="store_true", help="save the current pictures as the goldens")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--uncovered", action="store_true")
    ap.add_argument("--prune", action="store_true", help="delete goldens no case uses")
    ap.add_argument("-j", type=int, default=(os.cpu_count() or 2) + 1)
    ap.add_argument("--paced", action="store_true",
                    help="run the app as players do: at 60 frames a second, drawing every frame")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--changed", nargs="?", const="auto", metavar="BASE",
                    help="only the games changed since BASE (default: where this branch left origin/dev)")
    ap.add_argument("--shard", metavar="I/N", help="run the Ith of N shares of the cases (1-based)")
    ap.add_argument("--fixture-paths", action="store_true",
                    help="list the fixture files the selected cases need (for a sparse checkout) and stop")
    ap.add_argument("--games-json", metavar="FILE", help="with --changed: write the games picked (or \"all\") as JSON and stop")
    ap.add_argument("--json", metavar="FILE", help="also write each case's results as JSON (tools/romhack.py reads it)")
    ap.add_argument("--any-rom", action="store_true",
                    help="run cases whose fixture ROM isn't the tested dump (tools/romhack.py's patched ROMs)")
    args = ap.parse_args()
    global ANY_ROM
    ANY_ROM = args.any_rom
    args.fixtures = os.path.abspath(args.fixtures)  # (cases run in their own folders)
    if args.changed:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import regress_changed
        slugs, reasons, base = regress_changed.select(None if args.changed == "auto" else args.changed)
        print("changed since %s:" % base[:10])
        for r in reasons:
            print("  " + r)
        picked = "all" if slugs is None else slugs
        print("games: %s\n" % ("all (shared code changed)" if slugs is None else " ".join(slugs) or "none"))
        if args.games_json:
            json.dump(picked, open(args.games_json, "w"))
            return
        if slugs is not None:
            if not slugs:
                print("Nothing to run: no change affects a game.")
                return
            args.only = (args.only or []) + slugs
    cases = load_cases(args.only)
    bad = toggle_lint(cases)
    if bad:
        print("Toggle lint (GEN-14):")
        for b in bad:
            print("  " + b)
        sys.exit(1)
    if args.shard:
        i, n = (int(v) for v in args.shard.split("/"))
        # (dealt round-robin by game then case, so each share gets a mix of slow and quick games)
        cases = sorted(cases, key=lambda c: (c["slug"], c["id"]))[i - 1::n]
        if not cases:
            print("Nothing in share %d of %d." % (i, n))
            return
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
                if "retired" in m.group(3)[:40].lower():
                    continue
                by = re.search(r"\*By hand: ([^*]+)\*", m.group(3))
                if by:
                    hand.append((m.group(1), m.group(2).rstrip("."), by.group(1)))
                elif m.group(1) not in covered:
                    print("%-8s %-28s %s" % (m.group(1), m.group(2).rstrip("."), os.path.basename(doc)))
                    n += 1
        print("%d rules without a case" % n)
        # releases whose only case is their logo: everything else they show is
        # assumed to work as another release's does
        thin = []
        for doc in sorted(glob.glob(os.path.join(ROOT, "docs", "games", "*.md"))):
            slug = os.path.basename(doc)[:-3]
            row = next((l for l in open(doc) if l.startswith("| Releases")), "")
            mine = [c for c in load_cases([]) if c["slug"] == slug]
            for code in re.findall(r"`([A-Z0-9]{4})`", row):
                cs = [c for c in mine if c.get("rom") == code]
                if not cs:
                    thin.append((code, slug, "no case at all"))
                elif all(c.get("kind") == "logo" for c in cs):
                    thin.append((code, slug, "only its logo"))
        if thin:
            print("\nReleases tested only on their logo (or not at all):")
            for code, slug, what in thin:
                print("%-6s %-36s %s" % (code, slug, what))
        if hand:
            print("\nChecked by hand (before a release):")
            for rid, name, why in hand:
                print("%-8s %-28s %s" % (rid, name, why))
        return
    if not cases:
        sys.exit("no cases match %s" % " ".join(args.only))
    if args.fixture_paths:
        need = set()
        for c in cases:
            need.add("roms/%s.zip" % c["rom"])
            for other in c.get("rom_files", {}).values():
                need.add("roms/%s.zip" % other)
            need.add("golden/%s/%s/" % (c["slug"], c["id"]))
            if c.get("state"):
                need.add("states/" + c["state"])
            if c.get("save"):
                need.add("saves/" + c["save"])
            for f in ([c["patch"]] if c.get("patch") else []) + c.get("patch_folder", []) + patches_on(c):
                need.add("patches/" + f)
            for f in list(c.get("logo_folder", {}).values()) + ([c["own_logo"]] if c.get("own_logo") else []):
                need.add("logos/" + f)
            for _, keys in c.get("input", []):
                if keys.startswith("LOAD:"):
                    need.add("states/" + keys[5:])
            for fx in c.get("retroarch", {}).get("states", {}).values():  # (GEN-13's stand-in RetroArch folder)
                need.add("states/" + fx)
            if c.get("bios"):  # (GEN-15's player-supplied GBA BIOS)
                need.add("bios/" + c["bios"])
            for f in c.get("cheat_files", {}).values():  # (GEN-17's cheat files)
                need.add("cheats/" + f)
            for f in c.get("state_files", {}).values():  # (an earlier play's auto state: GB-6's resume note)
                if not f.startswith("text:"):
                    need.add("states/" + f)
        print("\n".join(sorted(need)))
        return
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
    if args.json:
        json.dump([{"id": r["case"]["id"], "rom": r["case"]["rom"], "rules": r["case"].get("rules", []), "ok": r["ok"],
                    "error": r["error"], "checks": [{"rule": a, "what": b, "ok": c, "seen": d} for a, b, c, d in r["checks"]],
                    "goldens": [{"shot": a, "ok": b, "seen": c} for a, b, c in r["goldens"]]} for r in results],
                  open(args.json, "w"), indent=1)
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
        if not r["ok"]:  # (what the app said of the ROM, its patches and the state: GitHub's runs keep only this output)
            for line in (r.get("log") or "").splitlines():
                if line.startswith(LOG_KEYS):
                    print("       log: %s" % line[:240])
    shutil.rmtree(args.work, ignore_errors=True)
    print("\n%d of %d cases pass%s. Report: %s" % (len(results) - len(fails), len(results),
          " (goldens approved)" if args.update else "", os.path.join(args.report, "index.html")))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
