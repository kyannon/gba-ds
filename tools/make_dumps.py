#!/usr/bin/env python3
"""Write each supported release's known dumps: the "Dumps" row of every rules
file (docs/games/*.md) and src/dumps_data.h, the table GBA DS checks a ROM's
CRC32 against to tell a clean copy of a game from a modified one.

  tools/make_dumps.py --dat DAT [--fixtures DIR] [--check]

DAT is No-Intro's Game Boy Advance list as libretro keeps it (it has each
dump's game code), fetched with git (the shell can't reach raw.githubusercontent):

  git init -q lrdb && cd lrdb
  git remote add origin https://github.com/libretro/libretro-database
  git config core.sparseCheckout true
  echo "metadat/no-intro/Nintendo - Game Boy Advance.dat" > .git/info/sparse-checkout
  git fetch -q --depth 1 --filter=blob:none origin master && git checkout -q FETCH_HEAD

The releases are the codes in each rules file's "Releases" row. Every official
dump with that code is listed (the cartridge, later revisions, Virtual Console
and collection dumps; not prototypes, betas, demos or unlicensed copies). The
one the regression tests run on (the fixtures' roms/CODE.zip, --fixtures,
default ../gba-ds-tests) is marked tested; without the fixtures, the marks
already in the rules files are kept. A rules file's "Mods" row (written by
hand: docs/games/README.md) goes into the table too, as mods, its tested
marks as they are. --check writes nothing and exits 1 if either file would
change.
"""
import argparse, glob, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = re.compile(r"\((?:Proto|Beta|Demo|Sample|Kiosk|Debug|Unl|Pirate)[^)]*\)")
LANGS = re.compile(r"^[A-Z][a-z](?:,[A-Z][a-z])*$")
CODE = re.compile(r"`([A-Z0-9]{4})`")


def kind(name):
    """What sets a dump apart from the cartridge: "Rev 1", "Virtual Console"..."""
    groups = re.findall(r"\(([^)]*)\)", name)[1:]  # (the first is the region)
    rest = [g for g in groups if not LANGS.match(g)]
    return ", ".join(rest) or "cartridge"


def read_dat(path):
    by = {}
    text = open(path, encoding="utf-8").read()
    for m in re.finditer(r'rom \( name "([^"]*)" size \d+ crc ([0-9A-Fa-f]{8})[^)]*?serial "([^"]*)" \)', text):
        name, crc, serial = m.groups()
        if SKIP.search(name):
            continue
        for code in re.split(r"[ ,]+", serial):
            by.setdefault(code, []).append((crc.lower(), name[:-4] if name.endswith(".gba") else name))
    return by


def fixture_crc(fixtures, code):
    z = os.path.join(fixtures, "roms", code + ".zip")
    if not os.path.exists(z):
        return None
    with zipfile.ZipFile(z) as f:
        for i in f.infolist():
            if i.filename.lower().endswith(".gba"):
                return "%08x" % i.CRC
    return None


def old_tested(line):
    """The CRCs a rules file's Dumps row marks tested."""
    return set(re.findall(r"`([0-9a-f]{8})` \(tested\)", line or ""))


def read_mods(line):
    """A Mods row's entries: (code, crc, tested, name)."""
    out = []
    for part in (line or "").split(" · "):
        m = CODE.search(part)
        if not m:
            continue
        for item in part[m.end():].split(","):
            mm = re.match(r"\s*(.*?)\s*`([0-9a-f]{8})`(\s*\(tested\))?\s*\|?\s*$", item)
            if mm:
                out.append((m.group(1), mm.group(2), bool(mm.group(3)), mm.group(1)))
            elif item.strip(" |\n"):
                sys.exit("can't read the Mods row's %r" % item)
    return out


def row(codes, by, tested):
    parts = []
    for code in codes:
        dumps = sorted(by.get(code, []), key=lambda d: (kind(d[1]) != "cartridge", kind(d[1])))
        if not dumps:
            parts.append("`%s` none known" % code)
            continue
        items = ["%s `%s`%s" % (kind(n), crc, " (tested)" if crc in tested else "") for crc, n in dumps]
        parts.append("`%s` %s" % (code, ", ".join(items)))
    return "| Dumps | " + " · ".join(parts) + " |\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dat", required=True)
    ap.add_argument("--fixtures", default=os.environ.get("GBADS_TESTS", os.path.join(ROOT, "..", "gba-ds-tests")))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    by = read_dat(a.dat)
    haveFixtures = os.path.isdir(os.path.join(a.fixtures, "roms"))

    changed, table = [], []
    for path in sorted(glob.glob(os.path.join(ROOT, "docs", "games", "*.md"))):
        lines = open(path, encoding="utf-8").readlines()
        rel = next((i for i, l in enumerate(lines) if l.startswith("| Releases |")), None)
        if rel is None:
            continue
        codes = list(dict.fromkeys(CODE.findall(lines[rel])))
        dumpsAt = next((i for i, l in enumerate(lines) if l.startswith("| Dumps |")), None)
        tested = set()
        for code in codes:
            if haveFixtures:
                crc = fixture_crc(a.fixtures, code)
                if crc and crc not in {c for c, _ in by.get(code, [])}:
                    print("%s: the fixture roms/%s.zip (%s) isn't a known dump of %s: a patched ROM?" % (
                        os.path.basename(path), code, crc, code), file=sys.stderr)
                elif crc:
                    tested.add(crc)
                # (roms/CODE+name.zip: another official dump the tests run too,
                # such as a Virtual Console one; a patched ROM isn't one)
                for z in sorted(glob.glob(os.path.join(a.fixtures, "roms", glob.escape(code) + "+*.zip"))):
                    c = fixture_crc(a.fixtures, os.path.basename(z)[:-4])
                    if c and c in {k for k, _ in by.get(code, [])}:
                        tested.add(c)
        if not haveFixtures:
            tested = old_tested(lines[dumpsAt] if dumpsAt is not None else "")
        new = row(codes, by, tested)
        if dumpsAt is None:
            lines.insert(rel + 1, new)
        elif lines[dumpsAt] != new:
            lines[dumpsAt] = new
        else:
            new = None
        if new is not None:
            changed.append(path)
            if not a.check:
                open(path, "w", encoding="utf-8").writelines(lines)
        for code in codes:
            for crc, name in by.get(code, []):
                table.append((code, crc, crc in tested, False, name))
        modsAt = next((l for l in lines if l.startswith("| Mods |")), None)
        for code, crc, t, name in read_mods(modsAt):
            if any(crc == c for c, _ in by.get(code, [])):
                sys.exit("%s: the mod %s has the CRC32 of a dump of %s" % (os.path.basename(path), name, code))
            table.append((code, crc, t, True, name))

    out = ["/* Generated by tools/make_dumps.py from No-Intro's list and the rules\n",
           " * files' Releases rows: don't edit, re-run it. Every official dump of\n",
           " * each supported release, then the mods in their Mods rows; tested: the\n",
           " * ones the regression tests run on. */\n",
           "static const KnownDump knownDumps[] = {\n"]
    for code, crc, t, mod, name in sorted(table, key=lambda e: (e[3], e[0], e[1])):
        out.append('\t{ "%s", 0x%s, %s, %s, "%s" },\n' % (code, crc, "true" if t else "false", "true" if mod else "false",
                                                          name.replace('"', '\\"')))
    out.append("};\n")
    hdr = os.path.join(ROOT, "src", "dumps_data.h")
    old = open(hdr, encoding="utf-8").read() if os.path.exists(hdr) else ""
    if old != "".join(out):
        changed.append(hdr)
        if not a.check:
            open(hdr, "w", encoding="utf-8").write("".join(out))
    for p in changed:
        print(("would change " if a.check else "wrote ") + os.path.relpath(p, ROOT))
    if a.check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
