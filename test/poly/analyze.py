#!/usr/bin/env python3
"""Front-end + sparse beta-NMF of the POLY prototype (jalon 1 bis), offline,
numpy. Writes one cache file per take with the per-hop pitch activations that
notes.py turns into note events, so that the note-rule thresholds can be
grid-searched without re-running the NMF.

  python3 test/poly/analyze.py --guitarset <dir> --takes <takes.json> --set solo,comp,mix2,mix3 --cache <dir> [--jobs 4]

Front-end (mirrors the plan): WAV 44.1 kHz -> polyphase decimation to 24 kHz
(scipy resample_poly 80/147, no 3 kHz low-pass: partials up to 10 kHz) ->
hop 64 samples (2.67 ms), windows anchored on the newest sample (a frame is
stamped at the time of its newest sample), 2048-point FFT, two window lengths:
medium 1024 samples (42.7 ms) and short 512 (21.3 ms), symmetric Hann.
Level = RMS of the 64 samples of the hop, in dBFS.
Onset features (from the short window): log-spectral flux = mean over the
bins 47 Hz..5 kHz of max(0, dB(V_t) - dB(V_{t-3})) with the magnitudes
floored at -80 dBFS and each bin's rise clipped at 20 dB (level-independent);
notes.py combines it with the level rise.

Decomposer per hop and per window: beta-divergence NMF, beta = 0.5,
multiplicative updates, W fixed (templates.py), h warm-started from the
previous hop, L1 penalty lambda, active set <= 40 templates = the templates
sounding at the previous hop (h above a floor) completed by the highest
harmonic salience W^T v, 6 iterations. Pitch activation P[m] = sum of h over
the string/fret templates of midi m (one note per pitch, not per string).

Octave-guard feature: for every pitch and hop, the ratio of the energy of
the odd partials 1, 3, 5 to the even partials 2, 4, 6 (max magnitude within
+-1 bin of each), from the same window's spectrum.

Cache (npz): P_<win> (T x 44), noise_<win> (T x 2), odd_<win> (T x 44),
level (T), flux (T), hop_s, midi (44), plus the NMF time per hop.
"""
import argparse
import json
import os
import sys
import time

import numpy as np
from scipy.signal import resample_poly

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import templates as T  # noqa: E402

HOP = 64
BETA = 0.5
N_ITER = 6
ACTIVE_MAX = 40
LAMBDA = 0.02        # L1 penalty (see docstring of nmf_run)
H_FLOOR = 1e-6       # templates below this leave the active set
EPS = 1e-9
FLUX_LAG = 3
FLUX_LO, FLUX_HI = 4, 427      # bins: 47 Hz .. 5 kHz
PITCHES = np.arange(40, 84)    # 44 pitches E2..B5


def frames(x, n, hop=HOP):
    """Newest-anchored frames: frame k = x[(k+1)hop - n : (k+1)hop], zero-padded at the start."""
    K = len(x) // hop
    xp = np.concatenate([np.zeros(n - hop), x[:K * hop]])
    idx = np.arange(K)[:, None] * hop + np.arange(n)[None, :]
    return xp[idx]


def spectra(x, win_name, chunk=2048):
    n = T.WINDOWS[win_name]
    w = T.window(n)
    K = len(x) // HOP
    out = np.empty((K, T.NBINS), dtype=np.float32)
    xp = np.concatenate([np.zeros(n - HOP), x[:K * HOP]])
    for k0 in range(0, K, chunk):
        k1 = min(K, k0 + chunk)
        idx = np.arange(k0, k1)[:, None] * HOP + np.arange(n)[None, :]
        out[k0:k1] = np.abs(np.fft.rfft(xp[idx] * w, T.NFFT, axis=1))
    return out


def odd_even(V, midis):
    """(T x len(midis)) ratio odd/even partial energy; max over +-1 bin."""
    f0 = T.midi_to_hz(midis)
    Vmax = np.maximum(np.maximum(V[:, :-2], V[:, 1:-1]), V[:, 2:])   # Vmax[:, b] = max(V[b..b+2]) -> centred on b+1
    def at(j):
        b = np.clip(np.round(j * f0 / T.BIN_HZ).astype(int) - 1, 0, Vmax.shape[1] - 1)
        return Vmax[:, b]
    odd = at(1) + at(3) + at(5)
    even = at(2) + at(4) + at(6)
    return (odd / (even + EPS)).astype(np.float32)


def nmf_run(V, W, meta):
    """Sequential sparse beta-NMF over the hops of one window. Returns
    (H (T x K) float32, seconds per hop).

    lambda: the L1 penalty is applied in the units of the multiplicative
    update's denominator W^T (Wh)^(beta-1); with |V| in amplitude units
    (full-scale sine -> 1) that denominator is ~1-30 for the levels of
    GuitarSet (-20..-50 dBFS), so lambda = 0.02 is a mild sparsity push."""
    Tn, K = V.shape[0], W.shape[1]
    H = np.zeros((Tn, K), dtype=np.float32)
    h = np.zeros(K)
    Wt = W.T.astype(np.float64)
    W64 = W.astype(np.float64)
    noise = np.array([i for i, m in enumerate(meta) if m['midi'] < 0])
    t0 = time.perf_counter()
    for t in range(Tn):
        v = V[t].astype(np.float64)
        sal = Wt @ v                                   # harmonic salience of every template
        sounding = np.nonzero(h > H_FLOOR)[0]
        if len(sounding) >= ACTIVE_MAX:
            act = sounding[np.argsort(h[sounding])[::-1][:ACTIVE_MAX]]
        else:
            sal_rest = sal.copy()
            sal_rest[sounding] = -1
            fill = np.argpartition(sal_rest, -(ACTIVE_MAX - len(sounding)))[-(ACTIVE_MAX - len(sounding)):]
            act = np.concatenate([sounding, fill])
        act = np.union1d(act, noise)
        Wa = W64[:, act]
        Wat = Wt[act]
        ha = np.maximum(h[act], 1e-4 * max(v.max(), 1e-6))
        for _ in range(N_ITER):
            y = Wa @ ha + EPS
            num = Wat @ (v * y ** (BETA - 2))
            den = Wat @ (y ** (BETA - 1)) + LAMBDA
            ha *= num / den
        h[:] = 0
        h[act] = ha
        H[t] = h
    return H, (time.perf_counter() - t0) / max(Tn, 1)


def analyze_take(wav_path):
    import soundfile as sf
    x, sr = sf.read(wav_path, dtype='float64', always_2d=True)
    x = x[:, 0]
    if sr != T.SR:
        g = np.gcd(sr, T.SR)
        x = resample_poly(x, T.SR // g, sr // g)
    K = len(x) // HOP
    level = 20 * np.log10(np.sqrt(np.mean(x[:K * HOP].reshape(K, HOP) ** 2, axis=1)) + 1e-9).astype(np.float32)
    out = {'level': level, 'hop_s': HOP / T.SR, 'midi': PITCHES, 'duration': len(x) / T.SR}
    for win in T.WINDOWS:
        W, meta, _ = T.build(win)
        V = spectra(x, win)
        if win == 'short':
            dB = 20 * np.log10(np.maximum(V[:, FLUX_LO:FLUX_HI], 1e-4))
            d = np.zeros(K, dtype=np.float32)
            d[FLUX_LAG:] = np.minimum(np.maximum(dB[FLUX_LAG:] - dB[:-FLUX_LAG], 0), 20).mean(axis=1)
            out['flux'] = d
        H, per_hop = nmf_run(V, W, meta)
        midi_of = np.array([m['midi'] for m in meta])
        P = np.zeros((K, len(PITCHES)), dtype=np.float32)
        for j, m in enumerate(PITCHES):
            P[:, j] = H[:, midi_of == m].sum(axis=1)
        out['P_' + win] = P
        out['noise_' + win] = H[:, midi_of < 0]
        out['odd_' + win] = odd_even(V, PITCHES)
        out['nmf_s_per_hop_' + win] = per_hop
    return out


def _job(args):
    take, wav, cache = args
    dst = os.path.join(cache, take + '.npz')
    if os.path.exists(dst):
        return take, None
    t0 = time.perf_counter()
    r = analyze_take(wav)
    np.savez(dst, **r)
    return take, f"{r['duration']:.1f} s audio in {time.perf_counter() - t0:.1f} s; NMF {1e3 * r['nmf_s_per_hop_medium']:.3f} ms/hop medium, {1e3 * r['nmf_s_per_hop_short']:.3f} ms/hop short"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--takes', required=True)
    ap.add_argument('--set', default='solo,comp,mix2,mix3')
    ap.add_argument('--cache', required=True)
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    os.makedirs(a.cache, exist_ok=True)
    lists = json.load(open(a.takes))
    jobs = [(t, os.path.join(a.guitarset, 'audio_mono-pickup_mix', t + '_mix.wav'), a.cache)
            for s in a.set.split(',') for t in lists[s]]
    from multiprocessing import Pool
    with Pool(a.jobs) as pool:
        for take, msg in pool.imap_unordered(_job, jobs):
            print(f'{take}: {msg or "cached"}', flush=True)


if __name__ == '__main__':
    main()
