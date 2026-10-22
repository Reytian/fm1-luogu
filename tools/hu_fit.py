#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's bowed strings from recordings: the 京胡's and the 二胡's tone, as a body filter over a sawtooth, written into
app/dsp/hu_tab.h; and the same measurement of the synthesis, to compare.

  python3 tools/hu_fit.py measure          each instrument's spectral envelope from its stable notes (a report)
  python3 tools/hu_fit.py fit              ... and the body filters fitted to it (app/dsp/hu_tab.h)
  python3 tools/hu_fit.py compare DIR      the synthesis's notes (tests/host/hu_notes.c) measured the same way

The envelope: in every stable stretch of a note (its pitch within 1 % for 60 ms, loud enough), each harmonic's level
at its frequency (an FFT's peak near k f0); the envelope is the median level in sixth-octave bins, relative to the
loudest bin. A sawtooth's harmonics fall 6 dB an octave, so the filter wanted is the envelope tilted up 6 dB an
octave; it is fitted as a cascade of peaking biquads with a highpass and a lowpass (least squares in dB).

The recordings: the 京胡 accompaniment tracks of the Jingju a Cappella Recordings Collection (Rong Gong, Rafael Caro
Repetto, Xavier Serra; CC BY 4.0), ~/fm1/data/opera/jacrc_jinghu; the 二胡's open strings and scales in the CTIS
database (CC BY-NC-ND 4.0: measured only, nothing of it is in the firmware but these few numbers),
~/fm1/data/opera/ctis/audio/L0266. Needs numpy, scipy, soundfile.
"""
import argparse
import glob
import math
import os
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.optimize import least_squares
from scipy.signal import resample_poly

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("OPERA_DATA", Path.home() / "fm1/data/opera"))
SR = 44100
LO, HI = 100.0, 16000.0
NENV = 64
PER_OCT = (NENV - 1) / math.log2(HI / LO)
GRID = LO * 2 ** (np.arange(NENV) / PER_OCT)          # the envelope's frequencies (as hu_tab.h's HU_ENV)

SOURCES = {
    "jinghu": dict(glob=str(DATA / "jacrc_jinghu/JaCRC-recordings/JaCRC-accompaniment-recordings/*.wav"),
                   f0=(380.0, 1700.0)),
    "erhu": dict(glob=str(DATA / "ctis/audio/L0266/【二胡】*音阶*.wav"), f0=(270.0, 1400.0),
                 extra=str(DATA / "ctis/audio/L0266/【二胡】*空弦音*.wav")),
}


def load(path, max_s=240.0):
    x, sr = sf.read(path, dtype="float64", always_2d=True)
    x = x.mean(axis=1)[: int(max_s * sr)]
    if sr != SR:
        g = math.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g)
    return x


def f0_track(x, fmin, fmax, hop=441, win=2048):
    """a frame's f0 every 10 ms (the normalized autocorrelation's best lag, refined), its clarity and level"""
    out = []
    lmin, lmax = int(SR / fmax), int(SR / fmin) + 1
    w = np.hanning(win)
    for i in range(0, len(x) - win, hop):
        seg = x[i:i + win] * w
        lvl = np.sqrt(np.mean(seg ** 2))
        if lvl < 1e-4:
            out.append((0.0, 0.0, lvl))
            continue
        X = np.fft.rfft(seg, 2 * win)
        ac = np.fft.irfft(np.abs(X) ** 2)[:win]
        ac /= ac[0] + 1e-12
        k = lmin + int(np.argmax(ac[lmin:lmax]))
        if 1 <= k < len(ac) - 1:
            a, b, c = ac[k - 1], ac[k], ac[k + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        else:
            d = 0.0
        out.append((SR / (k + d), float(ac[k]), lvl))
    return out


def harmonics(x, i0, f0, win=4096):
    """the harmonics' levels (dB) and frequencies of the frame at i0"""
    seg = x[i0:i0 + win]
    if len(seg) < win:
        return []
    X = np.abs(np.fft.rfft(seg * np.hanning(win), 4 * win))
    f = np.fft.rfftfreq(4 * win, 1 / SR)
    out = []
    for k in range(1, 60):
        fk = k * f0
        if fk > HI:
            break
        m = (f > fk * 0.97) & (f < fk * 1.03)
        out.append((fk, 20 * math.log10(X[m].max() + 1e-12)))
    return out


def envelope(kind, verbose=True, max_files=None):
    src = SOURCES[kind]
    files = sorted(glob.glob(src["glob"]))
    if src.get("extra"):
        files += sorted(glob.glob(src["extra"]))
    if max_files:
        files = files[:max_files]
    pts, notes = [], 0
    for path in files:
        x = load(path)
        tr = f0_track(x, *src["f0"])
        lv = np.array([q[2] for q in tr])
        loud = np.percentile(lv[lv > 0], 60) if (lv > 0).any() else 1.0
        i = 0
        while i < len(tr) - 6:
            f0s = [q[0] for q in tr[i:i + 6]]
            ok = all(q[1] > 0.85 and q[2] > loud * 0.5 for q in tr[i:i + 6]) and \
                max(f0s) / max(min(f0s), 1.0) < 1.01
            if ok:
                f0 = float(np.median(f0s))
                hs = harmonics(x, (i + 1) * 441, f0)
                if hs:
                    top = max(h[1] for h in hs)
                    pts += [(fk, db - top, f0) for fk, db in hs]
                    notes += 1
                i += 6
            else:
                i += 1
    pts = np.array(pts)
    env = np.full(NENV, np.nan)
    for j, g in enumerate(GRID):
        m = (pts[:, 0] > g * 2 ** (-1 / 12)) & (pts[:, 0] <= g * 2 ** (1 / 12))
        if m.sum() >= 5:
            env[j] = np.median(pts[m, 1])
    good = ~np.isnan(env)
    env = np.interp(np.arange(NENV), np.where(good)[0], env[good])   # (the gaps: from their neighbours)
    env -= env.max()
    if verbose:
        f0s = pts[:, 2]
        print(f"== {kind}: {len(files)} files, {notes} stable stretches, f0 {np.percentile(f0s, 5):.0f}.."
              f"{np.percentile(f0s, 95):.0f} Hz")
        for j in range(0, NENV, 4):
            print(f"   {GRID[j]:7.0f} Hz  {env[j]:6.1f} dB")
    return env, pts


# ---------------------------------------------------------------- the filter
def biquad_db(f, kind, fc, gain_db, q):
    """the magnitude (dB) of an RBJ biquad at frequencies f"""
    w0 = 2 * math.pi * fc / SR
    A = 10 ** (gain_db / 40)
    alpha = math.sin(w0) / (2 * q)
    cw = math.cos(w0)
    if kind == "peak":
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]
        a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    elif kind == "hp":
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    else:                                                 # lp
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    z = np.exp(-1j * 2 * np.pi * f / SR)
    h = (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)
    return 20 * np.log10(np.abs(h) + 1e-12)


NPEAK = 6


def response(p, f):
    db = biquad_db(f, "hp", p[0], 0, p[1]) + biquad_db(f, "lp", p[2], 0, p[3])
    for i in range(NPEAK):
        fc, g, q = p[4 + 3 * i:7 + 3 * i]
        db += biquad_db(f, "peak", fc, g, q)
    return db + p[-1]


def fit_filter(env, kind):
    """the body: hp, lp, NPEAK peaks, a gain, so that saw (-6 dB/oct) x filter = the envelope where the instrument
    has harmonics (from its lowest note); moderate peaks (+-15 dB, Q 0.5..6), the hp and lp not resonant"""
    target = env + 20 * np.log10(GRID / GRID[0])          # undo the saw's fall
    use = GRID >= (400.0 if kind == "jinghu" else 280.0)
    peaks0 = [700, 1800, 3000, 5000, 9000, 1200] if kind == "jinghu" else [500, 1800, 900, 3000, 6000, 1300]
    p0 = [300.0, 0.707, 12000.0, 0.707]
    lo = [80.0, 0.5, 3000.0, 0.5]
    hi = [600.0, 0.9, 19000.0, 0.9]
    for fc in peaks0:
        p0 += [fc, 3.0, 1.5]
        lo += [200.0, -15.0, 0.5]
        hi += [15000.0, 15.0, 6.0]
    p0 += [float(np.median(target[use]))]
    lo += [-80.0]
    hi += [80.0]
    res = least_squares(lambda p: (response(p, GRID[use]) - target[use]), p0, bounds=(lo, hi))
    err = np.sqrt(np.mean(res.fun ** 2))
    return res.x, err


def c_filter(name, p):
    peaks = ", ".join(f"{{{p[4 + 3 * i]:.1f}f, {p[5 + 3 * i]:.2f}f, {p[6 + 3 * i]:.3f}f}}" for i in range(NPEAK))
    return (f"    {{{p[0]:.1f}f, {p[1]:.3f}f, {p[2]:.1f}f, {p[3]:.3f}f, {{{peaks}}}}},   /* {name} */")


def cmd_fit(args):
    rows, envs = [], {}
    for kind in ("jinghu", "erhu"):
        env, _ = envelope(kind, verbose=True)
        p, err = fit_filter(env, kind)
        print(f"   {kind}: fitted, rms {err:.1f} dB over 150 Hz .. 16 kHz")
        rows.append(c_filter(kind, p))
        envs[kind] = env
    path = ROOT / "app/dsp/hu_tab.h"
    text = path.read_text()
    start = text.index("static const hu_body_t HU_BODY")
    end = text.index("};", start) + 2
    body = "static const hu_body_t HU_BODY[HU_NKIND] = {   /* tools/hu_fit.py fit: hp, lp, peaks {Hz, dB, Q} */\n" + \
        "\n".join(rows) + "\n};"
    path.write_text(text[:start] + body + text[end:])
    np.save(ROOT / "build/hu_env.npy", np.array([envs["jinghu"], envs["erhu"]]))
    print("fit: app/dsp/hu_tab.h's HU_BODY written")


def cmd_measure(args):
    for kind in ("jinghu", "erhu"):
        envelope(kind)


def cmd_compare(args):
    envs = np.load(ROOT / "build/hu_env.npy")
    for ki, kind in enumerate(("jinghu", "erhu")):
        pts = []
        for path in sorted(glob.glob(str(Path(args.dir) / f"{kind}_*.wav"))):
            x = load(path)
            f0 = 440.0 * 2 ** ((int(Path(path).stem.split("_")[1]) - 69) / 12)
            for i0 in range(int(0.3 * SR), int(1.2 * SR), 4410):
                hs = harmonics(x, i0, f0)
                if hs:
                    top = max(h[1] for h in hs)
                    pts += [(fk, db - top) for fk, db in hs]
        pts = np.array(pts)
        env = np.full(NENV, np.nan)
        for j, g in enumerate(GRID):
            m = (pts[:, 0] > g * 2 ** (-1 / 12)) & (pts[:, 0] <= g * 2 ** (1 / 12))
            if m.sum() >= 2:
                env[j] = np.median(pts[m, 1])
        env -= np.nanmax(env)
        d = env - envs[ki]
        print(f"== {kind}: synth - recorded envelope (dB): " + " ".join(
            f"{GRID[j] / 1000:.1f}k:{d[j]:+.0f}" for j in range(4, NENV, 6) if not np.isnan(d[j])) +
              f"  (rms {np.sqrt(np.nanmean(d[GRID >= 300] ** 2)):.1f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["measure", "fit", "compare"])
    ap.add_argument("dir", nargs="?")
    args = ap.parse_args()
    {"measure": cmd_measure, "fit": cmd_fit, "compare": cmd_compare}[args.cmd](args)


if __name__ == "__main__":
    main()
