#!/usr/bin/env python3
"""Track B of the POLY improvement session (docs/polyphonic-plan.md, section
12): convergence of the DECOMPOSER with the generic bank of templates.py kept
fixed. Variants of analyze.py's sparse beta-NMF (beta 0.5, multiplicative
updates, W fixed) exposed as parameters, a cache writer compatible with
notes.py / onset_notes.py, and a one-take timing tool.

  python3 test/poly/decomp/decomp.py analyze --guitarset <mixdir> --takes <takes.json> --set dev_solo,... --cache <dir>
        [--base-cache <analyze.py cache>] [--active 40] [--iter 15] [--lambda 400] [--init-sal 0] [--jobs 4]
  python3 test/poly/decomp/decomp.py time --wav <file> [--active ..] [--iter ..] [--lambda ..] [--init-sal ..] [--repeat 3]

Variant parameters (defaults = the BASELINE decomposer, params-baseline.json):
  --active N     active set: the templates sounding at the previous hop (h above
                 H_FLOOR, the noise templates included) completed to N by harmonic
                 salience W^T v; the two noise templates are always active (so up to
                 N + 2 when they were not counted). 122 (or 'all') = no gating.
  --iter N       multiplicative updates per hop.
  --lambda L     L1 penalty, in the units of the update's denominator (see analyze.py).
  --init-sal s   warm start: h is carried over from the previous hop; a template
                 that (re-)enters the active set starts at max(1e-4 max(v), s x W_k^T v)
                 (s = 0: the baseline's tiny floor, from which a multiplicative
                 update needs several iterations to grow; s > 0: a small non-zero
                 floor proportional to the template's own projection on the frame).

The cache holds the same keys as analyze.py for the medium window only
(P_medium, noise_medium, odd_medium, low_medium, level, flux, hop_s, midi,
duration, nmf_s_per_hop_medium) plus the variant in `decomp` (JSON string) and
<cache>/settings.json. The decomposer-independent arrays (level, flux, odd,
low) are copied from --base-cache when it holds the take (bit-identical
front-end), otherwise recomputed by analyze.py's functions. The short-window
NMF of analyze.py is not run: notes.py only reads the medium window.
"""
import argparse
import json
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import analyze as A  # noqa: E402
import templates as T  # noqa: E402

DEFAULT = dict(active=40, n_iter=15, lam=400.0, init_sal=0.0)


def nmf_run(V, W, meta, active=40, n_iter=15, lam=400.0, init_sal=0.0):
    """Sequential sparse beta-NMF over the hops of one window, parameterised
    (same arithmetic as analyze.nmf_run at active 40 / init_sal 0).
    Returns (H (T x K) float32, seconds per hop of the NMF loop)."""
    Tn, K = V.shape[0], W.shape[1]
    H = np.zeros((Tn, K), dtype=np.float32)
    h = np.zeros(K)
    Wt = W.T.astype(np.float64)
    W64 = W.astype(np.float64)
    noise = np.array([i for i, m in enumerate(meta) if m['midi'] < 0])
    full = active >= K
    nmax = min(active, K)
    t0 = time.perf_counter()
    for t in range(Tn):
        v = V[t].astype(np.float64)
        sal = Wt @ v
        if full:
            act = np.arange(K)
            Wa, Wat = W64, Wt
        else:
            # verbatim analyze.nmf_run: the noise templates count among the sounding ones
            sounding = np.nonzero(h > A.H_FLOOR)[0]
            if len(sounding) >= nmax:
                act = sounding[np.argsort(h[sounding])[::-1][:nmax]]
            else:
                sal_rest = sal.copy()
                sal_rest[sounding] = -1
                fill = np.argpartition(sal_rest, -(nmax - len(sounding)))[-(nmax - len(sounding)):]
                act = np.concatenate([sounding, fill])
            act = np.union1d(act, noise)
            Wa = W64[:, act]
            Wat = Wt[act]
        floor = 1e-4 * max(v.max(), 1e-6)
        if init_sal > 0:
            ha = np.maximum(h[act], np.maximum(floor, init_sal * sal[act]))
        else:
            ha = np.maximum(h[act], floor)
        for _ in range(n_iter):
            y = Wa @ ha + A.EPS
            num = Wat @ (v * y ** (A.BETA - 2))
            den = Wat @ (y ** (A.BETA - 1)) + lam
            ha *= num / den
        if full:
            h = ha
        else:
            h[:] = 0
            h[act] = ha
        H[t] = h
    return H, (time.perf_counter() - t0) / max(Tn, 1)


def load_audio(wav_path):
    import soundfile as sf
    from scipy.signal import resample_poly
    x, sr = sf.read(wav_path, dtype='float64', always_2d=True)
    x = x[:, 0]
    if sr != T.SR:
        g = np.gcd(sr, T.SR)
        x = resample_poly(x, T.SR // g, sr // g)
    return x


def decompose(x, params, win='medium', V=None):
    """P (T x 44), noise (T x 2), seconds per hop for one window."""
    W, meta, _ = T.build(win)
    if V is None:
        V = A.spectra(x, win)
    H, per_hop = nmf_run(V, W, meta, params['active'], params['n_iter'], params['lam'], params['init_sal'])
    midi_of = np.array([m['midi'] for m in meta])
    P = np.zeros((len(V), len(A.PITCHES)), dtype=np.float32)
    for j, m in enumerate(A.PITCHES):
        P[:, j] = H[:, midi_of == m].sum(axis=1)
    return P, H[:, midi_of < 0], per_hop, V


def analyze_take(wav_path, params, base=None):
    x = load_audio(wav_path)
    K = len(x) // A.HOP
    out = {'hop_s': A.HOP / T.SR, 'midi': A.PITCHES, 'duration': len(x) / T.SR,
           'n_iter': params['n_iter'], 'lambda': params['lam'], 'decomp': json.dumps(params)}
    V = A.spectra(x, 'medium')
    if base is not None and len(base['level']) == K:
        for k in ('level', 'flux', 'odd_medium', 'low_medium'):
            out[k] = base[k]
    else:
        out['level'] = 20 * np.log10(np.sqrt(np.mean(x[:K * A.HOP].reshape(K, A.HOP) ** 2, axis=1)) + 1e-9).astype(np.float32)
        Vs = A.spectra(x, 'short')
        dB = 20 * np.log10(np.maximum(Vs[:, A.FLUX_LO:A.FLUX_HI], 1e-4))
        d = np.zeros(K, dtype=np.float32)
        d[A.FLUX_LAG:] = np.minimum(np.maximum(dB[A.FLUX_LAG:] - dB[:-A.FLUX_LAG], 0), 20).mean(axis=1)
        out['flux'] = d
        out['odd_medium'], out['low_medium'] = A.partial_features(V, A.PITCHES)
    P, noise, per_hop, _ = decompose(x, params, 'medium', V)
    out['P_medium'], out['noise_medium'], out['nmf_s_per_hop_medium'] = P, noise, per_hop
    return out


def _job(args):
    take, wav, cache, params, base_cache = args
    dst = os.path.join(cache, take + '.npz')
    if os.path.exists(dst):
        return take, None
    base = None
    if base_cache:
        bp = os.path.join(base_cache, take + '.npz')
        if os.path.exists(bp):
            z = np.load(bp)
            base = {k: z[k] for k in ('level', 'flux', 'odd_medium', 'low_medium')}
    t0 = time.perf_counter()
    r = analyze_take(wav, params, base)
    np.savez(dst, **r)
    return take, f"{r['duration']:.1f} s audio in {time.perf_counter() - t0:.1f} s; NMF {1e3 * r['nmf_s_per_hop_medium']:.3f} ms/hop medium" + ('' if base is not None else ' (front-end recomputed)')


def params_from_args(a):
    active = 122 if str(a.active) == 'all' else int(a.active)
    return dict(active=active, n_iter=a.iter, lam=a.lam, init_sal=a.init_sal)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['analyze', 'time'])
    ap.add_argument('--guitarset')
    ap.add_argument('--takes')
    ap.add_argument('--set', default='dev_solo,dev_comp,dev_mix2,dev_mix3,dev_hex2,dev_hex3')
    ap.add_argument('--cache')
    ap.add_argument('--base-cache', help='analyze.py cache to copy level/flux/odd/low from')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--active', default=DEFAULT['active'], help="active set size (40 baseline, 'all' = 122)")
    ap.add_argument('--iter', type=int, default=DEFAULT['n_iter'])
    ap.add_argument('--lambda', dest='lam', type=float, default=DEFAULT['lam'])
    ap.add_argument('--init-sal', type=float, default=DEFAULT['init_sal'])
    ap.add_argument('--wav', help='time: the take to time')
    ap.add_argument('--repeat', type=int, default=3)
    a = ap.parse_args()
    params = params_from_args(a)
    if a.cmd == 'time':
        x = load_audio(a.wav)
        V = A.spectra(x, 'medium')
        W, meta, _ = T.build('medium')
        ts = []
        for _ in range(a.repeat):
            _, per_hop = nmf_run(V, W, meta, params['active'], params['n_iter'], params['lam'], params['init_sal'])
            ts.append(1e3 * per_hop)
        print(f"{json.dumps(params)}: {len(V)} hops, NMF ms/hop " + ' '.join(f'{t:.3f}' for t in ts) + f'  (min {min(ts):.3f})')
        return
    os.makedirs(a.cache, exist_ok=True)
    settings = dict(params, windows=['medium'], beta=A.BETA, source='test/poly/decomp/decomp.py')
    spath = os.path.join(a.cache, 'settings.json')
    if os.path.exists(spath):
        old = json.load(open(spath))
        if old != settings:
            raise SystemExit(f'{spath} holds {old}, not {settings}: use another --cache')
    else:
        with open(spath, 'w') as f:
            json.dump(settings, f, indent=1)
    print(f'settings: {settings}', flush=True)
    lists = json.load(open(a.takes))
    jobs = [(t, os.path.join(a.guitarset, 'audio_mono-pickup_mix', t + '_mix.wav'), a.cache, params, a.base_cache)
            for s in a.set.split(',') for t in lists[s]]
    from multiprocessing import Pool
    with Pool(a.jobs) as pool:
        for take, msg in pool.imap_unordered(_job, jobs):
            print(f'{take}: {msg or "cached"}', flush=True)


if __name__ == '__main__':
    main()
