#!/usr/bin/env python3
"""Onset -> note-on latency of scored event files, by chord size (track B,
docs/polyphonic-plan.md section 12): the per-onset decision delays the
note-on, this prints the price next to the F1.

  python3 test/poly/decomp/latency.py --guitarset <mixdir> --takes <takes.json> [--lag 9] [--list test_solo] ev1.json [ev2.json ...]

For every reference note matched by test/score.py's primary matching
(mir_eval, +-50 ms, pitch +-50 cents, no offsets): raw latency = emitted
onset - annotated onset; corrected = raw + lag, the annotation lag of
GuitarSet measured by score.py (annotated onset - energy onset, +9 ms median
on 1449 isolated solo attacks, section 11 of the plan): corrected = emitted
note-on - energy onset, i.e. what the player feels. Notes are grouped by
chord size (score.chord_sizes: onsets within 50 ms); the columns are median
(p25 / p75 / p90) in ms, corrected, for all matched notes and by size, and
the share of reference notes matched. --list restricts the takes to one list
of takes.json (e.g. test_solo); the file's own takes otherwise.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import score  # noqa: E402

CLASSES = ['1', '2', '3', '4', '5-6']


def latencies(events, refs):
    out = {c: [] for c in CLASSES}
    out['all'] = []
    n_ref = 0
    for take, ref in refs.items():
        if take not in events['takes']:
            continue
        est = events['takes'][take]['events']
        n_ref += len(ref)
        sizes = score.chord_sizes(ref)
        for r, e in score.match(ref, est, 0.05, False):
            d = est[e]['onset'] - ref[r]['onset']
            out['all'].append(d)
            out[CLASSES[min(sizes[r], 5) - 1]].append(d)
    return out, n_ref


def fmt(v, lag):
    if not len(v):
        return '-'
    v = 1e3 * np.asarray(v) + lag
    return f"{np.median(v):+.0f} ({np.percentile(v, 25):+.0f} / {np.percentile(v, 75):+.0f} / {np.percentile(v, 90):+.0f}) n={len(v)}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('events', nargs='+')
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--lag', type=float, default=9.0, help='annotation lag in ms added to the raw latency (default 9)')
    ap.add_argument('--list', help='takes.json list to restrict the takes to')
    ap.add_argument('--json')
    a = ap.parse_args()
    lists = json.load(open(a.takes))
    rows = []
    print(f'corrected latency = emitted note-on - annotated onset + {a.lag:.0f} ms (annotation lag); median (p25 / p75 / p90) ms, n matched notes')
    print('| file | matched / ref | all | size 1 | size 2 | size 3 | size 4 | size 5-6 |')
    print('|---|---|---|---|---|---|---|---|')
    out = {}
    for path in a.events:
        ev = score.read_events(path)
        takes = [t for t in ev['takes'] if not a.list or t in set(lists[a.list])]
        refs = {t: score.read_jams_notes(os.path.join(a.guitarset, 'annotation', t + '.jams')) for t in takes}
        lat, n_ref = latencies(ev, refs)
        name = (ev.get('label') or os.path.basename(path)) + (f" ({ev['set']})" if ev.get('set') else '') + (f' [{a.list}]' if a.list else '')
        print(f"| {name} | {len(lat['all'])} / {n_ref} | " + ' | '.join(fmt(lat[c], a.lag) for c in ['all'] + CLASSES) + ' |')
        out[name] = {c: [1e3 * float(x) + a.lag for x in lat[c]] for c in lat}
    if a.json:
        with open(a.json, 'w') as f:
            json.dump(out, f)


if __name__ == '__main__':
    main()
