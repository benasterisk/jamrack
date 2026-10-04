#!/usr/bin/env python3
"""Scores note-event JSON files (test/dump-events.mjs format) against GuitarSet
JAMS annotations with mir_eval, and prints a Markdown table — one column per
events file. This is the single evaluation script of docs/polyphonic-plan.md
(jalon 1); every engine (mono, POLY, Basic Pitch) is scored by it on the
fixed take list of test/takes.json.

  python3 test/score.py --guitarset <dir> events-mono-solo.json [events-poly-solo.json ...]
  python3 test/score.py --guitarset <dir> --basic-pitch solo --out events-bp-solo.json

Dependencies (evaluation only, never the app): numpy, soundfile, mir_eval
(pip3 install --user mir_eval), basic-pitch for the --basic-pitch dump.

Reference notes are the six per-string `note_midi` annotations of each take.
Matching: mir_eval.transcription.match_notes, onset within the tolerance,
pitch within 50 cents, offsets ignored unless stated. The "primary" matching
(±50 ms, no offsets) is the one used for chord-size recall, ghost / stuck /
octave / early notes and latency.

Metrics:
  F1 / precision / recall at ±50 ms and ±20 ms, without and with offsets
    (offset within 20 % of the reference duration, at least 50 ms), pooled
    over the takes (sum of matched / emitted / reference notes).
  Recall by chord size: reference notes are grouped when their onsets fall
    within 50 ms of the group's first onset; classes 1 / 2 / 3 / 4 / 5-6.
  Ghost notes: emitted notes shorter than 30 ms and unmatched.
  Stuck notes: matched emitted notes longer than 2 x reference duration + 0.5 s.
  Octave errors: unmatched emitted notes 12 semitones (±50 cents) from a
    reference note whose onset is within 50 ms.
  Early notes: matched emitted notes starting > 20 ms before their reference.
  Latency (matched notes): raw = emitted onset − annotated onset; the
    annotation lag = annotated onset − energy onset measured on the mono mix
    of the takes.json solo list (isolated attacks only, see --lag-threshold
    and --lag-set); corrected = raw + lag, i.e. emitted onset − energy onset.
    Positive lag means the annotation is late.
"""
import argparse
import json
import os
import sys
import warnings

import numpy as np

STRINGS = 6
CHORD_WINDOW = 0.05     # s: onsets within this of the group's first onset form a chord
GHOST_MAX = 0.03        # s
STUCK_EXTRA = 0.5       # s: stuck if est_dur > 2 * ref_dur + STUCK_EXTRA
EARLY = 0.02            # s
SIZE_CLASSES = [(1, 1, '1'), (2, 2, '2'), (3, 3, '3'), (4, 4, '4'), (5, 99, '5-6')]


# ----------------------------------------------------------------- readers
def read_jams_notes(path):
    """Reference notes of one take: list of dicts {onset, offset, midi, string}."""
    with open(path) as f:
        jams = json.load(f)
    notes = []
    for a in jams['annotations']:
        if a['namespace'] != 'note_midi':
            continue
        s = int(a['annotation_metadata'].get('data_source') or 0)
        for d in a['data']:
            if d['duration'] <= 0:
                continue
            notes.append({'onset': float(d['time']), 'offset': float(d['time'] + d['duration']),
                          'midi': float(d['value']), 'string': s})
    notes.sort(key=lambda n: (n['onset'], n['midi']))
    return notes


def read_events(path):
    with open(path) as f:
        d = json.load(f)
    for take, t in d['takes'].items():
        for e in t['events']:
            if e['offset'] <= e['onset']:
                e['offset'] = e['onset'] + 1e-3
    return d


def read_wav_mono(path):
    import soundfile as sf
    x, sr = sf.read(path, dtype='float32', always_2d=True)
    return x[:, 0], sr


# ------------------------------------------------------------- mir_eval glue
def midi_to_hz(m):
    return 440.0 * 2.0 ** ((np.asarray(m, dtype=float) - 69.0) / 12.0)


def arrays(notes):
    if not notes:
        return np.zeros((0, 2)), np.zeros(0)
    iv = np.array([[n['onset'], n['offset']] for n in notes], dtype=float)
    return iv, midi_to_hz([n['midi'] for n in notes])


def match(ref, est, onset_tol, offsets):
    """List of (ref_index, est_index) pairs."""
    from mir_eval.transcription import match_notes
    if not ref or not est:
        return []
    ri, rp = arrays(ref)
    ei, ep = arrays(est)
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        return match_notes(ri, rp, ei, ep, onset_tolerance=onset_tol, pitch_tolerance=50.0,
                           offset_ratio=(0.2 if offsets else None), offset_min_tolerance=0.05)


def chord_sizes(ref):
    """Chord size of each reference note (grouping by onset proximity)."""
    sizes = np.zeros(len(ref), dtype=int)
    i = 0
    while i < len(ref):
        j = i
        while j + 1 < len(ref) and ref[j + 1]['onset'] - ref[i]['onset'] <= CHORD_WINDOW:
            j += 1
        sizes[i:j + 1] = j - i + 1
        i = j + 1
    return sizes


# ----------------------------------------------------------- annotation lag
def energy_onsets(x, sr, ref, threshold):
    """Energy-based onset for each reference note whose attack is isolated
    (no other reference onset in [−150 ms, +50 ms]); NaN otherwise.
    Envelope: RMS over 2 ms, hop 0.5 ms. Baseline = minimum of the envelope in
    [t−60 ms, t−5 ms]; peak = maximum in [t−30 ms, t+60 ms]; the onset is the
    first time after the baseline where the envelope exceeds
    baseline + threshold × (peak − baseline). Needs a rise of ≥ 10 dB."""
    hop = int(round(sr * 0.0005))
    win = int(round(sr * 0.002))
    n = (len(x) - win) // hop
    idx = np.arange(n)[:, None] * hop + np.arange(win)[None, :]
    env = np.sqrt(np.mean(x[idx] ** 2, axis=1)) + 1e-9
    t_env = (np.arange(n) * hop + win) / sr   # envelope stamped at window end
    onsets = np.array([r['onset'] for r in ref])
    out = np.full(len(ref), np.nan)
    for k, t in enumerate(onsets):
        d = onsets - t
        if np.any((d != 0) & (d > -0.15) & (d < 0.05)):
            continue
        a = np.searchsorted(t_env, t - 0.06)
        b = np.searchsorted(t_env, t - 0.005)
        c = np.searchsorted(t_env, t - 0.03)
        e = np.searchsorted(t_env, t + 0.06)
        if b <= a or e <= c or e > n:
            continue
        ib = a + int(np.argmin(env[a:b]))
        base = env[ib]
        peak = env[c:e].max()
        if 20 * np.log10(peak / base) < 10:
            continue
        thr = base + threshold * (peak - base)
        above = np.nonzero(env[ib:e] >= thr)[0]
        if len(above) == 0:
            continue
        out[k] = t_env[ib + above[0]]
    return out


def measure_lag(guitarset, takes_ref, threshold):
    lags = []
    for take, ref in takes_ref.items():
        wav = os.path.join(guitarset, 'audio_mono-pickup_mix', take + '_mix.wav')
        if not os.path.exists(wav):
            continue
        x, sr = read_wav_mono(wav)
        eo = energy_onsets(x, sr, ref, threshold)
        for r, t in zip(ref, eo):
            if not np.isnan(t):
                lags.append(r['onset'] - t)
    return np.array(lags)


# ------------------------------------------------------------------ scoring
def quant(v, q):
    return float(np.percentile(v, q)) if len(v) else float('nan')


def score_file(events, takes_ref):
    tot = {'ref': 0, 'est': 0, 'seconds': 0.0}
    for key in ('m50', 'm20', 'm50off', 'm20off'):
        tot[key] = 0
    cls_ref = {c[2]: 0 for c in SIZE_CLASSES}
    cls_hit = {c[2]: 0 for c in SIZE_CLASSES}
    ghosts = stuck = octave = early = 0
    lat = []
    per_take = {}
    for take, ref in takes_ref.items():
        if take not in events['takes']:
            print(f'warning: {take} missing from events file', file=sys.stderr)
            continue
        est = events['takes'][take]['events']
        tot['ref'] += len(ref)
        tot['est'] += len(est)
        tot['seconds'] += events['takes'][take].get('duration', 0)
        m50 = match(ref, est, 0.05, False)
        tot['m50'] += len(m50)
        tot['m20'] += len(match(ref, est, 0.02, False))
        tot['m50off'] += len(match(ref, est, 0.05, True))
        tot['m20off'] += len(match(ref, est, 0.02, True))
        per_take[take] = (len(ref), len(est), len(m50))
        sizes = chord_sizes(ref)
        matched_ref = {r for r, _ in m50}
        matched_est = {e: r for r, e in m50}
        for i, s in enumerate(sizes):
            for lo, hi, name in SIZE_CLASSES:
                if lo <= s <= hi:
                    cls_ref[name] += 1
                    if i in matched_ref:
                        cls_hit[name] += 1
        ref_on = np.array([r['onset'] for r in ref]) if ref else np.zeros(0)
        ref_mi = np.array([r['midi'] for r in ref]) if ref else np.zeros(0)
        for j, e in enumerate(est):
            if j in matched_est:
                r = ref[matched_est[j]]
                lat.append(e['onset'] - r['onset'])
                if e['offset'] - e['onset'] > 2 * (r['offset'] - r['onset']) + STUCK_EXTRA:
                    stuck += 1
                if e['onset'] < r['onset'] - EARLY:
                    early += 1
            else:
                if e['offset'] - e['onset'] < GHOST_MAX:
                    ghosts += 1
                near = np.abs(ref_on - e['onset']) <= 0.05
                if np.any(near & (np.abs(np.abs(ref_mi - e['midi']) - 12) <= 0.5)):
                    octave += 1
    lat = np.array(lat)
    return dict(tot=tot, cls_ref=cls_ref, cls_hit=cls_hit, ghosts=ghosts, stuck=stuck,
                octave=octave, early=early, lat=lat, per_take=per_take)


def prf(m, est, ref):
    p = m / est if est else float('nan')
    r = m / ref if ref else float('nan')
    f = 2 * p * r / (p + r) if (p + r) > 0 else 0.0
    return p, r, f


def fmt_pct(x):
    return 'n/a' if x != x else f'{100 * x:.1f} %'


def fmt_ms(x):
    return 'n/a' if x != x else f'{1000 * x:+.0f} ms'


def build_rows(results, labels, lag, lag_n, lag_threshold):
    rows = []
    lag_med = float(np.median(lag)) if len(lag) else float('nan')

    def row(name, fn):
        rows.append([name] + [fn(r) for r in results])

    row('Prises / notes de référence / notes émises',
        lambda r: f"{len(r['per_take'])} / {r['tot']['ref']} / {r['tot']['est']}")
    for key, tol, off in (('m50', 50, False), ('m20', 20, False), ('m50off', 50, True), ('m20off', 20, True)):
        name = f"F1 ±{tol} ms{' avec fins' if off else ''}"
        row(name, lambda r, key=key: fmt_pct(prf(r['tot'][key], r['tot']['est'], r['tot']['ref'])[2]))
    row('Précision ±50 ms', lambda r: fmt_pct(prf(r['tot']['m50'], r['tot']['est'], r['tot']['ref'])[0]))
    row('Rappel ±50 ms', lambda r: fmt_pct(prf(r['tot']['m50'], r['tot']['est'], r['tot']['ref'])[1]))
    row('Précision ±20 ms', lambda r: fmt_pct(prf(r['tot']['m20'], r['tot']['est'], r['tot']['ref'])[0]))
    row('Rappel ±20 ms', lambda r: fmt_pct(prf(r['tot']['m20'], r['tot']['est'], r['tot']['ref'])[1]))
    for _, _, name in SIZE_CLASSES:
        row(f'Rappel accords de {name} note{"s" if name != "1" else ""} (n réf.)',
            lambda r, name=name: f"{fmt_pct(r['cls_hit'][name] / r['cls_ref'][name]) if r['cls_ref'][name] else 'n/a'} ({r['cls_ref'][name]})")
    row('Fantômes (< 30 ms, non appariées) / émises',
        lambda r: f"{r['ghosts']} ({fmt_pct(r['ghosts'] / r['tot']['est'] if r['tot']['est'] else float('nan'))})")
    row('Notes coincées (> 2× réf. + 0,5 s) / appariées',
        lambda r: f"{r['stuck']} ({fmt_pct(r['stuck'] / r['tot']['m50'] if r['tot']['m50'] else float('nan'))})")
    row("Erreurs d'octave / émises",
        lambda r: f"{r['octave']} ({fmt_pct(r['octave'] / r['tot']['est'] if r['tot']['est'] else float('nan'))})")
    row('Notes avant le pincement (> 20 ms) / appariées',
        lambda r: f"{r['early']} ({fmt_pct(r['early'] / r['tot']['m50'] if r['tot']['m50'] else float('nan'))})")
    row('Latence brute vs annotation : médiane (p25 / p75 / p90)',
        lambda r: f"{fmt_ms(quant(r['lat'], 50))} ({fmt_ms(quant(r['lat'], 25))} / {fmt_ms(quant(r['lat'], 75))} / {fmt_ms(quant(r['lat'], 90))})")
    row(f'Retard annotation GuitarSet vs attaque énergie ({lag_n} notes) : médiane (p25 / p75)',
        lambda r: f"{fmt_ms(lag_med)} ({fmt_ms(quant(lag, 25))} / {fmt_ms(quant(lag, 75))})")
    row('Latence corrigée (brute + retard) : médiane (p25 / p75 / p90)',
        lambda r: f"{fmt_ms(quant(r['lat'], 50) + lag_med)} ({fmt_ms(quant(r['lat'], 25) + lag_med)} / {fmt_ms(quant(r['lat'], 75) + lag_med)} / {fmt_ms(quant(r['lat'], 90) + lag_med)})")
    return rows


def print_table(rows, labels):
    head = ['Mesure'] + labels
    widths = [max(len(str(r[i])) for r in [head] + rows) for i in range(len(head))]
    line = lambda r: '| ' + ' | '.join(str(c).ljust(w) for c, w in zip(r, widths)) + ' |'
    print(line(head))
    print('|' + '|'.join('-' * (w + 2) for w in widths) + '|')
    for r in rows:
        print(line(r))


# -------------------------------------------------------------- Basic Pitch
def dump_basic_pitch(guitarset, takes, out, set_name, label='basic-pitch'):
    """Offline Basic Pitch (Spotify, ICASSP 2022 model, ONNX, CPU) with its
    default thresholds — a non-real-time reference point, not a competitor."""
    import logging
    logging.disable(logging.WARNING)
    from basic_pitch.inference import predict
    result = {'label': label, 'set': set_name, 'source': 'basic_pitch.inference.predict (defaults)', 'takes': {}}
    for take in takes:
        wav = os.path.join(guitarset, 'audio_mono-pickup_mix', take + '_mix.wav')
        _, _, notes = predict(wav)
        x, sr = read_wav_mono(wav)
        ev = [{'onset': float(s), 'offset': float(e), 'midi': int(p), 'velocity': float(a), 'why': 'basic-pitch'}
              for s, e, p, a, _ in notes]
        ev.sort(key=lambda n: n['onset'])
        result['takes'][take] = {'duration': len(x) / sr, 'events': ev}
        print(f'{take}: {len(ev)} notes', file=sys.stderr)
    with open(out, 'w') as f:
        json.dump(result, f)
    print(f'wrote {out}', file=sys.stderr)


# --------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('events', nargs='*', help='events JSON files (test/dump-events.mjs format)')
    ap.add_argument('--guitarset', required=True, help='GuitarSet root (annotation/, audio_mono-pickup_mix/)')
    ap.add_argument('--takes', default=os.path.join(os.path.dirname(__file__), 'takes.json'))
    ap.add_argument('--basic-pitch', metavar='SET', help='dump Basic Pitch events for the solo|comp takes, then exit')
    ap.add_argument('--out', help='output file for --basic-pitch')
    ap.add_argument('--lag-threshold', type=float, default=0.2,
                    help='energy-onset threshold as a fraction of the attack rise (default 0.2)')
    ap.add_argument('--no-lag', action='store_true', help='skip the annotation-lag measurement')
    ap.add_argument('--lag-set', default='solo', help='takes.json list the annotation lag is measured on (default solo)')
    ap.add_argument('--per-take', action='store_true', help='also print per-take ref / emitted / matched')
    ap.add_argument('--json', help='also write the raw numbers to this file')
    a = ap.parse_args()

    if a.basic_pitch:
        with open(a.takes) as f:
            takes = json.load(f)[a.basic_pitch]
        dump_basic_pitch(a.guitarset, takes, a.out or f'events-basic-pitch-{a.basic_pitch}.json', a.basic_pitch)
        return
    if not a.events:
        ap.error('no events file given')

    events = [read_events(p) for p in a.events]
    labels = [(e.get('label') or os.path.basename(p)) + (f" ({e['set']})" if e.get('set') else '')
              for e, p in zip(events, a.events)]
    all_takes = sorted({t for e in events for t in e['takes']})
    takes_ref = {t: read_jams_notes(os.path.join(a.guitarset, 'annotation', t + '.jams')) for t in all_takes}

    results = [score_file(e, {t: takes_ref[t] for t in e['takes']}) for e in events]
    lag = np.zeros(0)
    if not a.no_lag:
        # the lag is a property of the dataset, measured on isolated attacks:
        # always the takes.json solo list (comp strums make it ambiguous)
        with open(a.takes) as f:
            lag_takes = json.load(f)[a.lag_set]
        lag_ref = {t: takes_ref.get(t) or read_jams_notes(os.path.join(a.guitarset, 'annotation', t + '.jams'))
                   for t in lag_takes}
        lag = measure_lag(a.guitarset, lag_ref, a.lag_threshold)
        if len(lag) < 500:
            print(f'warning: annotation lag measured on only {len(lag)} notes (< 500)', file=sys.stderr)
    rows = build_rows(results, labels, lag, len(lag), a.lag_threshold)
    print_table(rows, labels)
    if a.per_take:
        print()
        print_table([[t] + [f"{r['per_take'][t][0]} / {r['per_take'][t][1]} / {r['per_take'][t][2]}" if t in r['per_take'] else '' for r in results]
                     for t in all_takes], [f'{l}: réf / émises / appariées' for l in labels])
    if a.json:
        with open(a.json, 'w') as f:
            json.dump({'labels': labels, 'lag_ms': [1000 * float(v) for v in lag],
                       'results': [{k: (v.tolist() if isinstance(v, np.ndarray) else v) for k, v in r.items()} for r in results]}, f)


if __name__ == '__main__':
    main()
