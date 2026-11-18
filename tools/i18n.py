#!/usr/bin/env python3
"""GBA DS translations: extract, check and compile (docs/I18N.md).

  tools/i18n.py pot        write assets/i18n/gba-ds.pot from the source
  tools/i18n.py check      check the .po files against the template
  tools/i18n.py compile    write src/i18n_data.c and src/i18n_data.h
  tools/i18n.py all        pot, check, compile (what the build and CI run)
  tools/i18n.py test       self-test (pure functions)

The .po files in assets/i18n/ are the source of truth. The app reads no files:
`compile` turns them into tables in src/i18n_data.c (generated: regenerate it,
never merge it by hand), which end up inside the zip and the APK with the rest
of the binary. A "pseudo" language (accented, 35% longer) is made from the
template at the same time, to show clipping and strings that were never marked
for translation.

Strings reach the template three ways (all in src/*.c):
  - tr("..."), trc("context", "..."), trn("one", "other", n),
    trnc("context", "one", "other", n), and the marker N_("...") / NC_("ctx", "...")
    for strings in tables that are translated later, where they are drawn;
  - a literal passed straight to a text-drawing call (DRAW_CALLS below);
  - the rows of config.c's settings tables, read by their shape.
"""
import argparse, glob, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
I18N = os.path.join(ROOT, "assets", "i18n")
POT = os.path.join(I18N, "gba-ds.pot")
OUT_C = os.path.join(SRC, "i18n_data.c")
OUT_H = os.path.join(SRC, "i18n_data.h")

SKIP_SRC = {"fonts_data.c", "brand_data.c", "bezel_logo.c", "logo_masks.c", "i18n_data.c", "i18n.c"}

# function -> index of its text argument
DRAW_CALLS = {
    "ui_label": 4, "ui_font_text": 5, "ui_font_wrap": 6, "ui_font_wrap_around": 6,
    "ui_font_wrap_height": 5, "ui_label_width": 2, "ui_font_width": 2, "ui_title_block": 2,
}
# macro -> (number of context literals, number of message literals)
MACROS = {"tr": (0, 1), "trc": (1, 1), "trn": (0, 2), "trnc": (1, 2), "N_": (0, 1), "NC_": (1, 1)}

# Plural-Forms expressions the app implements (src/i18n.c's plural_index), whitespace removed.
PLURAL_RULES = {
    "nplurals=1;plural=0;": ("PL_NONE", 1),
    "nplurals=2;plural=(n!=1);": ("PL_ONE_OTHER", 2),
    "nplurals=2;plural=n!=1;": ("PL_ONE_OTHER", 2),
    "nplurals=2;plural=(n>1);": ("PL_FRENCH", 2),
    "nplurals=2;plural=n>1;": ("PL_FRENCH", 2),
    "nplurals=3;plural=(n%10==1&&n%100!=11?0:n%10>=2&&n%10<=4&&(n%100<10||n%100>=20)?1:2);": ("PL_EAST_SLAVIC", 3),
    "nplurals=3;plural=(n==1?0:n%10>=2&&n%10<=4&&(n%100<10||n%100>=20)?1:2);": ("PL_POLISH", 3),
    "nplurals=3;plural=(n==1)?0:(n>=2&&n<=4)?1:2;": ("PL_CZECH", 3),
    "nplurals=3;plural=(n==1?0:(n>=2&&n<=4)?1:2);": ("PL_CZECH", 3),
}
SCRIPTS = ["latin"]  # step 1: Jost only; scripts are added with their fonts (CJK, Cyrillic...)
STATUSES = ("machine", "reviewed", "test")


class Err(Exception):
    pass


# ------------------------------------------------------------------ C literals

ESC = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "r": "\r", "0": "\0"}


def parse_c_string(s, i):
    """Concatenated string literals starting at s[i] (after whitespace/comments): (value, end) or None."""
    out, found = [], False
    n = len(s)
    while True:
        while i < n and s[i] in " \t\r\n":
            i += 1
        if s.startswith("/*", i):
            j = s.find("*/", i)
            i = j + 2 if j >= 0 else n
            continue
        if s.startswith("//", i):
            j = s.find("\n", i)
            i = j + 1 if j >= 0 else n
            continue
        if i < n and s[i] == '"':
            found = True
            i += 1
            while i < n and s[i] != '"':
                if s[i] == "\\" and i + 1 < n:
                    c = s[i + 1]
                    if c == "u" and re.fullmatch(r"[0-9A-Fa-f]{4}", s[i + 2:i + 6]):
                        out.append(chr(int(s[i + 2:i + 6], 16)))
                        i += 6
                        continue
                    out.append(ESC.get(c, "\\" + c))
                    i += 2
                else:
                    out.append(s[i])
                    i += 1
            i += 1
            continue
        break
    return ("".join(out), i) if found else None


def split_args(s, i):
    """Top-level arguments of the call whose '(' is at s[i-1]: ([text...], end)."""
    args, depth, start, n = [], 0, i, len(s)
    j = i
    while j < n:
        c = s[j]
        if c == '"':
            j += 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
        elif c == "'":
            j += 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == "\\" else 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append(s[start:j])
                return args, j + 1
            depth -= 1
        elif c == "," and depth == 0:
            args.append(s[start:j])
            start = j + 1
        j += 1
    return args, n


def only_literal(arg):
    r = parse_c_string(arg, 0)
    if r and not arg[r[1]:].strip():
        return r[0]
    return None


def line_of(s, pos):
    return s.count("\n", 0, pos) + 1


# ------------------------------------------------------------------ extraction


class Msg:
    def __init__(self, ctx, one, plural=None):
        self.ctx, self.one, self.plural, self.refs = ctx, one, plural, []

    @property
    def key(self):
        return (self.ctx, self.one)


def strip_comments(s):
    """Blank out comments (keeping offsets and newlines) so a call inside one isn't read."""
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith("/*", i):
            j = s.find("*/", i)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", s[i:j]))
            i = j
        elif s.startswith("//", i):
            j = s.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s[i] == "'":
            j = i + 1
            while j < n and s[j] != "'":
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def extract_source(name, text, msgs):
    s = strip_comments(text)
    ref = "src/" + name

    def add(ctx, one, plural, pos):
        if not one.strip():
            return
        m = msgs.setdefault((ctx, one), Msg(ctx, one, plural))
        if plural and not m.plural:
            m.plural = plural
        m.refs.append("%s:%d" % (ref, line_of(s, pos)))

    for m in re.finditer(r"(?<![A-Za-z0-9_])(%s)\s*\(" % "|".join(list(MACROS) + list(DRAW_CALLS)), s):
        fn = m.group(1)
        args, _ = split_args(s, m.end())
        if fn in MACROS:
            nctx, nmsg = MACROS[fn]
            lits = [only_literal(a) for a in args[:nctx + nmsg]]
            if len(lits) < nctx + nmsg or any(v is None for v in lits):
                # a call with a variable (tr(text) in ui.c) is a lookup, not a string to extract
                if any('"' in a for a in args[:nctx + nmsg]):
                    print("warning: %s:%d: %s() needs literal strings" % (ref, line_of(s, m.start()), fn), file=sys.stderr)
                continue
            ctx = lits[0] if nctx else ""
            add(ctx, lits[nctx], lits[nctx + 1] if nmsg == 2 else None, m.start())
        else:
            idx = DRAW_CALLS[fn]
            if idx < len(args):
                v = only_literal(args[idx])
                if v:
                    add("", v, None, m.start())
    if name == "config.c":
        extract_config_tables(s, ref, msgs, text)


def extract_config_tables(s, ref, msgs, text):
    """The choices ({ "value", "Label", n }) and the settings ({ "key", "Label", ..., "note" }) of config.c."""
    for m in re.finditer(r"static const (SettingChoice|Setting) (\w+)\[\]\s*=\s*\{", s):
        kind = m.group(1)
        i = m.end()
        depth = 1
        while i < len(s) and depth:
            if s[i] == "{":
                # one row
                j = i + 1
                args, end = split_args(s, j)
                lits = [only_literal(a) for a in args]
                label = lits[1] if len(lits) > 1 else None
                note = lits[-1] if kind == "Setting" and len(args) >= 10 else None
                for v in (label, note):
                    if v:
                        key = ("", v)
                        mm = msgs.setdefault(key, Msg("", v))
                        mm.refs.append("%s:%d" % (ref, line_of(s, i)))
                i = end
            elif s[i] == "}":
                depth -= 1
                i += 1
            elif s[i] == '"':
                i += 1
                while i < len(s) and s[i] != '"':
                    i += 2 if s[i] == "\\" else 1
                i += 1
            else:
                i += 1


def extract_all():
    msgs = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.c")) + glob.glob(os.path.join(SRC, "*.h"))):
        name = os.path.basename(path)
        if name in SKIP_SRC or name.endswith("_data.h"):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            extract_source(name, f.read(), msgs)
    return msgs


# ------------------------------------------------------------------ .po files


def po_quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'


def po_unquote(q, where):
    q = q.strip()
    if len(q) < 2 or q[0] != '"' or q[-1] != '"':
        raise Err("%s: bad string %r" % (where, q))
    out, i, body = [], 0, q[1:-1]
    while i < len(body):
        if body[i] == "\\" and i + 1 < len(body):
            out.append(ESC.get(body[i + 1], "\\" + body[i + 1]))
            i += 2
        else:
            out.append(body[i])
            i += 1
    return "".join(out)


class PoEntry:
    def __init__(self):
        self.ctx = self.id = self.plural = None
        self.str = {}  # index -> text
        self.flags = set()
        self.line = 0

    @property
    def fuzzy(self):
        return "fuzzy" in self.flags

    @property
    def translated(self):
        return not self.fuzzy and bool(self.str) and all(v != "" for v in self.str.values())


def parse_po(path):
    """(headers dict, [PoEntry]) from a .po file."""
    entries, cur, field = [], None, None
    where = os.path.relpath(path, ROOT)

    def finish():
        nonlocal cur
        if cur is not None and cur.id is not None:
            entries.append(cur)
        cur = None

    with open(path, encoding="utf-8") as f:
        for no, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            w = "%s:%d" % (where, no)
            if not line.strip():
                finish()
                field = None
                continue
            if line.startswith("#~"):
                continue
            if line.startswith("#"):
                if cur is None:
                    cur = PoEntry()
                    cur.line = no
                if line.startswith("#,"):
                    cur.flags.update(x.strip() for x in line[2:].split(","))
                continue
            if cur is None:
                cur = PoEntry()
                cur.line = no
            m = re.match(r"(msgctxt|msgid_plural|msgid|msgstr(?:\[(\d+)\])?)\s+(.*)$", line)
            if m:
                kw, idx, val = m.group(1), m.group(2), po_unquote(m.group(3), w)
                if kw == "msgctxt":
                    if cur.id is not None:
                        finish(); cur = PoEntry(); cur.line = no
                    cur.ctx, field = val, ("ctx",)
                elif kw == "msgid":
                    if cur.id is not None:
                        finish(); cur = PoEntry(); cur.line = no
                    cur.id, field = val, ("id",)
                elif kw == "msgid_plural":
                    cur.plural, field = val, ("plural",)
                else:
                    i = int(idx) if idx is not None else 0
                    cur.str[i], field = val, ("str", i)
            elif line.lstrip().startswith('"'):
                if field is None:
                    raise Err("%s: string without a keyword" % w)
                val = po_unquote(line, w)
                if field[0] == "ctx": cur.ctx += val
                elif field[0] == "id": cur.id += val
                elif field[0] == "plural": cur.plural += val
                else: cur.str[field[1]] += val
            else:
                raise Err("%s: cannot read %r" % (w, line))
    finish()
    headers = {}
    if entries and entries[0].id == "" and entries[0].ctx is None:
        for l in entries[0].str.get(0, "").split("\n"):
            if ":" in l:
                k, v = l.split(":", 1)
                headers[k.strip()] = v.strip()
        entries = entries[1:]
    return headers, entries


# ------------------------------------------------------------------ format specs

SPEC = re.compile(r"%(?:(\d+)\$)?([-+ #0]*)(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|z|j|t|L)?([diuoxXeEfgGcspn%])")


def specs(s):
    """[(position or None, conversion)] for each argument-taking conversion in s."""
    out = []
    for m in SPEC.finditer(s):
        if m.group(6) == "%":
            continue
        if m.group(3) == "*" or m.group(4) == "*":
            raise Err("'*' width in %r" % s)
        out.append((int(m.group(1)) if m.group(1) else None, (m.group(5) or "") + m.group(6)))
    return out


def spec_types(s):
    """{argument number: conversion}; raises on mixing numbered and plain."""
    sp = specs(s)
    numbered = [p for p, _ in sp if p is not None]
    if numbered and len(numbered) != len(sp):
        raise Err("mixes %%1$s and %%s in %r" % s)
    if numbered:
        d = {}
        for p, c in sp:
            if d.setdefault(p, c) != c:
                raise Err("argument %d used as two types in %r" % (p, s))
        return d
    return {i + 1: c for i, (_, c) in enumerate(sp)}


def check_format(en, tr, subset):
    """None if tr's conversions fit en's, else why not."""
    try:
        a, b = spec_types(en), spec_types(tr)
    except Err as e:
        return str(e)
    if subset:
        bad = {k: v for k, v in b.items() if a.get(k) != v}
        return ("uses an argument English doesn't: %r" % tr) if bad else None
    if a != b:
        return "arguments %s don't match English's %s" % (sorted(b.items()), sorted(a.items()))
    return None


# ------------------------------------------------------------------ pot


def write_pot(msgs, path):
    lines = [
        "# GBA DS: the strings the interface can say (made by tools/i18n.py pot: don't edit).",
        '# Translators: copy this file to <language code>.po, fill in the header and the msgstr lines.',
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        "",
    ]
    for key in sorted(msgs, key=lambda k: (k[1].lower(), k[0], k[1])):
        m = msgs[key]
        seen = []
        for r in m.refs:
            if r not in seen:
                seen.append(r)
        lines.append("#: " + " ".join(seen[:4]) + (" ..." if len(seen) > 4 else ""))
        if "%" in m.one and specs(m.one) or (m.plural and specs(m.plural)):
            lines.append("#, c-format")
        if m.ctx:
            lines.append("msgctxt " + po_quote(m.ctx))
        lines.append("msgid " + po_quote(m.one))
        if m.plural:
            lines.append("msgid_plural " + po_quote(m.plural))
            lines.append('msgstr[0] ""')
            lines.append('msgstr[1] ""')
        else:
            lines.append('msgstr ""')
        lines.append("")
    return "\n".join(lines)


def read_pot(path):
    _, entries = parse_po(path)
    msgs = {}
    for e in entries:
        m = Msg(e.ctx or "", e.id, e.plural)
        msgs[m.key] = m
    return msgs


# ------------------------------------------------------------------ languages


class Lang:
    pass


def plural_rule(headers, where):
    pf = re.sub(r"\s+", "", headers.get("Plural-Forms", ""))
    if pf not in PLURAL_RULES:
        raise Err("%s: Plural-Forms %r isn't one the app implements (add it to PLURAL_RULES and src/i18n.c)" % (where, headers.get("Plural-Forms", "")))
    return PLURAL_RULES[pf]


def load_langs(pot_msgs, problems, warnings):
    langs = []
    for path in sorted(glob.glob(os.path.join(I18N, "*.po"))):
        where = os.path.relpath(path, ROOT)
        try:
            headers, entries = parse_po(path)
        except Err as e:
            problems.append(str(e)); continue
        L = Lang()
        L.path, L.where, L.headers, L.entries = path, where, headers, entries
        L.code = headers.get("Language", "")
        if not re.fullmatch(r"[a-z]{2,3}(_[A-Z][A-Za-z]{1,3})?", L.code) or os.path.basename(path)[:-3] != L.code:
            problems.append("%s: Language must be the file name (de.po: de, zh_CN.po: zh_CN), got %r" % (where, L.code))
            continue
        L.autonym = headers.get("X-GBADS-Autonym", "")
        if not L.autonym:
            problems.append("%s: X-GBADS-Autonym (the language's own name, shown in Settings) is missing" % where)
        L.script = headers.get("X-GBADS-Script", "latin")
        if L.script not in SCRIPTS:
            problems.append("%s: X-GBADS-Script %r isn't one of %s" % (where, L.script, SCRIPTS))
        L.status = headers.get("X-GBADS-Status", "")
        if L.status not in STATUSES:
            problems.append("%s: X-GBADS-Status must be one of %s" % (where, "/".join(STATUSES)))
        L.aliases = headers.get("X-GBADS-Locales", "").split()
        try:
            L.rule, L.nplurals = plural_rule(headers, where)
        except Err as e:
            problems.append(str(e)); continue
        L.stats = check_lang(L, pot_msgs, problems, warnings)
        langs.append(L)
    return langs


def check_lang(L, pot, problems, warnings):
    seen, translated, fuzzy, missing = set(), 0, 0, 0
    for e in L.entries:
        key = (e.ctx or "", e.id)
        w = "%s:%d" % (L.where, e.line)
        if key in seen:
            problems.append("%s: %r is in the file twice" % (w, e.id)); continue
        seen.add(key)
        m = pot.get(key)
        if m is None:
            warnings.append("%s: %r isn't in the code any more (obsolete)" % (w, e.id)); continue
        if (e.plural is not None) != (m.plural is not None):
            problems.append("%s: %r: plural in one and not the other (code: %s)" % (w, e.id, "plural" if m.plural else "single")); continue
        if e.fuzzy:
            fuzzy += 1; continue
        if not e.translated:
            continue
        translated += 1
        if m.plural:
            if len(e.str) != L.nplurals or sorted(e.str) != list(range(L.nplurals)):
                problems.append("%s: %r needs msgstr[0]..msgstr[%d]" % (w, e.id, L.nplurals - 1)); continue
            for i, v in sorted(e.str.items()):
                bad = check_format(m.plural if i else m.one, v, subset=True) if "%" in (m.one + m.plural) else None
                if bad: problems.append("%s: %r form %d: %s" % (w, e.id, i, bad))
        else:
            bad = check_format(m.one, e.str[0], subset=False)
            if bad: problems.append("%s: %r: %s" % (w, e.id, bad))
        for v in e.str.values():
            if v.strip() != v and m.one.strip() == m.one:
                warnings.append("%s: %r: translation has leading or trailing space" % (w, e.id)); break
    missing = [k for k in pot if k not in seen]
    return dict(translated=translated, fuzzy=fuzzy, untranslated=len(pot) - translated - fuzzy, total=len(pot), missing=missing)


# ------------------------------------------------------------------ pseudo

ACCENT = dict(zip("AEIOUYaeiouycnsz", "ÀÉÎÕÜÝàéîõüýçñšž"))


def pseudo_text(s):
    """Accented, 35% longer, format specifiers and newlines untouched."""
    out, i, letters = [], 0, 0
    while i < len(s):
        if s[i] == "%":
            m = SPEC.match(s, i)
            if m:
                out.append(m.group(0)); i = m.end(); continue
        if s[i] == "\n":
            out.append(s[i]); i += 1; continue
        c = ACCENT.get(s[i], s[i])
        letters += c.isalpha()
        out.append(c); i += 1
    pad = int(round(letters * 0.35))
    if pad and "\n" not in s:
        out.append(" " + "·" * pad)
    return "".join(out)


def pseudo_lang(pot):
    L = Lang()
    L.code, L.autonym, L.script, L.status, L.aliases = "pseudo", "Pseudo (testing)", "latin", "test", []
    L.rule, L.nplurals = "PL_ONE_OTHER", 2
    L.entries = []
    for key, m in pot.items():
        e = PoEntry()
        e.ctx, e.id, e.plural = key[0] or None, key[1], m.plural
        if m.plural:
            e.str = {0: pseudo_text(m.one), 1: pseudo_text(m.plural)}
        else:
            e.str = {0: pseudo_text(m.one)}
        L.entries.append(e)
    return L


# ------------------------------------------------------------------ compile


def c_str(s):
    out = ['"']
    for b in s.encode("utf-8"):
        if b in (0x22, 0x5C):
            out.append("\\" + chr(b))
        elif b == 0x0A:
            out.append("\\n")
        elif b == 0x09:
            out.append("\\t")
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("\\%03o" % b)
    out.append('"')
    return "".join(out)


def compile_c(langs, pot):
    """The text of i18n_data.c and i18n_data.h."""
    hdr = [
        "/* Generated by tools/i18n.py compile from the .po files in assets/i18n: regenerate, never merge by hand. */",
        "#ifndef GBADS_I18N_DATA_H",
        "#define GBADS_I18N_DATA_H",
        "",
        "#include \"i18n.h\"",
        "",
        "/* the Settings > Language choices after Automatic and English: { code, autonym, 0 }.",
        " * The pseudo-language (accented, longer text, to show clipping and strings never marked",
        " * for translation) is only listed in -dev builds (the Makefile defines GBADS_I18N_PSEUDO). */",
        "#ifdef GBADS_I18N_PSEUDO",
        "#define I18N_PSEUDO_CHOICE { \"pseudo\", \"Pseudo (testing)\", 0 },",
        "#else",
        "#define I18N_PSEUDO_CHOICE",
        "#endif",
        "#define I18N_CHOICES I18N_PSEUDO_CHOICE \\",
    ]
    shown = [L for L in langs if L.status != "test"]
    for L in shown:
        hdr.append("\t{ %s, %s, 0 }, \\" % (c_str(L.code), c_str(L.autonym)))
    hdr.append("")
    hdr.append("#endif")
    hdr.append("")

    c = ["/* Generated by tools/i18n.py compile from the .po files in assets/i18n: regenerate, never merge by hand. */",
         '#include "i18n_data.h"', ""]
    for L in langs:
        ident = "lang_" + re.sub(r"\W", "_", L.code)
        rows = []
        for e in L.entries:
            if not e.translated:
                continue
            m = pot.get((e.ctx or "", e.id))
            if m is None:
                continue
            key = ((e.ctx + "\x04") if e.ctx else "") + e.id
            rows.append((key.encode("utf-8"), e, m))
        rows.sort(key=lambda r: r[0])
        for n, (_, e, m) in enumerate(rows):
            forms = [e.str[i] for i in range(L.nplurals)] if m.plural else [e.str[0]]
            c.append("static const char* const %s_f%d[] = { %s };" % (ident, n, ", ".join(c_str(f) for f in forms)))
        c.append("static const I18nEntry %s_entries[] = {" % ident)
        for n, (k, e, m) in enumerate(rows):
            c.append("\t{ %s, %s_f%d, %d }," % (c_str(k.decode("utf-8")), ident, n, len(e.str) if not m.plural else L.nplurals))
        if not rows:
            c.append("\t{ \"\", 0, 0 },")
        c.append("};")
        c.append("")
    c.append("const I18nLang i18nLangs[] = {")
    for L in langs:
        ident = "lang_" + re.sub(r"\W", "_", L.code)
        n = sum(1 for e in L.entries if e.translated and pot.get((e.ctx or "", e.id)))
        c.append("\t{ %s, %s, %s, %s, %d, %d, %s_entries, %d }," % (
            c_str(L.code), c_str(L.autonym), c_str(" ".join(L.aliases)), L.rule,
            SCRIPTS.index(L.script), {"machine": 1, "reviewed": 2, "test": 3}[L.status], ident, n))
    c.append("};")
    c.append("const int i18nLangCount = %d;" % len(langs))
    c.append("")
    return "\n".join(c), "\n".join(hdr)


# ------------------------------------------------------------------ commands


def do_pot(write=True):
    msgs = extract_all()
    text = write_pot(msgs, POT)
    if write:
        os.makedirs(I18N, exist_ok=True)
        with open(POT, "w", encoding="utf-8") as f:
            f.write(text)
    return msgs, text


def do_check(strict_pot=True):
    problems, warnings = [], []
    msgs, text = do_pot(write=False)
    if strict_pot:
        old = open(POT, encoding="utf-8").read() if os.path.exists(POT) else ""
        if old != text:
            problems.append("assets/i18n/gba-ds.pot is out of date: run tools/i18n.py pot")
    langs = load_langs(msgs, problems, warnings)
    for L in langs:
        st = L.stats
        print("%-8s %-10s %4d/%d translated, %d fuzzy, %d to do   (%s)" % (L.code, L.autonym, st["translated"], st["total"], st["fuzzy"], st["untranslated"], L.status))
    for w in warnings:
        print("warning:", w)
    for p in problems:
        print("error:", p)
    print("%d strings in the code, %d languages, %d errors" % (len(msgs), len(langs), len(problems)))
    return msgs, langs, problems


def do_compile():
    msgs, langs, problems = do_check()
    if problems:
        raise Err("fix the errors above first")
    langs = langs + [pseudo_lang(msgs)]
    langs.sort(key=lambda L: L.code)
    c, h = compile_c(langs, msgs)
    for path, text in ((OUT_C, c), (OUT_H, h)):
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    print("wrote src/i18n_data.c and src/i18n_data.h (%d languages with pseudo)" % len(langs))


def self_test():
    assert specs("%d of %s %%") == [(None, "d"), (None, "s")]
    assert spec_types("%2$s on %1$d") == {2: "s", 1: "d"}
    assert check_format("%s on %s", "Auf %2$s: %1$s", False) is None
    assert check_format("%d games", "%s Spiele", False) is not None
    assert check_format("Found %d game", "%d Spiele", True) is None
    assert check_format("Found %d game", "%d %s", True) is not None
    assert parse_c_string('"a" "b\\n" x', 0)[0] == "a" + "b\n"
    assert po_quote('a"b\n') == '"a\\"b\\n"'
    assert pseudo_text("Open %s") == "Õpéñ %s ·", pseudo_text("Open %s")
    s = strip_comments('x = tr("a"); /* tr("b") */ // tr("c")\n')
    m = {}
    extract_source("t.c", s, m)
    assert list(m) == [("", "a")], m
    m = {}
    extract_source("t.c", 'f(trc("menu", "Back"), trn("%d game", "%d games", n), ui_label(0, 1, 2, 3, "Play", c));', m)
    assert ("menu", "Back") in m and ("", "%d game") in m and m[("", "%d game")].plural == "%d games" and ("", "Play") in m
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["pot", "check", "compile", "all", "test"])
    a = ap.parse_args()
    try:
        if a.cmd == "test":
            self_test()
        elif a.cmd == "pot":
            msgs, _ = do_pot()
            print("wrote assets/i18n/gba-ds.pot: %d strings" % len(msgs))
        elif a.cmd == "check":
            _, _, problems = do_check()
            return 1 if problems else 0
        elif a.cmd == "compile":
            do_compile()
        else:
            msgs, _ = do_pot()
            print("assets/i18n/gba-ds.pot: %d strings" % len(msgs))
            do_compile()
    except Err as e:
        print("error:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
