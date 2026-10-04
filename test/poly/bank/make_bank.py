#!/usr/bin/env python3
"""Builds a template bank for the POLY decomposer (analyze.py via
test/poly/bank/analyze_bank.py) from the measurements of extract.py.

  python3 test/poly/bank/make_bank.py --measure <dir>/measurements.npz --kind avg|fit|bfit|synth --out bank-<kind>.npz
                                      [--phases att,dec] [--min-notes 8] [--floor-db 40] [--no-fill]
  python3 test/poly/bank/make_bank.py --stats test/poly/bank/profile-dev.json --kind fit|bfit --out bank-<kind>.npz

--stats (the stats.json of extract.py, committed as profile-dev.json: per-string
profiles prof_att / prof_dec and the B law) is enough for the parametric kinds;
--measure (60 MB, scratch only) is needed for avg.

Three kinds, all 6 strings x 20 frets, one column per (string, fret, phase)
plus the two noise templates of templates.py, unit-L2 columns, one matrix
per analysis window (W_medium 1025 x K, W_short):
  synth  the generic bank of templates.py (Barbancho B, 1/n^1.2), one
         phase only — the baseline, rebuilt here for the record;
  avg    DATA-AVERAGED: the mean unit-L2 attack / decay spectra of the
         string's debleeded channel (extract.py), floored at -floor_db
         under the column maximum (the averaged noise floor and residual
         bleed would otherwise make every column explain a bit of
         everything); cells with fewer than --min-notes notes (attack) or
         decay examples are filled by the parametric model below;
  bfit   B-ONLY: the synthetic amplitudes 1/n^1.2 of templates.py with the
         B law fitted to the data (isolates the inharmonicity from the
         measured partial profile);
  fit    DATA-FITTED PARAMETRIC: rendered like templates.py (sum over the
         partials of a_n |W(f - f_n)|) with the per-string partial-amplitude
         profiles prof_att / prof_dec of extract.py and the B law fitted to
         the data, B(s, fret) = B_open_fit(s) * 2^(fret * slope / 6)
         (slope from stats.json, 1.0 = Barbancho's doubling every 6 frets),
         f0 at equal temperament (A 440): the measured tuning of the
         GuitarSet guitar does not enter the bank.
meta (JSON in the npz) lists string, fret, midi, f0, B, phase, origin
('data' or 'model') per column so that analyze.py sums the activations of
one midi over strings and phases as before.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import templates as T  # noqa: E402

WINS = ('medium', 'short')


def render_profile(f0, B, amps, kern):
    """templates.render with an explicit amplitude vector (amps[n-1] for partial n)."""
    W, ov = kern
    spec = np.zeros(T.NBINS)
    bins = np.arange(T.NBINS)
    for n in range(1, len(amps) + 1):
        fn = n * f0 * np.sqrt(1 + B * n * n)
        if fn > T.F_MAX or amps[n - 1] <= 0:
            continue
        off = np.abs(bins - fn / T.BIN_HZ) * ov
        idx = np.minimum(off.astype(int), len(W) - 2)
        frac = off - idx
        spec += amps[n - 1] * (W[idx] * (1 - frac) + W[idx + 1] * frac)
    return spec


def b_law(stats, s, fret, slope_override=None):
    law = stats['B_law'].get(str(s))
    if law is None:
        return T.B_OPEN[s] * 2 ** (fret / 6)
    slope = law['slope_per_6_frets'] if slope_override is None else slope_override
    B_open = law['B_open_fit'] if slope_override is None else law['B_open_slope1']
    return B_open * 2 ** (fret * slope / 6)


def noise_columns():
    freqs = np.arange(T.NBINS) * T.BIN_HZ
    return [np.ones(T.NBINS), 1 / np.sqrt(np.maximum(freqs, 50.0))]


def build(kind, M, stats, phases, min_notes, floor_db, fill, slope1):
    out = {}
    meta = None
    for win in WINS:
        n = T.WINDOWS[win]
        w = T.window(n)
        kern = T.kernel(w)
        cols, meta = [], []
        for s in range(6):
            for f in range(T.FRETS):
                midi = T.OPEN_MIDI[s] + f
                f0 = T.midi_to_hz(midi)
                for ph in phases:
                    origin = 'model'
                    if kind == 'synth':
                        B = T.B_OPEN[s] * 2 ** (f / 6)
                        col = T.render(f0, B, w, kern)
                    else:
                        B = b_law(stats, s, f, 1.0 if slope1 else None)
                        if kind == 'bfit':
                            col = T.render(f0, B, w, kern)
                        else:
                            prof = M['prof_' + ph][s]
                            col = render_profile(f0, B, prof, kern)
                        if kind == 'avg':
                            cnt = M['n_att'][s, f] if ph == 'att' else M['n_dec'][s, f]
                            if cnt >= min_notes or not fill:
                                data = M[f'{ph}_{win}'][s, f].astype(np.float64)
                                if data.max() > 0:
                                    col = np.where(data >= data.max() * 10 ** (-floor_db / 20), data, 0.0)
                                    origin = 'data'
                                    Bm = M['B_med'][s, f]
                                    if np.isfinite(Bm):
                                        B = float(Bm)
                    cols.append(col)
                    meta.append({'string': s, 'fret': f, 'midi': midi, 'f0': f0, 'B': float(B), 'phase': ph, 'origin': origin})
        cols += noise_columns()
        for name in T.NOISE_NAMES:
            meta.append({'string': -1, 'fret': -1, 'midi': -1, 'f0': 0, 'B': 0, 'name': name, 'phase': '-', 'origin': 'model'})
        W = np.stack(cols, axis=1)
        W /= np.linalg.norm(W, axis=0, keepdims=True)
        out['W_' + win] = W.astype(np.float32)
    out['meta'] = np.array(json.dumps(meta))
    return out, meta


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--measure', help='measurements.npz of extract.py (stats.json next to it); needed for avg')
    ap.add_argument('--stats', help='stats.json of extract.py: enough for fit / bfit')
    ap.add_argument('--kind', required=True, choices=['avg', 'fit', 'bfit', 'synth'])
    ap.add_argument('--out', required=True)
    ap.add_argument('--phases', default='att,dec', help='template phases (default att,dec; synth has one phase)')
    ap.add_argument('--min-notes', type=int, default=8, help='avg: cells with fewer examples use the parametric model')
    ap.add_argument('--floor-db', type=float, default=40.0, help='avg: bins more than this under the column peak are zeroed')
    ap.add_argument('--no-fill', action='store_true', help='avg: keep sparse cells as measured (no parametric fill)')
    ap.add_argument('--slope1', action='store_true', help='fit/avg fill: force the B law slope to 1 (doubling every 6 frets)')
    a = ap.parse_args()
    phases = ['att'] if a.kind in ('synth', 'bfit') else a.phases.split(',')
    M = stats = None
    if a.kind == 'avg' or (a.kind != 'synth' and not a.stats):
        M = np.load(a.measure)
        stats = json.load(open(os.path.join(os.path.dirname(a.measure), 'stats.json')))
    elif a.kind != 'synth':
        stats = json.load(open(a.stats))
        M = {'prof_att': np.array(stats['prof_att']), 'prof_dec': np.array(stats['prof_dec'])}
    out, meta = build(a.kind, M, stats, phases, a.min_notes, a.floor_db, not a.no_fill, a.slope1)
    out['settings'] = np.array(json.dumps({'kind': a.kind, 'phases': phases, 'min_notes': a.min_notes, 'floor_db': a.floor_db,
                                           'fill': not a.no_fill, 'slope1': a.slope1, 'measure': a.measure and os.path.abspath(a.measure),
                                           'stats': a.stats and os.path.abspath(a.stats)}))
    np.savez_compressed(a.out, **out)
    K = out['W_medium'].shape[1]
    nd = sum(m['origin'] == 'data' for m in meta)
    print(f'{a.out}: {K} columns ({len(phases)} phase(s)), {nd} from data, {K - 2 - nd} from the model')


if __name__ == '__main__':
    main()
