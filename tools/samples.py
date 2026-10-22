#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's recorded strokes: the 单皮鼓, the 板, the 小锣 and the 铙钹 played from real strokes instead of their modes
(the 大锣, a 虎音锣, and the 堂鼓 stay synthesized), written into app/dsp/samp_tab.h.

  python3 tools/samples.py [--wav DIR]          (numpy, scipy, soundfile; the recordings: BOPI, as perc_fit.py)

The strokes are the Beijing Opera Percussion Instrument dataset's (Mi Tian, Ajay Srinivasamurthy, Mark Sandler and
Xavier Serra, 2014; played by Ying Wan of the London Jing Kun Opera Association; CC BY 4.0, so is what this writes):
each one from its onset (1 ms before it), cut where it has fallen 50 dB (or at its longest, faded over FADE), at
32 kHz, peak normalised and coded as G.711 mu-law, 8 bits a sample. Its level (a float beside it) puts each kind of stroke where the
synthesis's was (its loudest 50 ms, K-weighted: TARGET, from tests/host/perc_strokes at full velocity),
so LUOGU's mix (lg.c LG_PERC_GAIN) holds, and its light and damped strokes where the synthesis's were.
--wav writes each stroke as coded, decoded, to listen to.
"""
import argparse
import glob
import math
import os
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import lfilter, resample_poly

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app" / "dsp" / "samp_tab.h"
DATA = Path(os.environ.get("BOPI", Path.home() / "fm1/data/opera/bopi/beijing_opera_percussion_instrument_1.0"))
RATE = 32000
INST = {"I_GU": "鼓", "I_BAN": "板", "I_NAOBO": "铙钹", "I_XIAOLUO": "小锣"}
KINDS = ("SK_OPEN", "SK_LIGHT", "SK_MUTE")
# (instrument, stroke kind, the dataset's class and number, longest kept (s)). Chosen from a listing of every stroke
# (level, ring, main tone, clipped samples): one instrument each, none clipped, none of the dataset's duplicates; the
# open strokes two each where the instrument is struck again and again (two sticks, two spots: never the same twice)
STROKES = [
    ("I_GU", "SK_OPEN", "bangu", 35, 0.3), ("I_GU", "SK_OPEN", "bangu", 38, 0.3), ("I_GU", "SK_LIGHT", "bangu", 44, 0.3),
    ("I_BAN", "SK_OPEN", "bangu", 4, 0.35), ("I_BAN", "SK_LIGHT", "bangu", 11, 0.35),
    ("I_XIAOLUO", "SK_OPEN", "xiaoluo", 57, 1.1), ("I_XIAOLUO", "SK_OPEN", "xiaoluo", 58, 1.1),
    ("I_XIAOLUO", "SK_LIGHT", "xiaoluo", 10, 0.6), ("I_XIAOLUO", "SK_MUTE", "xiaoluo", 13, 0.35),
    ("I_NAOBO", "SK_OPEN", "naobo", 10, 2.0), ("I_NAOBO", "SK_MUTE", "naobo", 19, 0.6),
]
FADE = 0.25           # s: a tail cut before it had fallen 50 dB fades out over this (else over 30 ms)
# the synthesis's strokes at full velocity, their loudest 50 ms (K-weighted, dB in the engine's units), as measured on
# tests/host/perc_strokes before the instruments were sampled (2026-10-22): each kind of stroke is brought to its
# own (the recordings' light and damped strokes were struck as hard as came: the 铙钹's 扑 louder than its 七)
TARGET = {("I_GU", "SK_OPEN"): -0.91, ("I_GU", "SK_LIGHT"): -11.46,
          ("I_BAN", "SK_OPEN"): -2.74, ("I_BAN", "SK_LIGHT"): -16.03,
          ("I_NAOBO", "SK_OPEN"): 9.24, ("I_NAOBO", "SK_MUTE"): 3.20,
          ("I_XIAOLUO", "SK_OPEN"): 7.22, ("I_XIAOLUO", "SK_LIGHT"): -7.64, ("I_XIAOLUO", "SK_MUTE"): 2.95}

def kweight(x, fs):
    """BS.1770's K-weighting: its high shelf and its highpass, from their analog prototypes at fs"""
    g, q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    a_, w0 = 10 ** (g / 40), 2 * math.pi * fc / fs
    al, c = math.sin(w0) / (2 * q), math.cos(w0)
    b = [a_ * ((a_ + 1) + (a_ - 1) * c + 2 * math.sqrt(a_) * al), -2 * a_ * ((a_ - 1) + (a_ + 1) * c),
         a_ * ((a_ + 1) + (a_ - 1) * c - 2 * math.sqrt(a_) * al)]
    a = [(a_ + 1) - (a_ - 1) * c + 2 * math.sqrt(a_) * al, 2 * ((a_ - 1) - (a_ + 1) * c),
         (a_ + 1) - (a_ - 1) * c - 2 * math.sqrt(a_) * al]
    y = lfilter(b, a, x)
    q, fc = 0.5003270373238773, 38.13547087602444
    w0 = 2 * math.pi * fc / fs
    al, c = math.sin(w0) / (2 * q), math.cos(w0)
    return lfilter([(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al], y)


def loud50(x, fs):
    y = kweight(x, fs)
    w = int(0.05 * fs)
    return 10 * math.log10(max(np.mean(y[i:i + w] ** 2) for i in range(0, max(1, len(y) - w), w // 4)) + 1e-20)


def stroke(cls, num, longest):
    path = glob.glob(str(DATA / f"*__{cls}-{num:02d}.wav"))[0]
    x, sr = sf.read(path, dtype="float64")
    x = x if x.ndim == 1 else x.mean(axis=1)
    pk = np.abs(x).max()
    on = max(0, int(np.argmax(np.abs(x) > 0.05 * pk)) - int(0.001 * sr))
    x = x[on:]
    env = np.sqrt(np.convolve(x ** 2, np.ones(441) / 441, mode="same"))   # where it has fallen 50 dB
    above = np.nonzero(env > pk * 10 ** (-50 / 20))[0]
    quiet = (above[-1] + 1) if len(above) else len(x)
    end = min(len(x), int(longest * sr), quiet)
    x = x[:end].copy()
    f = min(len(x), int((FADE if end < quiet else 0.03) * sr))
    x[-f:] *= np.linspace(1.0, 0.0, f) ** 2
    return resample_poly(x, RATE, sr), path


def mulaw(x):
    """G.711 mu-law, 8 bits a sample (about 38 dB of signal to noise at any level: a ring's tail keeps it);
    returns the bytes and the decoded signal"""
    BIAS, CLIP = 0x84, 32635
    v = np.clip(np.round(x * 32767), -32768, 32767).astype(int)
    out, dec = bytearray(), np.zeros(len(v))
    for i, s in enumerate(v):
        sign = 0x80 if s < 0 else 0
        m = min(abs(int(s)), CLIP) + BIAS
        exp = max(0, m.bit_length() - 8)
        mant = (m >> (exp + 3)) & 0x0F
        b = ~(sign | (exp << 4) | mant) & 0xFF
        out.append(b)
        dec[i] = MULAW[b] / 32767.0
    return bytes(out), dec


def mulaw_table():
    t = []
    for b in range(256):
        u = ~b & 0xFF
        sign, exp, mant = u & 0x80, (u >> 4) & 7, u & 0x0F
        m = ((mant << 3) + 0x84) << exp
        t.append(-(m - 0x84) if sign else m - 0x84)
    return t


MULAW = mulaw_table()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", help="a directory: each stroke, coded and decoded, as a WAV")
    args = ap.parse_args()
    loaded = []
    for inst, kind, cls, num, longest in STROKES:
        x, path = stroke(cls, num, longest)
        loaded.append((inst, kind, cls, num, x, Path(path).name))
    # each kind of stroke's level: its loudness (the mean of its recordings) brought to its target
    gain = {}
    for key in TARGET:
        xs = [x for i, k, _, _, x, _ in loaded if (i, k) == key]
        gain[key] = 10 ** ((TARGET[key] - float(np.mean([loud50(x, RATE) for x in xs]))) / 20)
    data, rows, sets, report = bytearray(), [], {i: {k: [] for k in KINDS} for i in INST}, []
    for inst, kind, cls, num, x, name in loaded:
        pk = float(np.abs(x).max())
        coded, dec = mulaw(x / pk * 0.98)   # (peak normalised for the coding; the level restores it)
        level = gain[(inst, kind)] * pk / 0.98
        snr = 10 * math.log10(np.sum((x / pk * 0.98) ** 2) / max(np.sum((x / pk * 0.98 - dec) ** 2), 1e-20))
        sets[inst][kind].append(len(rows))
        rows.append(f"    {{{len(data)}u, {len(x)}u, {level:.6f}f}},   /* {INST[inst]} {kind[3:].lower()}: {name} */")
        report.append(f"{INST[inst]} {kind[3:].lower():5s} {cls}-{num:02d}: {len(x) / RATE:.2f} s, {len(coded)} B, "
                      f"coding SNR {snr:.1f} dB, loudest 50 ms {loud50(dec * level, RATE):+.2f} dB "
                      f"(target {TARGET[(inst, kind)]:+.2f})")
        data += coded
        if args.wav:
            Path(args.wav).mkdir(parents=True, exist_ok=True)
            sf.write(str(Path(args.wav) / f"{cls}-{num:02d}.wav"), dec * level, RATE)
    lines = ["/* generated by tools/samples.py: LUOGU's recorded strokes, from the Beijing Opera Percussion Instrument",
             " * dataset (Tian, Srinivasamurthy, Sandler, Serra, 2014; played by Ying Wan; CC BY 4.0: CREDITS.md). Each:",
             " * its start in SAMP_DATA, its length (samples at SAMP_RATE, a byte each: G.711 mu-law, SAMP_MULAW its",
             " * values), its level; SAMP_SET: each instrument's strokes by kind, -1 none (all -1: the instrument is",
             " * synthesized) */",
             "#pragma once", "#include <stdint.h>", "",
             f"#define SAMP_RATE {RATE}", f"#define SAMP_N {len(rows)}",
             "typedef struct { uint32_t off, len; float level; } samp_t;",
             "enum { SK_OPEN, SK_LIGHT, SK_MUTE, SK_N };",
             "typedef struct { int8_t s[SK_N][2]; } samp_set_t;   /* up to two recordings a kind, taken in turn */",
             f"static const samp_t SAMP[{len(rows)}] = {{"] + rows + ["};"]
    lines.append("static const samp_set_t SAMP_SET[I_NPERC] = {")
    for inst in ("I_GU", "I_BAN", "I_DALUO", "I_NAOBO", "I_XIAOLUO", "I_TANGGU"):
        if inst in sets:
            ks = ", ".join("{" + ", ".join(str(v) for v in (sets[inst][k] + [-1, -1])[:2]) + "}" for k in KINDS)
        else:
            ks = "{-1, -1}, {-1, -1}, {-1, -1}"
        lines.append(f"    [{inst}] = {{{{{ks}}}}},")
    lines.append("};")
    lines.append("static const int16_t SAMP_MULAW[256] = {" + ", ".join(str(v) for v in MULAW) + "};")
    lines.append(f"static const uint8_t SAMP_DATA[{len(data)}] = {{")
    for i in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")
    print("\n".join(report))
    print(f"samples: {len(rows)} strokes, {len(data)} B -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
