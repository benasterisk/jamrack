"""Re-simulates the M0 attack detector (plugin/m0/PluginProcessor.cpp) on
GuitarSet (mono pickup mix, 360 files) to compare the OLD rule (trigger
> -30 dBFS, re-arm < -40 dBFS once the note is over) with rise detectors:
fast 5 ms mean square > a floor AND fast > r * slow; re-armed once the
note is over and fast < h * slow (or under the floor). The prototype uses
r = 2, slow = 30 ms, h = 1.25, floor -45 dBFS. Each file is scaled to
several peak levels; notes are held 600 ms, as in the prototype.
GuitarSet onsets lag the audio by 5-20 ms, hence the -25..+50 ms window.

  python -I plugin/m0/sim_detector.py D:/guitarset

8 October 2026, attacks caught while no note sounds at a -6 / -12 / -18 /
-24 dBFS peak (precision 87-96 % for the chosen rule):
  old -30 / re-arm -40 dBFS          39 / 37 / 13 / 0 %
  rise r=2, 30 ms, h=1.25, -30 dBFS  68 / 48 / 13 / 0 %
  rise r=2, 30 ms, h=1.25, -45 dBFS  72 / 73 / 71 / 61 %   (the prototype)
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
    levels = (-6, -12, -18, -24)                 # file peak, dBFS
    rules = {'old -30/-40 dBFS': ('old', -30.0, -40.0)}
    for r, ts, h in ((2.0, 0.03, 2.0), (2.0, 0.03, 1.5), (2.0, 0.03, 1.25), (2.0, 0.05, 1.5), (1.5, 0.03, 1.25)):
        rules[f'rise r={r} slow={int(ts*1000)}ms rearm<{h}, floor -30'] = ('new', -30.0, r, ts, h)
    for floor in (-40.0, -45.0, -50.0):
        rules[f'rise r=2.0 slow=30ms rearm<1.25, floor {int(floor)}'] = ('new', floor, 2.0, 0.03, 1.25)
    tot = {(k, L): np.zeros(4, int) for k in rules for L in levels}
    lag = {(k, L): [] for k in rules for L in levels}
    for f in files:
        base = os.path.basename(f).replace('_mix.wav', '')
        jams = json.load(open(os.path.join(root, 'annotation', base + '.jams'), encoding='utf-8'))
        att = clusters([d['time'] for a in jams['annotations'] if a['namespace'] == 'note_midi' for d in a['data']])
        x, sr = sf.read(f, dtype='float64')
        if x.ndim > 1:
            x = x[:, 0]
        x = x * (0.5 / np.max(np.abs(x)))          # -6 dBFS peak; other levels scale the thresholds
        x2 = x * x
        envF = env(x2, 0.005, sr)
        slows = {}
        a = np.asarray(att)
        for L in levels:
            g2 = 10 ** ((L + 6) / 10)              # mean-square gain of that level
            for name, v in rules.items():
                if v[0] == 'old':
                    ons = run_old(envF, sr, 10 ** (v[1] / 10) / g2, 10 ** (v[2] / 10) / g2)
                else:
                    _, floor, r, ts, h = v
                    if ts not in slows:
                        slows[ts] = env(x2, ts, sr)
                    thr = 10 ** (floor / 10) / g2
                    rising = (envF > thr) & (envF > r * slows[ts])
                    quiet = ~((envF > thr) & (envF > h * slows[ts]))
                    ons = run_new(rising, sr, quiet)
                tt = [o / sr for o in ons]
                tot[(name, L)] += np.array(score(tt, att))
                for t in tt:
                    d = t - a
                    d = d[(d >= -0.025) & (d <= 0.05)]
                    if len(d):
                        lag[(name, L)].append(1000 * d.min())
    print('window: trigger within -25..+50 ms of an annotated attack (GuitarSet onsets lag the audio by 5-20 ms)')
    print(f'{len(files)} files, scaled to a peak of {", ".join(str(L) for L in levels)} dBFS')
    print('cells: attacks caught while no note sounds (recall) / notes on a real attack (precision) / lag median ms')
    print()
    print('| rule | ' + ' | '.join(f'peak {L} dBFS' for L in levels) + ' |')
    print('|---|' + '---|' * len(levels))
    for name in rules:
        cells = []
        for L in levels:
            ok, n, det, el = tot[(name, L)]
            lg = lag[(name, L)]
            cells.append(f'{100*det/max(el,1):.1f} % / {100*ok/max(n,1):.1f} % / {np.median(lg) if lg else float("nan"):.1f}')
        print(f'| {name} | ' + ' | '.join(cells) + ' |')


if __name__ == '__main__':
    main()
