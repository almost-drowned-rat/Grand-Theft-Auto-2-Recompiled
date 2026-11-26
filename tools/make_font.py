#!/usr/bin/env python3
"""The glyphs a translation needs that the game's sprite fonts lack (Polish letters, Cyrillic, CJK ...),
cut from Noto Sans CJK Bold (SIL OFL 1.1; Latin, Cyrillic, Japanese and Chinese in one family) into a small
font for the translations pak (data/fonts/<letter>.otf, read by src/gta2_text.c). Polish and Russian use
Noto Sans Bold (Latin Extended, Cyrillic; Noto CJK lacks the Polish ogonek letters), Japanese / Chinese the
CJK family's JP / SC face. Both SIL OFL 1.1.

  make_font.py LANG.json OUT.otf [--face JP|SC|LATIN] [--ttc PATH] [--ttf PATH]

Characters taken: every character of the texts that tools/gxt.py would not write as a game font code, plus
its upper / lower case form (the game upper-cases some texts) and a few common punctuation marks.
"""
import json
import sys

from fontTools import subset
from fontTools.ttLib import TTCollection

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import gxt  # noqa: E402

FACES = {"JP": "Noto Sans CJK JP Bold", "SC": "Noto Sans CJK SC Bold"}
EXTRA = "。、「」『』！？：；（）・…，"


def needed(texts):
    out = set()
    for v in texts.values():
        for c in v:
            if c in gxt.CHAR_TO_CODE or c in gxt.PASS or 0x20 <= ord(c) < 0x7F or ord(c) < 0x20:
                continue
            if 0xE080 <= ord(c) <= 0xE0B0:
                continue
            for x in (c, c.upper(), c.lower()):
                if len(x) == 1 and x not in gxt.CHAR_TO_CODE and not 0x20 <= ord(x) < 0x7F:
                    out.add(x)
    return out


def main(a):
    texts = json.load(open(a[0], encoding="utf-8"))
    face = a[a.index("--face") + 1] if "--face" in a else "SC"
    ttc = a[a.index("--ttc") + 1] if "--ttc" in a else "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc"
    chars = needed(texts)
    if any(0x3000 <= ord(c) <= 0x9FFF or 0xFF00 <= ord(c) <= 0xFFEF for c in chars):
        chars |= set(EXTRA)
    if face == "LATIN":
        from fontTools.ttLib import TTFont
        font = TTFont(a[a.index("--ttf") + 1] if "--ttf" in a else "/usr/share/fonts/noto/NotoSans-Bold.ttf")
    else:
        coll = TTCollection(ttc)
        font = next(f for f in coll.fonts if f["name"].getDebugName(4) == FACES[face])
    cmap = font.getBestCmap()
    missing = sorted(c for c in chars if ord(c) not in cmap)
    opts = subset.Options()
    opts.layout_features = []          # no shaping: one glyph per character
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.hinting = False
    opts.desubroutinize = True
    s = subset.Subsetter(opts)
    s.populate(unicodes=[ord(c) for c in chars if ord(c) in cmap])
    s.subset(font)
    font.save(a[1])
    print(f"{a[1]}: {len(chars) - len(missing)} glyphs ({face}), missing {''.join(missing) or 'none'}")


if __name__ == "__main__":
    main(sys.argv[1:])
