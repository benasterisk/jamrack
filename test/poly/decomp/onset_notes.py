#!/usr/bin/env python3
"""Per-onset note decision (track B, docs/polyphonic-plan.md section 12,
improvement track 3): instead of notes.py's per-hop rule (a pitch fires at
the first two consecutive hops where it holds 12-16 % of the activation sum
and has risen since the onset), every detected onset opens a window whose
activations are INTEGRATED, and all the pitches of that window are decided
once, at onset + `decide` hops (8..15 hops = 21..40 ms).

  python3 test/poly/decomp/onset_notes.py events --cache <dir> --takes <takes.json> --set mix2 --out ev.json [--params p.json]
  python3 test/poly/decomp/onset_notes.py grid   --cache <dir> --takes <takes.json> --guitarset <mixdir> --out best.json
                                                 [--params base.json] [--space space.json] [--jobs 4]

Same cache (analyze.py or decomp.py), same onset detector (notes.detect_onsets:
flux + level rise, refractory 10 hops with the 4-hop override), same note-off
(voice activation `release_db` under its peak, or under abs_on / 4, for
off_hops hops), same event format and the same grid machinery as notes.py
(DEV lists only, objective = mean F1 +-50 ms over the dev sets).

Decision for the window of an onset at hop t_o, at hop t_d = t_o + decide, over
the hops t = max(t_o+1, t_d+1-integ) .. t_d (the last `integ` hops of the
window: a note that arrives late in a strum is not diluted by the hops before
it), per pitch m (P = pitch activations, S = their sum per hop, R = P / S):
  int_share  = sum_t P[t,m] / sum_t S[t]      >= share         (integrated share)
  peak_share = max_t R[t,m]                   >= peak_share    (0 = off)
  stat(P)                                     >  abs_on        (stat = mean of the last 3 hops, or the max over the hops)
  stat(P)                                     >= rise_x * max(P[t_o-4, m], abs_on/4)   (growth since before the onset)
  mean P second half / mean P first half      >= growth        (0 = off; a new note grows, a sustained one does not)
An optional middle checkpoint (mid > 0) applies the same decision at
t_o + mid with the stricter threshold mid_share, so that clear notes leave
earlier; a pitch fired there is not decided again at t_d.
  mean odd/even partial ratio over the window >= oct_odd       (sub-octave guard)
  mean (p1+p2)/(p3..p6) over the window       >= low_guard     (0 = off)
  not (m-12 is a voice or a candidate with int_share[m] < oct_up * int_share[m-12])   (octave-up ghost)
  not (a voice or candidate 12/19/24/28/31 under m has int_share > harm_up * int_share[m])  (0 = off)
Candidates are taken in decreasing int_share; a sounding pitch is re-struck
(note-off + note-on) if its last note-on is >= refractory hops old; at most
max_voices voices (the weakest is released if weaker than the candidate).
A new onset within merge_hops of a pending window's onset does not open a
second window: it is absorbed and the pending decision is postponed to the
new onset + decide (a strum is one decision). A sounding voice is re-struck
by a window only if its growth since before the onset is >= repick_x. Early path (early_share > 0): inside
a pending window, a pitch whose per-hop share is >= early_share at two
consecutive hops (abs_on, rise_x and oct_odd checked at that hop) fires at
once; the window decides the rest at t_o + decide. Note-on times are stamped
at the end of the deciding hop, (t + 1) x hop.
"""
import argparse
import itertools
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..'))
import notes as N  # noqa: E402

DEFAULT = dict(win='medium', off_win='medium', decide=15, share=0.10, integ=6, mid=0, mid_share=0.25, peak_share=0.0, abs_on=0.008,
               stat='end', rise_x=1.2, repick_x=2.0, growth=0.0, early_share=0.0, merge_hops=4, flux_thr=2.0, rise_db=6.0, refractory=10, release_db=20.0,
               off_hops=9, oct_odd=0.25, oct_up=0.5, harm_up=0.0, low_guard=0.0, max_voices=6)
HARMONIC_INTERVALS = N.HARMONIC_INTERVALS
EPS = 1e-12


def load_params(path=None):
    p = dict(DEFAULT)
    if path:
        d = json.load(open(path))
        d = d.get('best', d)
        unknown = [k for k in d if not k.startswith('_') and k not in DEFAULT]
        if unknown:
            raise SystemExit(f'{path}: unknown thresholds {unknown}')
        p.update({k: v for k, v in d.items() if not k.startswith('_')})
    return p


def run_take(c, p):
    P = c['P_' + p['win']]
    Poff = c['P_' + p['off_win']]
    odd = c['odd_' + p['win']]
    low = c['low_' + p['win']] if p['low_guard'] > 0 else None
    hop = float(c['hop_s'])
    K = len(P)
    S = P.sum(axis=1) + EPS
    R = P / S[:, None]
    onsets = N.detect_onsets(c['level'], c['flux'], p)
    D = int(p['decide'])
    floor_before = p['abs_on'] / 4
    off_gain = 10 ** (-p['release_db'] / 20)
    floor = p['abs_on'] / 4
    voices = {}       # m -> dict(on, peak, below, vel)
    pending = []      # dicts: t_o, t_d, fired (set of pitches)
    events = []
    onset_set = {}
    for t in onsets:
        onset_set[int(t)] = True

    def note_off(m, t):
        v = voices.pop(m)
        events.append({'onset': (v['on'] + 1) * hop, 'offset': (t + 1) * hop, 'midi': int(m + 40),
                       'velocity': v['vel'], 'why': 'nmf'})

    def velocity(x):
        return float(np.clip((20 * np.log10(max(x, 1e-9)) + 50) / 30, 0.1, 1.0))

    def fire(m, t, strength, weight, rise):
        """note-on of pitch m at hop t; strength = the value compared with max_voices' weakest;
        rise = growth since before the onset (a sounding voice is re-struck only if rise >= repick_x)."""
        v = voices.get(m)
        if v is not None:
            if t - v['on'] < p['refractory'] or rise < p['repick_x']:
                return False
            note_off(m, t)
        if len(voices) >= p['max_voices']:
            weakest = min(voices, key=lambda q: Poff[t][q])
            if Poff[t][weakest] >= strength:
                return False
            note_off(weakest, t)
        voices[m] = {'on': t, 'peak': strength, 'below': 0, 'vel': velocity(weight)}
        return True

    def decide(w, t, share):
        t0, t1 = max(w['t_o'] + 1, t + 1 - int(p['integ'])), t + 1     # the last `integ` hops of t_o+1 .. t
        seg = P[t0:t1]
        n = len(seg)
        if n == 0:
            return
        tot = S[t0:t1].sum()
        ints = seg.sum(axis=0) / tot
        peak = R[t0:t1].max(axis=0)
        mx = seg.max(axis=0)
        end = mx if p['stat'] == 'max' else seg[-3:].mean(axis=0)
        rise = end / np.maximum(P[max(w['t_o'] - 4, 0)], floor_before)
        ok = (ints >= share) & (end > p['abs_on']) & (rise >= p['rise_x'])
        if p['peak_share'] > 0:
            ok &= peak >= p['peak_share']
        if p['growth'] > 0 and n >= 2:
            h = n // 2
            ok &= seg[h:].mean(axis=0) >= p['growth'] * (seg[:h].mean(axis=0) + 1e-9)
        ok &= odd[t0:t1].mean(axis=0) >= p['oct_odd']
        if low is not None:
            ok &= low[t0:t1].mean(axis=0) >= p['low_guard']
        for m in w['fired']:
            ok[m] = False
        cands = [int(m) for m in np.argsort(-ints) if ok[m]]
        cand_set = set(cands)
        for m in cands:
            if m - 12 >= 0 and p['oct_up'] > 0 and ((m - 12) in voices or (m - 12) in cand_set) and ints[m] < p['oct_up'] * ints[m - 12]:
                continue
            if p['harm_up'] > 0 and any(m - d >= 0 and ((m - d) in voices or (m - d) in cand_set) and ints[m - d] > p['harm_up'] * ints[m]
                                        for d in HARMONIC_INTERVALS):
                continue
            if fire(m, t, Poff[t][m], mx[m], rise[m]):
                w['fired'].add(m)

    def early(w, t):
        if t < w['t_o'] + 2 or t <= 0:
            return
        rise = P[t] / np.maximum(P[max(w['t_o'] - 4, 0)], floor_before)
        ok = (R[t] >= p['early_share']) & (R[t - 1] >= p['early_share']) & (P[t] > p['abs_on']) & (P[t - 1] > p['abs_on'])
        ok &= rise >= p['rise_x']
        ok &= odd[t] >= p['oct_odd']
        for m in np.nonzero(ok)[0]:
            m = int(m)
            if m in w['fired']:
                continue
            if m - 12 >= 0 and p['oct_up'] > 0 and (m - 12) in voices and P[t, m] < p['oct_up'] * P[t, m - 12]:
                continue
            if fire(m, t, P[t, m], P[t, m], rise[m]):
                w['fired'].add(m)

    for t in range(K):
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
        if t in onset_set:
            if pending and t - pending[-1]['t_o'] <= p['merge_hops']:
                pending[-1]['t_d'] = min(t + D, K - 1)          # absorbed: the pending window waits for the strum to complete
            else:
                pending.append({'t_o': t, 't_d': min(t + D, K - 1), 'fired': set()})
        if pending:
            if p['early_share'] > 0:
                for w in pending:
                    early(w, t)
            if p['mid'] > 0:
                for w in pending:
                    if t == w['t_o'] + int(p['mid']) and t < w['t_d']:
                        decide(w, t, p['mid_share'])
            done = [w for w in pending if w['t_d'] == t]
            for w in done:
                decide(w, t, p['share'])
            if done:
                pending = [w for w in pending if w['t_d'] > t]
    for m in list(voices):
        note_off(m, K - 1)
    events.sort(key=lambda e: e['onset'])
    return {'duration': float(c['duration']), 'events': events}


def build_events(cache, takes, p, label='poly-onset', set_name=None):
    out = {'label': label, 'source': 'test/poly/decomp/onset_notes.py (per-onset decision)', 'params': p, 'takes': {}}
    if set_name:
        out['set'] = set_name
    for t in takes:
        out['takes'][t] = run_take(N.load_cache(cache, t), p)
    return out


def _grid_job(args):
    p, sets, caches, refs = args
    res = {}
    for name, takes in sets.items():
        ev = {'takes': {t: run_take(caches[t], p) for t in takes}}
        res[name] = N._f1(ev, refs)
    return p, res


def grid(a):
    import score
    lists = json.load(open(a.takes))
    sets = {k: lists['dev_' + k] for k in ('solo', 'comp', 'mix2', 'mix3', 'hex2', 'hex3') if 'dev_' + k in lists}
    all_takes = sorted({t for v in sets.values() for t in v})
    caches = {t: N.load_cache(a.cache, t) for t in all_takes}
    spath = os.path.join(a.cache, 'settings.json')
    settings = json.load(open(spath)) if os.path.exists(spath) else None
    refs = {t: score.read_jams_notes(os.path.join(a.guitarset, 'annotation', t + '.jams')) for t in all_takes}
    if not a.space:
        raise SystemExit('grid needs --space')
    space = json.load(open(a.space))
    space = space.get('space', space)
    base = load_params(a.params)
    keys = list(space)
    combos = [dict(base, **dict(zip(keys, vals))) for vals in itertools.product(*[space[k] for k in keys])]
    print(f'{len(combos)} combinations x {len(all_takes)} dev takes ({", ".join(f"{k} {len(v)}" for k, v in sets.items())})', file=sys.stderr)
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
        json.dump({'best': rows[0][1], 'objective': 'mean F1 +-50 ms over the dev lists ' + '/'.join(sets),
                   'dev_takes': {k: len(v) for k, v in sets.items()}, 'space': space, 'base': base,
                   'cache_settings': settings, 'rule': 'test/poly/decomp/onset_notes.py',
                   'top': [{'objective': o, 'params': p, 'f1': res} for o, p, res in rows[:30]]}, f, indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['events', 'grid'])
    ap.add_argument('--cache', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--set')
    ap.add_argument('--out', required=True)
    ap.add_argument('--params')
    ap.add_argument('--label', default='poly-onset')
    ap.add_argument('--guitarset')
    ap.add_argument('--space')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    if a.cmd == 'grid':
        grid(a)
        return
    p = load_params(a.params)
    lists = json.load(open(a.takes))
    ev = build_events(a.cache, lists[a.set], p, a.label, a.set)
    with open(a.out, 'w') as f:
        json.dump(ev, f)
    n = sum(len(t['events']) for t in ev['takes'].values())
    print(f'wrote {a.out}: {len(ev["takes"])} takes, {n} notes', file=sys.stderr)


if __name__ == '__main__':
    main()
