#!/usr/bin/env python3
"""Scores the POLY and mono event files of the jalon 1 bis prototype with
test/score.py on every set, on all takes and on the held-out TEST takes
(the thresholds were grid-searched on the DEV takes of <mixdir>/takes.json),
and prints a compact Markdown summary plus the full score.py tables.

  python3 test/poly/report.py --mixdir <mixdir> --out <dir> [--sets solo,comp,mix2,mix3,hex2,hex3]

Expects <dir>/events-poly-<set>.json and (for mix2/mix3/hex2/hex3)
<dir>/events-mono-<set>.json; the mono solo/comp files come from the bench
(docs/polyphonic-plan.md section 11) if present as <dir>/events-mono-<set>.json.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCORE = os.path.join(HERE, '..', 'score.py')


def subset(path, takes, out):
    d = json.load(open(path))
    d['takes'] = {t: v for t, v in d['takes'].items() if t in takes}
    d['label'] = d.get('label', '') + '/test'
    with open(out, 'w') as f:
        json.dump(d, f)
    return out


def score(mixdir, files, json_out):
    cmd = [sys.executable, SCORE, '--guitarset', mixdir, '--takes', os.path.join(mixdir, 'takes.json'), '--json', json_out] + files
    table = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    return table, json.load(open(json_out))


def summary_rows(res):
    tot, cls_hit, cls_ref = res['tot'], res['cls_hit'], res['cls_ref']
    p = tot['m50'] / tot['est'] if tot['est'] else float('nan')
    r = tot['m50'] / tot['ref'] if tot['ref'] else float('nan')
    f = 2 * p * r / (p + r) if p + r > 0 else 0
    rec = {k: (f"{100 * cls_hit[k] / cls_ref[k]:.1f} % ({cls_ref[k]})" if cls_ref[k] else 'n/a') for k in cls_ref}
    return dict(P=p, R=r, F1=f, rec=rec, ghosts=res['ghosts'], octave=res['octave'], est=tot['est'], ref=tot['ref'], n=len(res['per_take']))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--mixdir', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--sets', default='solo,comp,mix2,mix3,hex2,hex3')
    a = ap.parse_args()
    lists = json.load(open(os.path.join(a.mixdir, 'takes.json')))
    sets = a.sets.split(',')
    rows = []
    tables = []
    for s in sets:
        for engine in ('poly', 'mono'):
            path = os.path.join(a.out, f'events-{engine}-{s}.json')
            if not os.path.exists(path):
                continue
            files = [path, subset(path, set(lists['test_' + s]), os.path.join(a.out, f'events-{engine}-{s}-test.json'))]
            table, js = score(a.mixdir, files, os.path.join(a.out, f'score-{engine}-{s}.json'))
            tables.append(f'### {engine} {s}\n\n' + table)
            for label, res in zip(('all', 'test'), js['results']):
                sm = summary_rows(res)
                rows.append([s, engine, label, f"{sm['n']} / {sm['ref']} / {sm['est']}", f"{100 * sm['P']:.1f}", f"{100 * sm['R']:.1f}", f"{100 * sm['F1']:.1f}",
                             sm['rec']['1'], sm['rec']['2'], sm['rec']['3'], sm['rec']['4'], sm['rec']['5-6'],
                             f"{sm['ghosts']} ({100 * sm['ghosts'] / max(sm['est'], 1):.1f} %)", f"{sm['octave']} ({100 * sm['octave'] / max(sm['est'], 1):.1f} %)"])
    head = ['set', 'engine', 'takes', 'n / ref / emitted', 'P %', 'R %', 'F1 %', 'rec 1', 'rec 2', 'rec 3', 'rec 4', 'rec 5-6', 'ghosts', 'octave']
    w = [max(len(str(r[i])) for r in [head] + rows) for i in range(len(head))]
    line = lambda r: '| ' + ' | '.join(str(c).ljust(x) for c, x in zip(r, w)) + ' |'
    print(line(head))
    print('|' + '|'.join('-' * (x + 2) for x in w) + '|')
    for r in rows:
        print(line(r))
    print()
    print('\n'.join(tables))


if __name__ == '__main__':
    main()
