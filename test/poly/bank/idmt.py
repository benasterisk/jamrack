#!/usr/bin/env python3
"""IDMT-SMT-Guitar (Zenodo 7544110, CC BY-NC-ND 4.0: evaluation only, nothing
redistributed) as a cross-guitar check of the POLY template banks.

  python3 test/poly/bank/idmt.py convert --files <dir with IDMT-SMT-GUITAR_V2/...> --out <mixdir> [--no-align] [--jams-only]
  python3 test/poly/bank/idmt.py bfit    --files <dir> --out <json>

convert: writes a GuitarSet-like directory (annotation/<take>.jams with one
note_midi annotation per string, data_source = string 0..5 from the low E;
audio_mono-pickup_mix/<take>_mix.wav, mono 44.1 kHz) so that analyze.py,
notes.py and score.py run unchanged, plus takes.json with the lists:
  frets_AR / frets_FS / frets_LP   dataset2 "<string>_fret_0-20" runs, every
                                   fret of every string, picked, one guitar
                                   each (AR Aristides 010, FS Fender
                                   Stratocaster, LP Gibson Les Paul, DI)
  licks_AR / licks_FS / licks_LP   dataset2 licks 1-12, finger (FN) and
                                   pick (KN), normal expression, mono and
                                   polyphonic parts
  licks_muted                      the palm-muted (MN) versions, all guitars
  pieces                           dataset3: five short pieces, Ibanez RG2820
  chords_strat / chords_ibanez     dataset1 chords: A/E shapes major/minor,
                                   barré up to the 10th fret, 4-6 notes per
                                   strum, Fender Strat (neck single coil)
                                   and Ibanez RG2820 (bridge humbucker)
  solo = the fret runs (for score.py's annotation-lag measurement).
Dead notes (expressionStyle DN) are dropped from the reference. Each wav is
scaled so that its active frames (50 ms frames above -45 dBFS) have an RMS
of -26 dBFS, the level of the GuitarSet solo constituents of make_mixes.py
(a web visitor sets a comparable input gain); the original level is kept
in takes.json.
ONSET ALIGNMENT (default, --no-align disables it): the IDMT onsets are
annotated ~40 ms BEFORE the energy rise (score.py's lag on the 376 isolated
notes of the fret runs: median -39.5 ms, p25/p75 -45/-33) where GuitarSet's
are 9 ms after it, and the dataset1 chords carry a synthetic per-string
spacing; the +-50 ms matching of score.py would miss most notes. The
reference is therefore re-aligned to GuitarSet's convention: the notes of a
take are grouped by onset (within 50 ms), the energy onset of each group is
measured as score.py does for the lag (2 ms RMS envelope, 20 % of the rise,
>= 10 dB, no other group within [-150, +50] ms), the group's first onset is
set to that energy onset + 9 ms and the other notes of the group keep their
annotated spacing; a group without a measurable energy onset is shifted by
the take's median shift (or the list's when the take has none). The applied
shifts are stored per take in takes.json (recipe[take].align) and the
original onsets are kept in the jams (value "onset_idmt").
bfit: fits the inharmonicity of every note of the fret runs (extract.py's
fit_inharmonicity on the DI signal) and prints B per string x fret and the
B law per string and guitar (frets 0-12), next to the GuitarSet law.
"""
import argparse
import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import templates as T  # noqa: E402

TARGET_RMS_DB = -26.0
GUITARS = {'AR': 'Aristides 010', 'FS': 'Fender Stratocaster', 'LP': 'Gibson Les Paul'}


def read_xml(path):
    root = ET.parse(path).getroot()
    g = {c.tag: (c.text or '').strip() for c in root.find('globalParameter')}
    ev = []
    for e in root.find('transcription').findall('event'):
        d = {c.tag: (c.text or '').strip() for c in e}
        ev.append({'midi': int(float(d['pitch'])), 'onset': float(d['onsetSec']), 'offset': float(d['offsetSec']),
                   'string': int(d.get('stringNumber', 0)) - 1, 'fret': int(d.get('fretNumber', -1)),
                   'exc': d.get('excitationStyle', ''), 'expr': d.get('expressionStyle', '')})
    return g, ev


def collect(files):
    """{take name: (xml, wav, list name)}"""
    root = os.path.join(files, 'IDMT-SMT-GUITAR_V2')
    out = {}
    for xml in sorted(glob.glob(os.path.join(root, 'dataset2', 'annotation', '*.xml'))):
        base = os.path.basename(xml)[:-4]
        m = re.match(r'(AR|FS|LP)_(.*)$', base)
        if not m:
            continue
        g, rest = m.groups()
        if rest.endswith('_fret_0-20'):
            lst = 'frets_' + g
        elif re.match(r'Lick\d+_[FK]N(_Lage\d?)?$', rest):
            lst = 'licks_' + g
        elif re.match(r'Lick\d+_MN(_Lage\d?)?$', rest):
            lst = 'licks_muted'
        else:
            continue
        out['d2_' + base] = (xml, os.path.join(root, 'dataset2', 'audio', base + '.wav'), lst)
    for xml in sorted(glob.glob(os.path.join(root, 'dataset3', 'annotation', '*.xml'))):
        base = os.path.basename(xml)[:-4]
        out['d3_' + base] = (xml, os.path.join(root, 'dataset3', 'audio', base + '.wav'), 'pieces')
    for folder, lst in (('Fender Strat Clean Neck SC Chords', 'chords_strat'), ('Ibanez Power Strat Clean Bridge HU Chords', 'chords_ibanez')):
        for xml in sorted(glob.glob(os.path.join(root, 'dataset1', folder, 'annotation', '*.xml'))):
            base = os.path.basename(xml)[:-4]
            out[f'd1_{lst[7:]}_{base.replace(" ", "_")}'] = (xml, os.path.join(root, 'dataset1', folder, 'audio', base + '.wav'), lst)
    return {k: v for k, v in out.items() if os.path.exists(v[1])}


def active_rms_db(x, sr):
    n = int(0.05 * sr)
    K = len(x) // n
    fr = x[:K * n].reshape(K, n)
    rms = np.sqrt(np.mean(fr ** 2, axis=1)) + 1e-9
    act = rms > 10 ** (-45 / 20)
    if act.sum() == 0:
        return -120.0
    return float(20 * np.log10(np.sqrt(np.mean(rms[act] ** 2))))


def align_groups(ev, x, sr):
    """Per onset group: (first annotated onset, energy onset or None)."""
    sys.path.insert(0, os.path.join(HERE, '..', '..'))
    import score
    ev = sorted(ev, key=lambda e: e['onset'])
    groups = []
    for e in ev:
        if groups and e['onset'] - groups[-1][0]['onset'] <= 0.05:
            groups[-1].append(e)
        else:
            groups.append([e])
    collapsed = [{'onset': g[0]['onset'], 'offset': max(e['offset'] for e in g), 'midi': g[0]['midi'], 'string': 0} for g in groups]
    eo = score.energy_onsets(x, sr, collapsed, 0.2)
    return groups, eo


def convert(a):
    takes = collect(a.files)
    list_shifts = {}
    pending = []
    ann_dir, wav_dir = os.path.join(a.out, 'annotation'), os.path.join(a.out, 'audio_mono-pickup_mix')
    os.makedirs(ann_dir, exist_ok=True)
    os.makedirs(wav_dir, exist_ok=True)
    lists, recipe = {}, {}
    for take, (xml, wav, lst) in takes.items():
        g, ev = read_xml(xml)
        x, sr = sf.read(wav, dtype='float64', always_2d=True)
        x = x[:, 0]
        lvl = active_rms_db(x, sr)
        gain = 10 ** ((TARGET_RMS_DB - lvl) / 20)
        y = x * gain
        peak = float(np.abs(y).max())
        if peak > 0.98:
            y *= 0.98 / peak
            gain *= 0.98 / peak
        if not a.jams_only:
            sf.write(os.path.join(wav_dir, take + '_mix.wav'), y.astype(np.float32), sr, subtype='PCM_16')
        kept = [e for e in ev if e['expr'] != 'DN' and e['offset'] > e['onset'] and 0 <= e['string'] < 6]
        align = None
        if not a.no_align:
            groups, eo = align_groups(kept, y, sr)
            shifts = [float(t + 0.009 - g[0]['onset']) for g, t in zip(groups, eo) if not np.isnan(t)]
            align = {'groups': len(groups), 'measured': len(shifts), 'median_shift': float(np.median(shifts)) if shifts else None, 'groups_data': (groups, eo)}
            list_shifts.setdefault(lst, []).extend(shifts)
        lists.setdefault(lst, []).append(take)
        recipe[take] = {'list': lst, 'source': os.path.relpath(wav, a.files), 'notes': len(kept), 'dropped_dead_notes': len(ev) - len(kept),
                        'duration': len(x) / sr, 'level_db': lvl, 'gain_db': 20 * np.log10(gain), 'sr': sr}
        pending.append((take, lst, kept, align, {'duration': len(x) / sr, 'title': take, 'idmt': os.path.relpath(wav, a.files),
                                                 'instrumentModel': g.get('instrumentModel', ''), 'gain_db': 20 * np.log10(gain)}))
        print(f'{take}: {len(kept)} notes, {len(x) / sr:.1f} s, level {lvl:.1f} dBFS -> gain {20 * np.log10(gain):+.1f} dB')
    for take, lst, kept, align, fm in pending:
        if align is not None:
            groups, eo = align.pop('groups_data')
            fallback = align['median_shift']
            if fallback is None:
                fallback = float(np.median(list_shifts[lst])) if list_shifts.get(lst) else 0.0
            applied = []
            for grp, t in zip(groups, eo):
                shift = float(t + 0.009 - grp[0]['onset']) if not np.isnan(t) else fallback
                for e in grp:
                    e['onset_idmt'] = e['onset']
                    e['onset'] = e['onset'] + shift
                    e['offset'] = max(e['offset'] + shift, e['onset'] + 0.01)
                applied.append(shift)
            align['fallback_shift'] = fallback
            align['applied_median_ms'] = 1000 * float(np.median(applied))
            align['applied_min_ms'], align['applied_max_ms'] = 1000 * float(min(applied)), 1000 * float(max(applied))
            recipe[take]['align'] = align
        anns = []
        for s in range(6):
            data = [{'time': e['onset'], 'duration': e['offset'] - e['onset'], 'value': float(e['midi']), 'confidence': None,
                     'onset_idmt': e.get('onset_idmt', e['onset'])} for e in kept if e['string'] == s]
            anns.append({'namespace': 'note_midi', 'annotation_metadata': {'data_source': str(s)}, 'data': data})
        fm['aligned'] = align is not None
        with open(os.path.join(ann_dir, take + '.jams'), 'w') as f:
            json.dump({'annotations': anns, 'file_metadata': fm}, f)
    if not a.no_align:
        for lst, sh in sorted(list_shifts.items()):
            print(f'align {lst}: {len(sh)} measured groups, shift median {1000 * np.median(sh):+.0f} ms (p25 {1000 * np.percentile(sh, 25):+.0f} / p75 {1000 * np.percentile(sh, 75):+.0f})')
    lists['solo'] = sum((lists.get('frets_' + g, []) for g in GUITARS), [])
    lists['all'] = sorted(takes)
    lists['_comment'] = ['IDMT-SMT-Guitar subset converted by test/poly/bank/idmt.py (see its docstring); CC BY-NC-ND 4.0, evaluation only.']
    lists['recipe'] = recipe
    with open(os.path.join(a.out, 'takes.json'), 'w') as f:
        json.dump(lists, f, indent=1)
    for k, v in lists.items():
        if isinstance(v, list) and not k.startswith('_'):
            print(f'{k}: {len(v)} takes, {sum(recipe[t]["notes"] for t in v if t in recipe)} notes, {sum(recipe[t]["duration"] for t in v if t in recipe):.0f} s')


def bfit(a):
    import extract
    from scipy.signal import resample_poly
    takes = collect(a.files)
    res = {}
    for take, (xml, wav, lst) in sorted(takes.items()):
        if not lst.startswith('frets_'):
            continue
        g = lst[6:]
        _, ev = read_xml(xml)
        x, sr = sf.read(wav, dtype='float64', always_2d=True)
        x = x[:, 0]
        if sr != T.SR:
            d = np.gcd(sr, T.SR)
            x = resample_poly(x, T.SR // d, sr // d)
        for e in ev:
            s, f = e['string'], e['fret']
            if e['expr'] == 'DN' or not (0 <= s < 6) or not (0 <= f < T.FRETS):
                continue
            t, dur = e['onset'], e['offset'] - e['onset']
            if dur < extract.MIN_FIT_DUR:
                continue
            i0, i1 = int((t + extract.FIT_START) * T.SR), int((t + min(dur, extract.FIT_MAX_END)) * T.SR)
            if i1 > len(x):
                continue
            r = extract.fit_inharmonicity(x[i0:i1], T.midi_to_hz(e['midi']))
            if r is None:
                continue
            res.setdefault(g, {}).setdefault(s, {})[f] = {'B': r[0], 'cents': 1200 * np.log2(r[1] / T.midi_to_hz(T.OPEN_MIDI[s] + f)), 'n': r[2]}
    gs = json.load(open(a.gs_stats)) if a.gs_stats else None
    out = {'guitars': {}, 'barbancho_electric': [1.56e-4, 9.04e-5, 5.30e-5, 8.27e-5, 5.02e-5, 1.50e-5]}
    for g, strings in res.items():
        out['guitars'][g] = {'name': GUITARS[g], 'B': {}, 'law': {}}
        print(f'\n{GUITARS[g]} ({g}): B per string x fret 0..12, then law fit')
        for s in range(6):
            cells = strings.get(s, {})
            out['guitars'][g]['B'][s] = {f: cells[f]['B'] for f in cells}
            fr = np.array(sorted(f for f in cells if cells[f]['B'] > 0 and f <= 12))   # frets 13-20: short notes, few partials
            line = f'  s{s}: ' + ' '.join(f"{cells[f]['B']:.1e}" if f in cells else '   -   ' for f in range(13))
            if len(fr) >= 4:
                y = np.log2([cells[f]['B'] for f in fr])
                A = np.stack([np.ones(len(fr)), fr / 6.0], axis=1)
                c, *_ = np.linalg.lstsq(A, y, rcond=None)
                b0 = np.mean(y - fr / 6.0)
                out['guitars'][g]['law'][s] = {'B_open_fit': float(2 ** c[0]), 'slope_per_6_frets': float(c[1]), 'B_open_slope1': float(2 ** b0), 'n_frets': len(fr)}
                line += f'   law: B_open {2 ** c[0]:.2e} slope {c[1]:.2f} (slope 1: {2 ** b0:.2e})'
                if gs and str(s) in gs['B_law']:
                    line += f"   GuitarSet fit {gs['B_law'][str(s)]['B_open_fit']:.2e} slope {gs['B_law'][str(s)]['slope_per_6_frets']:.2f}"
                line += f"   Barbancho electric {out['barbancho_electric'][s]:.2e}"
            print(line)
    with open(a.out, 'w') as f:
        json.dump(out, f, indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['convert', 'bfit'])
    ap.add_argument('--files', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--gs-stats', help='bfit: stats.json of extract.py for the GuitarSet comparison')
    ap.add_argument('--no-align', action='store_true', help='convert: keep the IDMT onsets as annotated')
    ap.add_argument('--jams-only', action='store_true', help='convert: do not rewrite the wavs')
    a = ap.parse_args()
    (convert if a.cmd == 'convert' else bfit)(a)


if __name__ == '__main__':
    main()
