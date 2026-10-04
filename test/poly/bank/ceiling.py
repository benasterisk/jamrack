#!/usr/bin/env python3
"""Activation ceiling of a decomposer cache, per bank: share of the reference
notes whose pitch activation exceeds abs_on AND frac x sum(P) at some hop
within [+10, +60] ms of the annotated onset (medium window), by chord size,
on the dev_* or test_* lists of a takes.json (the previous session's
diagnostic, docs/polyphonic-plan.md section 12: "only 83 % of hex2 dyad notes
and 54 % of comp notes ever get > 8 %").

  python3 test/poly/bank/ceiling.py --cache <dir> --takes <takes.json> --guitarset <mixdir> [--lists dev|test] [--abs 0.01] [--frac 0.08]
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
import notes  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--lists', default='dev')
    ap.add_argument('--sets', default='solo,comp,mix2,mix3,hex2,hex3')
    ap.add_argument('--abs', type=float, default=0.01)
    ap.add_argument('--frac', type=float, default=0.08)
    ap.add_argument('--win', default='medium')
    a = ap.parse_args()
    lists = json.load(open(a.takes))
    out = {}
    for k in a.sets.split(','):
        hit, tot = {}, {}
        for take in lists[a.lists + '_' + k]:
            c = notes.load_cache(a.cache, take)
            ref = score.read_jams_notes(os.path.join(a.guitarset, 'annotation', take + '.jams'))
            P = c['P_' + a.win]
            hop = float(c['hop_s'])
            Ssum = P.sum(axis=1)
            K = len(P)
            sizes = score.chord_sizes(ref)
            for r, sz in zip(ref, sizes):
                m = int(round(r['midi'])) - 40
                if not 0 <= m < P.shape[1]:
                    continue
                k0, k1 = int((r['onset'] + 0.01) / hop), min(K, int((r['onset'] + 0.06) / hop))
                seg = P[k0:k1, m]
                ok = np.any((seg > a.abs) & (seg > a.frac * Ssum[k0:k1]))
                cls = min(sz, 5)
                tot[cls] = tot.get(cls, 0) + 1
                hit[cls] = hit.get(cls, 0) + int(ok)
        out[k] = {str(c): [hit[c], tot[c]] for c in tot}
        print(f'{a.lists} {k:5s}: ' + '  '.join(f'size {c if c < 5 else "5-6"}: {100 * hit[c] / tot[c]:.0f} % ({tot[c]})' for c in sorted(tot))
              + f'   all: {100 * sum(hit.values()) / sum(tot.values()):.0f} %')
    print(json.dumps(out))


if __name__ == '__main__':
    main()
