#!/usr/bin/env python3
"""Activation ceiling of a decomposer cache (track B, docs/polyphonic-plan.md
section 12): the share of reference notes whose pitch ever gets a sufficient
share of the activation sum shortly after the pluck, i.e. the recall that a
perfect note rule could reach, plus the spurious pitches that such a rule
would also see (its precision side).

  python3 test/poly/decomp/ceiling.py --cache <dir> [--cache <dir> ...] --takes <takes.json> --guitarset <mixdir>
                                      [--sets solo,comp,mix2,mix3,hex2,hex3] [--prefix dev_] [--share 0.08] [--json out.json]

For each reference note (midi m, annotated onset t): the hops k with
t + 10 ms <= time(k) < t + 60 ms (time(k) = (k + 1) x hop, the end of the hop);
hit = P[k, m] > share x sum_m' P[k, m'] at some hop k ("any"), at two
consecutive hops ("2 hops"), or with share 2 x --share ("strict").
Spurious = pitches whose share exceeds --share at some hop of the same window
while no reference note of that pitch sounds (onset <= time(k) + 50 ms and
offset >= time(k) - 50 ms), counted once per reference chord (group of
onsets within 50 ms, score.chord_sizes) and reported per chord.
Hit rates are reported by chord size (1 / 2 / 3 / 4 / 5-6) and overall; the
cost column is the mean NMF ms/hop stored by the analysis in the cache.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..'))
import score  # noqa: E402

CLASSES = ['1', '2', '3', '4', '5-6']


def cls_of(sz):
    return CLASSES[min(sz, 5) - 1]


def ceiling_take(c, ref, share, t_lo=0.010, t_hi=0.060, win='medium'):
    P = c['P_' + win]
    hop = float(c['hop_s'])
    K = len(P)
    S = P.sum(axis=1) + 1e-12
    R = P / S[:, None]
    sizes = score.chord_sizes(ref)
    on = np.array([r['onset'] for r in ref])
    off = np.array([r['offset'] for r in ref])
    mi = np.array([int(round(r['midi'])) - 40 for r in ref])
    res = {'hit': {}, 'hit2': {}, 'strict': {}, 'tot': {}, 'spur': 0, 'chords': 0, 'ms': float(c['nmf_s_per_hop_' + win]) * 1e3}
    i = 0
    while i < len(ref):
        j = i
        while j + 1 < len(ref) and ref[j + 1]['onset'] - ref[i]['onset'] <= score.CHORD_WINDOW:
            j += 1
        # window of the chord (first onset) for the spurious count
        t = ref[i]['onset']
        k0 = max(0, int(np.ceil((t + t_lo) / hop)) - 1)
        k1 = min(K, int(np.ceil((t + t_hi) / hop)) - 1)
        if k1 > k0:
            seg = R[k0:k1]
            tk = (np.arange(k0, k1) + 1) * hop
            sounding = np.zeros(44, dtype=bool)
            for q in range(len(ref)):
                if on[q] <= tk[-1] + 0.05 and off[q] >= tk[0] - 0.05 and 0 <= mi[q] < 44:
                    sounding[mi[q]] = True
            exceed = (seg > share).any(axis=0)
            res['spur'] += int((exceed & ~sounding).sum())
            res['chords'] += 1
        for q in range(i, j + 1):
            m = mi[q]
            if not 0 <= m < 44:
                continue
            t = ref[q]['onset']
            k0 = max(0, int(np.ceil((t + t_lo) / hop)) - 1)
            k1 = min(K, int(np.ceil((t + t_hi) / hop)) - 1)
            cl = cls_of(sizes[q])
            res['tot'][cl] = res['tot'].get(cl, 0) + 1
            if k1 <= k0:
                continue
            s = R[k0:k1, m]
            a = s > share
            res['hit'][cl] = res['hit'].get(cl, 0) + int(a.any())
            res['hit2'][cl] = res['hit2'].get(cl, 0) + int((a[1:] & a[:-1]).any())
            res['strict'][cl] = res['strict'].get(cl, 0) + int((s > 2 * share).any())
        i = j + 1
    return res


def add(acc, r):
    for key in ('hit', 'hit2', 'strict', 'tot'):
        for k, v in r[key].items():
            acc[key][k] = acc[key].get(k, 0) + v
    acc['spur'] += r['spur']
    acc['chords'] += r['chords']
    acc['ms'].extend(r['ms'] if isinstance(r['ms'], list) else [r['ms']])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', action='append', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--sets', default='solo,comp,mix2,mix3,hex2,hex3')
    ap.add_argument('--prefix', default='dev_')
    ap.add_argument('--share', type=float, default=0.08)
    ap.add_argument('--json')
    a = ap.parse_args()
    lists = json.load(open(a.takes))
    sets = a.sets.split(',')
    out = {}
    print(f'share > {a.share:.2f} of sum(P) within [+10, +60) ms of the annotated onset, {a.prefix}* lists; '
          f'hit = any hop / 2 consecutive hops / share > {2 * a.share:.2f}; spurious pitches per reference chord; ms/hop = mean NMF cost of the cache')
    for cache in a.cache:
        name = os.path.basename(cache.rstrip('/'))
        settings = json.load(open(os.path.join(cache, 'settings.json'))) if os.path.exists(os.path.join(cache, 'settings.json')) else {}
        out[name] = {'settings': settings, 'sets': {}}
        total = {'hit': {}, 'hit2': {}, 'strict': {}, 'tot': {}, 'spur': 0, 'chords': 0, 'ms': []}
        print(f'\n## {name}  {json.dumps(settings)}')
        print('| set | hit % (n) by chord size 1 / 2 / 3 / 4 / 5-6 | all: any / 2 hops / strict | spurious per chord | ms/hop |')
        print('|---|---|---|---|---|')
        for s in sets:
            acc = {'hit': {}, 'hit2': {}, 'strict': {}, 'tot': {}, 'spur': 0, 'chords': 0, 'ms': []}
            for take in lists[a.prefix + s]:
                z = np.load(os.path.join(cache, take + '.npz'))
                c = {k: z[k] for k in z.files}
                ref = score.read_jams_notes(os.path.join(a.guitarset, 'annotation', take + '.jams'))
                add(acc, ceiling_take(c, ref, a.share))
            add(total, acc)
            out[name]['sets'][s] = acc
            print(row(s, acc))
        out[name]['sets']['all'] = total
        print(row('all', total))
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(out, f, indent=1)


def row(s, acc):
    tot = sum(acc['tot'].values())
    by = ' / '.join(f"{100 * acc['hit'].get(c, 0) / acc['tot'][c]:.0f} ({acc['tot'][c]})" if acc['tot'].get(c) else '-' for c in CLASSES)
    allv = ' / '.join(f"{100 * sum(acc[k].values()) / tot:.1f}" for k in ('hit', 'hit2', 'strict'))
    return f"| {s} | {by} | {allv} | {acc['spur'] / max(acc['chords'], 1):.2f} | {np.mean(acc['ms']):.3f} |"


if __name__ == '__main__':
    main()
