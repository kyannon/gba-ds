#!/usr/bin/env python3
"""Makes the Chinese interfaces' fonts: assets/fonts/NotoSans{SC,TC}-*-subset.ttf.

    tools/make_zh_font.py [--check]

Noto Sans SC and Noto Sans TC are under the SIL Open Font License 1.1
(assets/fonts/NotoSansSC-OFL.txt, -TC-OFL.txt), which allows bundling them.
Each whole font is 12-17 MB, so this cuts them down to the characters its
interface can show: the translations in assets/i18n/zh_CN.po (Simplified) and
zh_TW.po (Traditional), the ASCII the menus already have (names, numbers,
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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402
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
    {"name": "Simplified", "source": "NotoSansSC[wght].ttf", "po": "zh_CN.po",
     "out": {400: "NotoSansSC-Regular-subset.ttf", 700: "NotoSansSC-Bold-subset.ttf"}},
    {"name": "Traditional", "source": "NotoSansTC[wght].ttf", "po": "zh_TW.po",
     "out": {400: "NotoSansTC-Regular-subset.ttf", 700: "NotoSansTC-Bold-subset.ttf"}},
    # Japanese: all of hiragana and katakana too (they're few), so a new kana never needs a new subset
    {"name": "Japanese", "source": "NotoSansJP[wght].ttf", "po": "ja.po",
     "extra": "".join(chr(c) for c in list(range(0x3041, 0x3097)) + list(range(0x30A1, 0x30FD))),
     "out": {400: "NotoSansJP-Regular-subset.ttf", 700: "NotoSansJP-Bold-subset.ttf"}},
]


def translations(po):
    """(the characters of a catalogue's msgstr lines, how many strings)."""
    chars, count = set(), 0
    _, entries = i18n.parse_po(os.path.join(I18N, po))
    for e in entries:
        for t in e.str.values():
            if t:
                chars |= set(t)
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
        chars, count = translations(family["po"])
        if not count:
            print(f"{family['po']} has no lines")
            return 1
        # (every language's own name: the Language list shows them all whatever language is in use)
        autonyms = set()
        for fn in os.listdir(I18N):
            if fn.endswith(".po"):
                hdr, _ = i18n.parse_po(os.path.join(I18N, fn))
                autonyms |= set(hdr.get("X-GBADS-Autonym", ""))
        wanted = sorted(chars | set(BASE) | set(family.get("extra", "")) | {c for c in autonyms if ord(c) > 0x7F})
        print(f"{family['name']}: {count} strings, {len(chars)} Chinese characters")
        for weight, name in family["out"].items():
            font = TTFont(source)
            font = instantiateVariableFont(font, {"wght": weight}, inplace=False, updateFontNames=True)
            cmap = font.getBestCmap()
            # (an autonym's character the font lacks, like 简 in Traditional, comes from the other face in the chain)
            wanted = [c for c in wanted if c not in autonyms or c in chars or c in BASE or ord(c) in cmap]
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
