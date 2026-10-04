#!/usr/bin/env python3
"""Compact table + exit gate of the POLY improvement session from the JSON
output of test/score.py (--json), one column per scored events file.

  python3 test/poly/gate.py --score score-test.json [--score score-fresh.json ...]
                            [--gate mix2b,mix3b,hex2b,hex3b,comp2] [--lag 9] [--md out.md]

Table per set: takes / reference notes / emitted notes, F1 +-50 and +-20 ms,
precision and recall +-50 ms, recall by chord size (1 / 2 / 3 / 4 / 5-6, n in
parentheses), ghosts and octave errors (per emitted), corrected latency
median (p25 / p75 / p90) = raw latency + the annotation lag measured by
score.py on the takes.json solo list (median of its lag_ms; --lag when the
file has none).

Gate (docs/polyphonic-plan.md, section 12, "porte de sortie": on takes never
seen by the tuning, dyads >= 70 %, triads >= 60 %, precision >= 80 %): the
sets named by --gate (their labels must contain the name, e.g. "poly-merged
(hex2b)") are POOLED (sum of matched / reference notes of the 2-note chords,
of the 3-note chords, and of matched / emitted notes) and each criterion is
printed with PASS or FAIL, next to its per-set values.
"""
import argparse
import json

import numpy as np


def set_of(label):
    return label[label.rfind('(') + 1:label.rfind(')')] if '(' in label else label


def pct(a, b):
    return 100.0 * a / b if b else float('nan')


def f(x, d=1):
    return 'n/a' if x != x else f'{x:.{d}f}'


def rows_of(path, lag_default):
    d = json.load(open(path))
    lag = float(np.median(d['lag_ms'])) if d.get('lag_ms') else lag_default
    out = []
    for label, r in zip(d['labels'], d['results']):
        t = r['tot']
        P, R = pct(t['m50'], t['est']), pct(t['m50'], t['ref'])
        F = 2 * P * R / (P + R) if P + R > 0 else 0.0
        P20, R20 = pct(t['m20'], t['est']), pct(t['m20'], t['ref'])
        F20 = 2 * P20 * R20 / (P20 + R20) if P20 + R20 > 0 else 0.0
        lat = 1e3 * np.asarray(r['lat']) + lag if len(r['lat']) else np.zeros(0)
        q = (lambda p: f'{np.percentile(lat, p):+.0f}') if len(lat) else (lambda p: 'n/a')
        rec = {k: (f"{pct(r['cls_hit'][k], r['cls_ref'][k]):.1f} ({r['cls_ref'][k]})" if r['cls_ref'][k] else '-') for k in r['cls_ref']}
        out.append({'set': set_of(label), 'label': label, 'r': r,
                    'cells': [set_of(label), f"{len(r['per_take'])}/{t['ref']}/{t['est']}", f(F), f(F20), f(P), f(R),
                              rec['1'], rec['2'], rec['3'], rec['4'], rec['5-6'],
                              f"{r['ghosts']} ({pct(r['ghosts'], t['est']):.1f}%)", f"{r['octave']} ({pct(r['octave'], t['est']):.1f}%)",
                              f"{q(50)} ({q(25)}/{q(75)}/{q(90)})"], 'lag': lag})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--score', action='append', required=True)
    ap.add_argument('--gate', default='', help='comma-separated set names pooled for the gate (empty: no gate)')
    ap.add_argument('--lag', type=float, default=9.0)
    ap.add_argument('--md')
    a = ap.parse_args()
    rows = [r for p in a.score for r in rows_of(p, a.lag)]
    head = ['set', 'takes/ref/emitted', 'F1 ±50', 'F1 ±20', 'P', 'R', 'rec 1', 'rec 2', 'rec 3', 'rec 4', 'rec 5-6', 'ghosts', 'octave',
            'lat. corrected median (p25/p75/p90) ms']
    w = [max(len(str(x)) for x in [head[i]] + [r['cells'][i] for r in rows]) for i in range(len(head))]
    line = lambda c: '| ' + ' | '.join(str(x).ljust(k) for x, k in zip(c, w)) + ' |'
    lines = [line(head), '|' + '|'.join('-' * (k + 2) for k in w) + '|'] + [line(r['cells']) for r in rows]
    lags = sorted({round(r['lag'], 1) for r in rows})
    lines.append(f'\nannotation lag added to the raw latency: {", ".join(f"{x:+.1f}" for x in lags)} ms')
    if a.gate:
        names = a.gate.split(',')
        sel = [r for r in rows if r['set'] in names]
        missing = [n for n in names if n not in {r['set'] for r in sel}]
        if missing:
            raise SystemExit(f'gate sets not found in the scores: {missing}')
        h2 = sum(r['r']['cls_hit']['2'] for r in sel); n2 = sum(r['r']['cls_ref']['2'] for r in sel)
        h3 = sum(r['r']['cls_hit']['3'] for r in sel); n3 = sum(r['r']['cls_ref']['3'] for r in sel)
        m = sum(r['r']['tot']['m50'] for r in sel); e = sum(r['r']['tot']['est'] for r in sel)
        per = lambda key: ', '.join(f"{r['set']} {pct(r['r']['cls_hit'][key], r['r']['cls_ref'][key]):.1f} ({r['r']['cls_ref'][key]})" for r in sel if r['r']['cls_ref'][key])
        perp = ', '.join(f"{r['set']} {pct(r['r']['tot']['m50'], r['r']['tot']['est']):.1f}" for r in sel)
        lines.append(f'\nGATE (section 12) on the pooled sets {"+".join(names)}:')
        for name, val, thr, detail in (('dyads recall >= 70 %', pct(h2, n2), 70, f'{h2}/{n2}; per set: {per("2")}'),
                                       ('triads recall >= 60 %', pct(h3, n3), 60, f'{h3}/{n3}; per set: {per("3")}'),
                                       ('precision >= 80 %', pct(m, e), 80, f'{m}/{e}; per set: {perp}')):
            lines.append(f'- {name}: {val:.1f} % -> {"PASS" if val >= thr else "FAIL"}  ({detail})')
    text = '\n'.join(lines)
    print(text)
    if a.md:
        with open(a.md, 'w') as fh:
            fh.write(text + '\n')


if __name__ == '__main__':
    main()
