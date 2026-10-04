#!/usr/bin/env python3
"""Note logic of the POLY prototype (jalon 1 bis): turns the cached pitch
activations of analyze.py into note events (test/dump-events.mjs JSON
format, scored by test/score.py), and grid-searches its thresholds on the
DEV subset of a takes.json produced by make_mixes.py.

  python3 test/poly/notes.py events --cache <dir> --takes <takes.json> --set mix2 --out events-poly-mix2.json [--params p.json]
  python3 test/poly/notes.py grid   --cache <dir> --takes <takes.json> --guitarset <mixdir> --out best.json [--jobs 4]

Rules (docs/polyphonic-plan.md, section 3, "logique de notes"):
  onset   = log-spectral flux >= flux_thr AND the level rose >= rise_db over
            where it was 8-16 ms ago, or flux >= 2 x flux_thr alone (a note
            over sounding strings may not move the level); refractory 10 hops
            (27 ms) that a 1.5 x stronger flux may override during the first
            4 hops. Each onset opens a decision window of 15 hops (40 ms).
  note-on for pitch m at hop t inside the window of onset t_o:
            P[t, m] > abs_on and P[t-1, m] > abs_on            (absolute)
            P[t, m] > frac * sum(P[t]) and same at t-1         (relative, 2 consecutive hops)
            P[t, m] >= rise_x * P[t_o - 4, m]                   (rise since before the onset)
            ... for `agree` consecutive hops (2 in the plan), and not before
            min_delay hops after the onset (the pick transient)
            odd/even partial energy of m >= oct_odd             (sub-octave guard)
            (p1 + p2) / (p3 + .. + p6) of m >= low_guard        (fundamental present: sub-harmonic guard)
            not (a voice sounds at m-12 and P[t, m] < oct_up * P[t, m-12])  (octave-up ghost)
            not (a voice or candidate sounds 12/19/24/28/31 under m with P > harm_up * P[t, m])  (harmonic ghost)
            one note per pitch (templates of the same midi are summed by
            analyze.py), at most one note-on per pitch per onset window,
            at most max_voices voices (the weakest is released otherwise).
            A sounding pitch is re-struck by the same rule (repick).
  note-off when the voice activation stays release_db under its peak, or
            under abs_on / 4, for off_hops consecutive hops (9 = 24 ms).
  Times are stamped at the end of the hop that made the decision (what a
  host would see), i.e. (t + 1) * 2.67 ms.
"""
import argparse
import itertools
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

DEFAULT = dict(win='medium', off_win='medium', abs_on=0.02, frac=0.12, rise_x=3.0, flux_thr=3.0, rise_db=6.0,
               release_db=24.0, off_hops=9, window_hops=15, refractory=10, oct_odd=0.25, oct_up=0.5, max_voices=6,
               agree=2, min_delay=0, low_guard=0.0, harm_up=0.0)
HARMONIC_INTERVALS = (12, 19, 24, 28, 31)   # semitones under m whose partials 2..6 land on m
NPITCH = 44


def load_cache(cache, take):
    z = np.load(os.path.join(cache, take + '.npz'))
    return {k: z[k] for k in z.files}


def detect_onsets(level, flux, p):
    """Hop indices of the onsets (refractory applied)."""
    K = len(level)
    past = np.full(K, np.inf)
    for d in range(3, 7):
        past[d:] = np.minimum(past[d:], level[:-d])
    rise = level - past
    raw = ((flux >= p['flux_thr']) & (rise >= p['rise_db'])) | (flux >= 2 * p['flux_thr'])
    onsets = []
    last, last_flux = -10 ** 9, 0.0
    for t in np.nonzero(raw)[0]:
        if t - last >= p['refractory'] or (t - last <= 4 and flux[t] >= 1.5 * last_flux):
            onsets.append(int(t))
            last, last_flux = t, flux[t]
    return np.array(onsets, dtype=int)


def run_take(c, p):
    """Note events of one take from its cache dict and the params."""
    P = c['P_' + p['win']]
    Poff = c['P_' + p['off_win']]
    odd = c['odd_' + p['win']]
    hop = float(c['hop_s'])
    K = len(P)
    onsets = detect_onsets(c['level'], c['flux'], p)
    # onset window bookkeeping, vectorised
    onset_id = np.full(K, -1, dtype=int)
    onset_hop = np.full(K, -10 ** 6, dtype=int)
    if len(onsets):
        idx = np.searchsorted(onsets, np.arange(K), side='right') - 1
        ok = idx >= 0
        onset_id[ok] = idx[ok]
        onset_hop[ok] = onsets[idx[ok]]
    in_win = (np.arange(K) - onset_hop) <= p['window_hops']
    in_win &= onset_id >= 0
    S = P.sum(axis=1)
    before = np.zeros_like(P)
    bh = np.clip(onset_hop - 4, 0, K - 1)
    before[in_win] = P[bh[in_win]]
    cond = (P > p['abs_on']) & (P > p['frac'] * S[:, None])
    base = cond.copy()
    for d in range(1, int(p['agree'])):
        cond[d:] &= base[:-d]
    cond &= in_win[:, None]
    if p['min_delay'] > 0:
        cond &= ((np.arange(K) - onset_hop) >= p['min_delay'])[:, None]
    if p['low_guard'] > 0:
        cond &= c['low_' + p['win']] >= p['low_guard']
    cond &= P >= p['rise_x'] * np.maximum(before, p['abs_on'] / 4)
    cond &= odd >= p['oct_odd']
    cand_hops = np.nonzero(cond.any(axis=1))[0]
    cand_set = set(cand_hops.tolist())
    off_gain = 10 ** (-p['release_db'] / 20)
    floor = p['abs_on'] / 4
    voices = {}       # m -> dict(on=hop, peak, below, vel)
    fired = set()     # (onset id, pitch) pairs already fired
    events = []

    def note_off(m, t):
        v = voices.pop(m)
        events.append({'onset': (v['on'] + 1) * hop, 'offset': (t + 1) * hop, 'midi': int(m + 40),
                       'velocity': v['vel'], 'why': 'nmf'})

    for t in range(K):
        # note-off
        if voices:
            row = Poff[t]
            for m in list(voices):
                v = voices[m]
                x = row[m]
                if x > v['peak']:
                    v['peak'] = x
                if x < v['peak'] * off_gain or x < floor:
                    v['below'] += 1
                    if v['below'] >= p['off_hops']:
                        note_off(m, t)
                else:
                    v['below'] = 0
        if t not in cand_set:
            continue
        oid = onset_id[t]
        row = P[t]
        for m in np.nonzero(cond[t])[0]:
            m = int(m)
            v = voices.get(m)
            if (oid, m) in fired:
                continue
            if v is not None and t - v['on'] < p['refractory']:
                continue
            if m - 12 >= 0 and (m - 12) in voices and p['oct_up'] > 0 and row[m] < p['oct_up'] * row[m - 12]:
                continue
            if p['harm_up'] > 0 and any(m - d >= 0 and ((m - d) in voices or cond[t, m - d]) and row[m - d] > p['harm_up'] * row[m]
                                        for d in HARMONIC_INTERVALS):
                continue
            if v is not None:
                note_off(m, t)
            if len(voices) >= p['max_voices']:
                weakest = min(voices, key=lambda q: Poff[t][q])
                if Poff[t][weakest] >= row[m]:
                    continue
                note_off(weakest, t)
            vel = float(np.clip((20 * np.log10(max(row[m], 1e-9)) + 50) / 30, 0.1, 1.0))
            voices[m] = {'on': t, 'peak': row[m], 'below': 0, 'vel': vel}
            fired.add((oid, m))
    for m in list(voices):
        note_off(m, K - 1)
    events.sort(key=lambda e: e['onset'])
    return {'duration': float(c['duration']), 'events': events}


def build_events(cache, takes, p, label='poly', set_name=None):
    out = {'label': label, 'source': 'test/poly (generic templates + sparse beta-NMF)', 'params': p, 'takes': {}}
    if set_name:
        out['set'] = set_name
    for t in takes:
        out['takes'][t] = run_take(load_cache(cache, t), p)
    return out


# ------------------------------------------------------------------ grid
def _f1(ev, refs):
    import score
    m = est = ref = 0
    for take, r in ev['takes'].items():
        for e in r['events']:
            if e['offset'] <= e['onset']:
                e['offset'] = e['onset'] + 1e-3
        pairs = score.match(refs[take], r['events'], 0.05, False)
        m += len(pairs)
        est += len(r['events'])
        ref += len(refs[take])
    return score.prf(m, est, ref)


def _grid_job(args):
    p, sets, caches, refs = args
    res = {}
    for name, takes in sets.items():
        ev = {'takes': {t: run_take(caches[t], p) for t in takes}}
        res[name] = _f1(ev, refs)
    return p, res


def grid(a):
    import score
    lists = json.load(open(a.takes))
    sets = {k: lists['dev_' + k] for k in ('solo', 'comp', 'mix2', 'mix3', 'hex2', 'hex3') if 'dev_' + k in lists}
    all_takes = sorted({t for v in sets.values() for t in v})
    caches = {t: load_cache(a.cache, t) for t in all_takes}
    refs = {t: score.read_jams_notes(os.path.join(a.guitarset, 'annotation', t + '.jams')) for t in all_takes}
    space = json.load(open(a.space)) if a.space else {
        'abs_on': [0.01, 0.02, 0.04], 'frac': [0.12, 0.16, 0.22], 'rise_x': [2.0], 'flux_thr': [2.0], 'release_db': [20.0],
        'oct_up': [0.5], 'agree': [2, 4], 'min_delay': [0, 4], 'low_guard': [0.0, 0.3], 'harm_up': [0.0, 1.0]}
    keys = list(space)
    combos = [dict(DEFAULT, **dict(zip(keys, vals))) for vals in itertools.product(*[space[k] for k in keys])]
    print(f'{len(combos)} combinations x {len(all_takes)} dev takes', file=sys.stderr)
    from multiprocessing import Pool
    rows = []
    with Pool(a.jobs) as pool:
        for i, (p, res) in enumerate(pool.imap_unordered(_grid_job, [(p, sets, caches, refs) for p in combos], chunksize=2)):
            obj = float(np.mean([res[k][2] for k in sets]))
            rows.append((obj, p, res))
            if (i + 1) % 20 == 0:
                print(f'{i + 1}/{len(combos)} best so far {max(r[0] for r in rows):.3f}', file=sys.stderr)
    rows.sort(key=lambda r: -r[0])
    for obj, p, res in rows[:15]:
        print(f"{obj:.3f}  " + ' '.join(f"{k}={res[k][2]:.3f}" for k in sets) + '  ' + ' '.join(f'{k}={p[k]}' for k in keys))
    with open(a.out, 'w') as f:
        json.dump({'best': rows[0][1], 'objective': 'mean F1 +-50 ms over the dev lists ' + '/'.join(sets), 'space': space,
                   'top': [{'objective': o, 'params': p, 'f1': res} for o, p, res in rows[:30]]}, f, indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['events', 'grid'])
    ap.add_argument('--cache', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--set')
    ap.add_argument('--out', required=True)
    ap.add_argument('--params', help='JSON file with the thresholds (grid output or a dict)')
    ap.add_argument('--label', default='poly')
    ap.add_argument('--guitarset')
    ap.add_argument('--space', help='JSON grid space')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    if a.cmd == 'grid':
        grid(a)
        return
    p = dict(DEFAULT)
    if a.params:
        d = json.load(open(a.params))
        p.update(d.get('best', d))
    lists = json.load(open(a.takes))
    ev = build_events(a.cache, lists[a.set], p, a.label, a.set)
    with open(a.out, 'w') as f:
        json.dump(ev, f)
    n = sum(len(t['events']) for t in ev['takes'].values())
    print(f'wrote {a.out}: {len(ev["takes"])} takes, {n} notes', file=sys.stderr)


if __name__ == '__main__':
    main()
