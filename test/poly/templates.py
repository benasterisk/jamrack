#!/usr/bin/env python3
"""Generic inharmonic template bank for the POLY prototype (jalon 1 bis of
docs/polyphonic-plan.md). Evaluation tooling only — numpy, never the app.

Bank: 6 strings x frets 0..19 = 120 templates, partials n = 1..N with
amplitudes 1/n^1.2 and frequencies f_n = n f0 sqrt(1 + B n^2), where
B(string, fret) = B_open(string) * 2^(fret/6) (Barbancho et al. 2012, eq. 9:
the coefficient doubles every six frets because the vibrating length halves
at the 12th fret and B ~ 1/L^2).

B_open per string: the "Acoustic" row of Table II of Barbancho, Tardón,
Sammartino & Barbancho, "Inharmonicity-based method for the automatic
generation of guitar tablature", IEEE TASLP 2012 (average of the acoustic
guitars of their test set; GuitarSet is an acoustic guitar with a magnetic
hexaphonic pickup). Their string 1 is the high E:
    string 1 (E4) 1.48e-5, 2 (B3) 4.97e-5, 3 (G3) 2.77e-5,
    string 4 (D3) 4.31e-5, 5 (A2) 6.87e-5, 6 (E2) 9.92e-5.
For comparison their electric row is 1.50e-5 / 5.02e-5 / 8.27e-5 / 5.30e-5 /
9.04e-5 / 1.56e-4: at the 12th fret of the low E the 10th partial sits
+0.5 % (8 cents) sharp with the acoustic value — about half a bin of the
medium window — which is why the bank is "generic until calibrated".

Each template is rendered as the magnitude spectrum the analysis would see:
the sum over partials of a_n |W(f - f_n)|, W being the transform of the
analysis window (same length and shape as the analysis), on the 1025 bins of
a 2048-point FFT at 24 kHz. Partials stop at 10 kHz or n = 40. Two banks are
produced, one per window: medium = 1024 samples (42.7 ms) and short = 512
samples (21.3 ms), symmetric Hann unless another shape is asked. Columns are
L2-normalised. Two extra non-pitch templates absorb the pick transient:
flat (white) and pink (1/sqrt f) noise.
"""
import numpy as np

SR = 24000
NFFT = 2048
NBINS = NFFT // 2 + 1
BIN_HZ = SR / NFFT
WINDOWS = {'medium': 1024, 'short': 512, 'long': 2048}
OPEN_MIDI = [40, 45, 50, 55, 59, 64]          # E2 A2 D3 G3 B3 E4 (strings 6..1)
B_OPEN = [9.92e-5, 6.87e-5, 4.31e-5, 2.77e-5, 4.97e-5, 1.48e-5]   # same order, Barbancho 2012 Table II (acoustic)
FRETS = 20
AMP_EXP = 1.2
F_MAX = 10000.0
N_MAX = 40
NOISE_NAMES = ['noise-white', 'noise-pink']


def midi_to_hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def window(n, shape='hann'):
    """Analysis window, index n-1 = newest sample. Normalised so that a
    full-scale sine shows a peak magnitude of 1 in the spectrum."""
    i = np.arange(n)
    if shape == 'hann':
        w = 0.5 - 0.5 * np.cos(2 * np.pi * (i + 0.5) / n)
    elif shape == 'plateau':   # half-Hann raise of 5 ms at the old edge, flat up to the newest sample
        r = int(0.005 * SR)
        w = np.where(i < r, 0.5 - 0.5 * np.cos(np.pi * (i + 0.5) / r), 1.0)
    else:
        raise ValueError(shape)
    return (w * 2.0 / w.sum()).astype(np.float64)


def kernel(w, oversample=32):
    """Magnitude of the window transform on a grid of 1/oversample bin,
    for bin offsets -NBINS..NBINS (symmetric)."""
    n = NFFT * oversample
    W = np.abs(np.fft.rfft(w, n))        # up to Nyquist at fine resolution
    W = W / W[0]
    return W, oversample


def render(f0, B, w, kern):
    W, ov = kern
    spec = np.zeros(NBINS)
    bins = np.arange(NBINS)
    for n in range(1, N_MAX + 1):
        fn = n * f0 * np.sqrt(1 + B * n * n)
        if fn > F_MAX:
            break
        a = n ** (-AMP_EXP)
        off = np.abs(bins - fn / BIN_HZ) * ov
        idx = np.minimum(off.astype(int), len(W) - 2)
        frac = off - idx
        spec += a * (W[idx] * (1 - frac) + W[idx + 1] * frac)
    return spec


def build(win_name='medium', shape='hann'):
    """Returns (W, meta): W is (NBINS, 122) float32 with unit-L2 columns,
    meta a list of dicts {string, fret, midi, f0, B} (string 0 = low E) for
    the 120 pitched templates followed by the two noise templates."""
    n = WINDOWS[win_name]
    w = window(n, shape)
    kern = kernel(w)
    cols, meta = [], []
    for s in range(6):
        for f in range(FRETS):
            m = OPEN_MIDI[s] + f
            B = B_OPEN[s] * 2 ** (f / 6)
            cols.append(render(midi_to_hz(m), B, w, kern))
            meta.append({'string': s, 'fret': f, 'midi': m, 'f0': midi_to_hz(m), 'B': B})
    freqs = np.arange(NBINS) * BIN_HZ
    cols.append(np.ones(NBINS))
    cols.append(1 / np.sqrt(np.maximum(freqs, 50.0)))
    for name in NOISE_NAMES:
        meta.append({'string': -1, 'fret': -1, 'midi': -1, 'f0': 0, 'B': 0, 'name': name})
    W = np.stack(cols, axis=1)
    W /= np.linalg.norm(W, axis=0, keepdims=True)
    return W.astype(np.float32), meta, w


if __name__ == '__main__':
    for name in WINDOWS:
        W, meta, w = build(name)
        print(f'{name}: W {W.shape}, window {len(w)} samples ({1000 * len(w) / SR:.1f} ms)')
    W, meta, _ = build('medium')
    for k in (0, 12, 19, 119):
        m = meta[k]
        top = np.argsort(W[:, k])[::-1][:4]
        print(f"  string {m['string']} fret {m['fret']} midi {m['midi']} f0 {m['f0']:.1f} Hz B {m['B']:.2e} peaks at bins {sorted(top)} ({[round(b * BIN_HZ) for b in sorted(top)]} Hz)")
