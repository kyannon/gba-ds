#!/usr/bin/env python3
"""Makes the Chinese interfaces' fonts: assets/fonts/NotoSans{SC,TC}-*-subset.ttf.

    tools/make_zh_font.py [--check]

Noto Sans SC and Noto Sans TC are under the SIL Open Font License 1.1
(assets/fonts/NotoSansSC-OFL.txt, -TC-OFL.txt), which allows bundling them.
Each whole font is 12-17 MB, so this cuts them down to the characters its
interface can show: the translations in assets/i18n/zh-CN.tsv (Simplified) and
zh-TW.tsv (Traditional), the ASCII the menus already have (names, numbers,
version numbers) and Chinese punctuation.

The bold face draws the game list's names, which are the player's own ROM
filenames, so it also carries a common-character set: GB2312's whole hanzi set for
Simplified (6763 characters), Big5's whole hanzi set for Traditional (13147), so a rare name like 鎧甲的呼喚 draws too.
Without it a Chinese-named ROM draws as boxes. A character outside both sets
still can't be drawn; the app's own text is always inside the catalogue's.

A character that isn't in the font would draw as nothing, so this stops if one
is missing.

Run it again after adding a translation with a character that wasn't used
before, then tools/embed_fonts.py.

The sources are Google Fonts' variable fonts; download them into
assets/fonts/ first (they aren't in the repo):
https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf
https://github.com/google/fonts/raw/main/ofl/notosanstc/NotoSansTC%5Bwght%5D.ttf

Needs fontTools (pip install fonttools).
"""
import os
import sys

from fontTools import subset  # noqa: E402
from fontTools.ttLib import TTFont  # noqa: E402
from fontTools.varLib.instancer import instantiateVariableFont  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(ROOT, "assets", "i18n")
FONTS = os.path.join(ROOT, "assets", "fonts")

# The menus' own characters: ASCII, the arrows and quotes ui.c and the menus
# draw, and Chinese punctuation.
BASE = "".join(chr(c) for c in range(0x20, 0x7F))
BASE += "…–—‘’“”•·‹›«»°±×÷≤≥→←↑↓"
BASE += "，。、；：？！“”‘’（）【】《》「」『』〈〉～￥·—…％＋－×÷＝＃＠"

FAMILIES = [
    {"name": "Simplified", "source": "NotoSansSC[wght].ttf", "tsv": "zh-CN.tsv",
     "out": {400: "NotoSansSC-Regular-subset.ttf", 700: "NotoSansSC-Bold-subset.ttf"}},
    {"name": "Traditional", "source": "NotoSansTC[wght].ttf", "tsv": "zh-TW.tsv",
     "out": {400: "NotoSansTC-Regular-subset.ttf", 700: "NotoSansTC-Bold-subset.ttf"}},
]


def translations(tsv):
    chars, count = set(), 0
    with open(os.path.join(I18N, tsv), encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            en, zh = line.split("\t", 1)
            chars |= set(zh)
            count += 1
    return chars, count


def gb2312_all():
    """GB2312's hanzi, both levels: 6763 characters (rows B0-F7)."""
    out = set()
    for row in range(0xB0, 0xF8):
        for col in range(0xA1, 0xFF):
            try:
                out.add(bytes([row, col]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return out


def big5_all():
    """Big5's hanzi, both levels: 13147 characters (rows A4-F9)."""
    out = set()
    for first, last in ((0xA4, 0xC6), (0xC9, 0xF9)):
        for row in range(first, last + 1):
            for col in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
                try:
                    out.add(bytes([row, col]).decode("big5"))
                except UnicodeDecodeError:
                    pass
    return out


def main():
    failed = False
    for family in FAMILIES:
        source = os.path.join(FONTS, family["source"])
        if not os.path.exists(source):
            print(f"{family['source']} isn't in assets/fonts/ (see this tool's header)")
            failed = True
            continue
        chars, count = translations(family["tsv"])
        if not count:
            print(f"{family['tsv']} has no lines")
            return 1
        wanted = sorted(chars | set(BASE))
        # the bold face draws the game list's names: the player's own filenames
        common = gb2312_all() if family["name"] == "Simplified" else big5_all()
        print(f"{family['name']}: {count} strings, {len(chars)} Chinese characters")
        for weight, name in family["out"].items():
            set_ = wanted if weight != 700 else sorted(chars | set(BASE) | common)
            font = TTFont(source)
            font = instantiateVariableFont(font, {"wght": weight}, inplace=False, updateFontNames=True)
            cmap = font.getBestCmap()
            missing = [c for c in wanted if ord(c) not in cmap]
            if missing:
                print(f"  {weight}: not in the font: {''.join(missing)}")
                failed = True
                continue
            have_common = len(common) - sum(1 for c in common if ord(c) not in cmap)
            opts = subset.Options()
            opts.hinting = False
            opts.desubroutinize = True
            opts.name_IDs = ["*"]
            opts.name_languages = ["*"]
            opts.notdef_outline = True
            opts.recalc_bounds = False
            opts.drop_tables += ["DSIG"]
            sub = subset.Subsetter(options=opts)
            sub.populate(unicodes=[ord(c) for c in set_])
            sub.subset(font)
            # the same bytes every run (a save would stamp the time into head)
            font.recalcTimestamp = False
            font["head"].created = font["head"].modified = 0
            out = os.path.join(FONTS, name)
            font.save(out, reorderTables=True)
            extra = f" ({have_common} of the {len(common)} common ones, for game names)" if weight == 700 else ""
            print(f"  {name}: {len(set_)} characters{extra}, {os.path.getsize(out) / 1024:.0f} KB")
    if failed:
        print("nothing written for the families that failed: fix that first")
        return 1
    print("now run tools/embed_fonts.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
