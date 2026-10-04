#!/usr/bin/env python3
"""Builds exactly-labelled polyphony for the jalon 1 bis prototype by MIXING
solo GuitarSet takes of different players (real pickup signal, exact labels),
because the hexaphonic stems were not available.

  python3 test/poly/make_mixes.py --guitarset <dir> --out <mixdir>

Rule (deterministic, from the 24-take solo list of test/takes.json, indices
i = 0..23, 4 takes per player in order):
  dyads  (2 takes): (i, i+9 mod 24)            for i = 0..11  -> 12 mixes, players differ
  triads (3 takes): (i, i+9 mod 24, i+18 mod 24) for i = 0..7  ->  8 mixes, 3 different players
Each take is scaled to the same RMS (-26 dBFS, about the median of the solo
list) before the unit-gain sum ("equal gains"); the mix is truncated to the
shortest take; if the sum peaks above 0.98 the whole mix is scaled down (the
labels do not depend on level). Reference = union of the note_midi
annotations of the constituents (notes cut at the truncation point, notes
starting after it dropped). The output directory mimics GuitarSet so that
test/score.py and test/dump-events.mjs run unchanged on it:
  <mixdir>/annotation/<name>.jams, <mixdir>/audio_mono-pickup_mix/<name>_mix.wav
plus symlinks to the original solo and comp takes, and <mixdir>/takes.json
with lists "solo", "comp" (copied), "mix2", "mix3", and "dev"/"test" splits.
"""
import argparse
import json
import os

import numpy as np
import soundfile as sf

TARGET_RMS_DB = -26.0
PEAK_MAX = 0.98
# DEV subset = players 00 and 03 (solo and comp) and the mixes whose first
# constituent index is 0..3 (player 00); everything else is TEST.
DEV_PLAYERS = ('00', '03')


def read_jams(path):
    with open(path) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--guitarset', required=True)
    ap.add_argument('--takes', default=os.path.join(os.path.dirname(__file__), '..', 'takes.json'))
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    takes = json.load(open(a.takes))
    solo = takes['solo']
    ann_dir = os.path.join(a.out, 'annotation')
    wav_dir = os.path.join(a.out, 'audio_mono-pickup_mix')
    os.makedirs(ann_dir, exist_ok=True)
    os.makedirs(wav_dir, exist_ok=True)
    for t in solo + takes['comp']:
        for sub, fn in (('annotation', t + '.jams'), ('audio_mono-pickup_mix', t + '_mix.wav')):
            dst = os.path.join(a.out, sub, fn)
            if not os.path.lexists(dst):
                os.symlink(os.path.abspath(os.path.join(a.guitarset, sub, fn)), dst)

    groups = [('mix2', [(i, (i + 9) % 24) for i in range(12)]),
              ('mix3', [(i, (i + 9) % 24, (i + 18) % 24) for i in range(8)])]
    lists = {'solo': solo, 'comp': takes['comp'], 'mix2': [], 'mix3': []}
    recipe = {}
    for lname, combos in groups:
        for combo in combos:
            names = [solo[i] for i in combo]
            xs, jams, gains = [], [], []
            for n in names:
                x, sr = sf.read(os.path.join(a.guitarset, 'audio_mono-pickup_mix', n + '_mix.wav'), dtype='float64', always_2d=True)
                x = x[:, 0]
                rms = np.sqrt(np.mean(x ** 2))
                g = 10 ** (TARGET_RMS_DB / 20) / rms
                xs.append(x * g)
                gains.append(float(g))
                jams.append(read_jams(os.path.join(a.guitarset, 'annotation', n + '.jams')))
            L = min(len(x) for x in xs)
            mix = sum(x[:L] for x in xs)
            dur = L / sr
            peak = float(np.abs(mix).max())
            post = 1.0 if peak <= PEAK_MAX else PEAK_MAX / peak
            mix *= post
            mixname = f"{lname}_{'+'.join(f'{i:02d}' for i in combo)}"
            sf.write(os.path.join(wav_dir, mixname + '_mix.wav'), np.clip(mix, -1, 1).astype(np.float32), sr, subtype='PCM_16')
            out = {'annotations': [], 'file_metadata': {'duration': dur, 'title': mixname,
                                                         'sources': names, 'gains': gains, 'post_gain': post}}
            for j in jams:
                for ann in j['annotations']:
                    if ann['namespace'] != 'note_midi':
                        continue
                    data = []
                    for d in ann['data']:
                        if d['time'] >= dur:
                            continue
                        dd = dict(d)
                        dd['duration'] = min(d['duration'], dur - d['time'])
                        data.append(dd)
                    out['annotations'].append({'namespace': 'note_midi', 'annotation_metadata': ann['annotation_metadata'], 'data': data})
            with open(os.path.join(ann_dir, mixname + '.jams'), 'w') as f:
                json.dump(out, f)
            lists[lname].append(mixname)
            recipe[mixname] = {'sources': names, 'gains_db': [20 * np.log10(g) for g in gains], 'post_gain': post, 'duration': dur,
                               'notes': sum(len(x['data']) for x in out['annotations'])}
            print(f'{mixname}: {dur:.1f} s, {recipe[mixname]["notes"]} notes, peak {peak:.2f}, post {post:.2f}')
    dev = {k: [] for k in lists}
    test = {k: [] for k in lists}
    for k in ('solo', 'comp'):
        for t in lists[k]:
            (dev if t[:2] in DEV_PLAYERS else test)[k].append(t)
    for k in ('mix2', 'mix3'):
        for t in lists[k]:
            (dev if int(t.split('_')[1].split('+')[0]) < 4 else test)[k].append(t)
    lists['_comment'] = __doc__.strip().splitlines()
    lists['dev'] = dev
    lists['test'] = test
    lists['recipe'] = recipe
    for k in ('solo', 'comp', 'mix2', 'mix3'):
        lists['dev_' + k] = dev[k]
        lists['test_' + k] = test[k]
    with open(os.path.join(a.out, 'takes.json'), 'w') as f:
        json.dump(lists, f, indent=1)


if __name__ == '__main__':
    main()
