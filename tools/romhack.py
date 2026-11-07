#!/usr/bin/env python3
"""How well does GBA DS take to a romhack? Applies a patch to the release it's
made for and runs that release's regression cases on the patched ROM.

  tools/romhack.py PATCH [--base CODE|ROM] [--name NAME] [--out DIR]
  tools/romhack.py PATCHED.gba|.zip [--base CODE|ROM] ...

PATCH is an IPS, UPS or BPS file. UPS and BPS name the ROM they're made for
(its CRC32), so the base is found among the fixtures (roms/*.zip); an IPS
doesn't, so give --base: a release's code (its fixture, roms/CODE.zip) or a
ROM file. A ROM that's already patched is matched to its release's fixture
by the code in its header (or --base).

It reports:
  - the patched ROM: its header (a changed code means GBA DS won't know the
    game at all), size and CRC32, and whether that's a dump or mod GBA DS
    already knows (src/dumps_data.h);
  - what the patch changes: how much, and where (the largest areas, and
    which went into the base's free space, 0x00 / 0xFF);
  - the release's cases (tests/regress/<game>.json, those whose rom is the
    code) run on the patched ROM through a copy of the fixtures with it in
    the release's place (regress.py --any-rom): checks that fail mean a rule
    broke; pictures that differ are often the mod's own content (renamed
    items, new numbers), so look at each in the report;
  - what to add to make it a tested mod (docs/games/README.md, "Releases and
    dumps"): the Mods row entry and the fixture name.

Output (default build/romhack/<name>/): report.md, the patched ROM as
<name>.gba, regress.json and the regress report (regress/index.html).
Needs the host build (make && make tools) and the fixtures (../gba-ds-tests
or $GBADS_TESTS).
"""
import argparse, glob, json, os, re, shutil, subprocess, sys, tempfile, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


# ------------------------------------------------------------ patch formats
def vlq(p, i):
    """UPS / BPS numbers: 7 bits a byte, the high bit ending it."""
    n, shift = 0, 1
    while True:
        x = p[i]
        i += 1
        n += (x & 0x7F) * shift
        if x & 0x80:
            return n, i
        shift <<= 7
        n += shift


def apply_ips(src, p):
    out = bytearray(src)
    i = 5
    while True:
        if p[i:i + 3] == b"EOF":
            if len(p) >= i + 6:  # (the truncation extension)
                del out[int.from_bytes(p[i + 3:i + 6], "big"):]
            return bytes(out)
        off = int.from_bytes(p[i:i + 3], "big")
        n = int.from_bytes(p[i + 3:i + 5], "big")
        i += 5
        if n == 0:  # a run: length, then the byte
            n = int.from_bytes(p[i:i + 2], "big")
            data = p[i + 2:i + 3] * n
            i += 3
        else:
            data = p[i:i + n]
            i += n
        if off + n > len(out):
            out.extend(b"\0" * (off + n - len(out)))
        out[off:off + n] = data


def ups_info(p):
    return int.from_bytes(p[-12:-8], "little")  # the source's CRC32


def apply_ups(src, p):
    i = 4
    srcSize, i = vlq(p, i)
    dstSize, i = vlq(p, i)
    if crc32(src) != ups_info(p) or len(src) != srcSize:
        raise ValueError("this UPS is for another ROM (CRC32 %08x)" % ups_info(p))
    out = bytearray(src[:dstSize]) + bytearray(max(0, dstSize - len(src)))
    pos = 0
    while i < len(p) - 12:
        skip, i = vlq(p, i)
        pos += skip
        while True:
            x = p[i]
            i += 1
            if pos < dstSize:
                out[pos] ^= x
            pos += 1
            if x == 0:
                break
    if crc32(out) != int.from_bytes(p[-8:-4], "little"):
        raise ValueError("the UPS's result doesn't match its checksum")
    return bytes(out)


def bps_info(p):
    return int.from_bytes(p[-12:-8], "little")


def apply_bps(src, p):
    i = 4
    srcSize, i = vlq(p, i)
    dstSize, i = vlq(p, i)
    meta, i = vlq(p, i)
    i += meta
    if crc32(src) != bps_info(p) or len(src) != srcSize:
        raise ValueError("this BPS is for another ROM (CRC32 %08x)" % bps_info(p))
    out = bytearray(dstSize)
    o = srcRel = dstRel = 0
    while i < len(p) - 12:
        d, i = vlq(p, i)
        cmd, n = d & 3, (d >> 2) + 1
        if cmd == 0:  # SourceRead
            out[o:o + n] = src[o:o + n]
            o += n
        elif cmd == 1:  # TargetRead
            out[o:o + n] = p[i:i + n]
            i += n
            o += n
        else:
            r, i = vlq(p, i)
            r = -(r >> 1) if r & 1 else r >> 1
            if cmd == 2:  # SourceCopy
                srcRel += r
                out[o:o + n] = src[srcRel:srcRel + n]
                srcRel += n
                o += n
            else:  # TargetCopy, a byte at a time (it may overlap itself)
                dstRel += r
                for _ in range(n):
                    out[o] = out[dstRel]
                    o += 1
                    dstRel += 1
    if crc32(out) != int.from_bytes(p[-8:-4], "little"):
        raise ValueError("the BPS's result doesn't match its checksum")
    return bytes(out)


def read_rom(path):
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            n = next(i for i in z.namelist() if i.lower().endswith(".gba"))
            return z.read(n)
    return open(path, "rb").read()


def fixture_crcs(fixtures):
    """{crc32: (fixture name, path)} of the fixtures' ROMs (from their zips' directories)."""
    out = {}
    for z in sorted(glob.glob(os.path.join(fixtures, "roms", "*.zip"))):
        with zipfile.ZipFile(z) as f:
            for i in f.infolist():
                if i.filename.lower().endswith(".gba"):
                    out[i.CRC] = (os.path.basename(z)[:-4], z)
    return out


def known_dumps():
    """{(code, crc): (tested, mod, name)} from src/dumps_data.h."""
    out = {}
    for m in re.finditer(r'\{ "(\w{4})", 0x([0-9a-f]{8}), (true|false), (true|false), "((?:[^"\\]|\\.)*)" \}',
                         open(os.path.join(ROOT, "src", "dumps_data.h"), encoding="utf-8").read()):
        out[(m.group(1), int(m.group(2), 16))] = (m.group(3) == "true", m.group(4) == "true", m.group(5))
    return out


def diff_positions(a, b):
    """Where two ROMs differ, skipping the 4 KB blocks that are the same."""
    n = min(len(a), len(b))
    out = []
    for s in range(0, n, 4096):
        e = min(n, s + 4096)
        if a[s:e] != b[s:e]:
            out.extend(i for i in range(s, e) if a[i] != b[i])
    return out


def regions(pos, gap=64):
    """Changed positions joined into areas [(start, end)] where they're less than gap bytes apart."""
    out = []
    for i in pos:
        if out and i - out[-1][1] < gap:
            out[-1][1] = i + 1
        else:
            out.append([i, i + 1])
    return [tuple(r) for r in out]


def header(rom):
    return rom[0xA0:0xAC].decode("latin1").rstrip("\0"), rom[0xAC:0xB0].decode("latin1"), rom[0xBC]


def game_for(code):
    """The rules file and cases of the game whose Releases row has the code."""
    for path in sorted(glob.glob(os.path.join(ROOT, "docs", "games", "*.md"))):
        for line in open(path, encoding="utf-8"):
            if line.startswith("| Releases |") and "`%s`" % code in line:
                slug = os.path.basename(path)[:-3]
                cases = os.path.join(ROOT, "tests", "regress", slug + ".json")
                return slug, cases if os.path.exists(cases) else None
    return None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("patch", help="an IPS, UPS or BPS patch, or a patched ROM (.gba, .zip)")
    ap.add_argument("--base", help="the release's code (its fixture) or a ROM file")
    ap.add_argument("--name", help="a short name for the mod (default: the patch's file name)")
    ap.add_argument("--out", help="where to write (default build/romhack/<name>)")
    ap.add_argument("--fixtures", default=os.environ.get("GBADS_TESTS", os.path.join(os.path.dirname(ROOT), "gba-ds-tests")))
    ap.add_argument("--no-tests", action="store_true", help="only apply the patch and describe it")
    a = ap.parse_args()
    a.fixtures = os.path.abspath(a.fixtures)
    name = a.name or re.sub(r"\.(ips|ups|bps|gba|zip)$", "", os.path.basename(a.patch), flags=re.I)
    slugName = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    out = os.path.abspath(a.out or os.path.join(ROOT, "build", "romhack", slugName))
    os.makedirs(out, exist_ok=True)
    fixtures = fixture_crcs(a.fixtures) if os.path.isdir(os.path.join(a.fixtures, "roms")) else {}

    data = open(a.patch, "rb").read() if not a.patch.lower().endswith(".zip") else None
    kind = "ROM"
    if data and data[:5] == b"PATCH":
        kind = "IPS"
    elif data and data[:4] == b"UPS1":
        kind = "UPS"
    elif data and data[:4] == b"BPS1":
        kind = "BPS"

    # the base: --base, the UPS / BPS's source CRC, or the patched ROM's code
    base = baseName = None
    if a.base and os.path.exists(a.base):
        base, baseName = read_rom(a.base), os.path.basename(a.base)
    elif a.base:
        z = os.path.join(a.fixtures, "roms", a.base + ".zip")
        if not os.path.exists(z):
            sys.exit("no fixture roms/%s.zip" % a.base)
        base, baseName = read_rom(z), "roms/%s.zip" % a.base
    elif kind in ("UPS", "BPS"):
        want = ups_info(data) if kind == "UPS" else bps_info(data)
        if want not in fixtures:
            sys.exit("the %s is for a ROM with CRC32 %08x, which isn't in the fixtures: give --base" % (kind, want))
        base, baseName = read_rom(fixtures[want][1]), "roms/%s.zip" % fixtures[want][0]
    elif kind == "IPS":
        sys.exit("an IPS doesn't say which ROM it's for: give --base CODE (the release's fixture) or --base ROM")

    if kind == "ROM":
        rom = read_rom(a.patch)
        if base is None:
            code = header(rom)[1]
            z = os.path.join(a.fixtures, "roms", code + ".zip")
            if not os.path.exists(z):
                sys.exit("no fixture roms/%s.zip for the ROM's code: give --base" % code)
            base, baseName = read_rom(z), "roms/%s.zip" % code
    else:
        try:
            rom = {"IPS": apply_ips, "UPS": apply_ups, "BPS": apply_bps}[kind](base, data)
        except ValueError as e:
            sys.exit(str(e))

    bTitle, bCode, _ = header(base)
    title, code, ver = header(rom)
    crc = crc32(rom)
    known = known_dumps()
    romPath = os.path.join(out, slugName + ".gba")
    open(romPath, "wb").write(rom)

    rep = ["# %s" % name, ""]
    rep.append("**Patch:** %s%s, applied to %s (%s, CRC32 %08x)." % (
        kind if kind != "ROM" else "an already patched ROM", "" if kind == "ROM" else " (" + os.path.basename(a.patch) + ")",
        baseName, bCode, crc32(base)))
    rep.append("")
    rep.append("**Patched ROM:** `%s`, %d bytes, CRC32 `%08x`, header \"%s\" `%s` (version %d)." % (
        os.path.relpath(romPath, ROOT), len(rom), crc, title, code, ver))
    k = known.get((code, crc))
    if k:
        rep.append("GBA DS already knows it: %s%s (%s)." % ("a mod" if k[1] else "a dump", ", tested" if k[0] else "", k[2]))
    if code != bCode:
        rep.append("**Its header code is `%s`, not the game's `%s`: GBA DS won't know it as the game, so it plays on one "
                   "screen.** Supporting it means adding `%s` to the profile (as a release), not a Mods row." % (code, bCode, code))
    rep.append("")

    pos = diff_positions(base, rom)
    regs = regions(pos) + ([(len(base), len(rom))] if len(rom) > len(base) else [])
    changed = len(pos) + abs(len(rom) - len(base))  # (bytes added or cut off count too)
    free = [(s, e) for s, e in regs if all(x in (0, 0xFF) for x in base[s:min(e, len(base))])]
    rep.append("## What it changes")
    rep.append("")
    rep.append("%d bytes (%.2f%% of the ROM) in %d areas; %d bytes of them in %d areas that were free space in the base "
               "(new data or code). %s" % (
                   changed, 100.0 * changed / max(1, len(base), len(rom)), len(regs), sum(e - s for s, e in free), len(free),
                   "The ROM %s from %d to %d bytes." % ("grew" if len(rom) > len(base) else "was cut", len(base), len(rom))
                   if len(rom) != len(base) else ""))
    rep.append("")
    if regs:
        rep.append("The largest areas:")
        rep.append("")
        rep.append("| Address | Bytes | |")
        rep.append("|---|---|---|")
        for s, e in sorted(regs, key=lambda r: r[1] - r[0], reverse=True)[:15]:
            rep.append("| `%08X`-`%08X` | %d | %s |" % (0x08000000 + s, 0x08000000 + e - 1, e - s, "free space" if (s, e) in free else ""))
        rep.append("")

    slug, casesPath = game_for(bCode)
    verdict = None
    if not slug:
        rep.append("No rules file lists `%s` in its Releases row: GBA DS has no profile for this game." % bCode)
    elif a.no_tests:
        rep.append("Tests not run (--no-tests).")
    elif not casesPath:
        rep.append("No cases for %s yet (tests/regress/%s.json)." % (slug, slug))
    else:
        cases = [c for c in json.load(open(casesPath, encoding="utf-8"))["cases"] if c["rom"] == bCode]
        if not cases:
            rep.append("None of %s's cases run on `%s`." % (slug, bCode))
        else:
            # a copy of the fixtures with the patched ROM where the release's is
            fx = tempfile.mkdtemp(prefix="gbads-romhack-")
            for d in os.listdir(a.fixtures):
                if d not in ("roms", ".git"):
                    os.symlink(os.path.join(a.fixtures, d), os.path.join(fx, d))
            os.makedirs(os.path.join(fx, "roms"))
            for z in glob.glob(os.path.join(a.fixtures, "roms", "*.zip")):
                if os.path.basename(z) != bCode + ".zip":
                    os.symlink(z, os.path.join(fx, "roms", os.path.basename(z)))
            with zipfile.ZipFile(os.path.join(fx, "roms", bCode + ".zip"), "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr(slugName + ".gba", rom)
            res = os.path.join(out, "regress.json")
            cmd = [sys.executable, os.path.join(ROOT, "tools", "regress.py"), "--fixtures", fx, "--any-rom",
                   "--report", os.path.join(out, "regress"), "--json", res] + [c["id"] for c in cases]
            print("running %d of %s's cases on the patched ROM..." % (len(cases), slug), file=sys.stderr)
            subprocess.run(cmd, stdout=subprocess.DEVNULL)
            shutil.rmtree(fx, ignore_errors=True)
            results = json.load(open(res)) if os.path.exists(res) else []
            broke = [r for r in results if r["error"] or any(not c["ok"] for c in r["checks"])]
            pics = [(r["id"], g) for r in results for g in r["goldens"] if g["ok"] is False]
            rules = sorted({c["rule"] for r in results for c in r["checks"] if not c["ok"]})
            rep.append("## Tests: %s's %d cases on `%s`" % (slug, len(results), bCode))
            rep.append("")
            if broke:
                verdict = "breaks rules %s" % ", ".join(rules) if rules else "cases didn't run"
                rep.append("**%d of %d cases fail their checks%s:**" % (
                    len(broke), len(results), " (rules %s)" % ", ".join(rules) if rules else ""))
                rep.append("")
                for r in broke:
                    if r["error"]:
                        rep.append("- `%s`: %s" % (r["id"], r["error"]))
                    for c in r["checks"]:
                        if not c["ok"]:
                            rep.append("- `%s` %s %s: %s" % (r["id"], c["rule"], c["what"], c["seen"]))
            else:
                verdict = "every check holds"
                rep.append("**Every check holds** (%d cases): the rules these cases test hold on the patched ROM." % len(results))
            rep.append("")
            if pics:
                rep.append("**%d pictures differ** from the game's. Look at each (regress/index.html): the mod's own "
                           "content (names, numbers, colours) is fine; anything missing, cut off or wrong isn't." % len(pics))
                rep.append("")
                for cid, g in pics:
                    rep.append("- `%s` %s: %s" % (cid, g["shot"], g["seen"]))
            else:
                rep.append("Every picture is the same as the game's.")
            rep.append("")
            rep.append("Cases loading the game's own states can go wrong in a mod for reasons of their own (a "
                       "conversation's text moved, say): check a failure against a state saved in the mod before "
                       "calling it broken.")
            rep.append("")

    if code == bCode:
        rep.append("## To make it a tested mod")
        rep.append("")
        rep.append("1. A rule for it in `docs/games/%s.md` saying which rules hold and what differs." % (slug or "<game>"))
        rep.append("2. Its entry in the Mods row: `` `%s` %s `%08x` (tested) ``." % (code, name, crc))
        rep.append("3. The fixture `roms/%s+%s.zip` (holding `%s`) and its cases (`\"rom\": \"%s+%s\"`)." % (
            code, slugName, os.path.basename(romPath), code, slugName))
        rep.append("4. `tools/make_dumps.py --dat DAT`, and the mod in the wiki's Supported games table.")
        rep.append("")

    open(os.path.join(out, "report.md"), "w", encoding="utf-8").write("\n".join(rep))
    print("\n".join(rep))
    print("(written to %s)" % os.path.relpath(out, os.getcwd()), file=sys.stderr)
    sys.exit(0 if verdict in (None, "every check holds") else 1)


if __name__ == "__main__":
    main()
