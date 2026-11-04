"""Which games a change touches, for tools/regress.py --changed.

A change is the difference between BASE (a commit) and the working tree,
untracked files included. Each changed file is sorted into one of:

- one game's own files: docs/games/<slug>.md, tests/regress/<slug>.json;
- src/profiles.c: each changed line is traced to the top-level definition
  it's in (a function, a #define, a table, one entry of profiles[] or
  variants[]; a comment between them goes with the one below), then to every profile entry that uses that definition,
  directly or through other definitions, then to those entries' games
  (their codes and the regional variants built on them, matched to the
  "Releases" rows in docs/games/*.md). A line outside any definition, or
  a definition no profile reaches, counts as shared code;
- files that can't change what the app does (the wiki, the README, the
  changelog, Android-only code, State Saver, other docs): nothing;
- anything else (the app's other sources, the Makefile, the build
  scripts, regress.py itself...): shared code, so every game.

select() returns (slugs or None for every game, reasons).
"""
import glob, os, re, subprocess

ROOT = os.environ.get("GBADS_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILES = "src/profiles.c"

# paths (prefixes or globs) that can't change what the app does under test
NO_EFFECT = (
    "wiki/", "README.md", "CHANGELOG.md", "CLAUDE.md", "LICENSE", "android/", "src/android.c",
    "tools/statesaver/", "tools/scout/", "docs/", "assets/brand/", ".github/workflows/android.yml",
    ".github/workflows/wiki.yml", "dist/screens.sh", "dist/launch.sh", "dist/banana-play.sh",
)


def git(*args):
    return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True, check=True).stdout


def default_base():
    """The commit the change starts from: where this branch left origin/dev
    (HEAD itself on dev, so only uncommitted changes count)."""
    for ref in ("origin/dev", "refs/remotes/origin/dev", "dev"):
        try:
            return git("merge-base", "HEAD", ref).strip()
        except subprocess.CalledProcessError:
            continue
    return "HEAD"


def changed_files(base):
    names = git("diff", "--name-only", base).split()
    names += git("ls-files", "--others", "--exclude-standard").split()
    return sorted(set(names))


def changed_lines(base, path):
    """Line numbers (in the working tree's file) of lines added, changed or
    next to a deletion."""
    out = git("diff", "-U0", base, "--", path)
    lines = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", out, re.M):
        start, n = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
        lines |= set(range(start, start + n)) if n else {start, start + 1}  # (a deletion: the lines around it)
    return lines


def release_slugs():
    """Each release code in docs/games/*.md's "Releases" rows (and any code a
    rules file mentions, for codes like BGW1 that aren't releases) -> its games."""
    codes, mentions = {}, {}
    for doc in sorted(glob.glob(os.path.join(ROOT, "docs", "games", "*.md"))):
        slug = os.path.basename(doc)[:-3]
        if slug == "README":
            continue
        text = open(doc).read()
        for line in text.splitlines():
            if line.startswith("| Releases"):
                for c in re.findall(r"`([A-Z0-9]{4})`", line):
                    codes.setdefault(c, set()).add(slug)
        for c in set(re.findall(r"`([A-Z0-9]{4})`", text)):
            mentions.setdefault(c, set()).add(slug)
    return codes, mentions


def definitions(src):
    """Top-level definitions in profiles.c: (name, first line, last line), lines
    counted from 1. profiles[] and variants[] are split into their entries
    (named "profile:<first code>" and "variant:<code>")."""
    lines = src.split("\n")
    defs, i, n = [], 0, len(lines)
    while i < n:
        line = lines[i]
        m = re.match(r"#define (\w+)", line)
        if m:
            j = i
            while lines[j].endswith("\\") and j + 1 < n:
                j += 1
            defs.append((m.group(1), i + 1, j + 1))
            i = j + 1
            continue
        m = re.match(r"(?:static |const |typedef |struct )[^;=(]*?(\w+)\s*(\(|\[\]\s*=|=|\{)", line) or \
            re.match(r"[A-Za-z_][\w \*]*?\b(\w+)\([^;]*\)\s*\{\s*$", line)  # (a function that isn't static)
        if m and not line.startswith(" ") and not line.startswith("\t"):
            name = m.group(1)
            j = i
            if not line.rstrip().endswith(";"):
                while j + 1 < n and not re.match(r"^\}", lines[j]):
                    j += 1
            if name in ("profiles", "variants"):
                defs += entries(lines, i, j, name)
            else:
                if line.startswith("typedef"):  # (named at its end: "} Name;")
                    t = re.match(r"\}\s*(\w+)", lines[j])
                    name = t.group(1) if t else name
                defs.append((name, i + 1, j + 1))
            i = j + 1
            continue
        i += 1
    return defs


def entries(lines, first, last, table):
    out = []
    if table == "profiles":
        start = None
        for k in range(first + 1, last):
            if lines[k] == "\t{":
                start = k
            elif lines[k] == "\t}," and start is not None:
                body = "\n".join(lines[start:k + 1])
                codes = re.search(r"\.codes = \{([^}]*)\}", body)
                code = re.findall(r'"(\w{4})"', codes.group(1))[0] if codes else "?"
                out.append(("profile:" + code, start + 1, k + 1))
                start = None
    else:
        for k in range(first + 1, last):
            m = re.match(r'\t\{ "(\w{4})"', lines[k])
            if m:
                out.append(("variant:" + m.group(1), k + 1, k + 1))
    return out


def profile_games(src, defs, codes, mentions):
    """For each definition, the games whose profile entries reach it."""
    lines = src.split("\n")
    body = {name: "\n".join(lines[a - 1:b]) for name, a, b in defs}
    names = {name for name, _, _ in defs if ":" not in name}
    uses = {name: set(re.findall(r"\b\w+\b", text)) & names for name, text in body.items()}
    # variants: code -> (base code, patch function)
    variants = {}
    for name, text in body.items():
        if name.startswith("variant:"):
            m = re.match(r'\s*\{ "(\w{4})", "(\w{4})", [^,]+, (\w+)', text)
            if m:
                variants[m.group(1)] = (m.group(2), m.group(3))
    def games_of(code):
        return codes.get(code) or mentions.get(code) or set()
    reach = {}
    for name, text in body.items():
        if not name.startswith(("profile:", "variant:")):
            continue
        if name.startswith("profile:"):
            pcodes = set(re.findall(r'"(\w{4})"', re.search(r"\.codes = \{([^}]*)\}", text).group(1))) \
                if ".codes" in text else set()
            pcodes |= {v for v, (b, _) in variants.items() if b in pcodes}
        else:
            pcodes = {name.split(":")[1]}
        games = set().union(*[games_of(c) for c in pcodes]) if pcodes else set()
        seen, todo = set(), [name]
        while todo:
            d = todo.pop()
            if d in seen:
                continue
            seen.add(d)
            todo += list(uses.get(d, ()))
        for d in seen:
            reach.setdefault(d, set()).update(games)
    return reach


def select(base=None):
    base = base or default_base()
    codes, mentions = release_slugs()
    slugs, reasons, everything = set(), [], False
    for path in changed_files(base):
        m = re.match(r"(?:docs/games|tests/regress)/([\w-]+)\.(?:md|json)$", path)
        if m and m.group(1) != "README":
            slugs.add(m.group(1))
            reasons.append("%s: %s" % (path, m.group(1)))
            continue
        if path == PROFILES:
            full = os.path.join(ROOT, path)
            src = open(full).read() if os.path.exists(full) else ""
            defs = definitions(src)
            reach = profile_games(src, defs, codes, mentions)
            for ln in sorted(changed_lines(base, path)):
                hit = [d for d in defs if d[1] <= ln <= d[2]]
                if not hit:  # (a comment or blank line between definitions: the one below it)
                    hit = sorted((d for d in defs if d[1] > ln), key=lambda d: d[1])[:1]
                games = reach.get(hit[0][0]) if hit else None
                if not games:
                    everything = True
                    reasons.append("%s:%d: %s (shared code)" % (path, ln, hit[0][0] if hit else "outside any definition"))
                    break
                if games - slugs:
                    reasons.append("%s:%d: %s -> %s" % (path, ln, hit[0][0], " ".join(sorted(games))))
                slugs |= games
            continue
        if path.startswith(NO_EFFECT) or path.startswith("docs/games/README"):
            reasons.append("%s: no effect" % path)
            continue
        everything = True
        reasons.append("%s: shared code" % path)
    return (None if everything else sorted(slugs)), reasons, base
