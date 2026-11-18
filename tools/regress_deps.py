#!/usr/bin/env python3
"""Which games' regression cases run which of the app's code.

`tools/regress.py --changed` uses this to run only the games a change to
shared code can affect. The map is measured, not written by hand: the app
is built with gcov (`make OUT=build/cov OPTFLAGS="-O0 -g --coverage"`), each
game's cases are run with it, and the functions they execute are recorded.

    tools/regress_deps.py measure [GAME ...]   run each game's cases on the
                                               coverage build (every game
                                               if none named; about the
                                               time of a full suite)
    tools/regress_deps.py build                merge the measurements into
                                               ../gba-ds-tests/dependencies.json
                                               and DEPENDENCIES.md
    tools/regress_deps.py check                what changed since: functions
                                               in src/ the map doesn't know
    tools/regress_deps.py show GAME|FILE       what a game relies on / which
                                               games run a file

The map (kyannon/gba-ds-tests, dependencies.json) is, per source file, each
function and the games whose cases execute it ("*": all of them; []: none
does). A change inside a function picks that function's games. A change
outside any function (a table, a macro), in a function the map doesn't
know (new, renamed) or in a header picks every game that runs the file (a
header: every game). A file the map doesn't know picks every game, as
without a map. profiles.c has its own, finer tracing (regress_changed.py).

The map goes stale when code starts calling something it didn't: measure
again (after a release's full run, say) and commit the result to the
fixtures repo. `check` says how far the source has moved on.
"""
import argparse, glob, json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COV_BIN = os.path.join("build", "cov", "gba-ds")
COV_DIR = os.path.join(ROOT, "build", "cov")
OUT_DIR = os.path.join(ROOT, "build", "deps")
MAP_NAME = "dependencies.json"
DOC_NAME = "DEPENDENCIES.md"


def fixtures_dir(arg=None):
    return arg or os.environ.get("GBADS_TESTS") or os.path.join(os.path.dirname(ROOT), "gba-ds-tests")


def all_games():
    return sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(ROOT, "tests", "regress", "*.json")))


# ---------------------------------------------------------------- C functions

def c_functions(text):
    """[(name, first line, last line)] of the functions defined at the top
    level of a C file (1-based, inclusive): the header's first line (so a
    change to the signature counts) to the closing brace. Comments, strings
    and preprocessor lines are ignored; a `{` after `=` or with no
    parentheses before it is data (a table, a struct), not a function."""
    out, i, n, line = [], 0, len(text), 1
    clean = []
    at_line_start = True
    while i < n:
        c = text[i]
        if c == "/" and text[i:i + 2] == "//":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and text[i:i + 2] == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            clean.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            clean.append(c + "".join("\n" if ch == "\n" else " " for ch in text[i + 1:j]) + c)
            i = j + 1
            continue
        if c == "#" and at_line_start:
            j = i
            while True:
                k = text.find("\n", j)
                k = n if k < 0 else k
                if k < n and text[k - 1] == "\\":
                    j = k + 1
                    continue
                j = k
                break
            clean.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
            continue
        clean.append(c)
        if c == "\n":
            at_line_start = True
        elif not c.isspace():
            at_line_start = False
        i += 1
    src = "".join(clean)
    depth, hdr, hdr_line, line = 0, [], None, 1
    cur = None
    for ch in src:
        if ch == "\n":
            line += 1
        if depth == 0:
            if ch == "{":
                h = "".join(hdr).strip()
                name = None
                if h.endswith(")") or re.search(r"\)\s*__attribute__\s*\(\(.*\)\)\s*$", h):
                    h2 = re.sub(r"\s*__attribute__\s*\(\(.*\)\)\s*$", "", h)
                    # last parenthesised group, and the identifier before it
                    d, k = 0, len(h2) - 1
                    while k >= 0:
                        if h2[k] == ")":
                            d += 1
                        elif h2[k] == "(":
                            d -= 1
                            if d == 0:
                                break
                        k -= 1
                    before = h2[:k]
                    m = re.search(r"(\w+)\s*$", before)
                    top = re.sub(r"\([^()]*(\([^()]*\)[^()]*)*\)", "", before)
                    if m and "=" not in top:
                        name = m.group(1)
                cur = (name, hdr_line or line) if name else None
                depth = 1
                hdr, hdr_line = [], None
            elif ch in ";}":
                hdr, hdr_line = [], None
            else:
                if hdr_line is None and not ch.isspace():
                    hdr_line = line
                hdr.append(ch)
        else:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0 and cur:
                    out.append((cur[0], cur[1], line))
                    cur = None
    return out


# ---------------------------------------------------------------- measuring

def gcov_functions(gcda_dir):
    """{path: {function: execution count}} for the src/ files in the .gcda
    files under gcda_dir (their .gcno copied beside them first)."""
    res = {}
    for gcda in glob.glob(os.path.join(gcda_dir, "**", "*.gcda"), recursive=True):
        base = os.path.basename(gcda)
        if not base.startswith("gba-ds-"):
            continue
        shutil.copy(os.path.join(COV_DIR, base[:-5] + ".gcno"), os.path.dirname(gcda))
        r = subprocess.run(["gcov", "--json-format", "--stdout", gcda], capture_output=True, text=True, cwd=ROOT)
        if r.returncode:
            continue
        dec, s, k = json.JSONDecoder(), r.stdout, 0
        while k < len(s):
            while k < len(s) and s[k].isspace():
                k += 1
            if k >= len(s):
                break
            doc, k = dec.raw_decode(s, k)
            for f in doc["files"]:
                path = os.path.relpath(f["file"], ROOT) if os.path.isabs(f["file"]) else f["file"]
                if not path.startswith("src/"):
                    continue
                d = res.setdefault(path, {})
                for fn in f["functions"]:
                    d[fn["name"]] = max(d.get(fn["name"], 0), fn["execution_count"])
    return res


def measure(games, fixtures):
    if not os.path.exists(os.path.join(ROOT, COV_BIN)):
        sys.exit("build the coverage binary first: make OUT=build/cov OPTFLAGS=\"-O0 -g --coverage\" build/cov/gba-ds")
    os.makedirs(OUT_DIR, exist_ok=True)
    for slug in games:
        tmp = tempfile.mkdtemp(prefix="cov-")
        env = dict(os.environ, GCOV_PREFIX=tmp)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "regress.py"), slug, "--bin", COV_BIN,
                            "--fixtures", fixtures, "--report", os.path.join(ROOT, "build", "regress-cov", slug)],
                           capture_output=True, text=True, env=env, cwd=ROOT)
        m = re.search(r"(\d+) of (\d+) cases pass", r.stdout)
        ok, total = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        files = gcov_functions(tmp)
        shutil.rmtree(tmp, ignore_errors=True)
        json.dump({"slug": slug, "pass": ok, "total": total, "files": files},
                  open(os.path.join(OUT_DIR, slug + ".json"), "w"), indent=0)
        ran = sum(1 for f in files.values() for c in f.values() if c)
        print("%-45s %s of %s cases pass, %d functions run" % (slug, ok, total, ran), flush=True)


# ---------------------------------------------------------------- the map

def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True).stdout.strip()


def build(fixtures):
    games = all_games()
    per = {}
    for slug in games:
        p = os.path.join(OUT_DIR, slug + ".json")
        if not os.path.exists(p):
            sys.exit("no measurement for %s: tools/regress_deps.py measure" % slug)
        d = json.load(open(p))
        if d["total"] == 0:
            sys.exit("%s: no cases ran on the coverage build; its measurement can't be trusted" % slug)
        if d["pass"] != d["total"]:  # (a failing case still ran its code; a golden that's behind doesn't change what runs)
            print("note: %s: %s of %s cases passed on the coverage build" % (slug, d["pass"], d["total"]))
        per[slug] = d["files"]
    files = {}
    here = current_functions()
    for path in sorted({p for d in per.values() for p in d}):
        funcs = {}
        # (functions the host build leaves out, for Android or the web build, run by no game)
        for fn in sorted({f for d in per.values() for f in d.get(path, {})} | set(here.get(path, []))):
            who = [s for s in games if per[s].get(path, {}).get(fn, 0) > 0]
            funcs[fn] = "*" if len(who) == len(games) else who
        files[path] = {"functions": funcs}
    doc = {"about": "Which games' regression cases run which functions of gba-ds. Measured with gcov by "
                    "tools/regress_deps.py in kyannon/gba-ds; see DEPENDENCIES.md. \"*\" means every game, [] none.",
           "commit": git("rev-parse", "HEAD"), "date": git("show", "-s", "--format=%cs", "HEAD"),
           "games": games, "files": files}
    json.dump(doc, open(os.path.join(fixtures, MAP_NAME), "w"), indent=1)
    open(os.path.join(fixtures, MAP_NAME), "a").write("\n")
    open(os.path.join(fixtures, DOC_NAME), "w").write(render(doc))
    print("wrote %s and %s in %s" % (MAP_NAME, DOC_NAME, fixtures))


def load(fixtures=None):
    p = os.path.join(fixtures_dir(fixtures), MAP_NAME)
    return json.load(open(p)) if os.path.exists(p) else None


def games_of(doc, path, func=None):
    """The games running `func` of `path` (any function of it if None, or
    the function isn't known); None: the map can't say (every game)."""
    f = doc["files"].get(path)
    if f is None:
        return None
    fn = f["functions"].get(func) if func else None
    if fn is not None:
        return set(doc["games"]) if fn == "*" else set(fn)
    out = set()
    for v in f["functions"].values():
        out |= set(doc["games"]) if v == "*" else set(v)
    return out or None  # (no function of it ever runs: loose data in it may still be read elsewhere)


def render(doc):
    games = doc["games"]
    n = len(games)
    L = ["# What each game's regression cases rely on", "",
         "Generated by `tools/regress_deps.py build` in kyannon/gba-ds from a gcov measurement of every game's",
         "cases (gba-ds commit `%s`, %s). Don't edit by hand; `dependencies.json` is the same data for the tools." % (doc["commit"][:9], doc["date"]), "",
         "`tools/regress.py --changed` reads it: a change inside a function runs only the games below that run it.",
         "A change outside any function, to a function not listed here, or to a header runs every game that runs the",
         "file (a header: every game). Measure again when the source has moved on (`tools/regress_deps.py check`).", "",
         "## Files", "", "| file | functions run by any game | run by every game | games that run some of it |", "|---|---|---|---|"]
    for path, f in doc["files"].items():
        fs = f["functions"]
        ran = sum(1 for v in fs.values() if v)
        every = sum(1 for v in fs.values() if v == "*")
        who = games_of(doc, path) or set()
        L.append("| `%s` | %d of %d | %d | %s |" % (path, ran, len(fs), every, "all" if len(who) == n else ("none" if not who else "%d games" % len(who))))
    L += ["", "## Games", "",
          "For each game: the files it runs, and the functions in them that not every game runs (what makes its",
          "cases different from the rest). Everything else in those files, every game runs.", ""]
    for slug in games:
        L.append("### %s" % slug)
        L.append("")
        for path, f in doc["files"].items():
            mine = [fn for fn, v in f["functions"].items() if v == "*" or slug in v]
            if not mine:
                continue
            extra = [fn for fn in mine if f["functions"][fn] != "*"]
            if path == "src/profiles.c":
                L.append("- `%s`: %d functions (its profile entries; regress_changed.py traces them line by line)" % (path, len(mine)))
            elif extra:
                L.append("- `%s`: %s" % (path, ", ".join("`%s`" % e + ("" if len(f["functions"][e]) > 1 else " (only this game)") for e in extra)))
            else:
                L.append("- `%s`" % path)
        L.append("")
    return "\n".join(L) + "\n"


# ---------------------------------------------------------------- checking

def current_functions():
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "src", "*.[ch]"))):
        out[os.path.relpath(path, ROOT)] = [f[0] for f in c_functions(open(path, errors="replace").read())]
    return out


def check(fixtures):
    doc = load(fixtures)
    if not doc:
        sys.exit("no %s in %s" % (MAP_NAME, fixtures_dir(fixtures)))
    print("map from commit %s (%s)" % (doc["commit"][:9], doc["date"]))
    new = gone = 0
    for path, names in current_functions().items():
        known = doc["files"].get(path)
        if known is None:
            if names:
                print("  %s: not in the map (%d functions)" % (path, len(names)))
            continue
        for nme in names:
            if nme not in known["functions"]:
                print("  %s: %s is new" % (path, nme)); new += 1
        for nme in known["functions"]:
            if nme not in names:
                gone += 1
    since = git("rev-list", "--count", "%s..HEAD" % doc["commit"])
    print("%d new function(s), %d gone, %s commit(s) since the measurement" % (new, gone, since or "?"))
    print("measure again when this grows: tools/regress_deps.py measure && build")


def show(what, fixtures):
    doc = load(fixtures)
    if not doc:
        sys.exit("no %s" % MAP_NAME)
    if what in doc["files"] or ("src/" + what) in doc["files"]:
        path = what if what in doc["files"] else "src/" + what
        who = games_of(doc, path) or set()
        print("%s: %s" % (path, "no game" if not who else "%d of %d games: %s" % (len(who), len(doc["games"]), " ".join(sorted(who)))))
        return
    if what not in doc["games"]:
        sys.exit("not a game or file in the map: %s" % what)
    for path, f in doc["files"].items():
        mine = [fn for fn, v in f["functions"].items() if v == "*" or what in v]
        if mine:
            print("%s: %d of %d functions" % (path, len(mine), len(f["functions"])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["measure", "build", "check", "show"])
    ap.add_argument("what", nargs="*")
    ap.add_argument("--fixtures")
    a = ap.parse_args()
    fx = fixtures_dir(a.fixtures)
    if a.cmd == "measure":
        measure(a.what or all_games(), fx)
    elif a.cmd == "build":
        build(fx)
    elif a.cmd == "check":
        check(fx)
    else:
        show(a.what[0], fx)


if __name__ == "__main__":
    main()
