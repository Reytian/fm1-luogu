#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LUOGU's percussion from recordings: each instrument's modes (frequency at rest, level, ring, pitch glide), the
cymbals' bands of noise, the contact's burst, measured in isolated strokes, written as app/dsp/perc_tab.h; and the
same measurements of the synthesis, to compare.

  python3 tools/perc_fit.py measure [--data DIR]          what the recordings say, per instrument (a report)
  python3 tools/perc_fit.py fit [--data DIR] [--only daluo,...]   ... and app/dsp/perc_tab.h from it (--only: just
                                                          those, the rest of the file as it is)
  python3 tools/perc_fit.py compare SYNTH_DIR [--data DIR] the synthesis's strokes (tests/host/perc_strokes.c) beside
                                                          the recordings' (third-octave levels, ring, glide)

The recordings: the Beijing Opera Percussion Instrument dataset (Ajay Srinivasamurthy, Freesound pack 14056, CC BY
3.0: 板鼓 bangu, 大锣 daluo, 铙钹 naobo, 小锣 xiaoluo, 236 strokes), DATA (default ~/fm1/data/opera/bopi/...).
Needs numpy, scipy, soundfile.

How a mode is measured: the open strokes (the longest rings) are cut from their onset; the spectrum of 0.25..1.25 s
(after the glide) gives the modes' frequencies at rest; each mode is band-passed around its frequency and its
envelope (Hilbert) fitted with a line in dB: its ring (t60) and its level at the onset. The glide: the strongest
modes' instantaneous frequency (the analytic signal's phase) over the first 0.4 s, fitted as f(t) = f_rest (1 + g
exp(-t / tau)). Each number is the median over the strokes, the frequencies matched across them.
"""
import argparse
import glob
import math
import os
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt, hilbert, find_peaks

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("BOPI", Path.home() / "fm1/data/opera/bopi/beijing_opera_percussion_instrument_1.0"))
SR = 44100
INST = ["bangu", "daluo", "naobo", "xiaoluo", "ban"]
# the dataset's own labels (its Freesound pages): "bangu" 01-15 are the 板 (the clapper), 16-59 the 单皮鼓; daluo 03
# and 09-24 are rolls or rubbing, not single strokes; some strokes are there twice, trimmed differently (the higher
# number has the longer tail: keep it)
SKIP = {"daluo": set([3] + list(range(9, 25))),
        "naobo": {5, 42, 7, 9},
        "xiaoluo": {16}}


def load(path):
    x, sr = sf.read(path, dtype="float64")
    if x.ndim > 1:
        x = x.mean(1)
    assert sr == SR, path
    pk = np.max(np.abs(x)) + 1e-12
    on = int(np.argmax(np.abs(x) > 0.15 * pk))
    on = max(0, on - 32)
    return x[on:] / pk, 20 * math.log10(pk)


CTIS = Path(os.environ.get("CTIS", Path.home() / "fm1/data/opera/ctis/audio"))
# the 大锣 from CTIS's 虎音锣 (D0049, CC BY-NC-ND: numbers only): the 大锣 of 京剧 is a 虎音锣, BOPI's daluo isn't (its
# pitch sinks 3.5 semitones, a 虎音锣's 6 to 8, and it has little of the 160-320 Hz where its main tone sinks: 23 dB
# less of its energy there than CTIS's and than the 大锣 of a recorded 慢长锤). Its three natural strokes (强, 中强,
# 弱) and its damped one (鼓音)
CTIS_FILES = {"daluo": ["D0049/【虎音锣】自然衰减 力度强  第一遍.wav", "D0049/【虎音锣】自然衰减 力度中强  第一遍.wav",
                        "D0049/【虎音锣】自然衰减 力度弱  第一遍.wav", "D0049/【虎音锣】演奏技巧 鼓音  第一遍.wav"]}


def files(data, inst):
    if inst in CTIS_FILES:
        return [(i + 1, str(CTIS / f)) for i, f in enumerate(CTIS_FILES[inst])]
    out = []
    name = "bangu" if inst == "ban" else inst
    for f in sorted(glob.glob(str(Path(data) / f"*__{name}-*.wav"))):
        n = int(re.search(r"-(\d+)\.wav$", f).group(1))
        if inst == "ban" and n > 15 or inst == "bangu" and n <= 15 or n in SKIP.get(inst, ()):
            continue
        out.append((n, f))
    return out


def env_db(x, frame=441):
    n = len(x) // frame
    e = np.sqrt(np.mean(x[:n * frame].reshape(n, frame) ** 2, axis=1)) + 1e-12
    return 20 * np.log10(e)


def ring_t20(x):
    e = env_db(x)
    p = int(np.argmax(e))
    below = np.where(e[p:] < e[p] - 20)[0]
    return below[0] / 100.0 if len(below) else len(e) / 100.0


def noise_floor_db(x):
    e = env_db(x)
    return float(np.median(np.sort(e)[:max(3, len(e) // 10)]))


def bandpass(x, lo, hi):
    lo = max(lo, 20.0)
    hi = min(hi, SR / 2 - 200)
    sos = butter(3, [lo, hi], btype="band", fs=SR, output="sos")
    return sosfiltfilt(sos, x)


def psd(x, t0, t1, nper=8192):
    """the average power spectrum over [t0, t1) (Welch, Hann windows, half overlap): frequencies, dB"""
    seg = x[int(t0 * SR):int(t1 * SR)]
    if len(seg) < nper:
        nper = 1 << int(math.log2(max(256, len(seg))))
    hop = nper // 2
    w = np.hanning(nper)
    acc, k = 0, 0
    for i in range(0, len(seg) - nper + 1, hop):
        acc = acc + np.abs(np.fft.rfft(seg[i:i + nper] * w, nper * 2)) ** 2
        k += 1
    f = np.fft.rfftfreq(nper * 2, 1 / SR)
    return f, 10 * np.log10(acc / max(k, 1) + 1e-20)


def pick_peaks(f, db, fmin, fmax, nmax, prom=6.0):
    sel = (f >= fmin) & (f <= fmax)
    idx, _ = find_peaks(np.where(sel, db, -400.0), prominence=prom)
    idx = idx[np.argsort(db[idx])[::-1]][:nmax]
    out = []
    for i in sorted(idx):
        a, b, c = db[i - 1], db[i], db[i + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        out.append((f[i] + d * (f[1] - f[0]), b))
    return out


def mode_ring(x, hz, width=0.015):
    """a mode's level at the onset (dB re. the stroke's peak) and its t60, from its band-passed envelope: the line
    through it from its peak (+20 ms) down to 6 dB above the band's own floor (its last fifth)"""
    bw = max(8.0, width * hz)
    y = bandpass(x, hz - bw, hz + bw)
    e = np.abs(hilbert(y))
    fr = 220
    n = len(e) // fr
    edb = 20 * np.log10(np.mean(e[:n * fr].reshape(n, fr), axis=1) + 1e-12)
    t = (np.arange(n) + 0.5) * fr / SR
    floor = np.median(edb[int(n * 0.8):]) if n > 20 else edb.min()
    p = int(np.argmax(edb[:max(2, int(0.15 * SR / fr))]))
    stop = p + 1
    while stop < n and edb[stop] > max(floor + 6, edb[p] - 50):
        stop += 1
    s0 = p + int(0.02 * SR / fr)
    if stop - s0 < 6:
        return float(edb[p]), max(0.02, (stop - p) * fr / SR * 60 / max(6.0, edb[p] - floor))
    k, b = np.polyfit(t[s0:stop], edb[s0:stop], 1)
    k = min(k, -0.5)
    return float(k * t[p] + b), float(-60.0 / k)


def bloom(x, hz, width=0.015):
    """a mode that swells after the stroke: the time its envelope (50 ms means) peaks, and by how much it rose from
    the first 30 ms (dB)"""
    bw = max(8.0, width * hz)
    e = np.abs(hilbert(bandpass(x[:int(1.5 * SR)], hz - bw, hz + bw)))
    fr = 2205
    n = len(e) // fr
    edb = 20 * np.log10(np.mean(e[:n * fr].reshape(n, fr), axis=1) + 1e-12)
    k = int(np.argmax(edb[:min(n, 20)]))
    return (k + 0.5) * fr / SR, float(edb[k] - edb[0])


def track(x, hz, t1=0.5, span=0.15, nper=2048, hop=110):
    """a mode's frequency over its first t1 s: the STFT's strongest bin within +-span of hz, interpolated"""
    w = np.hanning(nper)
    nfft = nper * 4
    f = np.fft.rfftfreq(nfft, 1 / SR)
    sel = np.where((f >= hz * (1 - span)) & (f <= hz * (1 + span)))[0]
    ts, fs, amps = [], [], []
    for i in range(0, min(len(x) - nper, int(t1 * SR)), hop):
        X = np.abs(np.fft.rfft(x[i:i + nper] * w, nfft))
        db = 20 * np.log10(X + 1e-12)
        j = sel[np.argmax(db[sel])]
        a, b, c = db[j - 1], db[j], db[j + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        ts.append((i + nper / 2) / SR)
        fs.append(f[j] + d * (f[1] - f[0]))
        amps.append(X[j])
    return np.array(fs), np.array(amps), np.array(ts)


def fit_glide(fi, amp, t, f_rest):
    """f(t) = f_rest (1 + g exp(-t / tau)): g, tau, by a small search over tau"""
    keep = amp > amp.max() * 0.03
    if keep.sum() < 6:
        return 0.0, 0.1
    rel = fi[keep] / f_rest - 1
    tt = t[keep]
    w = amp[keep]
    best = (1e9, 0.0, 0.1)
    for tau in np.geomspace(0.01, 0.6, 50):
        basis = np.exp(-tt / tau)
        g = np.sum(w * basis * rel) / np.sum(w * basis * basis)
        err = np.sum(w * (rel - g * basis) ** 2)
        if err < best[0]:
            best = (err, g, tau)
    fit_glide.residual = math.sqrt(best[0] / np.sum(w))   # the weighted rms misfit (a fraction of f_rest)
    return best[1], best[2]


def third_octaves(x, t0, t1):
    """levels (dB) in third-octave bands 100 Hz .. 16 kHz over [t0, t1)"""
    seg = x[int(t0 * SR):int(t1 * SR)]
    if len(seg) < 512:
        return None
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    cs = [100 * 2 ** (i / 3) for i in range(22)]
    out = []
    for c in cs:
        m = (f >= c * 2 ** (-1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * math.log10(X[m].sum() + 1e-20))
    return np.array(out)


# ----------------------------------------------------------------------------------------------- measure
# the main tone's range, the modes' range and how many, the rest window (s) per instrument
# (the mode counts keep the render in the FM-1's budget: tests/host/lg_perf.c; the strongest are kept)
SPEC = {"bangu": dict(main=(700, 4000), band=(300, 9000), n=8, rest=(0.0, 0.08), glide=False),
        "ban": dict(main=(1500, 5000), band=(500, 12000), n=6, rest=(0.0, 0.08), glide=False),
        "daluo": dict(main=(150, 600), band=(120, 7000), n=24, rest=(0.25, 1.25), glide=True),
        "naobo": dict(main=(400, 6000), band=(300, 12000), n=14, rest=(0.25, 1.25), glide=False),
        "xiaoluo": dict(main=(400, 1200), band=(300, 9000), n=18, rest=(0.15, 0.6), glide=True)}


def measure_inst(data, inst, verbose=True):
    sp = dict(SPEC[inst])
    if inst in CTIS_FILES:                      # (CTIS's strokes ring 10 s: their partials at rest only after 2 s)
        sp["rest"] = (2.0, 4.5)
    strokes = []
    for num, f in files(data, inst):
        x, pk = load(f)
        s = dict(num=num, x=x, pk=pk, t20=ring_t20(x))
        fr, db = psd(x, *sp["rest"]) if len(x) > sp["rest"][1] * SR * 0.6 else psd(x, 0.0, len(x) / SR)
        mp = pick_peaks(fr, db, *sp["main"], 1, prom=3.0)
        s["main"] = mp[0][0] if mp else 0.0
        strokes.append(s)
    # the strokes of the instrument heard most (the recordings have more than one gong): the biggest cluster of
    # main tones within 4 %
    mains = sorted(s["main"] for s in strokes if s["main"] > 0)
    best = max(mains, key=lambda m: sum(1 for q in mains if abs(q / m - 1) < 0.04))
    same = [s for s in strokes if s["main"] > 0 and abs(s["main"] / best - 1) < 0.04]
    others = sorted({round(s["main"]) for s in strokes if s not in same})
    t20s = np.array([s["t20"] for s in same])
    long_cut = np.percentile(t20s, 50)
    opens = [s for s in same if s["t20"] >= long_cut]
    shorts = [s for s in same if s["t20"] <= np.percentile(t20s, 20)]
    # the modes: peaks of the open strokes' average spectrum at rest
    acc = None
    for s in opens:
        fr, db = psd(s["x"], *sp["rest"]) if len(s["x"]) > sp["rest"][1] * SR * 0.6 else psd(s["x"], 0.0, len(s["x"]) / SR)
        p = 10 ** (db / 10)
        acc = p if acc is None else acc + p[:len(acc)]
    db = 10 * np.log10(acc / len(opens))
    peaks = pick_peaks(fr, db, *sp["band"], sp["n"], prom=3.0)
    if sp["glide"]:                             # a gliding tone smears over its glide: one mode, not several
        mhz = max(peaks, key=lambda q: q[1])[0]
        peaks = [q for q in peaks if q[0] == mhz or abs(q[0] / mhz - 1) > 0.04]
    modes = []
    for hz, pdb in peaks:
        lv, t60, gl, tau, bt, br = [], [], [], [], [], []
        for s in opens:
            L, T = mode_ring(s["x"], hz)
            lv.append(L)
            t60.append(T)
            b_t, b_r = bloom(s["x"], hz)
            bt.append(b_t)
            br.append(b_r)
            if sp["glide"] and pdb > max(q[1] for q in peaks) - 25:
                fi, amp, t = track(s["x"], hz, t1=1.2 if inst == "daluo" else 0.4,
                                   span=0.12 if hz < 1500 else 0.06, nper=2048 if inst == "daluo" else 1024,
                                   hop=110)
                g, ta = fit_glide(fi, amp, t, hz)
                gl.append(g)
                tau.append(ta)
        modes.append(dict(hz=hz, psd=pdb, db=float(np.median(lv)), t60=float(np.median(t60)),
                          glide=float(np.median(gl)) if gl else 0.0, tau=float(np.median(tau)) if tau else 0.0,
                          gsd=float(np.std(gl)) if gl else 0.0, bloom_t=float(np.median(bt)),
                          bloom_db=float(np.median(br))))
    modes = [m for m in modes if m["t60"] >= 0.08 or inst in ("bangu", "ban")]   # (not ringing: noise, the bands' part)
    top = max(m["db"] for m in modes)
    for m in modes:
        m["amp"] = 10 ** ((m["db"] - top) / 20)
    mute_t60 = float(np.median([3 * s["t20"] for s in shorts])) if shorts else 0.1
    tob = [third_octaves(s["x"], 0.0, 0.05) for s in opens]
    tob2 = [third_octaves(s["x"], 0.3, 0.8) for s in opens]
    tob2 = [t for t in tob2 if t is not None]
    res = dict(inst=inst, n=len(strokes), same=len(same), nopen=len(opens), nshort=len(shorts), main=best,
               modes=modes, mute_t60=mute_t60, t20_open=float(np.median([s["t20"] for s in opens])),
               t20_short=float(np.median([s["t20"] for s in shorts])) if shorts else 0.0,
               third0=np.median([t for t in tob if t is not None], axis=0),
               third1=np.median(tob2, axis=0) if tob2 else None, opens=[s["num"] for s in opens])
    if verbose:
        print(f"== {inst}: {len(strokes)} strokes; {len(same)} of the instrument at {best:.0f} Hz (others: "
              f"{others[:12]}); {len(opens)} open (t20 >= {long_cut:.2f} s, median {res['t20_open']:.2f} s), "
              f"{len(shorts)} short (median t20 {res['t20_short']:.2f} s)")
        for m in modes:
            print(f"   {m['hz']:8.1f} Hz  psd {m['psd'] - max(q['psd'] for q in modes):6.1f} dB  onset "
                  f"{m['db'] - top:6.1f} dB  t60 {m['t60']:5.2f} s  glide {m['glide'] * 100:+6.2f} % "
                  f"(sd {m['gsd'] * 100:4.1f})  tau {m['tau'] * 1000:4.0f} ms  peaks at {m['bloom_t']:.2f} s "
                  f"+{m['bloom_db']:.1f} dB")
    return res


def bands_of(data, inst, verbose=True):
    """the cymbals' wash: third-octave bands' level at the onset and their ring, over the open strokes"""
    fs = files(data, inst)
    xs = []
    for num, f in fs:
        x, pk = load(f)
        xs.append((ring_t20(x), x))
    xs.sort(key=lambda q: -q[0])
    opens = xs[:max(3, len(xs) // 3)]
    centres = [500, 800, 1250, 2000, 3150, 5000, 8000, 12500]
    out = []
    for c in centres:
        lv, t6 = [], []
        for _, x in opens:
            L, T = mode_ring(x, c, width=0.35)
            lv.append(L)
            t6.append(T)
        out.append(dict(hz=c, db=float(np.median(lv)), t60=float(np.median(t6))))
    top = max(b["db"] for b in out)
    for b in out:
        b["amp"] = 10 ** ((b["db"] - top) / 20)
        if verbose:
            print(f"   band {b['hz']:6.0f} Hz  {b['db'] - top:6.1f} dB  t60 {b['t60']:5.2f} s")
    return out



# ----------------------------------------------------------------------------------------------- fit
def onset_level(x, hz, span):
    """a mode's level in the first 60 ms: the strongest bin within +-span (it may be gliding)"""
    seg = x[:int(0.06 * SR)]
    nfft = 1 << 15
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    f = np.fft.rfftfreq(nfft, 1 / SR)
    m = (f >= hz * (1 - span)) & (f <= hz * (1 + span))
    return 20 * math.log10(X[m].max() + 1e-12)


def mode_energy(x, hz, span, t1=1.0, nper=2048, hop=512):
    """a mode's energy over [0, t1): the STFT's power summed over its bins (+-span: through a glide)"""
    w = np.hanning(nper)
    f = np.fft.rfftfreq(nper, 1 / SR)
    m = (f >= hz * (1 - span) - SR / nper) & (f <= hz * (1 + span) + SR / nper)
    e = 0.0
    for i in range(0, min(len(x) - nper, int(t1 * SR)), hop):
        X = np.fft.rfft(x[i:i + nper] * w)
        e += np.sum(np.abs(X[m]) ** 2)
    return e


def fit_glide2(fi, amp, t, tau2):
    """f(t) = f_rest (1 + g1 e^-t/tau1 + g2 e^-t/tau2), tau2 given (the slow part follows the tone's own decay):
    f_rest, g1, tau1, g2 by a search over f_rest and tau1, g1 and g2 by least squares"""
    keep = (amp > amp.max() * 0.01) & (t > 0.02)
    fi, tt, w = fi[keep], t[keep], amp[keep] ** 0.5
    best = None
    for fr in np.linspace(fi.min() * 0.90, fi.min() * 1.0, 41):
        rel = fi / fr - 1
        for tau1 in np.geomspace(0.02, 0.6, 30):
            A = np.stack([np.exp(-tt / tau1), np.exp(-tt / tau2)], axis=1) * w[:, None]
            g, *_ = np.linalg.lstsq(A, rel * w, rcond=None)
            if g[0] < 0 or g[1] < 0:
                continue
            err = np.sum((A @ g - rel * w) ** 2)
            if best is None or err < best[0]:
                best = (err, fr, g[0], tau1, g[1])
    if best is None:
        return fi.min(), 0.0, 0.1, 0.0
    return best[1], best[2], best[3], best[4]


def track_fit(x, hz, inst):
    """the main tone followed through its glide: its rest frequency, glide and time constant, its level's rise
    (dB from 10 ms to its peak) and when it peaks, and its ring after the peak (t60)"""
    fi, amp, t = track(x, hz, t1=min(3.0, len(x) / SR - 0.1) if inst == "daluo" else 0.9, span=0.2,
                       nper=4096 if inst == "daluo" else 1024, hop=220 if inst == "daluo" else 110)
    tail = t > (0.9 if inst == "daluo" else 0.45)
    f_rest = float(np.median(fi[tail])) if tail.sum() > 3 else hz
    g, tau = fit_glide(fi, amp, t, f_rest)
    resid = fit_glide.residual
    db = 20 * np.log10(amp + 1e-12)
    k = int(np.argmax(db[:np.searchsorted(t, 0.4)]))
    rise, t_pk = float(db[k] - db[0]), float(t[k])
    after = (t > t[k] + 0.05) & (db > db.max() - 45)
    if after.sum() > 4:
        sl, _ = np.polyfit(t[after], db[after], 1)
        t60 = -60.0 / min(sl, -1.0)
    else:
        t60 = 1.0
    q = dict(f_rest=f_rest, g=g, tau=tau, rise=rise, t_pk=t_pk, t60=t60, resid=resid, g2=0.0, tau2=1.0)
    if inst == "daluo":                         # the big gong: a fast and a slow part
        tau2 = max(t60, 2.0) / 6.9078
        fr, g1, tau1, g2 = fit_glide2(fi, amp, t, tau2)
        q.update(f_rest=fr, g=g1 + g2, tau=tau1, g2=g2, tau2=tau2)
    return q


def fit_inst(data, inst):
    res = measure_inst(data, inst, verbose=False)
    fl = dict(files(data, inst))
    opens = [load(fl[n])[0] for n in res["opens"]]
    modes = res["modes"]
    glide_main, tau_main, bloom, slow_share, slow_tau = 0.0, 0.1, None, 0.0, 1.0
    main = max(modes, key=lambda m: m["psd"]) if inst in ("daluo", "xiaoluo") else None
    if main is not None:
        tf = [track_fit(x, main["hz"], inst) for x in opens]
        # the strokes whose main tone the tracker follows cleanly (a smooth glide, its misfit at most the median's):
        # in the others it hops between the tone and a neighbour
        # the strokes the tracker follows (misfit under 1.5 %), and of those the strongest half: the synthesis's full
        # stroke is a loud one, and a gong glides more the harder it is struck
        clean = [q for q in tf if q["resid"] <= 0.015 and abs(q["g"]) > 0.015]
        sign = np.sign(np.median([q["g"] for q in clean]))
        good = [q for q in clean if np.sign(q["g"]) == sign]
        good = sorted(good, key=lambda q: -abs(q["g"]))[:max(3, (len(good) + 1) // 2)]
        for q, n in zip(tf, res["opens"]):
            print(f"      stroke {n:3d}: glide {q['g'] * 100:+6.2f} % tau {q['tau'] * 1000:4.0f} ms  misfit "
                  f"{q['resid'] * 100:.2f} %{'  (used)' if q in good else ''}")
        glide_main = float(np.median([q["g"] for q in good]))
        tau_main = float(np.median([q["tau"] for q in good]))
        slow_share = float(np.median([q["g2"] / max(q["g"], 1e-6) for q in good]))
        slow_tau = float(np.median([q["tau2"] for q in good]))
        main["hz"] = float(np.median([q["f_rest"] for q in good]))
        main["t60"] = float(np.median([q["t60"] for q in good]))
        rises = [q for q in good if q["rise"] > 3.0]
        if len(rises) >= len(good) / 2:
            r = float(np.median([q["rise"] for q in rises]))
            tp = float(np.median([q["t_pk"] for q in rises]))
            bloom = (r, tp)
        print(f"   {inst} main tone: rest {main['hz']:.1f} Hz, glide {glide_main * 100:+.1f} % (tau "
              f"{tau_main * 1000:.0f} ms) in {len(good)} of {len(tf)} strokes, t60 {main['t60']:.2f} s, "
              f"rise {bloom[0] if bloom else 0:.1f} dB at {bloom[1] if bloom else 0:.2f} s; slow part "
              f"{slow_share * 100:.0f} % (tau {slow_tau:.2f} s)")
    # each mode's level at the onset, from its energy over the first second and its ring: E = A^2 tau/4 (1 - e^-2/tau)
    # (tau the amplitude's time constant), so a glide or a swell spreading it over bins and time doesn't hide it
    lv = []
    for m in modes:
        span = abs(m["glide"]) * 1.3 + 0.01 if main is not None else 0.012
        tau = m["t60"] / 6.9078
        es = [mode_energy(x, m["hz"], span) for x in opens]
        e = float(np.median(es)) / max(tau / 4 * (1 - math.exp(-2.0 / tau)), 1e-6)
        if m.get("bloom_fit"):
            e *= 1.0
        lv.append(10 * math.log10(e + 1e-20))
    top = max(lv)
    out = []
    for m, L in zip(modes, lv):
        g = 0.0
        if main is not None:
            if m is main:
                g = glide_main
            elif np.sign(m["glide"]) == np.sign(glide_main) and m["gsd"] < abs(m["glide"]) * 1.5:
                g = float(np.clip(m["glide"], -abs(glide_main) * 1.2, abs(glide_main) * 1.2))
            else:
                g = glide_main * 0.5
        b, direct = 0.0, 1.0
        if m is main and bloom is not None:     # e^-t/T1 - (1 - d) e^-t/T2 that rises bloom[0] dB by bloom[1] s
            t1 = m["t60"] / 6.9078
            best = None
            for t2 in np.geomspace(0.01, 0.5, 60):
                for d in np.linspace(0.02, 0.9, 45):
                    tt = np.linspace(0, 1.0, 1001)
                    e = np.exp(-tt / t1) - (1 - d) * np.exp(-tt / t2)
                    if t2 >= t1:
                        continue
                    k = int(np.argmax(e))
                    r = 20 * math.log10(max(e[k], 1e-9) / max(e[10], 1e-9))
                    err = (r - bloom[0]) ** 2 + (100 * (tt[k] - bloom[1])) ** 2
                    if best is None or err < best[0]:
                        best = (err, t2, d)
            b, direct = float(best[1]), float(best[2])
        out.append(dict(hz=m["hz"], amp=10 ** ((L - top) / 20), t60=min(m["t60"], 8.0), glide=g, bloom=b,
                        direct=direct))
    return dict(modes=out, tau=tau_main, mute_t60=res["mute_t60"], t20_open=res["t20_open"], slow=slow_share,
                slow_t=slow_tau, ref_glide=main["glide"] if main is not None else 0.0)


def band_t60s(data, inst):
    fs = files(data, inst)
    xs = sorted(((ring_t20(load(f)[0]), load(f)[0]) for _, f in fs), key=lambda q: -q[0])
    opens = [x for _, x in xs[:max(3, len(xs) // 3)]]
    centres = [500, 800, 1250, 2000, 3150, 5000, 8000, 12500]
    out = []
    for c in centres:
        lv, t6 = [], []
        for x in opens:
            y = bandpass(x, c * 2 ** -0.33, c * 2 ** 0.33)
            e = env_db(y)
            p = int(np.argmax(e))
            lv.append(e[p])
            below = np.where(e[p:] < e[p] - 20)[0]
            t6.append(3 * below[0] / 100.0 if len(below) else 3 * len(e) / 100.0)
        out.append(dict(hz=c, db=float(np.median(lv)), t60=float(np.median(t6))))
    top = max(b["db"] for b in out)
    for b in out:
        b["amp"] = 10 ** ((b["db"] - top) / 20)
    return out


HEAD = """/* SPDX-License-Identifier: MPL-2.0 */
/* The instruments' numbers (perc.h pc_inst_t). 板鼓, 大锣, 铙钹 and 小锣: modes measured by tools/perc_fit.py fit in
 * the Beijing Opera Percussion Instrument dataset (Ajay Srinivasamurthy, Freesound pack 14056, CC BY 3.0); the
 * contact, brightness, pans and sends set by ear and by tools/perc_fit.py compare. 檀板 and 堂鼓: set by hand. */
#pragma once
"""


def c_modes(name, comment, modes):
    rows = [f"    {{{m['hz']:.1f}f, {m['amp']:.3f}f, {m['t60']:.3f}f, {m['glide']:.4f}f, {m['bloom']:.3f}f, "
            f"{m['direct']:.3f}f}}," for m in modes]
    return f"static const pc_mode_t {name}[] = {{   /* {comment} */\n" + "\n".join(rows) + "\n};\n"


# The 虎音锣's glide: a strong stroke starts 6 to 8 semitones above its rest (CTIS's struck 强: 269 Hz to 186 Hz, +45 %,
# half of it within a second; the 大锣 of CompMusic's 慢长锤 recording, from 穆文义's book: 242 Hz to 147 Hz, +63 %).
# CTIS's three strokes give the partials' glides relative to each other; their size is set from those two: the main
# (lowest) tone 58 % up at the hardest stroke (50 % at a 仓's), the others in proportion, with the strong stroke's
# time constants (fast 0.15 s, slow 1.1 s, 70 % slow)
HUYIN = {"main": 0.58, "tau": 0.150, "slow_t": 1.10, "slow": 0.70}


def huyin(fit):
    k = HUYIN["main"] / max(fit["ref_glide"], 0.05)   # (the main tone's measured beside the others', same strokes)
    for i, m in enumerate(fit["modes"]):
        m["glide"] = HUYIN["main"] if i == 0 else float(np.clip(m["glide"] * k, 0.0, HUYIN["main"]))
    fit["tau"], fit["slow_t"], fit["slow"] = HUYIN["tau"], HUYIN["slow_t"], HUYIN["slow"]


MODE_TABLE = {"bangu": ("GU_M", "板鼓 (单皮鼓)"), "ban": ("BAN_M", "檀板 (板): the clapper"),
              "daluo": ("DALUO_M", "大锣: a 虎音锣, its pitch sinking as it rings"),
              "naobo": ("NAOBO_M", "铙钹: its strongest partials, over the wash"),
              "xiaoluo": ("XIAOLUO_M", "小锣: its pitch rises as it rings")}


def splice_block(text, head, block):
    """text with the C table starting at the line beginning head replaced by block (up to its closing };)"""
    i = text.index(head)
    j = text.index("};\n", i) + 3
    return text[:i] + block + text[j:]


def band_rows(inst, name, noise):
    q = 1.4
    return f"static const pc_band_t {name}[] = {{   /* {inst}: the noise its modes leave out */\n" + "\n".join(
        f"    {{{b['hz']:.0f}.0f, {q}f, {b['syn_amp'] * NOISE_GAIN:.4f}f, {b['t60']:.2f}f}},"
        for b in noise if b["share"] > 0.15 and b["syn_amp"] * NOISE_GAIN > 0.03 and
        (b["hz"] >= 600 or inst in ("bangu", "daluo"))) + "\n};\n"   # (the cymbals' and small gong's lows: a
                                                                      # rumble the recordings don't have)


def fit_only(args, only):
    """refit some of the instruments, the others' tables kept as perc_tab.h has them"""
    path = ROOT / "app/dsp/perc_tab.h"
    text = path.read_text()
    for inst in only:
        f = fit_inst(args.data, inst)
        if inst == "daluo":
            huyin(f)
        name, comment = MODE_TABLE[inst]
        text = splice_block(text, f"static const pc_mode_t {name}[]", c_modes(name, comment, f["modes"]))
        if inst in ("daluo", "xiaoluo"):
            P = PREFIX[inst]
            text = re.sub(rf"#define {P}_TAU [^ ]+", f"#define {P}_TAU {f['tau']:.3f}f", text)
            text = re.sub(rf"#define {P}_SLOW_T [^ ]+", f"#define {P}_SLOW_T {f['slow_t']:.3f}f", text)
            text = re.sub(rf"#define {P}_SLOW [^ ]+", f"#define {P}_SLOW {f['slow']:.3f}f", text)
        text = re.sub(rf"/\* {inst}: the recordings' shortest strokes ring [\d.]+ s",
                      f"/* {inst}: the recordings' shortest strokes ring {f['mute_t60']:.2f} s", text)
    path.write_text(text)
    build_tune()
    for inst in only:                           # the noise the modes leave out, against the synthesis as it now is
        name = PREFIX[inst] + "_B"
        text = splice_block(text, f"static const pc_band_t {name}[]",
                            band_rows(inst, name, residual(args.data, inst, verbose=True, centres=OCT, half=0.5)))
    path.write_text(text)
    build_tune()
    i0 = text.index("static const pc_band_t GU_B[]")   # the five band tables, in this order
    i1 = text.index("};\n", text.index("static const pc_band_t XIAOLUO_B[]")) + 3
    head, rows, tail = text[:i0], text[i0:i1], text[i1:]

    def compose(rows_now, ring, click):
        t = head + rows_now + tail
        for k in only:
            P = PREFIX[k]
            t = re.sub(rf"#define {P}_RING [^\n]*", f"#define {P}_RING {ring[k]:.3f}f", t, count=1)   # (not the
            t = re.sub(rf"#define {P}_CLICK [^\n]*", f"#define {P}_CLICK {click[k][0]:.3f}f, {click[k][1]:.4f}f, "
                       f"{click[k][2]:.0f}.0f, {click[k][3]:.0f}.0f", t, count=1)   # hand part's fallbacks)
        return t
    calibrate(args.data, compose, rows, only)
    print("fit: " + ", ".join(only) + " refitted in app/dsp/perc_tab.h, the rest as it was")


def cmd_fit(args):
    if args.only:
        return fit_only(args, args.only.split(","))
    fits = {inst: fit_inst(args.data, inst) for inst in INST}
    huyin(fits["daluo"])
    keep = (ROOT / "app/dsp/perc_tab.h").read_text()
    hand = keep[keep.index("/* --- set by hand"):] if "/* --- set by hand" in keep else ""
    if not hand:
        sys.exit("fit: perc_tab.h has no '/* --- set by hand' part to keep")
    out = HEAD
    out += c_modes("GU_M", "板鼓 (单皮鼓)", fits["bangu"]["modes"])
    out += c_modes("BAN_M", "檀板 (板): the clapper", fits["ban"]["modes"])
    out += c_modes("DALUO_M", MODE_TABLE["daluo"][1], fits["daluo"]["modes"])
    out += c_modes("NAOBO_M", "铙钹: its strongest partials, over the wash", fits["naobo"]["modes"])
    out += c_modes("XIAOLUO_M", "小锣: its pitch rises as it rings", fits["xiaoluo"]["modes"])
    out += "\n".join(f"#define {k.upper()}_TAU {fits[k]['tau']:.3f}f   /* {k}'s glide: its fast part (s) */\n"
                     f"#define {k.upper()}_SLOW_T {fits[k]['slow_t']:.3f}f   /* ... its slow part (s) */\n"
                     f"#define {k.upper()}_SLOW {fits[k]['slow']:.3f}f   /* ... the slow part's share */"
                     for k in ("daluo", "xiaoluo")) + "\n"
    out += "\n".join(f"/* {k}: the recordings' shortest strokes ring {fits[k]['mute_t60']:.2f} s (t60) */" for k in INST) + "\n\n"
    (ROOT / "app/dsp/perc_tab.h").write_text(out + noise_stub() + hand)
    # second pass: the noise the modes leave out, measured against the synthesis as it now is
    build_tune()
    noise = {}
    for inst in INST:
        noise[inst] = residual(args.data, inst, verbose=True, centres=OCT, half=0.5)
    rows = "".join(band_rows(inst, name, noise[inst]) for inst, name in (
        ("bangu", "GU_B"), ("ban", "BAN_B"), ("daluo", "DALUO_B"), ("naobo", "NAOBO_B"), ("xiaoluo", "XIAOLUO_B")))

    def compose(rows_now, ring, click):
        defs = "".join(f"#define {PREFIX[k]}_RING {ring[k]:.3f}f\n#define {PREFIX[k]}_CLICK {click[k][0]:.3f}f, "
                       f"{click[k][1]:.4f}f, {click[k][2]:.0f}.0f, {click[k][3]:.0f}.0f\n" for k in INST)
        return out + rows_now + defs + hand
    calibrate(args.data, compose, rows)
    print("fit: app/dsp/perc_tab.h written")


PREFIX = {"bangu": "GU", "ban": "BAN", "daluo": "DALUO", "naobo": "NAOBO", "xiaoluo": "XIAOLUO"}


def calibrate(data, compose, rows, only=None):
    """the ring (every t60 scaled so the stroke falls 20 dB as fast as the recordings'), the contact (a grid), and
    the noise bands' levels (each band's energy 50..800 ms matched), three rounds, for the instruments in only (all by
    default); compose(rows, ring, click) gives perc_tab.h's text"""
    import subprocess
    only = list(only or INST)
    idx = {"bangu": 0, "ban": 1, "daluo": 2, "naobo": 3, "xiaoluo": 4}
    ring = {k: 1.0 for k in INST}
    click = {k: (0.3, 0.003, 4000.0, 300.0) for k in INST}
    recs = {}
    for inst in only:
        res = measure_inst(data, inst, verbose=False)
        fl = dict(files(data, inst))
        xs = [load(fl[n])[0] for n in res["opens"]]
        w1 = (0.03, 0.25) if inst in ("bangu", "ban") else (0.05, 0.8)
        recs[inst] = dict(t20=res["t20_open"], x=xs, third0=res["third0"], w1=w1,
                          third1=np.median([third_octaves(x, *w1) for x in xs if len(x) > w1[1] * SR], axis=0))

    def write(rows_now):
        (ROOT / "app/dsp/perc_tab.h").write_text(compose(rows_now, ring, click))
        build_tune()

    def render(inst, secs, c=None):
        c = c or click[inst]
        out = subprocess.run([str(ROOT / "build/tests/perc_tune"), str(idx[inst]), str(secs), "1", f"{c[0]:.4f}",
                              str(c[1]), str(c[2]), str(c[3]), "1"], capture_output=True, check=True).stdout
        return np.frombuffer(out, dtype=np.float32).astype(np.float64)

    def ring_pass():
        for inst in only:                       # the ring
            for _ in range(3):
                t20 = ring_t20(render(inst, 4.0 if inst not in ("bangu", "ban") else 0.6))
                ring[inst] = float(np.clip(ring[inst] * np.clip(recs[inst]["t20"] / max(t20, 0.01), 0.5, 2.0), 0.5, 2.0))
                write(rows)

    def bands_pass(rows, lim):
        new_rows = ""
        for inst, name in (("bangu", "GU_B"), ("ban", "BAN_B"), ("daluo", "DALUO_B"), ("naobo", "NAOBO_B"),
                           ("xiaoluo", "XIAOLUO_B")):
            block = re.search(r"static const pc_band_t " + name + r"\[\] = \{[^\n]*\n(.*?)\n\};", rows, re.S)
            if inst not in only:                # (as it is)
                new_rows += block.group(0) + "\n"
                continue
            x = render(inst, 0.9)
            s1 = third_octaves(x, *recs[inst]["w1"])
            s0 = third_octaves(x, 0.0, 0.05)
            r = recs[inst]["third1"] - recs[inst]["third0"].max()
            d = (s1 - s0.max()) - r                  # synth - recorded, per third octave, 50..800 ms
            cs = np.array([100 * 2 ** (i / 3) for i in range(22)])
            lines = block.group(1).split("\n")
            out = []
            for ln in lines:
                m = re.match(r"\s*\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f, ([\d.]+)f\},", ln)
                hz, q, a, t6 = map(float, m.groups())
                near = (cs >= hz * 2 ** -0.5) & (cs <= hz * 2 ** 0.5)
                corr = float(np.clip(np.nanmedian(d[near]), -lim, lim)) if near.any() else 0.0
                corr = 0.0 if math.isnan(corr) else corr
                a = min(a * 10 ** (-corr / 20 * 0.7), 6.0)
                out.append(f"    {{{hz:.0f}.0f, {q}f, {a:.4f}f, {t6:.2f}f}},")
            new_rows += block.group(0).split("\n")[0] + "\n" + "\n".join(out) + "\n};\n"
        rows = new_rows
        return rows

    for rnd in range(3):
        write(rows)
        ring_pass()
        for inst in only:                       # the contact
            r0 = recs[inst]["third0"] - recs[inst]["third0"].max()
            best = None
            for lvl in np.geomspace(0.02, 2, 9):
                for ct in (0.002, 0.004, 0.008, 0.016, 0.032, 0.064):
                    for lp in (2000.0, 4000.0, 8000.0, 14000.0):
                        for hp in (150.0, 400.0, 1000.0, 2500.0):
                            x = render(inst, 0.06, (lvl, ct, lp, hp))
                            s0 = third_octaves(x, 0.0, 0.05)
                            e = (s0 - s0.max()) - np.maximum(r0, -50.0)
                            e = np.where(r0 > -50.0, e, np.maximum(e, 0.0))   # (below -50 dB only too much counts)
                            err = math.sqrt(np.mean(e[3:] ** 2))   # (from 200 Hz)
                            if best is None or err < best[0]:
                                best = (err, (float(lvl), ct, lp, hp))
            click[inst] = best[1]
        write(rows)
        rows = bands_pass(rows, 12.0)
        print(f"   round {rnd + 1}: ring " + ", ".join(f"{k} {ring[k]:.2f}" for k in only) + "; contact " +
              ", ".join(f"{k} {click[k][0]:.2f}/{click[k][1] * 1000:.0f}ms/{click[k][2]:.0f}/{click[k][3]:.0f}"
                        for k in only))
    for _ in range(4):                          # the bands again, by as much as they need
        write(rows)
        rows = bands_pass(rows, 30.0)
    write(rows)
    ring_pass()
    print("   ring at the end: " + ", ".join(f"{k} {ring[k]:.2f}" for k in only))





NOISE_GAIN = 1.0      # the bands' noise in synth units (a unit white noise through a bandpass of this Q)


def noise_stub():
    return "".join(f"static const pc_band_t {n}[] = {{{{1000.0f, 1.4f, 0.0f, 0.1f}}}};\n"
                   for n in ("GU_B", "BAN_B", "DALUO_B", "NAOBO_B", "XIAOLUO_B"))


def build_tune():
    import subprocess
    subprocess.run(["cc", "-O2", "-ffp-contract=off", "-std=c99", "-DLG_HOST", "-DPC_NO_SAMPLES", "-Iapp/dsp", "-Ibase/include",
                    "-Itests/host", "-o", "build/tests/perc_tune", "tests/host/perc_tune.c", "app/dsp/perc.c", "-lm"],
                   cwd=ROOT, check=True)


def cmd_tune(args):
    """the contact's level, decay and band for each measured instrument: the grid point whose open stroke's
    third-octave levels, 0..50 ms and 50..300 ms, are closest to the recordings' (rms dB)"""
    import subprocess
    exe = ROOT / "build/tests/perc_tune"
    idx = {"bangu": 0, "ban": 1, "daluo": 2, "naobo": 3, "xiaoluo": 4}
    for inst in INST:
        rec = measure_inst(args.data, inst, verbose=False)
        r0 = rec["third0"] - rec["third0"].max()
        fl = dict(files(args.data, inst))
        r1s = [third_octaves(load(fl[n])[0], 0.05, 0.3) for n in rec["opens"]]
        r1 = np.median(r1s, axis=0)
        r1 = r1 - rec["third0"].max()
        best = None
        for click in np.geomspace(0.03, 6, 10):
            for ct in (0.001, 0.002, 0.004, 0.008, 0.016):
                for lp in (1500.0, 3000.0, 6000.0, 12000.0):
                    for hp in (100.0, 300.0, 800.0, 2000.0):
                        out = subprocess.run([str(exe), str(idx[inst]), "0.32", "1", f"{click:.4f}", str(ct), str(lp),
                                              str(hp), "1"], capture_output=True, check=True).stdout
                        x = np.frombuffer(out, dtype=np.float32).astype(np.float64)
                        s0 = third_octaves(x, 0.0, 0.05)
                        s1 = third_octaves(x, 0.05, 0.3)
                        top = s0.max()
                        e0 = (s0 - top) - r0
                        e1 = (s1 - top) - r1
                        keep = r0 > -45                 # (bands the recordings have above their noise)
                        err = math.sqrt(np.mean(e0[keep] ** 2) + 0.5 * np.mean(e1[keep] ** 2))
                        if best is None or err < best[0]:
                            best = (err, click, ct, lp, hp, e0, e1)
        err, click, ct, lp, hp, e0, e1 = best
        cs = [100 * 2 ** (i / 3) for i in range(22)]
        print(f"{inst}: click {click:.3f}, decay {ct * 1000:.0f} ms, lowpass {lp:.0f} Hz, highpass {hp:.0f} Hz "
              f"(rms {err:.1f} dB)")
        print("   0-50 ms   " + " ".join(f"{c / 1000:.1f}k:{e0[i]:+.0f}" for i, c in enumerate(cs) if i % 2 == 0))
        print("   50-300 ms " + " ".join(f"{c / 1000:.1f}k:{e1[i]:+.0f}" for i, c in enumerate(cs) if i % 2 == 0))


HALF_OCT = [250 * 2 ** (i / 2) for i in range(12)]          # 250 Hz .. 11.3 kHz, half-octave bands
OCT = [354 * 2 ** i for i in range(6)]                      # 354 Hz .. 11.3 kHz, octave bands (the noise's)


def band_env(x, centres=HALF_OCT, frame=441, secs=2.0, half=0.25):
    """each band's power every 10 ms over the first secs (the band +-half an octave)"""
    x = x[:int(secs * SR)]
    out = []
    for c in centres:
        y = bandpass(x, c * 2 ** -half, c * 2 ** half)
        n = len(y) // frame
        p = np.mean(y[:n * frame].reshape(n, frame) ** 2, axis=1)
        out.append(p)
    return np.array(out)


def synth_stroke(inst, secs=2.0, click=0.0):
    import subprocess
    idx = {"bangu": 0, "ban": 1, "daluo": 2, "naobo": 3, "xiaoluo": 4}[inst]
    out = subprocess.run([str(ROOT / "build/tests/perc_tune"), str(idx), str(secs), "1", str(click), "0.002", "6000",
                          "300", "1"], capture_output=True, check=True).stdout
    return np.frombuffer(out, dtype=np.float32).astype(np.float64)


def residual(data, inst, verbose=True, centres=HALF_OCT, half=0.25):
    """what the modes leave out: per half-octave band, the recordings' power less the modes' (the modes scaled to
    the recordings in the band of their strongest), its level at the onset and its ring"""
    res = measure_inst(data, inst, verbose=False)
    fl = dict(files(data, inst))
    secs = 2.0 if inst not in ("bangu", "ban") else 0.4
    recs = []
    for n in res["opens"]:
        x = load(fl[n])[0]
        if len(x) < secs * SR:
            x = np.concatenate([x, np.zeros(int(secs * SR) - len(x))])
        e = band_env(x, centres, secs=secs, half=half)
        floor = np.median(e[:, int(e.shape[1] * 0.8):], axis=1, keepdims=True) if inst not in ("bangu", "ban") else \
            np.min(e, axis=1, keepdims=True)
        recs.append(np.maximum(e - floor, 0.0))
    rec = np.median(recs, axis=0)
    syn = band_env(synth_stroke(inst, secs), centres, secs=secs, half=half)
    t = (np.arange(rec.shape[1]) + 0.5) * 0.01
    early = t < 0.3
    b = int(np.argmax(syn[:, early].max(axis=1)))          # the modes' strongest band, early
    k = rec[b, early].max() / max(syn[b, early].max(), 1e-20)
    out = []
    for i, c in enumerate(centres):
        r = rec[i] - k * syn[i]
        r = np.maximum(r, 0.0)
        rdb = 10 * np.log10(r + 1e-20)
        tot = 10 * np.log10(rec[i] + 1e-20)
        p = int(np.argmax(rdb[:max(2, int(0.05 / 0.01))]))
        keep = (t > t[p] + 0.02) & (rdb > rdb[p] - 40) & (rdb > -150)
        if keep.sum() > 4:
            sl, b0 = np.polyfit(t[keep], rdb[keep], 1)
            t60 = -60.0 / min(sl, -1.0)
            lv = b0 + sl * t[p]
        else:
            t60, lv = 0.05, rdb[p]
        out.append(dict(hz=c, db=float(lv), t60=float(min(t60, 6.0)),
                        share=float(np.sum(r) / max(np.sum(rec[i]), 1e-20)), tot=float(tot[p])))
    # the noise's level in the synthesis's own units: the modes' band power scaled by k matches the recordings', so
    # the residual's amplitude in synth units is sqrt(power / k) (power of a unit noise through a band ~ its width)
    for q in out:
        q["syn_amp"] = math.sqrt(10 ** (q["db"] / 10) / k)
    top = max(q["tot"] for q in out)
    if verbose:
        print(f"== {inst}: what the modes leave out (half-octave bands; level re. the loudest band's total)")
        for q in out:
            print(f"   {q['hz']:7.0f} Hz  residual {q['db'] - top:6.1f} dB  t60 {q['t60']:5.2f} s  "
                  f"({q['share'] * 100:3.0f} % of the band's energy)")
    for q in out:
        q["amp"] = 10 ** ((q["db"] - top) / 20)
    return out


def cmd_residual(args):
    for inst in INST:
        residual(args.data, inst)


def cmd_measure(args):
    for inst in INST:
        measure_inst(args.data, inst)
        if inst == "naobo":
            bands_of(args.data, inst)


def cmd_compare(args):
    names = {"bangu": "gu", "ban": "ban", "daluo": "daluo", "naobo": "naobo", "xiaoluo": "xiaoluo"}
    for inst in INST:
        rec = measure_inst(args.data, inst, verbose=False)
        path = Path(args.synth) / f"{names[inst]}_open.wav"
        x, _ = load(str(path))
        print(f"== {inst}: synthesis {path.name} beside {rec['nopen']} recorded open strokes")
        print(f"   t20: recorded {rec['t20_open']:.2f} s, synth {ring_t20(x):.2f} s")
        a0, a1 = third_octaves(x, 0.0, 0.05), third_octaves(x, 0.3, 0.8)
        for lab, r, s in (("0-50 ms", rec["third0"], a0), ("0.3-0.8 s", rec["third1"], a1)):
            if r is None or s is None:
                continue
            r = r - r.max()
            s = s - s.max()
            cs = [100 * 2 ** (i / 3) for i in range(22)]
            print(f"   {lab:9s} " + " ".join(f"{c / 1000:.1f}k:{s[i] - r[i]:+.0f}" for i, c in enumerate(cs) if i % 2 == 0)
                  + f"   (synth - recorded, dB; rms {np.sqrt(np.mean((s - r) ** 2)):.1f})")
        if inst in ("daluo", "xiaoluo"):
            m0 = max(rec["modes"], key=lambda m: m["psd"])
            q = track_fit(x, m0["hz"], inst)
            print(f"   main tone (synth): rest {q['f_rest']:.1f} Hz, glide {q['g'] * 100:+.2f} % / tau "
                  f"{q['tau'] * 1000:.0f} ms, rise {q['rise']:.1f} dB at {q['t_pk']:.2f} s, t60 {q['t60']:.2f} s")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["measure", "fit", "compare", "tune", "residual"])
    ap.add_argument("synth", nargs="?")
    ap.add_argument("--data", default=str(DATA))
    ap.add_argument("--only", help="fit: these instruments only (comma-separated), the others' tables kept")
    args = ap.parse_args()
    if args.cmd == "measure":
        cmd_measure(args)
    elif args.cmd == "compare":
        cmd_compare(args)
    elif args.cmd == "tune":
        cmd_tune(args)
    elif args.cmd == "residual":
        cmd_residual(args)
    else:
        cmd_fit(args)


if __name__ == "__main__":
    main()
