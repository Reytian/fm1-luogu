#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's 郭德纲模式 (a 彩蛋): 郭德纲 and 于谦 as they stood in their 《黄鹤楼》 (郭: white cap, white towel, dark
大褂; 于: white 大褂), an 8-bit picture of 120 x 88 pixels in 16 colours, run-length coded; and 郭德纲's sung 包 and
肉包 from that show as 16 kHz IMA ADPCM.

  python3 tools/guo.py BAO.wav ROUBAO.wav [--preview FILE.png]     (Pillow, numpy, scipy, soundfile)

writes app/guo_pic.h (the picture, the bubble's two characters from the pixel font) and app/dsp/guo_snd.h (the two
clips). The picture is LUOGU's own drawing (MPL-2.0); the clips are cut from 德云社's recording of the show and
belong to its owners, not under LUOGU's licence (CREDITS.md). Without the two headers the mode is not built in.
"""
import argparse
import os
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
PIC_OUT = ROOT / "app" / "guo_pic.h"
SND_OUT = ROOT / "app" / "dsp" / "guo_snd.h"
FONT_DIR = Path(os.environ.get("FONT_DIR", Path.home() / "fm1" / "data" / "fonts"))
PIXEL = "fusion12/fusion-pixel-12px-proportional-zh_hans.ttf"   # github.com/TakWolf/fusion-pixel-font (OFL-1.1)

W, H = 120, 88
RATE = 16000
TARGET_LUFS = -20.0      # the clips' loudest 400 ms: about 7 dB over a pattern's median (-27)

# the 16 colours, by letter
PAL = [
    ("A", (100, 64, 24)),     # the backdrop: dark gold (its top, in shadow)
    ("B", (152, 104, 42)),    # ... gold
    ("D", (194, 142, 62)),   # ... lit
    ("E", (66, 40, 16)),     # ... the painted houses and tree
    ("F", (40, 26, 14)),     # the stage
    ("f", (76, 50, 26)),     # ... its boards' edges
    ("K", (14, 10, 8)),      # outlines, hair, shoes, the microphones
    ("W", (250, 250, 248)),  # 郭德纲's cap and towel, the cuffs
    ("w", (184, 188, 196)),  # ... their shade
    ("S", (240, 190, 146)),  # skin
    ("s", (196, 136, 100)),  # ... shade
    ("N", (46, 60, 108)),    # 郭德纲's 大褂
    ("n", (24, 32, 66)),     # ... shade
    ("C", (232, 224, 204)),  # 于谦's 大褂
    ("c", (178, 166, 140)),  # ... shade
    ("G", (120, 120, 128)),  # the microphone stands
]
IDX = {k: i for i, (k, _) in enumerate(PAL)}


class Canvas:
    def __init__(self):
        self.px = np.full((H, W), IDX["B"], np.uint8)

    def put(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y, x] = IDX[c]

    def rect(self, x0, y0, x1, y1, c):          # inclusive
        for y in range(max(0, y0), min(H - 1, y1) + 1):
            for x in range(max(0, x0), min(W - 1, x1) + 1):
                self.px[y, x] = IDX[c]

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.put(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)


class Figure:
    """a figure drawn alone (0 = nothing), outlined, then laid over the backdrop"""
    def __init__(self):
        self.px = np.zeros((H, W), np.int16)     # colour index + 1

    def poly(self, pts, c):
        from PIL import Image, ImageDraw
        im = Image.new("L", (W, H), 0)
        ImageDraw.Draw(im).polygon(pts, fill=1)
        m = np.array(im) > 0
        self.px[m] = IDX[c] + 1

    def ellipse(self, x0, y0, x1, y1, c):
        from PIL import Image, ImageDraw
        im = Image.new("L", (W, H), 0)
        ImageDraw.Draw(im).ellipse((x0, y0, x1, y1), fill=1)
        m = np.array(im) > 0
        self.px[m] = IDX[c] + 1

    def rect(self, x0, y0, x1, y1, c):
        self.px[y0:y1 + 1, x0:x1 + 1] = IDX[c] + 1

    def put(self, x, y, c):
        self.px[y, x] = IDX[c] + 1

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.put(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n), c)

    def onto(self, cv):
        inside = self.px > 0
        edge = np.zeros_like(inside)
        edge[1:, :] |= inside[:-1, :]
        edge[:-1, :] |= inside[1:, :]
        edge[:, 1:] |= inside[:, :-1]
        edge[:, :-1] |= inside[:, 1:]
        edge &= ~inside
        cv.px[edge] = IDX["K"]
        cv.px[inside] = (self.px[inside] - 1).astype(np.uint8)


def backdrop(cv):
    """德云社's painted backdrop, an old Beijing street in gold, and the stage boards"""
    cv.rect(0, 0, W - 1, 2, "A")
    for x in range(W):                                    # (dithered into the gold)
        if x % 2 == 0:
            cv.put(x, 3, "A")
        if x % 4 == 1:
            cv.put(x, 4, "A")
    for y in range(44, 61):                               # the lights on the backdrop, low down
        for x in range(14, 106):
            if (x + y) % 2 == 0 and (y > 50 or (x + y) % 4 == 0) and abs(x - 60) < 30 + (y - 44) * 2:
                cv.put(x, y, "D")
    # the gate house between them: two roofs with turned-up eaves
    cv.rect(54, 41, 66, 60, "E")
    cv.rect(57, 47, 59, 51, "D")
    cv.rect(61, 47, 63, 51, "D")
    cv.rect(58, 54, 62, 60, "A")
    for i, y in enumerate(range(35, 41)):
        cv.rect(50 - i // 2 + 2, y, 70 + i // 2 - 2, y, "E")
    cv.put(48, 34, "E")
    cv.put(72, 34, "E")
    cv.line(49, 35, 52, 35, "E")
    cv.line(68, 35, 71, 35, "E")
    cv.rect(56, 30, 64, 34, "E")
    for i, y in enumerate(range(26, 30)):
        cv.rect(53 - i // 2 + 1, y, 67 + i // 2 - 1, y, "E")
    cv.put(52, 25, "E")
    cv.put(68, 25, "E")
    cv.rect(57, 24, 63, 25, "E")
    # houses left and right
    for i, y in enumerate(range(45, 49)):
        cv.rect(0, y, 13 + i, y, "E")
    cv.rect(0, 49, 15, 60, "E")
    cv.rect(4, 52, 7, 55, "D")
    for i, y in enumerate(range(47, 51)):
        cv.rect(104 - i, y, W - 1, y, "E")
    cv.rect(102, 51, W - 1, 60, "E")
    cv.rect(110, 54, 113, 57, "D")
    # a bare tree, top right, and its branches into the top left
    cv.rect(110, 12, 112, 46, "E")
    for x0, y0, x1, y1 in ((111, 30, 100, 18), (100, 18, 96, 8), (100, 18, 92, 14), (111, 22, 119, 10),
                           (111, 16, 106, 5), (106, 5, 101, 2), (111, 38, 103, 31), (112, 26, 119, 22),
                           (0, 14, 9, 6), (9, 6, 14, 2), (5, 10, 1, 4), (0, 24, 6, 19)):
        cv.line(x0, y0, x1, y1, "E")
    # the stage: boards, closer together further back
    cv.rect(0, 61, W - 1, H - 1, "F")
    for y in (61, 63, 66, 70, 75, 81, 87):
        cv.rect(0, y, W - 1, y, "f")


def guo(cv):
    """郭德纲: white cap, white towel round the neck, dark 大褂, hands on his hips"""
    f = Figure()
    cx = 33
    f.poly([(cx - 13, 44), (cx + 13, 44), (cx + 15, 83), (cx - 15, 83)], "N")      # the 大褂
    f.rect(cx + 6, 52, cx + 7, 82, "n")                                             # its fold
    f.poly([(cx - 12, 45), (cx - 20, 54), (cx - 21, 57), (cx - 19, 59), (cx - 11, 64), (cx - 10, 61),
            (cx - 16, 56), (cx - 10, 50)], "N")                                     # arms akimbo
    f.poly([(cx + 12, 45), (cx + 20, 54), (cx + 21, 57), (cx + 19, 59), (cx + 11, 64), (cx + 10, 61),
            (cx + 16, 56), (cx + 10, 50)], "N")
    f.line(cx + 17, 55, cx + 13, 61, "n")
    f.rect(cx - 13, 59, cx - 11, 61, "W")                                           # cuffs, hands at the hips
    f.rect(cx + 11, 59, cx + 13, 61, "W")
    f.rect(cx - 11, 61, cx - 9, 64, "S")
    f.rect(cx + 9, 61, cx + 11, 64, "S")
    f.rect(cx - 9, 83, cx - 2, 85, "K")                                             # shoes
    f.rect(cx + 2, 83, cx + 9, 85, "K")
    f.ellipse(cx - 11, 23, cx + 11, 43, "S")                                        # the head
    f.ellipse(cx - 13, 30, cx - 9, 36, "S")                                         # ears
    f.ellipse(cx + 9, 30, cx + 13, 36, "S")
    f.put(cx - 11, 33, "s")
    f.put(cx + 11, 33, "s")
    f.poly([(cx - 9, 27), (cx - 9, 21), (cx - 6, 18), (cx + 6, 18), (cx + 9, 21), (cx + 9, 27)], "W")   # the cap
    f.rect(cx + 7, 20, cx + 8, 27, "w")
    f.rect(cx - 9, 27, cx + 9, 27, "w")
    f.rect(cx - 7, 30, cx - 4, 30, "K")                                             # brows, eyes (narrow)
    f.rect(cx + 4, 30, cx + 7, 30, "K")
    f.rect(cx - 6, 32, cx - 5, 32, "K")
    f.rect(cx + 5, 32, cx + 6, 32, "K")
    f.put(cx, 34, "s")                                                              # nose
    f.rect(cx - 9, 35, cx - 7, 36, "s")                                             # round cheeks
    f.rect(cx + 7, 35, cx + 9, 36, "s")
    f.rect(cx - 2, 38, cx + 2, 38, "s")                                             # the mouth, shut
    f.rect(cx - 8, 42, cx + 8, 44, "W")                                             # the towel, and its ends
    f.rect(cx - 7, 44, cx - 4, 58, "W")
    f.rect(cx + 4, 44, cx + 7, 58, "W")
    f.rect(cx - 4, 44, cx - 4, 58, "w")
    f.rect(cx + 7, 44, cx + 7, 58, "w")
    for x in (cx - 7, cx - 5, cx + 4, cx + 6):
        f.put(x, 58, "w")
    f.onto(cv)
    return cx


def yu(cv):
    """于谦: white 大褂, and (not as in this show: as he looks now, from a photo the owner chose) his hair permed
    in soft waves piled high, his long face; his hand held out to 郭德纲"""
    f = Figure()
    cx = 87
    f.poly([(cx - 12, 44), (cx + 12, 44), (cx + 14, 83), (cx - 14, 83)], "C")      # the 大褂
    f.line(cx - 3, 44, cx + 9, 52, "c")                                             # its 大襟
    f.rect(cx - 8, 56, cx - 7, 82, "c")
    f.rect(cx + 6, 60, cx + 7, 82, "c")
    f.poly([(cx + 12, 45), (cx + 15, 46), (cx + 17, 62), (cx + 13, 63), (cx + 12, 52)], "C")   # one arm down
    f.rect(cx + 13, 62, cx + 16, 63, "W")
    f.rect(cx + 13, 64, cx + 16, 66, "S")
    f.poly([(cx - 11, 45), (cx - 13, 47), (cx - 19, 54), (cx - 23, 50), (cx - 25, 47), (cx - 22, 45),
            (cx - 19, 49), (cx - 14, 45)], "C")                                     # the other, held out
    f.rect(cx - 26, 45, cx - 24, 47, "W")
    f.poly([(cx - 30, 44), (cx - 26, 43), (cx - 26, 47), (cx - 29, 48)], "S")       # the open hand
    f.put(cx - 31, 44, "S")
    f.rect(cx - 9, 83, cx - 2, 85, "K")
    f.rect(cx + 2, 83, cx + 9, 85, "K")
    f.rect(cx - 5, 42, cx + 5, 45, "C")                                             # the 大褂's high collar
    f.rect(cx - 5, 42, cx - 5, 45, "c")
    f.rect(cx + 5, 42, cx + 5, 45, "c")
    # his hair (a perm in soft waves): black, thinned (not piled so high), its hairline an arch over the
    # forehead, short at the sides down to his ears
    f.ellipse(cx - 12, 12, cx + 12, 30, "K")                                        # (one mass, thinned)
    for x, y in ((-6, 16), (2, 15), (5, 19)):                                       # (its waves: a dark sheen)
        f.put(cx + x - 1, y + 1, "n")
        f.put(cx + x, y, "n")
        f.put(cx + x + 1, y, "n")
        f.put(cx + x + 2, y + 1, "n")
    f.poly([(cx - 9, 26), (cx - 7, 23), (cx - 4, 21), (cx - 2, 21), (cx + 2, 21), (cx + 4, 21), (cx + 7, 23),
            (cx + 9, 26), (cx + 10, 28), (cx + 10, 33), (cx + 9, 37), (cx + 7, 40), (cx + 4, 43), (cx + 2, 44),
            (cx - 2, 44), (cx - 4, 43), (cx - 7, 40), (cx - 9, 37), (cx - 10, 33), (cx - 10, 28)],
           "S")                                                                     # a long face, broad cheekbones,
                                                                                    # the forehead's dome under an
                                                                                    # arched hairline
    f.ellipse(cx - 13, 28, cx - 9, 35, "S")                                         # ears
    f.ellipse(cx + 9, 28, cx + 13, 35, "S")
    f.rect(cx - 11, 31, cx - 11, 33, "s")
    f.rect(cx + 11, 31, cx + 11, 33, "s")
    for d in (-1, 1):
        f.line(cx + d * 7, 28, cx + d * 3, 27, "K")                                 # thick brows, gently arched
        f.line(cx + d * 7, 27, cx + d * 4, 26, "K")
        f.line(cx + d * 6, 29, cx + d * 3, 29, "s")                                 # hooded lids over
        f.rect(min(cx + d * 4, cx + d * 5), 30, max(cx + d * 4, cx + d * 5), 31, "K")   # small eyes
        f.put(cx + d * 8, 34, "s")                                                  # the cheekbones
        f.line(cx + d * 4, 36, cx + d * 4, 38, "s")                                 # the lines from the nose
    f.rect(cx + 1, 32, cx + 1, 34, "s")                                             # a long nose, broad below
    f.rect(cx - 1, 35, cx + 1, 35, "s")
    f.put(cx - 3, 38, "K")                                                          # a closed smile
    f.rect(cx - 2, 39, cx + 2, 39, "K")
    f.put(cx + 3, 38, "K")
    f.put(cx, 42, "s")                                                              # the chin
    f.onto(cv)
    return cx


def mics(cv, gx, yx):
    """a stand in front of each, its microphone under the mouth (the mouth stays in sight; 于谦's to the side of his
    chin, as it stood, and so it isn't read as a beard)"""
    for x, y in ((gx, 41), (yx - 7, 42)):
        cv.rect(x, y + 2, x, 86, "G")
        cv.rect(x - 3, 86, x + 3, 86, "G")
        cv.rect(x - 1, y, x + 1, y + 1, "K")


def picture():
    cv = Canvas()
    backdrop(cv)
    gx = guo(cv)
    yx = yu(cv)
    mics(cv, gx, yx)
    return cv.px, gx


MOUTH = (33 - 2, 37)          # 郭德纲's mouth, open while he sings: 5 x 3 from here
HEAD_TOP = 18                 # the top of his cap (the bubble's tail points down to it)


def rle(px):
    """runs of one colour, 1..16 long, a byte each: (length - 1) << 4 | colour; rows run on into the next"""
    flat = px.flatten()
    out, i = [], 0
    while i < len(flat):
        j = i
        while j < len(flat) and j - i < 16 and flat[j] == flat[i]:
            j += 1
        out.append((j - i - 1) << 4 | int(flat[i]))
        i = j
    return bytes(out)


def glyphs(chars):
    """each character, 12 x 12 from the pixel font, a bit a pixel, rows top to bottom (18 bytes)"""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(str(FONT_DIR / PIXEL), 12)
    top = min(font.getbbox(c)[1] for c in chars)
    assert max(font.getbbox(c)[3] for c in chars) - top <= 12, "a character taller than 12 rows"
    out = []
    for c in chars:
        im = Image.new("1", (12, 12), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((0, -top), c, font=font, fill=1)
        bits = [1 if im.getpixel((x, y)) else 0 for y in range(12) for x in range(12)]
        out.append(bytes(sum(bits[i + k] << (7 - k) for k in range(8)) for i in range(0, 144, 8)))
    return out


def preview(px, path, say=None, glyph=None, gx=33):
    """the screen as the FM-1 shows it (each pixel 2 x 2) with a black header and footer, at 2x again"""
    from PIL import Image
    rgb = np.array([c for _, c in PAL], np.uint8)[px]
    img = Image.new("RGB", (240, 240), (0, 0, 0))
    pic = Image.fromarray(rgb).resize((240, 176), Image.NEAREST)
    img.paste(pic, (0, 32))
    if say:                                       # the bubble, as app.c draws it
        n = len(say)
        bw, bh = 12 * n + 6, 18
        x0, y0 = gx - bw // 2, 0
        b = np.zeros((H, W), np.int8) - 1
        b[y0:y0 + bh, x0:x0 + bw] = IDX["W"]
        b[y0, x0:x0 + bw] = b[y0 + bh - 1, x0:x0 + bw] = IDX["K"]
        b[y0:y0 + bh, x0] = b[y0:y0 + bh, x0 + bw - 1] = IDX["K"]
        for xx, yy in ((x0, y0), (x0 + bw - 1, y0), (x0, y0 + bh - 1), (x0 + bw - 1, y0 + bh - 1)):
            b[yy, xx] = -1
        for i in range(3):                        # the tail, down to the cap
            b[y0 + bh - 1 + i, gx - 2 + i:gx + 2 - i] = IDX["W"]
            b[y0 + bh - 1 + i, gx - 3 + i] = b[y0 + bh - 1 + i, gx + 2 - i] = IDX["K"]
        for k, g in enumerate(glyph):
            for yy in range(12):
                for xx in range(12):
                    if g[(yy * 12 + xx) // 8] >> (7 - (yy * 12 + xx) % 8) & 1:
                        b[y0 + 3 + yy, x0 + 3 + 12 * k + xx] = IDX["K"]
        pal = np.array([c for _, c in PAL], np.uint8)
        for yy in range(H):
            for xx in range(W):
                if b[yy, xx] >= 0:
                    img.paste(tuple(int(v) for v in pal[b[yy, xx]]), (2 * xx, 32 + 2 * yy, 2 * xx + 2, 34 + 2 * yy))
    img.resize((480, 480), Image.NEAREST).save(path)


def kweight_lufs_max(x, fs):
    """the loudest 400 ms (ITU-R BS.1770 K-weighting), LUFS"""
    from scipy.signal import lfilter
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.974450955533
    A, w0 = 10 ** (G / 40), 2 * np.pi * fc / fs
    al = np.sin(w0) / (2 * Q)
    b = [A * ((A + 1) + (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * np.cos(w0)),
         A * ((A + 1) + (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al)]
    a = [(A + 1) - (A - 1) * np.cos(w0) + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * np.cos(w0)),
         (A + 1) - (A - 1) * np.cos(w0) - 2 * np.sqrt(A) * al]
    y = lfilter(b, a, x)
    Q, fc = 0.5003270373253953, 38.13547087613982
    w0 = 2 * np.pi * fc / fs
    al = np.sin(w0) / (2 * Q)
    y = lfilter([(1 + np.cos(w0)) / 2, -(1 + np.cos(w0)), (1 + np.cos(w0)) / 2], [1 + al, -2 * np.cos(w0), 1 - al], y)
    n = int(0.4 * fs)
    if len(y) <= n:
        return -0.691 + 10 * np.log10(np.mean(y ** 2) + 1e-12)
    ms = np.convolve(y ** 2, np.ones(n) / n, "valid")
    return -0.691 + 10 * np.log10(ms.max() + 1e-12)


IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def ima_encode(s):
    """16-bit samples -> IMA ADPCM nibbles (the first in a byte's low four bits), starting from 0, step 0"""
    pred, idx, nib = 0, 0, []
    for v in s:
        step = IMA_STEP[idx]
        d = int(v) - pred
        code = 8 if d < 0 else 0
        d = abs(d)
        diff = step >> 3
        if d >= step:
            code |= 4
            d -= step
            diff += step
        if d >= step >> 1:
            code |= 2
            d -= step >> 1
            diff += step >> 1
        if d >= step >> 2:
            code |= 1
            diff += step >> 2
        pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
        idx = max(0, min(88, idx + IMA_IDX[code]))
        nib.append(code)
    if len(nib) % 2:
        nib.append(0)
    return bytes(nib[i] | nib[i + 1] << 4 for i in range(0, len(nib), 2))


def ima_decode(data, n):
    pred, idx, out = 0, 0, []
    for i in range(n):
        code = data[i // 2] >> (4 * (i % 2)) & 15
        step = IMA_STEP[idx]
        diff = step >> 3
        if code & 4:
            diff += step
        if code & 2:
            diff += step >> 1
        if code & 1:
            diff += step >> 2
        pred = max(-32768, min(32767, pred - diff if code & 8 else pred + diff))
        idx = max(0, min(88, idx + IMA_IDX[code]))
        out.append(pred)
    return np.array(out, np.float64)


def clip(path):
    import soundfile as sf
    from scipy.signal import resample_poly
    x, fs = sf.read(path, dtype="float64")
    if x.ndim > 1:
        x = x.mean(axis=1)
    lufs = kweight_lufs_max(x, fs)
    g = 10 ** ((TARGET_LUFS - lufs) / 20)
    y = resample_poly(x, RATE, fs)
    y = y / max(1e-9, np.abs(y).max()) * 0.95                 # the ADPCM at full scale; the level is GUO_GAIN
    s = np.round(y * 32767).astype(np.int32)
    data = ima_encode(s)
    back = ima_decode(data, len(s))
    snr = 10 * np.log10(np.sum(s.astype(float) ** 2) / max(1.0, np.sum((back - s) ** 2)))
    return data, len(s), g * np.abs(x).max() / 0.95, lufs, snr


def write_pic(px, gx):
    code = rle(px)
    gl = glyphs("包肉")
    rows = []
    for i in range(0, len(code), 24):
        rows.append("    " + ", ".join("0x%02x" % b for b in code[i:i + 24]) + ",")
    pal = ", ".join("RGB(%d, %d, %d)" % c for _, c in PAL)
    PIC_OUT.write_text(
        "/* generated by tools/guo.py: the 郭德纲模式's picture, 郭德纲 and 于谦 in their 《黄鹤楼》, %d x %d pixels of 16\n"
        " * colours, run-length coded (a byte a run: (length - 1) << 4 | colour, rows running on), each pixel 2 x 2 on\n"
        " * the screen under the header; the bubble's characters 包 肉 from Fusion Pixel 12 px (OFL-1.1). LUOGU's own\n"
        " * drawing (MPL-2.0) */\n"
        "#pragma once\n#include <stdint.h>\n\n"
        "#define GUO_W %d\n#define GUO_H %d\n#define GUO_X %d          /* 郭德纲's middle */\n"
        "#define GUO_TOP %d        /* the top of his cap */\n"
        "#define GUO_MOUTH_X %d\n#define GUO_MOUTH_Y %d   /* his mouth: open (5 x 3) while he sings */\n"
        "enum { GUO_K = %d, GUO_W_ = %d, GUO_SKIN_SHADE = %d };\n"
        "static const uint16_t GUO_PAL[16] = {%s};\n"
        "static const uint8_t GUO_RLE[%d] = {\n%s\n};\n"
        "static const uint8_t GUO_GLYPH[2][18] = {%s};   /* 包, 肉 */\n"
        % (W, H, W, H, gx, HEAD_TOP, MOUTH[0], MOUTH[1], IDX["K"], IDX["W"], IDX["s"], pal, len(code),
           "\n".join(rows), ", ".join("{" + ", ".join("0x%02x" % b for b in g) + "}" for g in gl)),
        encoding="utf-8")
    return len(code)


def write_snd(clips):
    rows, offs, at = [], [], 0
    for data, n, gain, lufs, snr in clips:
        offs.append("{%du, %du, %.4ff}" % (at, n, gain))
        at += len(data)
        allb = data
        for i in range(0, len(allb), 24):
            rows.append("    " + ", ".join("0x%02x" % b for b in allb[i:i + 24]) + ",")
    SND_OUT.write_text(
        "/* generated by tools/guo.py: 郭德纲's sung 包 and 肉包 from 郭德纲 & 于谦's 《黄鹤楼》 (德云社's recording; 32:01 and\n"
        " * 42:51), %d Hz mono IMA ADPCM (two samples a byte, the first in the low bits; each clip starts at 0, step\n"
        " * 0); each: its start in GUO_ADPCM, its samples, its gain (its loudest 400 ms at %.0f LUFS). The recording\n"
        " * belongs to its owners: not under LUOGU's licence (CREDITS.md) */\n"
        "#pragma once\n#include <stdint.h>\n\n"
        "#define GUO_NSND %d\n#define GUO_RATE %d\n"
        "typedef struct { uint32_t off, n; float gain; } guo_snd_t;\n"
        "static const guo_snd_t GUO_SND[%d] = {%s};   /* 包, 肉包 */\n"
        "static const uint8_t GUO_ADPCM[%d] = {\n%s\n};\n"
        % (RATE, TARGET_LUFS, len(clips), RATE, len(clips), ", ".join(offs), at, "\n".join(rows)),
        encoding="utf-8")
    return at


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bao")
    ap.add_argument("roubao")
    ap.add_argument("--preview", help="the screen as a PNG, and with each bubble (-包, -肉包)")
    a = ap.parse_args()
    px, gx = picture()
    n_pic = write_pic(px, gx)
    clips = [clip(a.bao), clip(a.roubao)]
    n_snd = write_snd(clips)
    for name, (data, n, gain, lufs, snr) in zip(("包", "肉包"), clips):
        print("%s: %d samples (%.2f s), %d bytes, gain %.3f (was %.1f LUFS), ADPCM SNR %.1f dB"
              % (name, n, n / RATE, len(data), gain, lufs, snr))
    print("picture: %d bytes (%d raw); sound: %d bytes" % (n_pic, W * H // 2, n_snd))
    if a.preview:
        g = glyphs("包肉")
        preview(px, a.preview, gx=gx)
        stem = a.preview[:-4] if a.preview.endswith(".png") else a.preview
        preview(px, stem + "-包.png", "包", [g[0]], gx)
        preview(px, stem + "-肉包.png", "肉包", [g[1], g[0]], gx)


if __name__ == "__main__":
    main()
