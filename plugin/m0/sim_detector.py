"""Re-simulates the M0 attack detector (plugin/m0/PluginProcessor.cpp) on
GuitarSet (mono pickup mix, 360 files) to compare the OLD rule (trigger
> -30 dBFS, re-arm < -40 dBFS once the note is over) with rise detectors:
fast 5 ms mean square > -30 dBFS AND fast > r * slow; re-armed once the
note is over and fast < h * slow (or < -30 dBFS). The prototype uses
r = 2, slow = 30 ms, h = 1.25. Each file is scaled to a -6 dBFS peak (a
well-gained interface); notes are held 600 ms, as in the prototype.
GuitarSet onsets lag the audio by 5-20 ms, hence the -25..+50 ms window.

  python -I plugin/m0/sim_detector.py D:/guitarset

8 October 2026 (rule | precision | attacks caught while no note sounds):
  old -30/-40 dBFS      94.6 %   39.0 %
  r=2 slow=30 ms h=1.25 94.6 %   68.1 %   (lag p90 4.9 ms)
"""
import glob
import json
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

L_S = 0.600


def env(x2, tau, sr):
    c = 1.0 - np.exp(-1.0 / (tau * sr))
    return lfilter([c], [1.0, -(1.0 - c)], x2)


def first_at_or_after(idx, i):
    k = np.searchsorted(idx, i)
    return idx[k] if k < len(idx) else None


def run_old(envF, sr, thr, rearm):
    L = int(round(L_S * sr))
    above = np.flatnonzero(envF > thr)
    below = np.flatnonzero(envF < rearm)
    ons, e = [], 0
    while True:
        j = first_at_or_after(below, e)        # re-arm
        if j is None:
            break
        k = first_at_or_after(above, j)
        if k is None:
            break
        ons.append(k)
        e = k + L
    return ons


def run_new(rising, sr, quiet=None):
    L = int(round(L_S * sr))
    r_idx = np.flatnonzero(rising)
    q_idx = np.flatnonzero(~rising if quiet is None else quiet)
    ons, e = [], 0
    while True:
        j = first_at_or_after(q_idx, e)        # armed: one sample without a rise
        if j is None:
            break
        k = first_at_or_after(r_idx, j + 1)
        if k is None:
            break
        ons.append(k)
        e = k + L
    return ons


def clusters(onsets, gap=0.05):
    out = []
    for t in sorted(onsets):
        if not out or t - out[-1][-1] > gap:
            out.append([t])
        else:
            out[-1].append(t)
    return [c[0] for c in out]


def score(trig_t, att):
    att = np.asarray(att)
    ok = 0
    for t in trig_t:
        if np.any((att - 0.025 <= t) & (t <= att + 0.05)):
            ok += 1
    # eligible attacks: no note sounding at the attack
    elig = det = 0
    trig = np.asarray(trig_t)
    for c in att:
        prev = trig[trig < c - 0.025]
        if len(prev) and c < prev[-1] + L_S + 0.005:
            continue
        elig += 1
        if np.any((trig >= c - 0.025) & (trig <= c + 0.05)):
            det += 1
    return ok, len(trig_t), det, elig


def main():
    root = sys.argv[1]
    files = sorted(glob.glob(os.path.join(root, 'audio_mono-pickup_mix', '*.wav')))
    thr, rearm = 10 ** (-30 / 10), 10 ** (-40 / 10)
    variants = {'old -30/-40': None}
    for r, ts, h in ((2.0, 0.03, 2.0), (2.0, 0.03, 1.5), (2.0, 0.03, 1.25), (2.0, 0.05, 1.5), (1.5, 0.03, 1.25)):
        variants[f'new r={r} slow={int(ts*1000)}ms rearm<{h}'] = (r, ts, h)
    tot = {k: np.zeros(4, int) for k in variants}
    lag = {k: [] for k in variants}
    per_kind = {k: {'solo': np.zeros(4, int), 'comp': np.zeros(4, int)} for k in variants}
    peaks = []
    for f in files:
        base = os.path.basename(f).replace('_mix.wav', '')
        jams = json.load(open(os.path.join(root, 'annotation', base + '.jams'), encoding='utf-8'))
        onsets = [d['time'] for a in jams['annotations'] if a['namespace'] == 'note_midi' for d in a['data']]
        att = clusters(onsets)
        x, sr = sf.read(f, dtype='float64')
        if x.ndim > 1:
            x = x[:, 0]
        pk = np.max(np.abs(x))
        peaks.append(20 * np.log10(pk))
        x = x * (0.5 / pk)
        x2 = x * x
        envF = env(x2, 0.005, sr)
        slows = {}
        kind = 'solo' if base.endswith('solo') else 'comp'
        for name, v in variants.items():
            if v is None:
                ons = run_old(envF, sr, thr, rearm)
            else:
                r, ts, h = v
                if ts not in slows:
                    slows[ts] = env(x2, ts, sr)
                rising = (envF > thr) & (envF > r * slows[ts])
                quiet = ~((envF > thr) & (envF > h * slows[ts]))
                ons = run_new(rising, sr, quiet)
            tt = [o / sr for o in ons]
            s = np.array(score(tt, att))
            tot[name] += s
            per_kind[name][kind] += s
            a = np.asarray(att)
            for t in tt:
                d = t - a
                d = d[(d >= -0.025) & (d <= 0.05)]
                if len(d):
                    lag[name].append(1000 * d.min())
    print(f'{len(files)} files, raw peak {np.median(peaks):.1f} dBFS median (min {min(peaks):.1f}), scaled to -6 dBFS\n')
    print('| rule | triggers | on a real attack (precision) | free attacks caught (recall) | solo recall | comp recall | lag median / p90 ms |')
    print('|---|---|---|---|---|---|---|')
    for name in variants:
        ok, n, det, el = tot[name]
        so, co = per_kind[name]['solo'], per_kind[name]['comp']
        lg = np.array(lag[name])
        print(f'| {name} | {n} | {100*ok/max(n,1):.1f} % | {100*det/max(el,1):.1f} % ({det}/{el}) | '
              f'{100*so[2]/max(so[3],1):.1f} % | {100*co[2]/max(co[3],1):.1f} % | '
              f'{np.median(lg):.1f} / {np.percentile(lg, 90):.1f} |')


if __name__ == '__main__':
    main()
