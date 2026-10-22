#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's Chinese fonts: the characters its screens use, rendered from two open fonts into app/dsp/zh_font.h.

  python3 tools/zh_font.py [--preview FILE.png]      (Pillow; the fonts: FONT_DIR, default ~/fm1/data/fonts)

Four roles, each a face and a size:
  ZS  Noto Sans SC, weight 500, 13 px     small print: footers, hints, units
  ZM  Noto Sans SC, weight 600, 16 px     the body: names, values, the knobs
  ZL  Noto Sans SC, weight 700, 24 px     the 锣鼓经's syllables, big, as they sound
  ZT  LXGW WenKai Medium, 26 px           titles: a pattern's or a 板式's name, in a brush hand
Characters: every non-ASCII character in the app's sources and data, and the printable ASCII, in ZS and ZM; in ZL
the syllables (data/luogu.txt's "@syl" lines) and what the sources write in L_("..."); in ZT the names (the data's
"@pattern" and "@guomen" lines) and what the sources write in T_("..."); ZL and ZT also have the digits and the
ASCII their strings use. A character a role lacks is drawn from ZM (app/zh.c).

Each glyph is cropped to its ink: its box (x and y from the pen and the line's top, width and height), its advance,
and its coverage, 4 bits a pixel, two pixels a byte, rows top to bottom. The output is OFL-1.1, as its fonts are
(LICENSES/).
"""
import argparse
import os
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "app"
OUT = APP / "dsp" / "zh_font.h"
FONT_DIR = Path(os.environ.get("FONT_DIR", Path.home() / "fm1" / "data" / "fonts"))
NOTO = "notosanssc/NotoSansSC.ttf"               # github.com/google/fonts ofl/notosanssc (OFL-1.1)
KAI = "wenkai/LXGWWenKai-Medium.ttf"             # github.com/lxgw/LxgwWenKai v1.530 (OFL-1.1)
ROLES = [("ZS", NOTO, 500, 13), ("ZM", NOTO, 600, 16), ("ZL", NOTO, 700, 24), ("ZT", KAI, None, 26)]
ASCII = "".join(chr(c) for c in range(32, 127))
DIGITS = "0123456789 =.:-+#b/()%"
# the app's own sources, and its data (the generated headers have no text of their own)
SKIP = {"zh_font.h", "lg_data.h"}


def sources():
    for d, pats in ((APP, ["*.c", "*.h"]), (APP / "dsp", ["*.c", "*.h"]), (ROOT / "data", ["*.txt"])):
        for p in pats:
            for f in sorted(d.glob(p)):
                if f.name not in SKIP:
                    yield f


def charsets():
    every, big, title = set(), set(), set()
    for f in sources():
        text = f.read_text(encoding="utf-8")
        if f.suffix == ".txt":                   # the data: only what it shows (names, syllables, show, use)
            keep = []
            for l in text.splitlines():
                l = l.split("#", 1)[0]
                k = l.strip().split(" ", 1)[0]
                if l.startswith("@") or k in ("show", "use", "cat"):
                    keep.append(l)
            text = "\n".join(keep)
            for m in re.finditer(r"^@syl\s+(\S+)", text, re.M):
                big.update(m.group(1))
            for m in re.finditer(r"^@(?:pattern|guomen|banshi)\s+(\S+)", text, re.M):
                title.update(m.group(1))
        else:                                    # C: the strings, not the comments
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            for m in re.finditer(r'\bL_\("((?:[^"\\]|\\.)*)"\)', text):
                big.update(m.group(1))
            for m in re.finditer(r'\bT_\("((?:[^"\\]|\\.)*)"\)', text):
                title.update(m.group(1))
            if f.name == "tq_data.h":           # the 唱段: their titles; their lyrics are drawn from their own glyphs
                text = "".join(re.findall(r'\bT_\("((?:[^"\\]|\\.)*)"\)', text))
            else:
                text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
        every.update(c for c in text if ord(c) > 126 and c not in "\n\t")
    return every, big, title


def load(name, weight, px):
    path = FONT_DIR / name
    if not path.exists():
        sys.exit(f"zh_font: no {path} (FONT_DIR): see the docstring for where the fonts come from")
    f = ImageFont.truetype(str(path), px)
    if weight is not None:
        f.set_variation_by_axes([weight])
    return f


def render(font, chars):
    """the line's height and baseline, and each character's (code, x, y, w, h, adv, 4-bit rows)"""
    asc, desc = font.getmetrics()
    h = asc + desc
    glyphs = []
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        pad = 4
        img = Image.new("L", (adv + 2 * pad + 8, h + 2 * pad), 0)
        ImageDraw.Draw(img).text((pad, pad), ch, font=font, fill=255)
        px = [min(15, (v + 8) // 17) for v in img.tobytes()]
        W = img.width
        rows = [y for y in range(img.height) if any(px[y * W:(y + 1) * W])]
        cols = [x for x in range(W) if any(px[y * W + x] for y in range(img.height))]
        if not rows:                             # a space
            glyphs.append((ord(ch), 0, 0, 0, 0, adv, []))
            continue
        x0, x1, y0, y1 = cols[0], cols[-1] + 1, rows[0], rows[-1] + 1
        bits = []
        for y in range(y0, y1):
            row = px[y * W + x0:y * W + x1] + [0]
            bits += [(row[x] << 4) | row[x + 1] for x in range(0, x1 - x0, 2)]
        glyphs.append((ord(ch), x0 - pad, y0 - pad, x1 - x0, y1 - y0, adv, bits))
    return h, asc, glyphs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="a PNG of every role's characters")
    args = ap.parse_args()
    every, big, title = charsets()
    sets = {"ZS": every | set(ASCII), "ZM": every | set(ASCII),
            "ZL": big | set(DIGITS) | {c for c in big}, "ZT": title | set(DIGITS)}
    lines = ["/* generated by tools/zh_font.py: LUOGU's Chinese fonts (OFL-1.1: glyphs rendered from Noto Sans SC and",
             " * LXGW WenKai; LICENSES/). Per role: the code points in order, each glyph's box from the pen and the",
             " * line's top (x, y, w, h), its advance and its coverage's offset (4 bits a pixel, rows of (w + 1) / 2",
             " * bytes); app/zh.c draws them */", "#pragma once", "#include <stdint.h>", ""]
    total, report, preview = 0, [], []
    for role, file, weight, px in ROLES:
        font = load(file, weight, px)
        chars = sorted(sets[role], key=ord)
        h, asc, glyphs = render(font, chars)
        data, offs = [], []
        for g in glyphs:
            offs.append(len(data))
            data += g[6]
        missing = [chr(g[0]) for g in glyphs if not g[6] and chr(g[0]) != " "]
        if missing:
            sys.exit(f"zh_font: {role} has no glyph for {''.join(missing)!r}")
        n = len(glyphs)
        lines.append(f"static const uint16_t {role}_CP[{n}] = {{" + ", ".join(str(g[0]) for g in glyphs) + "};")
        lines.append(f"static const int8_t {role}_BOX[{n * 4}] = {{" +
                     ", ".join(f"{g[1]}, {g[2]}, {g[3]}, {g[4]}" for g in glyphs) + "};")
        lines.append(f"static const uint8_t {role}_ADV[{n}] = {{" + ", ".join(str(g[5]) for g in glyphs) + "};")
        lines.append(f"static const uint32_t {role}_OFF[{n}] = {{" + ", ".join(map(str, offs)) + "};")
        lines.append(f"static const uint8_t {role}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const zh_font_t {role} = {{{h}, {asc}, {n}, {role}_CP, {role}_BOX, {role}_ADV, "
                     f"{role}_OFF, {role}_DATA}};")
        lines.append("")
        size = len(data) + n * 11
        total += size
        report.append(f"{role}: {px} px, {n} glyphs, {size} B")
        preview.append((role, font, chars))
    OUT.write_text("\n".join(lines))
    print("zh_font: " + "; ".join(report) + f"; {total} B in all -> {OUT.relative_to(ROOT)}")
    if args.preview:
        rows = []
        for role, font, chars in preview:
            text = "".join(c for c in chars if ord(c) > 126) or "".join(chars)
            per = 240 // (font.size + 2)
            for i in range(0, len(text), per):
                rows.append((font, text[i:i + per]))
        img = Image.new("RGB", (240, sum(f.getmetrics()[0] + f.getmetrics()[1] for f, _ in rows) + 4), 0)
        y, d = 2, ImageDraw.Draw(img)
        for f, t in rows:
            d.text((2, y), t, font=f, fill=(255, 184, 70))
            y += sum(f.getmetrics())
        img.save(args.preview)


if __name__ == "__main__":
    main()
