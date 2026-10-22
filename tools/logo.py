#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's logo, 8-bit: a gong with 锣鼓 on its face, hung from a cord, its mallet, LUOGU under it; drawn on a
120 x 120 grid of 16 colours (each pixel 2 x 2 on the FM-1's screen), written into app/logo.h for the splash.

  python3 tools/logo.py [--preview FILE.png] [--frames DIR]      (Pillow; the pixel font: FONT_DIR, ~/fm1/data/fonts)

The gong's face is drawn in six bands by radius (colours 8..13): the splash cycles their colours, so rings run out
from the centre as if the gong had been struck. 锣鼓 is Fusion Pixel's 12 px (OFL-1.1, by TakWolf; LICENSES/), each of
its pixels 2 x 2 here; LUOGU's letters are drawn below, 7 x 8.
"""
import argparse
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "logo.h"
FONT_DIR = Path(os.environ.get("FONT_DIR", Path.home() / "fm1" / "data" / "fonts"))
PIXEL = "fusion12/fusion-pixel-12px-proportional-zh_hans.ttf"   # github.com/TakWolf/fusion-pixel-font (OFL-1.1)
N = 120

BG, BG_DK, INK, GOLD_DK, GOLD, GOLD_LT, SHINE, WHITE = range(8)
BAND = 8                                        # 8..13: the face's rings, by radius
WOOD, TEXT = 14, 15
PALETTE = [
    (168, 30, 22),      # BG       the red of the stage
    (110, 16, 12),      # BG_DK    its dither, the shadows
    (34, 10, 8),        # INK      outlines
    (150, 88, 18),      # GOLD_DK  the gong's lip, its shadow side
    (214, 146, 36),     # GOLD     the gong
    (246, 198, 84),     # GOLD_LT  its light side
    (255, 238, 176),    # SHINE    the highlight
    (255, 255, 255),    # WHITE    the strike's sparks
] + [(214, 146, 36)] * 6 + [   # BAND..BAND+5: the face, gold (the splash cycles these)
    (120, 58, 22),      # WOOD     the cord, the mallet's handle
    (124, 18, 12),      # TEXT     锣鼓, stamped on the face
]

LETTERS = {             # LUOGU's letters, 7 x 8, strokes two pixels thick
    "L": ["XX     ", "XX     ", "XX     ", "XX     ", "XX     ", "XX     ", "XXXXXX ", "XXXXXX "],
    "U": ["XX   XX", "XX   XX", "XX   XX", "XX   XX", "XX   XX", "XX   XX", "XXXXXXX", " XXXXX "],
    "O": [" XXXXX ", "XXXXXXX", "XX   XX", "XX   XX", "XX   XX", "XX   XX", "XXXXXXX", " XXXXX "],
    "G": [" XXXXX ", "XXXXXXX", "XX     ", "XX  XXX", "XX  XXX", "XX   XX", "XXXXXXX", " XXXXX "],
}
CX, CY, R = 60, 47, 37.5                         # the gong: centre, outer edge


def draw():
    img = [[BG] * N for _ in range(N)]

    def put(x, y, c):
        if 0 <= x < N and 0 <= y < N:
            img[y][x] = c

    # the ground: dithered darker toward the top and the bottom (a 4 x 4 ordered dither)
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    for y in range(N):
        edge = max(0.0, (14 - y) / 14.0, (y - (N - 15)) / 14.0)
        for x in range(N):
            if edge * 16 > bayer[y % 4][x % 4]:
                img[y][x] = BG_DK

    # the cord: from a nail at the top to two holes near the gong's top edge
    nail = (CX, 3)
    for hx in (CX - 6, CX + 6):
        x0, y0, x1, y1 = nail[0], nail[1], hx, int(CY - R + 6)
        steps = max(abs(x1 - x0), abs(y1 - y0)) * 2
        for i in range(steps + 1):
            x = round(x0 + (x1 - x0) * i / steps)
            y = round(y0 + (y1 - y0) * i / steps)
            put(x, y, WOOD)
            put(x + (1 if hx > CX else -1), y, WOOD)
    for dx in range(-1, 2):
        for dy in range(-1, 2):
            put(nail[0] + dx, nail[1] + dy, GOLD_DK if dx or dy else GOLD_LT)

    # the gong: an outline, a lip shaded by the light from the upper left, the face in rings
    lx, ly = -0.6, -0.8                             # where the light comes from
    for y in range(N):
        for x in range(N):
            dx, dy = x + 0.5 - CX, y + 0.5 - CY
            r = math.hypot(dx, dy)
            if r > R:
                continue
            if r > R - 1.2:
                put(x, y, INK)
                continue
            lit = (dx * lx + dy * ly) / max(r, 1e-6)    # -1 .. 1: facing the light
            if r > R - 6.5:                             # the lip
                put(x, y, GOLD_LT if lit > 0.45 else GOLD_DK if lit < -0.35 else GOLD)
                continue
            if r > R - 7.7:                             # the lip's inner edge
                put(x, y, GOLD_DK if lit < 0.6 else GOLD)
                continue
            put(x, y, BAND + min(5, int(r / 5.0)))
    # the face's highlight: a crescent at the upper left, and a spark of shine in it
    for y in range(N):
        for x in range(N):
            dx, dy = x + 0.5 - CX, y + 0.5 - CY
            r = math.hypot(dx, dy)
            if r < R - 9 and r > R - 15:
                a = math.degrees(math.atan2(dy, dx))
                if -165 < a < -105:
                    put(x, y, GOLD_LT if (-150 < a < -120) else img[y][x])
    for x, y in ((CX - 16, CY - 18), (CX - 18, CY - 15), (CX - 17, CY - 17)):
        put(x, y, SHINE)

    # 锣鼓 on the face: the pixel font's 12 px, each pixel 2 x 2, a shadow under it
    font = ImageFont.truetype(str(FONT_DIR / PIXEL), 12)
    g = Image.new("1", (40, 16), 0)
    d = ImageDraw.Draw(g)
    d.fontmode = "1"
    d.text((0, 0), "锣鼓", font=font, fill=1)
    x0, y0, x1, y1 = g.getbbox()
    gw, gh = (x1 - x0) * 2, (y1 - y0) * 2
    ox, oy = CX - gw // 2, CY - gh // 2 + 1
    for layer, (sx, sy, c) in enumerate(((1, 1, GOLD_DK), (0, 0, TEXT))):
        for y in range(y0, y1):
            for x in range(x0, x1):
                if g.getpixel((x, y)):
                    for k in range(4):
                        put(ox + (x - x0) * 2 + k % 2 + sx, oy + (y - y0) * 2 + k // 2 + sy, c)

    # the mallet: its handle from the lower right, its head against the lip, sparks where it strikes
    hx0, hy0, hx1, hy1 = 107, 92, 90, 75
    steps = 40
    for i in range(steps + 1):
        x = hx0 + (hx1 - hx0) * i / steps
        y = hy0 + (hy1 - hy0) * i / steps
        for t in (-1.5, -0.5, 0.5, 1.5):
            put(round(x + t * 0.7), round(y - t * 0.7), INK if abs(t) > 1 else WOOD)
    hcx, hcy = 87, 72
    for y in range(hcy - 5, hcy + 6):
        for x in range(hcx - 5, hcx + 6):
            r = math.hypot(x + 0.5 - hcx, y + 0.5 - hcy)
            if r <= 4.6:
                put(x, y, INK if r > 3.6 else BG_DK if (x - hcx) + (y - hcy) > 1 else BG)
    for sx, sy in ((96, 61), (99, 70)):           # (all WHITE: the intro turns that colour off to put them out)
        for k in range(-2, 3):
            put(sx + k, sy, WHITE)
            put(sx, sy + k, WHITE)

    # LUOGU: gold from light to deep, outlined, a shadow below
    word = "LUOGU"
    lw = 7 * 2
    total = len(word) * lw + (len(word) - 1) * 2
    bx, by = (N - total) // 2, 93
    grad = [SHINE, SHINE, GOLD_LT, GOLD_LT, GOLD_LT, GOLD, GOLD, GOLD_DK]
    cells = set()
    for i, ch in enumerate(word):
        for ry, row in enumerate(LETTERS[ch]):
            for rx, v in enumerate(row):
                if v == "X":
                    for k in range(4):
                        cells.add((bx + i * (lw + 2) + rx * 2 + k % 2, by + ry * 2 + k // 2))
    for (x, y) in cells:                            # the shadow, then the outline, then the letters
        for dx, dy in ((1, 2), (2, 2), (2, 3), (1, 3)):
            if (x + dx, y + dy) not in cells:
                put(x + dx, y + dy, BG_DK)
    for (x, y) in cells:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (x + dx, y + dy) not in cells:
                    put(x + dx, y + dy, INK)
    for (x, y) in cells:
        put(x, y, grad[min(7, (y - by) // 2)])
    return img


def write(img):
    rows = []
    for y in range(N):
        b = []
        for x in range(0, N, 2):
            b.append((img[y][x] << 4) | img[y][x + 1])
        rows.append(b)
    pal = ", ".join(f"RGB({r}, {g}, {b})" for r, g, b in PALETTE)
    lines = ["/* generated by tools/logo.py: LUOGU's logo, 120 x 120 pixels of 16 colours (4 bits each, two a byte, rows",
             " * top to bottom), each 2 x 2 on the screen; colours 8..13 the gong's face in rings from the centre, cycled",
             " * by the splash. 锣鼓 drawn from Fusion Pixel 12 px (OFL-1.1, LICENSES/) */",
             "#pragma once", "#include <stdint.h>", "",
             f"#define LOGO_N {N}", "#define LOGO_BAND 8   /* the face's rings: colours LOGO_BAND .. LOGO_BAND + 5 */",
             f"enum {{ LOGO_BG = {BG}, LOGO_GOLD = {GOLD}, LOGO_GOLD_LT = {GOLD_LT}, LOGO_SHINE = {SHINE}, "
             f"LOGO_SPARK = {WHITE} }};   /* (the sparks: their own colour) */",
             f"static const uint16_t LOGO_PAL[16] = {{{pal}}};",
             f"static const uint8_t LOGO_PX[{N * N // 2}] = {{"]
    for b in rows:
        lines.append("    " + ", ".join(f"0x{v:02x}" for v in b) + ",")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")


def preview(img, path, scale=2, pal=None):
    pal = pal or PALETTE
    im = Image.new("RGB", (N, N))
    im.putdata([pal[c] for row in img for c in row])
    im.resize((N * scale, N * scale), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="the logo as the screen shows it (240 x 240), and at 4x beside it")
    args = ap.parse_args()
    img = draw()
    write(img)
    print(f"logo: {N} x {N}, {N * N // 2} B -> {OUT.relative_to(ROOT)}")
    if args.preview:
        preview(img, args.preview, 2)
        p = Path(args.preview)
        preview(img, str(p.with_name(p.stem + "_x4" + p.suffix)), 4)


if __name__ == "__main__":
    main()
