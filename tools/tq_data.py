#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's 唱段 (arias with their 托腔): the 京胡 parts of a few arias, read from the Jingju Music Scores Collection
(JMSC: Rafael Caro Repetto and Xavier Serra, 2017; CC BY-NC-ND 4.0), written into app/dsp/tq_data.h with their lyrics.

  python3 tools/tq_data.py [--jmsc DIR] [--lines CSV]     (Pillow; the pixel font: FONT_DIR, ~/fm1/data/fonts)

The scores are not LUOGU's: the collection's licence (CC BY-NC-ND 4.0) lets them be shared unchanged, not
commercially, with credit, and the printed books they were set from are their editors'. The header this writes holds
them converted to a compact form to play from, their notes unchanged, so it is under that licence too (LICENSES/,
CREDITS.md), not LUOGU's MPL; without the collection it writes an empty one, and LUOGU has no 唱段. What is taken
from each score, as it is printed:
  - its 京胡 staff (the part without lyrics), note by note: the pitch in semitones from 1 (JMSC writes every score in
    1=E: E4 is 1, as LUOGU's 过门 count from 1), the length, a grace note before it, a slur into it, a trill (or a
    mordent) on it; tied notes as one; the fermata and the dynamics left out (the tempo is LUOGU's);
  - its sections where the time signature changes (慢板 4/4, 原板 2/4, 流水 1/4 ...), each named here by LUOGU's
    板式 (its tempo, its 锣鼓);
  - the sung syllables (where each starts, its first character; an interjection in （）, dim) and the lines they
    make (lines_data.csv: 上句 or 下句), for the screen;
  - the characters of the lyrics, drawn from Fusion Pixel 12 px (OFL-1.1, as the logo's), one bit a pixel.
Notes are packed: byte 0 = pitch + 32 (63 a rest) | 64 slurred into | 128 a grace note (its pitch + 32 next);
byte 1 = length / 6 ticks (TPB 48) | 64 a trill | 128 long (the length's next 6 bits next, x 64).
"""
import argparse
import csv
import os
import xml.etree.ElementTree as ET
from fractions import Fraction as Fr
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "dsp" / "tq_data.h"
DATA = Path(os.environ.get("JMSC", Path.home() / "fm1/data/opera/research/guomen_sources"))
FONT_DIR = Path(os.environ.get("FONT_DIR", Path.home() / "fm1" / "data" / "fonts"))
PIXEL = "fusion12/fusion-pixel-12px-proportional-zh_hans.ttf"
TPB = 48                                        # ticks a beat (lg.h LG_TPB), a beat a quarter note
QIANG = {"xipi": 0, "erhuang": 1}               # lg.h LGQ_XIPI, LGQ_ERHUANG
ROLE = {"dan": "旦", "laosheng": "老生", "laodan": "老旦", "jing": "净", "xiaosheng": "小生"}

# the 唱段: the score, the play, and each section's 板式 (LUOGU's, by its name in lg_data.h: its 锣鼓) and tempo
# (quarter notes a minute, as JMSC writes them: not the 板式's own, which counts 板 and 眼 its way). Measured on the
# JaCRC recordings of the same scores where there are any (苏三离了洪洞县: the 京胡 plays its 64 beats in 45.5 s, 84;
# 一轮明月照窗前: its first 63-73 beats in 44.6 s, 85-98); the others estimates from how many notes a beat their
# 京胡 plays (about 3.6 a second in both recordings). K1 moves them all together.
ARIAS = [
    ("daxp-SuSanLi-NvQiJie", "女起解", [("西皮流水", 84)]),
    ("lsxp-WoBenShi-KongChengJi-2", "空城计", [("西皮快三眼", 84), ("西皮原板", 96)]),
    ("lseh-YiLunMing-WenZhaoGuan-2", "文昭关", [("二黄慢板", 85)]),
    ("daxp-DangRiLi-SuoLinNang", "锁麟囊", [("西皮原板(旦)", 88), ("西皮流水", 100)]),
    ("lsxp-ShenGongWu-LongFengChengXiang", "龙凤呈祥", [("西皮原板", 80)]),
]
PUNCT = "，。！？、；：,.!?;:"


def midi(n):
    p = n.find("pitch")
    st = "CDEFGAB".index(p.findtext("step"))
    return 12 * (int(p.findtext("octave")) + 1) + [0, 2, 4, 5, 7, 9, 11][st] + int(p.findtext("alter") or 0)


def notations(n, tag):
    return [x for x in n.iter(tag)]


def part_events(part):
    """the part's notes in order: (start, length, midi or None, grace midi or None, slurred, trill, tie start, tie
    stop, lyric); the measures' starts and time signatures"""
    div, pos, out, graces, slur, meas = None, Fr(0), [], [], 0, []
    for m in part.findall("measure"):
        a = m.find("attributes")
        ts = None
        if a is not None:
            if a.findtext("divisions"):
                div = int(a.findtext("divisions"))
            t = a.find("time")
            if t is not None:
                ts = (int(t.findtext("beats")), int(t.findtext("beat-type")))
        meas.append((pos, ts))
        for n in m.findall("note"):
            if n.find("chord") is not None:
                continue
            if n.find("grace") is not None:
                if n.find("pitch") is not None:
                    graces.append(midi(n))
                continue
            ln = Fr(int(n.findtext("duration")), div)
            rest = n.find("rest") is not None
            starts = [s for s in notations(n, "slur") if s.get("type") == "start"]
            stops = [s for s in notations(n, "slur") if s.get("type") == "stop"]
            tie = {t.get("type") for t in n.findall("tie")}
            trill = bool(notations(n, "trill-mark") or notations(n, "inverted-mordent") or notations(n, "mordent"))
            ly = n.find("lyric")
            out.append(dict(at=pos, len=ln, m=None if rest else midi(n), g=graces[0] if graces and not rest else None,
                            slur=slur > 0 and not rest, trill=trill, tie_start="start" in tie,
                            tie_stop="stop" in tie, lyric=ly.findtext("text") if ly is not None else None))
            graces = []
            slur = max(0, slur + len(starts) - len(stops))
            pos += ln
    meas.append((pos, None))
    return out, meas


def pack(notes):
    """the notes as bytes (the docstring's packing), ties merged"""
    merged = []
    for n in notes:
        if merged and n["tie_stop"] and n["m"] is not None and merged[-1]["m"] == n["m"]:
            merged[-1]["len"] += n["len"]
            continue
        merged.append(dict(n))
    b = bytearray()
    for n in merged:
        ticks = n["len"] * TPB
        assert ticks.denominator == 1 and ticks % 6 == 0, ticks
        d = int(ticks) // 6
        p = 63 if n["m"] is None else n["m"] - 64 + 32
        assert 0 <= p <= 63, n
        b0 = p | (64 if n["slur"] else 0) | (128 if n["g"] is not None else 0)
        b1 = (d & 63) | (64 if n["trill"] else 0) | (128 if d > 63 else 0)
        b.append(b0)
        b.append(b1)
        if n["g"] is not None:
            g = n["g"] - 64 + 32
            assert 0 <= g < 63
            b.append(g)
        if d > 63:
            assert d >> 6 < 64
            b.append(d >> 6)
    return bytes(b), len(merged)


def read_lines(path):
    lines, cur = {}, None
    for r in csv.reader(open(path, encoding="utf-8")):
        if r[0]:
            cur = r[0].replace(".xml", "")
        if len(r) < 8 or not r[6] or not r[7]:
            continue
        lines.setdefault(cur, []).append((r[4], Fr(r[6]), Fr(r[7])))
    return lines


def glyphs(chars):
    """each character, 12 x 12 from the pixel font, a bit a pixel, rows top to bottom (18 bytes)"""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(str(FONT_DIR / PIXEL), 12)
    top = min(font.getbbox(c)[1] for c in chars)           # the ink's rows, all of them, inside the 12
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


def bs_names():
    """LUOGU's 板式, as compiled (lg_data.h)"""
    import re
    h = (ROOT / "app" / "dsp" / "lg_data.h").read_text(encoding="utf-8")
    return re.findall(r'\{"([^"]+)", "', h[h.index("static const lg_bs_t LG_BS"):])


def write_stub(why):
    OUT.write_text("/* generated by tools/tq_data.py: no 唱段 (" + why + ") */\n#pragma once\n#define TQ_N 0\n")
    print(f"tq_data: none ({why}) -> {OUT.relative_to(ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jmsc", default=str(DATA / "jmsc_github_Rafael-Caro" / "scores"))
    ap.add_argument("--lines", default=str(DATA / "jmsc_zenodo_1285613" / "jingju_music_scores_1.0" / "lines_data.csv"))
    args = ap.parse_args()
    jm, lines_csv = Path(args.jmsc), Path(args.lines)
    if not jm.is_dir() or not lines_csv.exists():
        write_stub("the Jingju Music Scores Collection is not here")
        return
    meta = {r[0]: r for r in csv.reader(open(jm / "scores_data.csv", encoding="utf-8")) if len(r) > 6}
    all_lines = read_lines(lines_csv)
    known = bs_names()
    for fn, play, sec in ARIAS:
        for name, _ in sec:
            assert name in known, f"{fn}: no 板式 named {name} in lg_data.h ({', '.join(known)})"
    notes_b, secs, syls, lines, arias, chars, maxn, maxbeats = bytearray(), [], [], [], [], [], 0, 0
    for fn, play, sec_spec in ARIAS:
        root = ET.parse(jm / (fn + ".xml")).getroot()
        parts = root.findall("part")
        has_lyr = [any(n.find("lyric") is not None for n in p.iter("note")) for p in parts]
        jh = [p for p, h in zip(parts, has_lyr) if not h]
        voice = [p for p, h in zip(parts, has_lyr) if h]
        assert len(jh) == 1 and len(voice) == 1, fn
        jn, meas = part_events(jh[0])
        vn, _ = part_events(voice[0])
        # sections: where the time signature changes
        cuts = [(i, ts) for i, (pos, ts) in enumerate(meas[:-1]) if ts]
        assert len(cuts) == len(sec_spec), (fn, cuts)
        s0 = len(secs)
        for k, (mi, ts) in enumerate(cuts):
            me = cuts[k + 1][0] if k + 1 < len(cuts) else len(meas) - 1
            start, end = meas[mi][0], meas[me][0]
            beats = Fr(ts[0] * 4, ts[1])
            assert beats.denominator == 1
            bars = me - mi
            pick = meas[mi + 1][0] - meas[mi][0]    # a pickup: the first bar short (the 京胡 in on a 眼)
            pick = pick if pick < beats else Fr(0)
            for j in range(mi + (1 if pick else 0), me - 1):   # the bars between full: the 板 fall on them
                assert meas[j + 1][0] - meas[j][0] == beats, (fn, j, meas[j + 1][0] - meas[j][0])
            assert pick.denominator == 1
            sec_notes = [dict(n, at=n["at"] - start) for n in jn if start <= n["at"] < end]
            b, nn = pack(sec_notes)
            secs.append(dict(bs=sec_spec[k][0], bpm=sec_spec[k][1], meter=int(beats), bars=bars, at=int(start * TPB),
                             off=len(notes_b), nn=nn, len=int((end - start) * TPB), pick=int(pick)))
            notes_b += b
            maxn = max(maxn, nn)
            maxbeats = max(maxbeats, bars * int(beats))
        # the sung syllables, in the lines lines_data.csv gives
        ly = [n for n in vn if n["lyric"]]
        l0 = len(lines)
        for typ, a, e in all_lines[fn]:
            inside = [n for n in ly if a <= n["at"] <= e]
            y0 = len(syls)
            for n in inside:
                t = n["lyric"].strip()
                dim = t.startswith("（")
                c = [ch for ch in t if ch not in PUNCT + "（）()"][0]
                if c not in chars:
                    chars.append(c)
                syls.append(dict(at=int(n["at"] * TPB), glyph=chars.index(c), dim=dim))
            lines.append(dict(typ=1 if typ.startswith("x") else 0, y0=y0, n=len(syls) - y0))
        assert syls[-1]["at"] < 65536
        m = meta[fn]
        roles = "、".join(ROLE.get(x.strip(), x.strip()) for x in m[2].split(";"))
        first = m[1].split("——")[0].strip("“”\"")
        for c in play:                              # (the title: drawn in the same pixel font, twice the size)
            if c not in chars:
                chars.append(c)
        arias.append(dict(fn=fn, play=play, first=first, role=roles, qiang=QIANG[m[3].strip()], s0=s0,
                          ns=len(secs) - s0, l0=l0, nl=len(lines) - l0, source=m[6]))
    gl = glyphs(chars)
    L = ["/* generated by tools/tq_data.py: LUOGU's 唱段, the 京胡 parts (托腔) of these arias with their lyrics, from the",
         " * Jingju Music Scores Collection (Rafael Caro Repetto, Xavier Serra; CompMusic, Universitat Pompeu Fabra, 2017;",
         " * https://doi.org/10.5281/zenodo.1285613), as set from the printed scores named below. Under CC BY-NC-ND 4.0",
         " * as they are, not LUOGU's MPL (non-commercial use only: LICENSES/CC-BY-NC-ND-4.0.txt): the scores converted to",
         " * a compact form to play from, their notes unchanged (ties joined); the tempos are LUOGU's. The lyrics'",
         " * characters drawn from Fusion Pixel 12 px (TakWolf, OFL-1.1). */"]
    for a in arias:
        L.append(f" /*   {a['first']}《{a['play']}》: {a['source']} */")
    L += ["#pragma once", "#include <stdint.h>", "",
          f"#define TQ_N {len(arias)}", f"#define TQ_MAXNOTES {maxn}   /* the most notes a section has */",
          f"#define TQ_MAXBEATS {maxbeats}   /* the most beats a section has (its 板 and 眼) */",
          f"#define TQ_NGLYPH {len(chars)}", ""]
    L.append(f"static const uint8_t TQ_NOTES[{len(notes_b)}] = {{")
    for i in range(0, len(notes_b), 24):
        L.append("    " + ", ".join(f"0x{v:02x}" for v in notes_b[i:i + 24]) + ",")
    L.append("};")
    L.append(f"static const lg_tq_sec_t TQ_SEC[{len(secs)}] = {{")
    for s in secs:
        L.append(f"    {{\"{s['bs']}\", {s['meter']}, {s['pick']}, {s['bpm']}, {s['at']}u, {s['len']}u, {s['off']}u, "
                 f"{s['nn']}}},")
    L.append("};")
    L.append(f"static const lg_tq_syl_t TQ_SYL[{len(syls)}] = {{")
    for i in range(0, len(syls), 8):
        L.append("    " + " ".join(f"{{{s['at']}u, {s['glyph']}, {1 if s['dim'] else 0}}}," for s in syls[i:i + 8]))
    L.append("};")
    L.append(f"static const lg_tq_line_t TQ_LINE[{len(lines)}] = {{")
    L.append("    " + " ".join(f"{{{x['y0']}, {x['n']}, {x['typ']}}}," for x in lines))
    L.append("};")
    L.append(f"static const lg_tq_t TQ_ARIA[TQ_N] = {{")
    for a in arias:
        L.append(f"    {{\"{a['play']}\", \"{a['first']}\", \"{a['role']}\", {a['qiang']}, {a['s0']}, {a['ns']}, "
                 f"{a['l0']}, {a['nl']}}},")
    L.append("};")
    L.append(f"static const uint16_t TQ_GLYPH_CODE[TQ_NGLYPH] = {{" + ", ".join(str(ord(c)) for c in chars) + "};")
    L.append(f"static const uint8_t TQ_GLYPH[TQ_NGLYPH][18] = {{")
    for i in range(0, len(gl), 4):
        L.append("    " + " ".join("{" + ", ".join(f"0x{v:02x}" for v in g) + "}," for g in gl[i:i + 4]))
    L.append("};")
    OUT.write_text("\n".join(L) + "\n")
    size = len(notes_b) + 13 * len(secs) + 4 * len(syls) + 4 * len(lines) + 20 * len(arias) + 20 * len(chars)
    print(f"tq_data: {len(arias)} 唱段, {len(secs)} sections, {sum(s['nn'] for s in secs)} notes ({len(notes_b)} B), "
          f"{len(syls)} syllables in {len(lines)} lines, {len(chars)} characters; about {size} B "
          f"-> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
