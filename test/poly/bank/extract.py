#!/usr/bin/env python3
"""Measures per string x fret templates from the debleeded hexaphonic stems
of GuitarSet (track A of the POLY improvement session, docs/polyphonic-plan.md
section 12). Evaluation tooling only — numpy/scipy, never the app.

  python3 test/poly/bank/extract.py --guitarset <GS> --players 00,03 --out <dir> [--jobs 4]

Takes = every GuitarSet take (solo and comp) of the given players, i.e. the
DEV players of test/poly/split.json: the TEST players' audio is never read.
For every annotated note (string s = data_source of the note_midi annotation,
fret = round(midi) - open midi of the string, kept if 0 <= fret < 20 and the
annotated pitch is within 0.3 semitone of the fret), from the string's own
debleeded channel resampled to 24 kHz like analyze.py (resample_poly 80/147):

  attack spectrum: mean of the magnitude spectra (|rfft(frame * w, 2048)|,
    the same newest-anchored Hann windows as analyze.py: medium 1024 samples
    = 42.7 ms, short 512 = 21.3 ms) of the hop-aligned frames whose newest
    sample lies 10..40 ms after the annotated onset (~11 frames at hop 64),
    normalised to unit L2 per note;
  decay spectrum: same, frames 200..600 ms after the onset (every 4th hop),
    only for notes that last >= 300 ms;
  inharmonicity: from a 65536-point FFT (Hann, 0.37 Hz bins) of the string
    channel from 60 ms after the onset up to min(offset, +560 ms), notes
    >= 260 ms only: the partials n = 1..30 below 10 kHz are peak-picked
    (parabolic interpolation on the log magnitude, >= 15 dB over the median
    of the +-4 % band, search +-1.5 % around the prediction) in three rounds
    (n <= 6 with B = 0, then n <= 14 and n <= 30 with the B fitted so far)
    and (f_n / n)^2 = f0^2 + f0^2 B n^2 is fitted by least squares (>= 5
    partials): B, f0 (cents off equal temperament, A 440), n partials;
  partial amplitudes: from the per-note attack and decay medium spectra,
    the maximum within +-1 bin of each partial (frequencies from the note's
    own fit, or from the cell's median B otherwise), as a unit-L2 vector
    over n = 1..40 (0 where the partial is above 10 kHz).

Notes whose string channel is under -50 dBFS RMS over the attack frames are
skipped (mis-annotated or inaudible). Output <out>/measurements.npz:
  att_<win>, dec_<win>     (6, 20, 1025) mean of the unit-L2 per-note spectra
  attmed_<win>, decmed_<win>  idem, per-bin median across notes
  n_att, n_dec             (6, 20) notes per cell
  B_med, B_q25, B_q75, n_B, cents_med   (6, 20) inharmonicity statistics
  amp_att, amp_dec         (6, 20, 40) per-cell median partial amplitudes
  prof_att, prof_dec       (6, 40) per-string median profiles (over all
                           notes of the string, normalised per note)
  and <out>/notes.json with one record per note (string, fret, take, onset,
  B, f0_cents, n_partials) plus <out>/stats.json (the summary printed).
"""
import argparse
import glob
import json
import os
import sys

import numpy as np
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import templates as T  # noqa: E402

HOP = 64
WINS = ('medium', 'short')
ATT = (0.010, 0.040)          # s after the annotated onset
DEC = (0.200, 0.600)
DEC_STEP = 4                  # hops between decay frames
MIN_DEC_DUR = 0.300
FIT_START, FIT_MAX_END, MIN_FIT_DUR = 0.060, 0.560, 0.260
NFIT = 65536
N_MAX = T.N_MAX               # 40
N_FIT = 30
F_MAX = T.F_MAX
PEAK_DB = 15.0
MIN_LEVEL_DB = -50.0
MAX_CENTS = 30.0


def load_hex(gs, take):
    import soundfile as sf
    x, sr = sf.read(os.path.join(gs, 'audio_hex-pickup_debleeded', take + '_hex_cln.wav'), dtype='float64', always_2d=True)
    if sr != T.SR:
        g = np.gcd(sr, T.SR)
        x = resample_poly(x, T.SR // g, sr // g, axis=0)
    return x


def take_notes(gs, take):
    j = json.load(open(os.path.join(gs, 'annotation', take + '.jams')))
    out = []
    for a in j['annotations']:
        if a['namespace'] != 'note_midi':
            continue
        s = int(a['annotation_metadata'].get('data_source') or 0)
        for d in a['data']:
            m = float(d['value'])
            f = int(round(m)) - T.OPEN_MIDI[s]
            if 0 <= f < T.FRETS and abs(m - round(m)) <= 0.3 and d['duration'] > 0:
                out.append((s, f, float(d['time']), float(d['duration']), m))
    return out


def frame_spectra(x, k0, k1, n, w, step=1):
    """Spectra of the newest-anchored frames k0..k1 (frame k ends at sample (k+1)*HOP)."""
    ks = np.arange(k0, k1 + 1, step)
    ends = (ks + 1) * HOP
    ks = ks[(ends <= len(x)) & (ends - n >= 0)]
    if len(ks) == 0:
        return None
    idx = ((ks + 1) * HOP)[:, None] + np.arange(-n, 0)[None, :]
    return np.abs(np.fft.rfft(x[idx] * w, T.NFFT, axis=1))


def hop_range(t0, t1):
    """Hops whose newest sample (k+1)*HOP/SR lies in [t0, t1]."""
    return int(np.ceil(t0 * T.SR / HOP)) - 1, int(np.floor(t1 * T.SR / HOP)) - 1


def pick_peak(mag, lo, hi):
    """Interpolated peak of mag[lo:hi] (bins), or None if not >= PEAK_DB over the band median."""
    lo, hi = max(lo, 1), min(hi, len(mag) - 2)
    if hi - lo < 3:
        return None
    seg = mag[lo:hi]
    i = int(np.argmax(seg))
    band = mag[max(lo - (hi - lo), 1):min(hi + (hi - lo), len(mag) - 1)]
    if seg[i] < 10 ** (PEAK_DB / 20) * np.median(band):
        return None
    b = lo + i
    y0, y1, y2 = np.log(mag[b - 1] + 1e-12), np.log(mag[b] + 1e-12), np.log(mag[b + 1] + 1e-12)
    den = y0 - 2 * y1 + y2
    delta = 0.5 * (y0 - y2) / den if den < 0 else 0.0
    return b + float(np.clip(delta, -1, 1)), mag[b]


def fit_inharmonicity(seg, f0_guess):
    """(B, f0, n_partials, amps dict) from a high-resolution spectrum of a decaying note."""
    w = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * w, NFIT))
    hz = T.SR / NFIT
    f0, B = f0_guess, 0.0
    found = {}
    for n_hi, tol in ((6, 0.015), (14, 0.012), (N_FIT, 0.010)):
        for n in range(1, n_hi + 1):
            if n in found:
                continue
            fn = n * f0 * np.sqrt(1 + B * n * n)
            if fn > F_MAX:
                break
            r = pick_peak(mag, int((fn * (1 - tol)) / hz), int((fn * (1 + tol)) / hz) + 1)
            if r is not None:
                found[n] = (r[0] * hz, r[1])
        if len(found) < 3:
            return None
        ns = np.array(sorted(found))
        fs = np.array([found[n][0] for n in ns])
        y = (fs / ns) ** 2
        A = np.stack([np.ones_like(ns, dtype=float), ns.astype(float) ** 2], axis=1)
        coef, *_ = np.linalg.lstsq(A, y, rcond=None)
        if coef[0] <= 0:
            return None
        f0 = float(np.sqrt(coef[0]))
        B = float(max(coef[1] / coef[0], 0.0))
        # drop partials more than 1 % off the fitted curve, refit once
        pred = ns * f0 * np.sqrt(1 + B * ns ** 2)
        bad = np.abs(fs / pred - 1) > 0.01
        if bad.any():
            for n in ns[bad]:
                del found[n]
    if len(found) < 5:
        return None
    return B, f0, len(found), {int(n): float(found[n][1]) for n in found}


def partial_amps(spec, f0, B):
    """Unit-L2 vector of the partial amplitudes 1..N_MAX read in a medium-window spectrum."""
    a = np.zeros(N_MAX)
    for n in range(1, N_MAX + 1):
        fn = n * f0 * np.sqrt(1 + B * n * n)
        if fn > F_MAX:
            break
        b = int(round(fn / T.BIN_HZ))
        a[n - 1] = spec[max(b - 1, 0):b + 2].max()
    nrm = np.linalg.norm(a)
    return a / nrm if nrm > 0 else a


def process_take(args):
    gs, take = args
    x = load_hex(gs, take)
    wins = {w: (T.WINDOWS[w], T.window(T.WINDOWS[w])) for w in WINS}
    recs = []
    for s, f, t, d, m in take_notes(gs, take):
        ch = x[:, s]
        k0, k1 = hop_range(t + ATT[0], t + ATT[1])
        att = {}
        for w, (n, win) in wins.items():
            S = frame_spectra(ch, k0, k1, n, win)
            if S is None:
                break
            att[w] = S.mean(axis=0)
        if len(att) < len(wins):
            continue
        seg_att = ch[max(k0 * HOP, 0):(k1 + 1) * HOP]
        level = 20 * np.log10(np.sqrt(np.mean(seg_att ** 2)) + 1e-9) if len(seg_att) else -120
        if level < MIN_LEVEL_DB:
            continue
        rec = {'string': s, 'fret': f, 'take': take, 'onset': t, 'dur': d, 'midi': m, 'level_db': float(level)}
        for w in wins:
            rec['att_' + w] = (att[w] / (np.linalg.norm(att[w]) + 1e-12)).astype(np.float32)
        if d >= MIN_DEC_DUR:
            k0, k1 = hop_range(t + DEC[0], t + min(DEC[1], d))
            dec = {}
            for w, (n, win) in wins.items():
                S = frame_spectra(ch, k0, k1, n, win, DEC_STEP)
                if S is not None:
                    dec[w] = S.mean(axis=0)
            if len(dec) == len(wins):
                for w in wins:
                    rec['dec_' + w] = (dec[w] / (np.linalg.norm(dec[w]) + 1e-12)).astype(np.float32)
        if d >= MIN_FIT_DUR:
            a, b = int((t + FIT_START) * T.SR), int((t + min(d, FIT_MAX_END)) * T.SR)
            if b <= len(ch) and b - a >= int(0.2 * T.SR):
                r = fit_inharmonicity(ch[a:b], T.midi_to_hz(m))
                if r is not None:
                    rec['B'], rec['f0'], rec['n_partials'] = r[0], r[1], r[2]
                    rec['cents'] = 1200 * np.log2(r[1] / T.midi_to_hz(T.OPEN_MIDI[s] + f))
        recs.append(rec)
    return take, recs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--players', default='00,03', help='GuitarSet players whose takes are read (default: the DEV players)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    players = a.players.split(',')
    takes = sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(a.guitarset, 'annotation', '*.jams'))
                   if os.path.basename(p)[:2] in players)
    print(f'{len(takes)} takes of players {players}', flush=True)
    os.makedirs(a.out, exist_ok=True)
    from multiprocessing import Pool
    cells = {}
    all_recs = []
    with Pool(a.jobs) as pool:
        for take, recs in pool.imap_unordered(process_take, [(a.guitarset, t) for t in takes]):
            print(f'{take}: {len(recs)} notes, {sum("dec_medium" in r for r in recs)} with decay, {sum("B" in r for r in recs)} B fits', flush=True)
            for r in recs:
                cells.setdefault((r['string'], r['fret']), []).append(r)
            all_recs.extend(recs)
    shape = (6, T.FRETS)
    out = {k: np.zeros(shape + (T.NBINS,), dtype=np.float32) for w in WINS for k in (f'att_{w}', f'dec_{w}', f'attmed_{w}', f'decmed_{w}')}
    out.update({k: np.zeros(shape) for k in ('n_att', 'n_dec', 'n_B', 'B_med', 'B_q25', 'B_q75', 'cents_med')})
    out['amp_att'] = np.zeros(shape + (N_MAX,))
    out['amp_dec'] = np.zeros(shape + (N_MAX,))
    out['B_med'][:] = np.nan
    out['B_q25'][:] = np.nan
    out['B_q75'][:] = np.nan
    out['cents_med'][:] = np.nan
    prof = {'att': [[] for _ in range(6)], 'dec': [[] for _ in range(6)]}
    for (s, f), recs in cells.items():
        out['n_att'][s, f] = len(recs)
        Bs = np.array([r['B'] for r in recs if 'B' in r])
        out['n_B'][s, f] = len(Bs)
        if len(Bs):
            out['B_med'][s, f], out['B_q25'][s, f], out['B_q75'][s, f] = np.median(Bs), np.percentile(Bs, 25), np.percentile(Bs, 75)
            out['cents_med'][s, f] = np.median([r['cents'] for r in recs if 'B' in r])
        for w in WINS:
            A = np.stack([r['att_' + w] for r in recs])
            out[f'att_{w}'][s, f] = A.mean(axis=0)
            out[f'attmed_{w}'][s, f] = np.median(A, axis=0)
            D = [r['dec_' + w] for r in recs if 'dec_' + w in r]
            if D:
                D = np.stack(D)
                out[f'dec_{w}'][s, f] = D.mean(axis=0)
                out[f'decmed_{w}'][s, f] = np.median(D, axis=0)
                if w == 'medium':
                    out['n_dec'][s, f] = len(D)
        B_cell = out['B_med'][s, f] if np.isfinite(out['B_med'][s, f]) else T.B_OPEN[s] * 2 ** (f / 6)
        f0_nom = T.midi_to_hz(T.OPEN_MIDI[s] + f)
        aa, ad = [], []
        for r in recs:
            f0, B = (r['f0'], r['B']) if 'B' in r else (f0_nom, B_cell)
            v = partial_amps(r['att_medium'], f0, B)
            aa.append(v)
            prof['att'][s].append(v)
            if 'dec_medium' in r:
                v = partial_amps(r['dec_medium'], f0, B)
                ad.append(v)
                prof['dec'][s].append(v)
        out['amp_att'][s, f] = np.median(np.stack(aa), axis=0)
        if ad:
            out['amp_dec'][s, f] = np.median(np.stack(ad), axis=0)
    for ph in ('att', 'dec'):
        P = np.zeros((6, N_MAX))
        for s in range(6):
            if prof[ph][s]:
                M = np.stack(prof[ph][s])
                # median of the partials that exist (below 10 kHz) in each note
                for n in range(N_MAX):
                    col = M[:, n]
                    col = col[col > 0]
                    P[s, n] = np.median(col) if len(col) >= 20 else 0.0
        out['prof_' + ph] = P
    out['players'] = np.array(players)
    out['takes'] = np.array(takes)
    np.savez_compressed(os.path.join(a.out, 'measurements.npz'), **out)
    with open(os.path.join(a.out, 'notes.json'), 'w') as fh:
        json.dump([{k: (float(v) if isinstance(v, (np.floating, float)) else v) for k, v in r.items() if not isinstance(v, np.ndarray)}
                   for r in all_recs], fh)
    # summary
    stats = {'takes': len(takes), 'notes': len(all_recs), 'notes_with_decay': int(out['n_dec'].sum()), 'B_fits': int(out['n_B'].sum()),
             'cells_with_data': int((out['n_att'] > 0).sum()), 'cells_lt_8': int((out['n_att'] < 8).sum()),
             'n_att': out['n_att'].astype(int).tolist(), 'n_dec': out['n_dec'].astype(int).tolist(), 'n_B': out['n_B'].astype(int).tolist(),
             'B_med': [[None if not np.isfinite(v) else float(v) for v in row] for row in out['B_med']],
             'cents_med': [[None if not np.isfinite(v) else float(v) for v in row] for row in out['cents_med']]}
    # B law fit per string: log2 B = log2 B_open + slope * fret / 6, weighted by n_B, cells with >= 5 fits
    law = {}
    for s in range(6):
        fr = np.array([f for f in range(T.FRETS) if out['n_B'][s, f] >= 5])
        if len(fr) >= 3:
            y = np.log2(out['B_med'][s, fr])
            wgt = np.sqrt(out['n_B'][s, fr])
            A = np.stack([np.ones_like(fr, dtype=float), fr / 6.0], axis=1) * wgt[:, None]
            c, *_ = np.linalg.lstsq(A, y * wgt, rcond=None)
            # same with the slope fixed at 1 (doubling every 6 frets)
            b0 = np.sum(wgt ** 2 * (y - fr / 6.0)) / np.sum(wgt ** 2)
            law[s] = {'B_open_fit': float(2 ** c[0]), 'slope_per_6_frets': float(c[1]), 'B_open_slope1': float(2 ** b0),
                      'frets_used': fr.tolist(), 'barbancho_acoustic': T.B_OPEN[s]}
    stats['B_law'] = law
    stats['prof_att'] = out['prof_att'].tolist()
    stats['prof_dec'] = out['prof_dec'].tolist()
    with open(os.path.join(a.out, 'stats.json'), 'w') as fh:
        json.dump(stats, fh, indent=1)
    print(f"notes {stats['notes']}, with decay {stats['notes_with_decay']}, B fits {stats['B_fits']}, cells with data {stats['cells_with_data']}/120, cells < 8 notes {stats['cells_lt_8']}")
    print('string  B_open(Barbancho)  B_open fit  slope/6 frets  B_open(slope 1)  frets')
    for s in range(6):
        if s in law:
            l = law[s]
            print(f"{s:6d}  {l['barbancho_acoustic']:.2e}  {l['B_open_fit']:.2e}  {l['slope_per_6_frets']:.2f}  {l['B_open_slope1']:.2e}  {l['frets_used'][0]}-{l['frets_used'][-1]}")
    print('median B per cell (string x fret 0..12):')
    for s in range(6):
        print(f'  s{s}: ' + ' '.join(f"{out['B_med'][s, f]:.1e}" if np.isfinite(out['B_med'][s, f]) else '   -   ' for f in range(13))
              + '   n: ' + ' '.join(f"{int(out['n_B'][s, f])}" for f in range(13)))
    print('median tuning offset (cents) per string, open..fret 5: ' + '; '.join(
        f"s{s} " + ' '.join(f"{out['cents_med'][s, f]:+.0f}" if np.isfinite(out['cents_med'][s, f]) else '-' for f in range(6)) for s in range(6)))


if __name__ == '__main__':
    main()
