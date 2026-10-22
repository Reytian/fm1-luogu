#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's music, from text to C: data/luogu.txt (the 锣鼓经's syllables and patterns) and data/guomen.txt (the
过门 and the 板式) into app/dsp/lg_data.h. Checks every bar's length and every name used.

  python3 tools/lg_data.py           writes app/dsp/lg_data.h
  python3 tools/lg_data.py --list    and prints what is in it

The format (one item a line; # to the end of a line is a comment):

  @syl 仓 cang 大锣:100 铙钹:95 小锣:80    a syllable, its pinyin, what it strikes: instrument[.technique]:velocity
                                           (instruments: 鼓 板 大锣 铙钹 小锣 堂鼓; techniques: 轻 light, 闷 struck and
                                           held, 止 the hand only, 双 both sticks, 滚 a roll till the next stroke)
  @cat 上场 亮相 ...                        the patterns' kinds, in order
  @pattern 急急风 ji-ji-feng                a pattern, its pinyin (- between syllables)
    cat 上场                                its kind
    meter 1                                 beats a bar: 1 (一板), 2 (一板一眼), 4 (一板三眼); 0 散板; 2+3 bars
                                           of 2 and 3 beats as they come (水底鱼; not checked)
    bpm 180 140 220                         its tempo, slowest, fastest
    reps 0                                  中 played this many times when nobody cuts it (0: until cut)
    gu -                                    the drum only as written (by default the 鼓师 strikes with the gongs:
                                           drum_part)
    accel 1.0 2.0                           散板: the tempo's factor at its start and its end
    use ...                                 what it is for (shown on the screen)
    qi    八:2 大:2 |                       起: events, bars between |; an event is 字:length in quarter beats (a beat
    zhong 仓:4 |                            is 4; 4/3 a triplet's third of a beat), 乙 a rest, - held; ! after the
    shou  仓:2 才:2 | 仓:4 |                 syllable accents it, ~ softens it. Lines of a section may continue on
                                            following lines that start with |
  @guomen 西皮原板大过门                    a 过门
    qiang 西皮                              its 声腔 (西皮 二黄 反二黄 四平调 南梆子)
    meter 2
    sing 4                                  the singer comes in this many quarter beats into its last bar (its length:
                                            right after it)
    notes | 6,:2 1:2 ... |                  jianpu: 1..7 (# and b before), , and ' after for an octave down or up,
                                            0 a rest, - held; :length in quarter beats; {5}6 a grace note before; /6
                                            and \\6 slides into it from below or above, 6\\ falls away at its end; 6~
                                            a trill; (6 5 3) a slur (one bow)
    perc  | 扎:4 大:4 | ...                  the 武场 with it (as a pattern's events), as long as the notes
  @banshi 西皮原板                          a 板式 to sing to
    qiang 西皮
    meter 2
    bpm 84 60 110
    intro 小锣帽儿头                         the 锣鼓 that leads in (a pattern; - none)
    big 西皮原板大过门                       the 大过门 (- none)
    small 西皮原板小过门 西皮原板小过门       after a 上句 and after a 下句
    keep 原板板眼                            while the player sings: a pattern whose 中 is played round
    tail -                                  the closing 过门
    end 大锣住头                             the closing 锣鼓
"""
import argparse
import re
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "dsp" / "lg_data.h"
TPB = 48                                         # ticks a beat (lg.h LG_TPB)
INST = {"鼓": 0, "板": 1, "大锣": 2, "铙钹": 3, "小锣": 4, "堂鼓": 5}      # perc.h I_*
TECH = {"": 0, "轻": 1, "闷": 2, "止": 3, "双": 4, "滚": 5}              # perc.h PS_*
QIANG = ["西皮", "二黄", "反二黄", "四平调", "南梆子"]                      # lg.h LG_XIPI ...
LGF_ACC, LGF_SOFT, LGF_BAR, LGF_BEAT, LGF_HOLD, LGF_EYE, LGF_ROLL, LGF_FILL = 1, 2, 4, 8, 16, 32, 64, 128
LGN_SLIDE_UP, LGN_SLIDE_DOWN, LGN_GRACE, LGN_TRILL, LGN_SLUR, LGN_REST, LGN_SLIDE_DN = 1, 2, 4, 8, 16, 32, 64
NOLAO = -128
DEG = {"1": 0, "2": 2, "3": 4, "4": 5, "5": 7, "6": 9, "7": 11}


class DataError(Exception):
    pass


def die(where, msg):
    raise DataError(f"{where}: {msg}")


def read_items(path):
    """the file's items: (kind, name, rest of the line, {key: [(line no, text)]}, line no)"""
    items, cur, key = [], None, None
    for no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        if line.startswith("@"):
            parts = line[1:].split(None, 2)
            cur = dict(kind=parts[0], name=parts[1] if len(parts) > 1 else "", rest=parts[2] if len(parts) > 2 else "",
                       keys={}, no=no, file=path.name)
            items.append(cur)
            key = None
            continue
        if cur is None:
            die(f"{path.name}:{no}", "a line before any @item")
        s = line.strip()
        if s.startswith("|") and key is not None:          # a section's continuation
            cur["keys"][key][-1] = (cur["keys"][key][-1][0], cur["keys"][key][-1][1] + " " + s)
            continue
        k, _, v = s.partition(" ")
        key = k
        cur["keys"].setdefault(k, []).append((no, v.strip()))
    return items


def one(item, key, default=None):
    v = item["keys"].get(key)
    if not v:
        if default is None:
            die(f"{item['file']}:{item['no']} {item['name']}", f"no '{key}'")
        return default
    return v[0][1]


def dur_of(text, where):
    try:
        return Fraction(text)
    except (ValueError, ZeroDivisionError):
        die(where, f"bad length {text!r}")


def events(text, meter, syl_index, where, sections=False):
    """'[起] 八:2 大:2 | [中] 仓:4 | [收] 仓:2 才:2 | 仓:4 |' -> the events [(tick, syllable, flags, 字, length)] and
    the length in ticks; with sections, a list of three (events, length) for 起 中 收, the ticks from each one's
    start. Every bar is meter beats long (meter 0: not checked). ~ after a syllable: a roll (a tremolo on the
    cymbals) for the event's length"""
    out, t = [], Fraction(0)
    marks = {}
    bars = [b.strip() for b in text.split("|")]
    while bars and bars[-1] == "":
        bars.pop()
    for bi, bar in enumerate(bars):
        if bar == "" and bi == 0:
            continue
        bar_start = t
        new_bar = True
        for tok in bar.split():
            if tok in ("[起]", "[中]", "[收]"):
                marks["起中收".index(tok[1])] = t
                continue
            m = re.fullmatch(r"([^:!~*]+)(!?)(~?)(\*?):([0-9/.]+)t?", tok)
            if not m:
                die(where, f"bad event {tok!r} (字:length)")
            zi, acc, roll, eye, d = m.group(1), m.group(2), m.group(3), m.group(4), dur_of(m.group(5), where)
            if zi not in syl_index:
                die(where, f"unknown syllable {zi!r}")
            fl = (LGF_ACC if acc else 0) | (LGF_EYE if eye else 0) | (LGF_ROLL if roll else 0)
            if new_bar:
                fl |= LGF_BAR
            tick, ln = t * TPB / 4, d * TPB / 4
            if tick.denominator != 1:
                die(where, f"{tok}: not on a tick ({tick})")
            if tick % TPB == 0:
                fl |= LGF_BEAT
            if zi == "-":
                fl |= LGF_HOLD
            out.append([int(tick), syl_index[zi], fl, zi, min(255, int(ln))])
            t += d
            new_bar = False
        if meter > 0 and t - bar_start != 4 * meter:
            die(where, f"bar {bi + 1} '{bar}' is {t - bar_start} quarter beats, not {4 * meter}")
    length = t * TPB / 4
    if length.denominator != 1:
        die(where, f"length {length} ticks")
    if not sections:
        return out, int(length)
    if not marks:
        marks = {1: Fraction(0)}                 # (no marks: all of it the 中)
    bounds = sorted((int(v * TPB / 4), k) for k, v in marks.items())
    secs = [([], 0), ([], 0), ([], 0)]
    for i, (b0, k) in enumerate(bounds):
        b1 = bounds[i + 1][0] if i + 1 < len(bounds) else int(length)
        evs = [[e[0] - b0] + e[1:] for e in out if b0 <= e[0] < b1]
        secs[k] = (evs, b1 - b0)
    first = min(bounds)[0]
    if first > 0:                                # events before the first mark: a 起 of their own
        if secs[0][1]:
            die(where, "events before [起]")
        secs[0] = ([e for e in out if e[0] < first], first)
    return secs


def pitch_of(text, where):
    """'#4', "6,", "1'" -> semitones from 1"""
    m = re.fullmatch(r"([#b]?)([1-7])([,']*)", text)
    if not m:
        die(where, f"bad pitch {text!r}")
    return DEG[m.group(2)] + (1 if m.group(1) == "#" else -1 if m.group(1) == "b" else 0) + \
        12 * (m.group(3).count("'") - m.group(3).count(","))


def notes(text, meter, where):
    """jianpu -> [(tick, len, pitch, grace, flags)], length, where the singer comes in ([唱]), the 落音 it follows
    ([落X]), free ([散]: no bar lengths checked)"""
    out, t = [], Fraction(0)
    sing, lao, free = None, None, False
    if "[散]" in text:
        free = True
        text = text.replace("[散]", " ")
    for m in re.finditer(r"\[落([^\]]+)\]", text):
        lao = pitch_of(m.group(1), where)
    text = re.sub(r"\[落[^\]]+\]", " ", text)
    bars = [b.strip() for b in text.split("|")]
    bars = [b for b in bars if b != ""]
    slur = False
    for bi, bar in enumerate(bars):
        bar_start = t
        toks = re.findall(r"\[唱\]|\(|\)|[^\s()]+", bar)
        for tok in toks:
            if tok == "[唱]":
                sing = t
                continue
            if tok == "(":
                slur = "open"
                continue
            if tok == ")":
                slur = False
                continue
            m = re.fullmatch(r"(?:\{([#b]?[1-7][,']*)\})?([/\\]?)([#b]?)([0-7-])([,']*)(\\?)(~?):([0-9/.]+)t?", tok)
            if not m:
                die(where, f"bad note {tok!r}")
            grace, slide_in, acc, deg, octs, fall, trill, d = m.groups()
            d = dur_of(d, where)
            tick, ln = t * TPB / 4, d * TPB / 4
            if tick.denominator != 1 or ln.denominator != 1:
                die(where, f"{tok}: not on a tick")
            if deg == "-":                       # held: the note before, longer
                if not out:
                    die(where, "- with no note before it")
                out[-1][1] += int(ln)
                t += d
                continue
            fl = 0
            if deg == "0":
                fl |= LGN_REST
                p = 0
            else:
                p = DEG[deg] + (1 if acc == "#" else -1 if acc == "b" else 0)
                p += 12 * (octs.count("'") - octs.count(","))
            g = 0
            if grace:
                g = pitch_of(grace, where)
                fl |= LGN_GRACE
            if slide_in == "/":
                fl |= LGN_SLIDE_UP
            if slide_in == "\\":
                fl |= LGN_SLIDE_DN
            if fall:
                fl |= LGN_SLIDE_DOWN
            if trill:
                fl |= LGN_TRILL
            if slur == "on":                     # inside a slur, after its first note: the same bow
                fl |= LGN_SLUR
            if slur == "open":
                slur = "on"
            out.append([int(tick), int(ln), p, g, fl])
            t += d
        if meter > 0 and not free and t - bar_start != 4 * meter:
            die(where, f"bar {bi + 1} '{bar}' is {t - bar_start} quarter beats, not {4 * meter}")
    total = t * TPB / 4
    if total.denominator != 1:
        die(where, f"length {total} ticks")
    sing_t = int(sing * TPB / 4) if sing is not None else int(total)
    return out, int(total), sing_t, lao, free


def LG_SYLS_REST(zi):
    return zi == "乙"


def drum_part(secs, meter, bpm, has_drum, is_gong, is_big, drum):
    """the 鼓师's strokes that a 锣鼓经's text leaves to him, as 穆文义's scores spell them out (CompMusic's five:
    导板头, 夺头, 小锣夺头, 慢长锤, 闪锤): a stroke with each syllable of the gongs (in the 中, the loop, only those on a
    beat), and in the 收 the eighths that a held syllable spans, while an eighth lasts 0.2 s or more. Added as events
    of 大 marked LGF_FILL (not on the screen, not owed in practice), accented on a bar's first beat, soft off the beat.
    (The scores never fill a bar's last beat: a quarter there. In the 收's last bar, the closing 答扑台仓, the drum
    strikes only with the big gong)"""
    eighth_ok = meter > 0 and 30.0 / bpm >= 0.2
    out = []
    for k, (evs, ln) in enumerate(secs):
        drums = {e[0] for e in evs if has_drum(e[3])}
        onsets = {e[0] for e in evs if e[3] != "-"}
        sounding = [e for e in evs if e[3] != "-" and not LG_SYLS_REST(e[3])]
        last = max((e[0] for e in sounding), default=-1)
        close = last // (meter * TPB) * (meter * TPB) if k == 2 and meter > 0 and last >= 0 else ln + 1
        add = []
        for e in evs:
            if not is_gong(e[3]) or (e[2] & LGF_ROLL) or e[0] in drums or (k == 1 and e[0] % TPB):
                continue
            if e[0] >= close and not is_big(e[3]):
                continue
            add.append(e[0])
            drums.add(e[0])
        if k == 2 and eighth_ok:
            for i, e in enumerate(evs):
                if not is_gong(e[3]) or e[0] >= close:
                    continue
                end = next((f[0] for f in evs[i + 1:] if f[3] != "-"), ln)   # (held on through any "-")
                t = e[0] + TPB // 2
                while t < end:
                    if t not in onsets and t not in drums and t % (meter * TPB) < (meter - 1) * TPB:
                        add.append(t)
                        drums.add(t)
                    t += TPB // 2
        for at in add:
            fl = LGF_FILL | (LGF_ACC if meter and at % (meter * TPB) == 0 else LGF_SOFT if at % TPB else 0)
            evs.append([at, drum, fl, "大", 0])
        evs.sort(key=lambda e: (e[0], 1 if e[2] & LGF_FILL else 0))   # (a stroke after the syllable it goes with)
        out.append((evs, ln))
    return out


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build():
    lu = read_items(ROOT / "data" / "luogu.txt")
    gm_path = ROOT / "data" / "guomen.txt"
    gu = read_items(gm_path) if gm_path.exists() else []
    syls, syl_index, cats, pats = [], {}, [], []
    for it in lu:
        where = f"{it['file']}:{it['no']}"
        if it["kind"] == "syl":
            parts = (it["rest"]).split()
            if not parts:
                die(where, "a syllable needs its pinyin")
            py, strokes = parts[0], []
            for s in parts[1:]:
                m = re.fullmatch(r"([^.:]+)(?:\.([^:]+))?:(\d+)", s)
                if not m or m.group(1) not in INST or (m.group(2) or "") not in TECH:
                    die(where, f"bad stroke {s!r}")
                strokes.append((INST[m.group(1)], TECH[m.group(2) or ""], int(m.group(3))))
            if len(strokes) > 4:
                die(where, "four strokes at most")
            syl_index[it["name"]] = len(syls)
            syls.append((it["name"], py, strokes))
        elif it["kind"] == "cat":
            cats = (it["name"] + " " + it["rest"]).split()
    strokes_of = {name: st for name, _, st in syls}

    def has_drum(zi):                           # the 鼓 or the 板 (the 鼓师's both) in it
        return any(inst in (INST["鼓"], INST["板"]) for inst, _, _ in strokes_of[zi])

    def is_gong(zi):                            # a gong or the cymbals in it
        return any(inst in (INST["大锣"], INST["铙钹"], INST["小锣"]) for inst, _, _ in strokes_of[zi])

    def is_big(zi):                             # the big gong in it
        return any(inst == INST["大锣"] for inst, _, _ in strokes_of[zi])

    for it in lu:
        if it["kind"] != "pattern":
            continue
        where = f"{it['file']}:{it['no']} {it['name']}"
        mixed = "+" in one(it, "meter")
        meter = 0 if mixed else int(one(it, "meter"))
        bpm = [int(x) for x in one(it, "bpm").split()]
        if len(bpm) != 3:
            die(where, "bpm: tempo, slowest, fastest")
        cat = one(it, "cat")
        if cat not in cats:
            die(where, f"kind {cat!r} not in @cat")
        acc = [float(x) for x in one(it, "accel", "1 1").split()]
        if one(it, "jing", ""):
            secs = events(one(it, "jing"), meter, syl_index, f"{where} jing", sections=True)
        else:
            secs = []
            for key in ("qi", "zhong", "shou"):
                text = one(it, key, "")
                secs.append(events(text, meter, syl_index, f"{where} {key}") if text else ([], 0))
        # a pickup's leading rests (乙) are only its place in the bar: the pattern starts at its first stroke
        evs, ln = secs[0] if secs[0][1] else secs[1]
        k = 0 if secs[0][1] else 1
        while evs and LG_SYLS_REST(evs[0][3]):
            cut = evs[1][0] if len(evs) > 1 else ln
            evs = [[e[0] - cut] + e[1:] for e in evs[1:]]
            ln -= cut
        secs[k] = (evs, ln)
        if not any(ln for _, ln in secs):
            die(where, "no events")
        if one(it, "gu", "+") != "-":           # ("gu -": the drum as written only)
            secs = drum_part(secs, meter, bpm[0], has_drum, is_gong, is_big, syl_index["大"])
        pats.append(dict(name=it["name"], py=it["rest"].strip(), use=one(it, "use", ""), cat=cats.index(cat),
                         meter=meter, mixed=int(mixed), bpm=bpm, reps=int(one(it, "reps", "0")), acc=acc,
                         secs=secs))
    pat_index = {p["name"]: i for i, p in enumerate(pats)}
    gms, bss = [], []
    for it in gu:
        if it["kind"] != "guomen":
            continue
        where = f"{it['file']}:{it['no']} {it['name']}"
        meter = int(one(it, "meter"))
        q = one(it, "qiang")
        if q not in QIANG:
            die(where, f"声腔 {q!r}")
        nt, ln, sing_t, lao, free = notes(one(it, "notes"), meter, where)
        perc = one(it, "perc", "")
        if perc == "板":                         # the 板 on every 板, the 鼓师's way of keeping time in a 过门
            bars = ln // (meter * TPB) if meter else 0
            perc = " | ".join(["扎:4" + " 乙:4" * (meter - 1)] * bars) + " |" if bars else ""
        pe, pln = events(perc, meter, syl_index, f"{where} perc") if perc else ([], 0)
        if pe and pln != ln:
            die(where, f"the percussion is {pln} ticks, the notes {ln}")
        gms.append(dict(name=it["name"], qiang=QIANG.index(q), meter=0 if free else meter, len=ln, notes=nt,
                        perc=pe, sing=sing_t, lao=NOLAO if lao is None else lao,
                        show=one(it, "show", it["name"]), conf=one(it, "conf", "")))
    gm_index = {g["name"]: i for i, g in enumerate(gms)}
    for it in gu:
        if it["kind"] != "banshi":
            continue
        where = f"{it['file']}:{it['no']} {it['name']}"

        def ref(key, table, allow_none=True):
            v = one(it, key, "-")
            if v == "-" and allow_none:
                return -1
            if v not in table:
                die(where, f"{key}: no {v!r}")
            return table[v]
        small = one(it, "small", "- -").split()
        if len(small) == 1:
            small = small * 2
        laos = []
        for name in one(it, "lao", "").split():
            if name not in gm_index:
                die(where, f"lao: no {name!r}")
            if gms[gm_index[name]]["lao"] == NOLAO:
                die(where, f"lao: {name} names no 落音 ([落X])")
            laos.append(gm_index[name])
        if len(laos) > 6:
            die(where, "six 落音 variants at most")
        keep = one(it, "keep", "-")
        bss.append(dict(name=it["name"], qiang=QIANG.index(one(it, "qiang")), meter=int(one(it, "meter")),
                        bpm=[int(x) for x in one(it, "bpm").split()], intro=ref("intro", pat_index),
                        big=ref("big", gm_index),
                        small=[-1 if x == "-" else gm_index[x] if x in gm_index else die(where, f"small: no {x!r}")
                               for x in small],
                        lao=laos + [-1] * (6 - len(laos)),
                        keep=-1 if keep == "-" else ref("keep", pat_index), keep_gm=ref("loop", gm_index),
                        tail=ref("tail", gm_index), end=ref("end", pat_index), gong=int(one(it, "gong", "0")),
                        show=one(it, "show", it["name"]), use=one(it, "use", "")))
    return syls, cats, pats, gms, bss


def write(syls, cats, pats, gms, bss):
    L = ["/* generated by tools/lg_data.py from data/luogu.txt and data/guomen.txt: do not edit */",
         "#pragma once", ""]
    L.append(f"static const lg_syl_t LG_SYL[{len(syls)}] = {{")
    for zi, py, st in syls:
        sts = ", ".join(f"{i << 4 | k}" for i, k, _ in st) or "0"
        vel = ", ".join(str(v) for _, _, v in st) or "0"
        L.append(f"    {{{c_str(zi)}, {c_str(py)}, {len(st)}, {{{sts}}}, {{{vel}}}}},")
    L.append("};")
    L.append(f"static const char *const LG_CAT_NAME[{max(1, len(cats))}] = {{" +
             ", ".join(c_str(c) for c in cats) + "};")
    for pi, p in enumerate(pats):
        for si, (ev, ln) in enumerate(p["secs"]):
            if ev:
                L.append(f"static const lg_ev_t P{pi}_{si}[{len(ev)}] = {{" +
                         ", ".join(f"{{{t}, {s}, {f}, {n}}}" for t, s, f, _, n in ev) + "};" +
                         f"   /* {p['name']} {['起', '中', '收'][si]}: {' '.join(e[3] for e in ev)} */")
    L.append(f"static const lg_pat_t LG_PAT[{len(pats)}] = {{")
    for pi, p in enumerate(pats):
        secs = ", ".join(f"{{P{pi}_{si}, {len(ev)}, {ln}}}" if ev else "{0, 0, 0}" for si, (ev, ln) in enumerate(p["secs"]))
        L.append(f"    {{{c_str(p['name'])}, {c_str(p['py'])}, {c_str(p['use'])}, {p['cat']}, {p['meter']}, {p['mixed']}, "
                 f"{p['bpm'][0]}, {p['bpm'][1]}, {p['bpm'][2]}, {p['reps']}, {p['acc'][0]:.3f}f, {p['acc'][1]:.3f}f, "
                 f"{{{secs}}}}},")
    L.append("};")
    for gi, g in enumerate(gms):
        L.append(f"static const lg_note_t G{gi}_N[{max(1, len(g['notes']))}] = {{" +
                 ", ".join(f"{{{t}, {ln}, {p}, {gr}, {f}}}" for t, ln, p, gr, f in g["notes"]) + "};" +
                 f"   /* {g['name']} */")
        if g["perc"]:
            L.append(f"static const lg_ev_t G{gi}_P[{len(g['perc'])}] = {{" +
                     ", ".join(f"{{{t}, {s}, {f}, {n}}}" for t, s, f, _, n in g["perc"]) + "};")
    L.append(f"static const lg_gm_t LG_GM[{max(1, len(gms))}] = {{")
    for gi, g in enumerate(gms):
        L.append(f"    {{{c_str(g['show'])}, {g['qiang']}, {g['meter']}, {g['len']}, G{gi}_N, {len(g['notes'])}, "
                 f"{'G%d_P' % gi if g['perc'] else '0'}, {len(g['perc'])}, {g['sing']}, {g['lao']}}},")
    if not gms:
        L.append("    {0, 0, 0, 0, 0, 0, 0, 0, 0, -128},")
    L.append("};")
    L.append(f"static const lg_bs_t LG_BS[{max(1, len(bss))}] = {{")
    for b in bss:
        L.append(f"    {{{c_str(b['show'])}, {c_str(b['use'])}, {b['qiang']}, {b['meter']}, {b['bpm'][0]}, {b['bpm'][1]}, "
                 f"{b['bpm'][2]}, {b['intro']}, {b['big']}, {{{b['small'][0]}, {b['small'][1]}}}, "
                 f"{{{', '.join(map(str, b['lao']))}}}, {b['keep']}, {b['keep_gm']}, {b['tail']}, {b['end']}, "
                 f"{b['gong']}}},")
    if not bss:
        L.append("    {0, 0, 0, 0, 0, 0, 0, -1, -1, {-1, -1}, {-1, -1, -1, -1, -1, -1}, -1, -1, -1, -1, 0},")
    L.append("};")
    L.append(f"static const uint8_t LG_NSYL = {len(syls)}, LG_NPAT = {len(pats)}, LG_NGM = {len(gms)}, "
             f"LG_NBS = {len(bss)}, LG_NCAT = {len(cats)};")
    OUT.write_text("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    try:
        syls, cats, pats, gms, bss = build()
    except DataError as e:
        sys.exit(f"lg_data: {e}")
    write(syls, cats, pats, gms, bss)
    print(f"lg_data: {len(syls)} syllables, {len(pats)} patterns, {len(gms)} 过门, {len(bss)} 板式 -> "
          f"{OUT.relative_to(ROOT)}")
    if args.list:
        for p in pats:
            print(f"  {p['name']} ({cats[p['cat']]}, meter {p['meter']}, {p['bpm'][0]} bpm): " +
                  " | ".join(" ".join(e[3] for e in ev) for ev, _ in p["secs"] if ev))


if __name__ == "__main__":
    main()
