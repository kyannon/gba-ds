#!/usr/bin/env python3
"""Makes the Chinese interfaces' fonts: assets/fonts/NotoSans{SC,TC}-*-subset.ttf.

    tools/make_zh_font.py [--check]

Noto Sans SC and Noto Sans TC are under the SIL Open Font License 1.1
(assets/fonts/NotoSansSC-OFL.txt, -TC-OFL.txt), which allows bundling them.
Each whole font is 12-17 MB, so this cuts them down to the characters its
interface can show: the translations in assets/i18n/zh-CN.tsv (Simplified) and
zh-TW.tsv (Traditional), the ASCII the menus already have (names, numbers,
version numbers) and Chinese punctuation. A character that isn't in the font
would draw as nothing, so this stops if one is missing.

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
        print(f"{family['name']}: {count} strings, {len(chars)} Chinese characters")
        for weight, name in family["out"].items():
            font = TTFont(source)
            font = instantiateVariableFont(font, {"wght": weight}, inplace=False, updateFontNames=True)
            cmap = font.getBestCmap()
            missing = [c for c in wanted if ord(c) not in cmap]
            if missing:
                print(f"  {weight}: not in the font: {''.join(missing)}")
                failed = True
                continue
            opts = subset.Options()
            opts.hinting = False
            opts.desubroutinize = True
            opts.name_IDs = ["*"]
            opts.name_languages = ["*"]
            opts.notdef_outline = True
            opts.recalc_bounds = False
            opts.drop_tables += ["DSIG"]
            sub = subset.Subsetter(options=opts)
            sub.populate(unicodes=[ord(c) for c in wanted])
            sub.subset(font)
            # the same bytes every run (a save would stamp the time into head)
            font.recalcTimestamp = False
            font["head"].created = font["head"].modified = 0
            out = os.path.join(FONTS, name)
            font.save(out, reorderTables=True)
            print(f"  {name}: {len(wanted)} characters, {os.path.getsize(out) / 1024:.0f} KB")
    if failed:
        print("nothing written for the families that failed: fix that first")
        return 1
    print("now run tools/embed_fonts.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
