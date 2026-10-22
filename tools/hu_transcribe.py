#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A 京胡 recording's opening as 简谱, roughly: to check a 过门's transcription against what a 琴师 plays.

  python3 tools/hu_transcribe.py FILE.wav [--secs 25] [--tonic E]

The pitch every 5 ms (normalized autocorrelation), notes where it holds within 40 cents for 50 ms, each note's scale
degree from the tonic (estimated from the pitch histogram: the major key whose scale holds most of the playing, unless
--tonic gives it), its octave marks (, below the 1 of the octave holding the most notes, ' above), and its start and
length in seconds. Ornaments (grace notes, slides) come out as short notes; the rhythm is left to the reader.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hu_fit  # noqa: E402

NAMES = ["C", "#C", "D", "bE", "E", "F", "#F", "G", "bA", "A", "bB", "B"]
DEG = {0: "1", 1: "#1", 2: "2", 3: "#2", 4: "3", 5: "4", 6: "#4", 7: "5", 8: "#5", 9: "6", 10: "b7", 11: "7"}


def track(x, hop=220, win=1536, fmin=380.0, fmax=1800.0):
    sr = hu_fit.SR
    lmin, lmax = int(sr / fmax), int(sr / fmin) + 1
    w = np.hanning(win)
    out = []
    for i in range(0, len(x) - win, hop):
        seg = x[i:i + win] * w
        lvl = float(np.sqrt(np.mean(seg ** 2)))
        X = np.fft.rfft(seg, 2 * win)
        ac = np.fft.irfft(np.abs(X) ** 2)[:win]
        ac /= ac[0] + 1e-12
        k = lmin + int(np.argmax(ac[lmin:lmax]))
        a, b, c = ac[k - 1], ac[k], ac[k + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        out.append((i / sr, sr / (k + d), float(ac[k]), lvl))
    return out


def notes_of(tr, min_dur=0.05, tol=40.0):
    lv = np.array([q[3] for q in tr])
    gate = np.percentile(lv, 40) * 0.5
    notes, cur = [], None
    for t, f, clar, l in tr:
        ok = clar > 0.8 and l > gate
        c = 1200 * math.log2(f / 440.0) if ok else None
        if cur and c is not None and abs(c - cur["c"]) < tol:
            cur["cs"].append(c)
            cur["c"] = float(np.median(cur["cs"]))
            cur["end"] = t
            continue
        if cur and cur["end"] - cur["start"] >= min_dur:
            notes.append(cur)
        cur = dict(start=t, end=t, c=c, cs=[c]) if c is not None else None
    if cur and cur["end"] - cur["start"] >= min_dur:
        notes.append(cur)
    return notes


def tonic_of(notes):
    pcs = np.zeros(12)
    for n in notes:
        pcs[int(round(n["c"] / 100.0 + 9)) % 12] += n["end"] - n["start"]
    major = [0, 2, 4, 5, 7, 9, 11]
    pent = [0, 2, 4, 7, 9]
    best = max(range(12), key=lambda r: sum(pcs[(r + d) % 12] for d in major) +
               0.5 * sum(pcs[(r + d) % 12] for d in pent))
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--secs", type=float, default=25.0)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--tonic")
    args = ap.parse_args()
    x = hu_fit.load(args.file, max_s=args.start + args.secs)[int(args.start * hu_fit.SR):]
    notes = notes_of(track(x))
    if not notes:
        sys.exit("no notes found")
    tonic = NAMES.index(args.tonic) if args.tonic else tonic_of(notes)
    midis = [69 + n["c"] / 100.0 for n in notes]
    # the octave: the 1 of the octave in which most notes lie
    base = 12 * round((np.median(midis) - tonic - 6) / 12) + tonic
    print(f"tonic 1={NAMES[tonic]}; the middle octave's 1 = MIDI {base:.0f}; {len(notes)} notes")
    line = []
    for n, m in zip(notes, midis):
        rel = int(round(m - base))
        deg = DEG[rel % 12] + ("'" * (rel // 12) if rel >= 12 else "," * (-(rel // 12)) if rel < 0 else "")
        line.append(f"{deg}({n['end'] - n['start'] + 0.005:.2f})")
    t0 = notes[0]["start"]
    for i in range(0, len(line), 12):
        print(f"{notes[i]['start'] - t0 + args.start:6.2f}s  " + "  ".join(line[i:i + 12]))


if __name__ == "__main__":
    main()
