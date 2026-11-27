#!/usr/bin/env python3
"""Checks that docs/LIBRETRO.md's list of settings is the module's own (discussion #75).

A libretro core gives the second-screen module its settings: the `Config` it points
`sec.cfg` at, and the few values outside `Config` that emu.c reads (its `extern`
variables in emu.h). docs/LIBRETRO.md lists them, with their defaults, as the
candidates for core options. This fails when the code and that list part ways:

- a module or core file reads a `Config` field (`cfg->name`) the list doesn't have,
  or the list has one no file reads any more;
- emu.h gains or loses an `extern` variable the list doesn't follow;
- a module or core file names the `Config` type anywhere but `sec.cfg` itself
  (second.h), which would be a way to read settings this check can't see.

The files are the Makefile's MODULE_SRC and CORE_SRC, src/logo.c (built with them by
`make module-check`) and their headers. Run from the repository's top folder:
  python3 tools/module_settings.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "LIBRETRO.md"


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def module_files():
    make = (ROOT / "Makefile").read_text()
    files = []
    for var in ("MODULE_SRC", "CORE_SRC"):
        m = re.search(rf"^{var}\s*:=\s*(.*)$", make, flags=re.M)
        if not m:
            sys.exit(f"module_settings: no {var} in the Makefile")
        files += m.group(1).split()
    files.append("src/logo.c")
    paths = [ROOT / f for f in files]
    headers = [p.with_suffix(".h") for p in paths if p.with_suffix(".h").exists()]
    return paths, headers


def doc_table(marker):
    """The first column of the table between <!-- marker --> and <!-- /marker -->."""
    text = DOC.read_text()
    m = re.search(rf"<!-- {marker} -->(.*?)<!-- /{marker} -->", text, flags=re.S)
    if not m:
        sys.exit(f"module_settings: docs/LIBRETRO.md has no <!-- {marker} --> table")
    names = []
    for line in m.group(1).splitlines():
        cell = re.match(r"\|\s*`([A-Za-z_]\w*)`\s*\|", line)
        if cell:
            names.append(cell.group(1))
    return names


def main():
    paths, headers = module_files()
    errors = []

    read = {}
    for p in paths + headers:
        code = strip_comments(p.read_text())
        for name in re.findall(r"\bcfg\s*->\s*([A-Za-z_]\w*)", code):
            read.setdefault(name, set()).add(p.name)
        for line in code.splitlines():
            if re.search(r"(?<![\w])Config\b", line) and not (p.name == "second.h" and re.fullmatch(r"\s*const Config\s*\*\s*cfg;\s*", line)):
                errors.append(f"{p.relative_to(ROOT)} names Config outside sec.cfg: {line.strip()}")

    listed = doc_table("module-settings")
    for name in sorted(set(read) - set(listed)):
        errors.append(f"the module reads cfg->{name} ({', '.join(sorted(read[name]))}), not in docs/LIBRETRO.md's settings")
    for name in sorted(set(listed) - set(read)):
        errors.append(f"docs/LIBRETRO.md's settings list {name}, which no module or core file reads")

    emu_h = strip_comments((ROOT / "src" / "emu.h").read_text())
    externs = set(re.findall(r"^\s*extern\s+[^;(]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;", emu_h, flags=re.M))
    globals_listed = doc_table("module-globals")
    for name in sorted(externs - set(globals_listed)):
        errors.append(f"emu.h's extern {name} is not in docs/LIBRETRO.md's values outside Config")
    for name in sorted(set(globals_listed) - externs):
        errors.append(f"docs/LIBRETRO.md's values outside Config list {name}, which emu.h doesn't declare")

    if errors:
        print("module_settings: docs/LIBRETRO.md's settings and the module's code differ:")
        for e in errors:
            print("  " + e)
        return 1
    print(f"module_settings: {len(listed)} Config fields and {len(globals_listed)} values outside it, as docs/LIBRETRO.md lists them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
