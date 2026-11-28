"""Make a smaller copy of a font with only the characters a project needs (for the HMI's "font").

    python -I tools/subset_font.py <font.otf|ttf> <out.otf> [--jis2] [--text file.txt ...]
    python -I tools/subset_font.py <font.ttf> <out.ttf> --latin [--text file.txt ...]
    python -I tools/subset_font.py <font.otf> <out.otf> --sc [--text file.txt ...]

Keeps ASCII, Latin-1, general punctuation and arrows, CJK symbols, hiragana, katakana, full-width
forms and the JIS X 0208 level-1 kanji (about 3000, those of everyday Japanese); --jis2 adds the
level-2 kanji, --text adds every character found in the given files (for example the project's
screens and project.json). Needs fontTools (pip install fonttools).

--latin makes a font for the languages of Europe instead (from Noto Sans, for example): Latin with
its accented letters (Latin-1, Extended-A and -B: German, French, Spanish, Polish, Czech, Hungarian,
Turkish, Romanian, the Baltic and Nordic languages ...), Greek, Cyrillic, punctuation, currency signs
(the euro), arrows and mathematical signs - and none of the Japanese sets. It is what
tools/designer/fonts/NotoSans-subset.ttf was made with.

--sc makes a font for Simplified Chinese instead (from Noto Sans SC): ASCII, Latin-1, punctuation,
the CJK symbols and full-width forms, and the 6763 hanzi of GB 2312 (level 1 and 2: the characters
of everyday Chinese). It is what tools/designer/fonts/NotoSansSC-subset.otf was made with.

A font under the SIL Open Font License (Noto Sans JP, for example) may be subset and shipped with
the project: keep its copyright notice and the license text (OFL.txt) next to the file.
"""
import argparse, io, sys
from fontTools import subset
from fontTools.ttLib import TTFont


def jis_kanji(level2):
    out = []
    rows = range(0xB0, 0xF5 if level2 else 0xD0)              # EUC-JP rows 16-47 (level 1), 48-84 (level 2)
    for a in rows:
        for b in range(0xA1, 0xFF):
            try:
                out.append(bytes([a, b]).decode('euc_jp'))
            except UnicodeDecodeError:
                pass
    return out


def gb2312():
    """the characters of GB 2312 (symbols, kana, Latin, Greek, Cyrillic and the 6763 hanzi)"""
    out = []
    for a in range(0xA1, 0xF8):
        for b in range(0xA1, 0xFF):
            try:
                out.append(bytes([a, b]).decode('gb2312'))
            except UnicodeDecodeError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('font')
    ap.add_argument('out')
    ap.add_argument('--jis2', action='store_true')
    ap.add_argument('--latin', action='store_true')
    ap.add_argument('--sc', action='store_true')
    ap.add_argument('--text', nargs='*', default=[])
    a = ap.parse_args()
    chars = set()
    japanese = [(0x20, 0x7E), (0xA0, 0xFF), (0x2010, 0x206F), (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2460, 0x24FF),
                (0x25A0, 0x25FF), (0x3000, 0x303F), (0x3040, 0x309F), (0x30A0, 0x30FF), (0xFF00, 0xFFEF)]
    european = [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F), (0x180, 0x24F), (0x370, 0x3FF), (0x400, 0x4FF), (0x2010, 0x206F),
                (0x20A0, 0x20BF), (0x2100, 0x214F), (0x2190, 0x21FF), (0x2200, 0x22FF), (0x25A0, 0x25FF)]
    chinese = [(0x20, 0x7E), (0xA0, 0xFF), (0x2010, 0x206F), (0x2190, 0x21FF), (0x2200, 0x22FF), (0x2460, 0x24FF),
               (0x25A0, 0x25FF), (0x3000, 0x303F), (0xFF00, 0xFFEF)]
    for lo, hi in european if a.latin else chinese if a.sc else japanese:
        chars.update(chr(c) for c in range(lo, hi + 1))
    if a.sc:
        chars.update(gb2312())
    elif not a.latin:
        chars.update(jis_kanji(a.jis2))
    for path in a.text:
        chars.update(io.open(path, encoding='utf-8', errors='ignore').read())
    font = TTFont(a.font)
    opts = subset.Options()
    opts.name_IDs = ['*']                                      # keep the copyright and license records
    opts.name_languages = ['*']
    opts.notdef_outline = True
    opts.layout_features = ['*']
    if a.latin or a.sc:
        opts.hinting = False                                   # (the runtime's rasteriser does not use the hints: half the size)
    s = subset.Subsetter(opts)
    s.populate(text=''.join(sorted(chars)))
    s.subset(font)
    font.save(a.out)
    print('%d characters -> %s' % (len(chars), a.out))


if __name__ == '__main__':
    sys.exit(main())
