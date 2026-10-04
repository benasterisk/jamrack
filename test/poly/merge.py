#!/usr/bin/env python3
"""MERGED configuration of the POLY improvement session (docs/polyphonic-plan.md,
section 12): the template bank of track A (test/poly/bank/make_bank.py, kind
"fit", attack columns, per-string partial profile and B law of
profile-dev.json), the parameterised decomposer of track B
(test/poly/decomp/decomp.py: active set, iterations, lambda, warm start) and
one of the two note rules (test/poly/notes.py per hop, or
test/poly/decomp/onset_notes.py per onset), all driven by ONE configuration
file, test/poly/params-merged.json:

  {"bank": {"kind": "fit", "phases": ["att"], "stats": "test/poly/bank/profile-dev.json", "slope1": false},
   "decomposer": {"active": 40, "iter": 15, "lambda": 400.0, "init_sal": 0.0},
   "rule": "onset" | "notes",
   "best": { ... thresholds of that rule ... }}

  python3 test/poly/merge.py bank    --config <cfg> --out bank.npz
  python3 test/poly/merge.py analyze --config <cfg> --guitarset <mixdir> --takes <takes.json> --set solo,comp,...
                                     --cache <dir> --features <dir> [--base-cache <dir>] [--jobs 4]
  python3 test/poly/merge.py grid    --config <cfg> --cache <dir> --takes <takes.json> --guitarset <mixdir>
                                     --out grid.json --space space.json [--params base.json] [--rule notes|onset] [--jobs 4]
  python3 test/poly/merge.py events  --config <cfg> --cache <dir> --takes <takes.json> --set <list> --out ev.json
                                     [--params p.json] [--rule notes|onset] [--label poly]
  python3 test/poly/merge.py time    --config <cfg> --wav <file> [--repeat 3]

The "bank" and "decomposer" entries of --config select the analysis; the
rule and its thresholds come from --params when given (a grid output or a
dict, "best" entry honoured) and from --config ("rule", "best") otherwise.
--rule overrides the rule name on the command line (grid comparisons).

CACHE. One npz per take with the pitch activations of the medium window
(P_medium, noise_medium: T x 44 / T x 2), level, flux, hop_s, midi, duration,
nmf_s_per_hop_medium and the settings, saved COMPRESSED (the activations are
sparse). The decomposer-independent spectral features odd_medium / low_medium
(dense, ~3 MB per take) are NOT duplicated: load_cache() reads them from the
first directory of the cache's "features" list (settings.json) that holds the
take, i.e. --features (where `analyze` writes them for a take no earlier cache
has: level, flux, odd_medium, low_medium) then --base-cache (an analyze.py
cache such as the baseline's, bit-identical front-end). The settings are
checked on every call and a cache made with another bank / decomposer /
feature source is refused. load_cache() is installed into notes.py before the
rules run, so notes.py and onset_notes.py work unchanged on these caches.

TIMING (`time`): NMF seconds per hop of the medium window on one take, numpy
single-threaded (OMP/OpenBLAS threads forced to 1 at import, as every cost
figure of the session), best of --repeat runs.
"""
import os

os.environ.setdefault('OMP_NUM_THREADS', '1')
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('MKL_NUM_THREADS', '1')

import argparse  # noqa: E402
import json  # noqa: E402
import sys  # noqa: E402
import time  # noqa: E402

import numpy as np  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
for p in (os.path.join(HERE, '..'), HERE, os.path.join(HERE, 'bank'), os.path.join(HERE, 'decomp')):
    sys.path.insert(0, p)
import analyze as A  # noqa: E402
import templates as T  # noqa: E402
import notes as N  # noqa: E402
import make_bank  # noqa: E402
import decomp  # noqa: E402
import onset_notes as O  # noqa: E402

RULES = {'notes': N, 'onset': O}
DEFAULT_BANK = {'kind': 'fit', 'phases': ['att'], 'stats': 'test/poly/bank/profile-dev.json', 'slope1': False}
DEFAULT_DECOMP = dict(decomp.DEFAULT)      # active 40, n_iter 15, lam 400, init_sal 0
_BANK = {}                                 # built bank per process (shared with the pool workers by fork)
_SETTINGS = {}                             # settings.json per cache dir


# ------------------------------------------------------------------ config
def load_config(path):
    cfg = json.load(open(path))
    bank = dict(DEFAULT_BANK, **cfg.get('bank', {}))
    dec = dict(DEFAULT_DECOMP)
    for k, v in cfg.get('decomposer', {}).items():
        dec[{'iter': 'n_iter', 'lambda': 'lam'}.get(k, k)] = v
    dec['active'] = 122 if str(dec['active']) == 'all' else int(dec['active'])
    dec['n_iter'] = int(dec['n_iter'])
    dec['lam'] = float(dec['lam'])
    dec['init_sal'] = float(dec['init_sal'])
    return {'bank': bank, 'decomposer': dec, 'rule': cfg.get('rule', 'notes'), 'best': cfg.get('best'), 'path': os.path.abspath(path)}


def build_bank(bank):
    """(W_medium (NBINS x K) float32, meta) of the configured bank, built in-process from the stats JSON."""
    key = json.dumps(bank, sort_keys=True)
    if key not in _BANK:
        if bank['kind'] == 'synth':
            W, meta, _ = T.build('medium')
            meta = [dict(m, phase='att', origin='model') for m in meta]
        else:
            stats_path = bank['stats'] if os.path.isabs(bank['stats']) else os.path.join(ROOT, bank['stats'])
            stats = json.load(open(stats_path))
            M = {'prof_att': np.array(stats['prof_att']), 'prof_dec': np.array(stats['prof_dec'])}
            out, meta = make_bank.build(bank['kind'], M, stats, list(bank['phases']), 8, 40.0, True, bank.get('slope1', False))
            W = out['W_medium']
        _BANK[key] = (W, meta)
    return _BANK[key]


# ------------------------------------------------------------------ cache
def cache_settings(cache):
    if cache not in _SETTINGS:
        _SETTINGS[cache] = json.load(open(os.path.join(cache, 'settings.json')))
    return _SETTINGS[cache]


def load_cache(cache, take):
    """notes.load_cache replacement: the take's npz completed with odd/low from the feature dirs."""
    z = np.load(os.path.join(cache, take + '.npz'))
    d = {k: z[k] for k in z.files}
    if 'odd_medium' not in d:
        for fdir in cache_settings(cache).get('features', []):
            p = os.path.join(fdir, take + '.npz')
            if os.path.exists(p):
                f = np.load(p)
                for k in ('odd_medium', 'low_medium'):
                    d[k] = f[k]
                break
        else:
            raise SystemExit(f'{take}: no odd_medium/low_medium in {cache} nor in its feature dirs {cache_settings(cache).get("features")}')
    return d


N.load_cache = load_cache


def features_of(x, V, K):
    """level, flux, odd_medium, low_medium of one take (analyze.py's definitions)."""
    level = 20 * np.log10(np.sqrt(np.mean(x[:K * A.HOP].reshape(K, A.HOP) ** 2, axis=1)) + 1e-9).astype(np.float32)
    Vs = A.spectra(x, 'short')
    dB = 20 * np.log10(np.maximum(Vs[:, A.FLUX_LO:A.FLUX_HI], 1e-4))
    flux = np.zeros(K, dtype=np.float32)
    flux[A.FLUX_LAG:] = np.minimum(np.maximum(dB[A.FLUX_LAG:] - dB[:-A.FLUX_LAG], 0), 20).mean(axis=1)
    odd, low = A.partial_features(V, A.PITCHES)
    return {'level': level, 'flux': flux, 'odd_medium': odd, 'low_medium': low}


def analyze_take(take, wav, cfg, feature_dirs, features_out):
    x = decomp.load_audio(wav)
    K = len(x) // A.HOP
    V = A.spectra(x, 'medium')
    feat = None
    for fdir in feature_dirs:
        p = os.path.join(fdir, take + '.npz')
        if os.path.exists(p):
            f = np.load(p)
            if len(f['level']) == K and 'odd_medium' in f.files:
                feat = {k: f[k] for k in ('level', 'flux')}
                break
    computed = feat is None
    if computed:
        full = features_of(x, V, K)
        os.makedirs(features_out, exist_ok=True)
        np.savez_compressed(os.path.join(features_out, take + '.npz'), **full)
        feat = {k: full[k] for k in ('level', 'flux')}
    W, meta = build_bank(cfg['bank'])
    d = cfg['decomposer']
    H, per_hop = decomp.nmf_run(V, W, meta, d['active'], d['n_iter'], d['lam'], d['init_sal'])
    midi_of = np.array([m['midi'] for m in meta])
    P = np.zeros((K, len(A.PITCHES)), dtype=np.float32)
    for j, m in enumerate(A.PITCHES):
        P[:, j] = H[:, midi_of == m].sum(axis=1)
    out = {'hop_s': A.HOP / T.SR, 'midi': A.PITCHES, 'duration': len(x) / T.SR, 'n_iter': d['n_iter'], 'lambda': d['lam'],
           'decomp': json.dumps(d), 'bank': json.dumps(cfg['bank']), 'P_medium': P, 'noise_medium': H[:, midi_of < 0],
           'nmf_s_per_hop_medium': per_hop}
    out.update(feat)
    return out, computed


def _job(args):
    take, wav, cache, cfg, feature_dirs, features_out = args
    dst = os.path.join(cache, take + '.npz')
    if os.path.exists(dst):
        return take, None
    t0 = time.perf_counter()
    r, computed = analyze_take(take, wav, cfg, feature_dirs, features_out)
    np.savez_compressed(dst + '.tmp.npz', **r)
    os.replace(dst + '.tmp.npz', dst)
    return take, (f"{r['duration']:.1f} s audio in {time.perf_counter() - t0:.1f} s; NMF {1e3 * r['nmf_s_per_hop_medium']:.3f} ms/hop medium"
                  + (' (features computed)' if computed else ''))


def cmd_analyze(a, cfg):
    os.makedirs(a.cache, exist_ok=True)
    feature_dirs = [os.path.abspath(a.features)] + ([os.path.abspath(a.base_cache)] if a.base_cache else [])
    settings = {'bank': cfg['bank'], 'decomposer': cfg['decomposer'], 'windows': ['medium'], 'beta': A.BETA,
                'source': 'test/poly/merge.py', 'features': feature_dirs}
    spath = os.path.join(a.cache, 'settings.json')
    if os.path.exists(spath):
        old = json.load(open(spath))
        if old != settings:
            raise SystemExit(f'{spath} holds {old}, not {settings}: use another --cache')
    else:
        with open(spath, 'w') as f:
            json.dump(settings, f, indent=1)
    print(f'settings: {settings}', flush=True)
    build_bank(cfg['bank'])        # before the fork
    lists = json.load(open(a.takes))
    jobs = [(t, os.path.join(a.guitarset, 'audio_mono-pickup_mix', t + '_mix.wav'), a.cache, cfg, feature_dirs, os.path.abspath(a.features))
            for s in a.set.split(',') for t in lists[s]]
    from multiprocessing import Pool
    with Pool(a.jobs) as pool:
        for take, msg in pool.imap_unordered(_job, jobs):
            print(f'{take}: {msg or "cached"}', flush=True)


# ------------------------------------------------------------------ rules
def rule_of(a, cfg):
    name = a.rule or cfg['rule']
    if name not in RULES:
        raise SystemExit(f'unknown rule {name}')
    return name, RULES[name]


def cmd_grid(a, cfg):
    name, R = rule_of(a, cfg)
    if not a.space:
        raise SystemExit('grid needs --space')
    ns = argparse.Namespace(cache=a.cache, takes=a.takes, guitarset=a.guitarset, out=a.out, jobs=a.jobs,
                            params=a.params or (cfg['path'] if cfg['best'] else None), space=a.space)
    R.grid(ns)
    d = json.load(open(a.out))
    d['rule'] = name
    d['config'] = {'bank': cfg['bank'], 'decomposer': cfg['decomposer'], 'path': cfg['path']}
    with open(a.out, 'w') as f:
        json.dump(d, f, indent=1)


def cmd_events(a, cfg):
    name, R = rule_of(a, cfg)
    if a.params:
        p = R.load_params(a.params)
    else:
        p = dict(R.DEFAULT)
        unknown = [k for k in cfg['best'] if k not in R.DEFAULT]
        if unknown:
            raise SystemExit(f'{cfg["path"]}: thresholds {unknown} unknown to the rule {name}')
        p.update(cfg['best'])
    lists = json.load(open(a.takes))
    ev = R.build_events(a.cache, lists[a.set], p, a.label, a.set)
    ev['source'] = f'test/poly/merge.py ({name} rule, bank {cfg["bank"]["kind"]}/{"+".join(cfg["bank"]["phases"])}, decomposer {json.dumps(cfg["decomposer"])})'
    with open(a.out, 'w') as f:
        json.dump(ev, f)
    n = sum(len(t['events']) for t in ev['takes'].values())
    print(f'wrote {a.out}: {len(ev["takes"])} takes, {n} notes', file=sys.stderr)


def cmd_time(a, cfg):
    x = decomp.load_audio(a.wav)
    V = A.spectra(x, 'medium')
    W, meta = build_bank(cfg['bank'])
    d = cfg['decomposer']
    ts = []
    for _ in range(a.repeat):
        _, per_hop = decomp.nmf_run(V, W, meta, d['active'], d['n_iter'], d['lam'], d['init_sal'])
        ts.append(1e3 * per_hop)
    print(f"{os.path.basename(a.wav)}: {len(V)} hops, bank {cfg['bank']['kind']} {W.shape[1]} columns, decomposer {json.dumps(d)}, "
          f"NMF ms/hop " + ' '.join(f'{t:.3f}' for t in ts) + f'  (min {min(ts):.3f}, single-threaded numpy)')


def cmd_bank(a, cfg):
    W, meta = build_bank(cfg['bank'])
    np.savez_compressed(a.out, W_medium=W, meta=np.array(json.dumps(meta)), settings=np.array(json.dumps(cfg['bank'])))
    print(f'{a.out}: {W.shape[1]} columns ({cfg["bank"]})')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['bank', 'analyze', 'grid', 'events', 'time'])
    ap.add_argument('--config', default=os.path.join(HERE, 'params-merged.json'))
    ap.add_argument('--guitarset')
    ap.add_argument('--takes')
    ap.add_argument('--set', default='solo,comp,mix2,mix3,hex2,hex3')
    ap.add_argument('--cache')
    ap.add_argument('--features', help='analyze: where the spectral features of takes absent from --base-cache are written (and read)')
    ap.add_argument('--base-cache', help='analyze: an analyze.py cache whose odd_medium / low_medium are reused')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--out')
    ap.add_argument('--params', help='thresholds (grid output or dict); default: the "best" of --config')
    ap.add_argument('--space')
    ap.add_argument('--rule', choices=list(RULES), help='note rule (default: "rule" of --config)')
    ap.add_argument('--label', default='poly-merged')
    ap.add_argument('--wav')
    ap.add_argument('--repeat', type=int, default=3)
    a = ap.parse_args()
    cfg = load_config(a.config)
    {'bank': cmd_bank, 'analyze': cmd_analyze, 'grid': cmd_grid, 'events': cmd_events, 'time': cmd_time}[a.cmd](a, cfg)


if __name__ == '__main__':
    main()
