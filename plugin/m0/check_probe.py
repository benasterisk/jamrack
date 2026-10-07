#!/usr/bin/env python3
"""Checks the MIDI that the installed M0 prototypes emit, as recorded by
plugin/tools/vst3_probe.cpp, against what plugin/m0/PluginProcessor.cpp is
meant to do (docs/plugin-plan.md, M0 task 2). No DAW, no person needed.

  python plugin/m0/check_probe.py --probe <vst3_probe.exe> --outdir <dir> [--vst3 D:/VST3]

Builds test WAVs (10 plucks, one per second, known onsets) at 44.1, 48 and
96 kHz, runs both plugins through the probe at every block size from 32 to
1024, plus a MIDI-channel test and a host-bypass test, and checks per note:
  - note-on 60, velocity 100, on the requested channel, within 2 ms of the
    attack, preceded at the same sample by a pitch-wheel reset to 8192;
  - pitch wheel only DURING the note: rising from 8192 to ~12288 between
    100 and 250 ms, falling back to 8192 by 400 ms, one message per 64
    samples, monotonic on each side;
  - note-off 600 ms after the note-on (to the sample), with the wheel back
    to 8192 at the same sample;
  - no event at all outside the notes, no hanging note at the end;
  - effect: audio passes through bit-exactly; instrument: silent output;
  - identical event positions whatever the block size (sample-accurate);
  - host bypass in mid-note: note-off and wheel reset at the bypass
    block, nothing while bypassed, normal notes again afterwards.
Exit code 1 on any failure; a Markdown summary is printed.
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

ONSETS = [0.5 + k * 1.0 for k in range(10)]
PLUGINS = {'Fx': 'JAMRACK GTM Fx', 'Inst': 'JAMRACK GTM Inst'}
RATES = [44100, 48000, 96000]
BLOCKS = [32, 64, 128, 256, 512, 1024]


def make_wav(path, sr):
    """10 plucks (110 Hz, 60 ms decay, a click on the attack), peak -6 dBFS."""
    x = np.zeros(int(sr * 11.0))
    rng = np.random.default_rng(1)
    for t0 in ONSETS:
        n0, n = int(round(t0 * sr)), int(0.6 * sr)
        t = np.arange(n) / sr
        s = sum((1.0 / k) * np.sin(2 * np.pi * k * 110.0 * t) for k in range(1, 15))
        env = np.minimum(1, t / 0.0005) * np.exp(-t / 0.06)
        click = np.exp(-t / 0.0008) * rng.standard_normal(n) * 0.5 * np.minimum(1, t / 0.0002)
        x[n0:n0 + n] += 0.35 * env * s + click
    x /= np.max(np.abs(x)) / 0.5
    sf.write(path, x.astype(np.float32), sr, subtype='PCM_16')


def run_probe(probe, plugin, wav, out, block, extra=()):
    r = subprocess.run([probe, '--plugin', plugin, '--wav', wav, '--out', out, '--block', str(block), *extra],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f'probe failed ({plugin}, {block}): {r.stderr.strip()}')
    return json.load(open(out, encoding='utf-8'))


def check_notes(d, sr, channel=1, bypass=None):
    """Returns (problems, per-note facts). bypass = (from_s, to_s) or None."""
    probs = []
    ev = d['events']
    ons = [e for e in ev if e['type'] == 'on']
    offs = [e for e in ev if e['type'] == 'off']
    wheel = [e for e in ev if e['type'] == 'wheel']
    other = [e for e in ev if e['type'] not in ('on', 'off', 'wheel')]
    if other:
        probs.append(f'{len(other)} unexpected messages: {other[:2]}')
    if any(e['ch'] != channel for e in ev):
        probs.append(f'events on another channel than {channel}: {sorted({e["ch"] for e in ev})}')
    if any(e['type'] == 'on' and (e['note'] != 60 or e['vel'] != 100) for e in ev):
        probs.append('a note-on is not note 60 velocity 100')
    if len(ons) != len(offs):
        probs.append(f'{len(ons)} note-ons but {len(offs)} note-offs (hanging note)')
    note_len = int(round(0.6 * sr))
    expected_onsets = ONSETS
    if bypass:
        b0, b1 = bypass
        # the note that started before the bypass is cut at the bypass block;
        # attacks inside the bypass window produce nothing
        expected_onsets = [t for t in ONSETS if t < b0 or t >= b1]
    if len(ons) != len(expected_onsets):
        probs.append(f'{len(ons)} notes for {len(expected_onsets)} attacks')
    facts = []
    for i, on in enumerate(ons):
        s_on = on['sample']
        t0 = min(expected_onsets, key=lambda t: abs(t - on['t']))
        lat_ms = 1000 * (on['t'] - t0)
        if not 0 <= lat_ms <= 2.0:
            probs.append(f'note {i}: note-on {lat_ms:.2f} ms after the attack (expected 0..2)')
        if not any(w['sample'] == s_on and w['value'] == 8192 for w in wheel):
            probs.append(f'note {i}: no wheel reset at the note-on sample')
        nxt = [o for o in offs if o['sample'] >= s_on]
        if not nxt:
            continue
        s_off = nxt[0]['sample']
        cut = bypass and bypass[0] * sr <= s_off < bypass[1] * sr + sr
        if not cut and s_off - s_on != note_len:
            probs.append(f'note {i}: note-off {s_off - s_on} samples after the note-on (expected {note_len})')
        if not any(w['sample'] == s_off and w['value'] == 8192 for w in wheel):
            probs.append(f'note {i}: wheel not reset at the note-off sample')
        during = [w for w in wheel if s_on < w['sample'] < s_off]
        if not cut:
            vals = [w['value'] for w in during]
            times = [(w['sample'] - s_on) / sr * 1000 for w in during]
            if not vals:
                probs.append(f'note {i}: no pitch wheel during the note')
            else:
                peak = max(vals)
                t_peak = times[vals.index(peak)]
                if not 12200 <= peak <= 12288:
                    probs.append(f'note {i}: wheel peak {peak} (expected ~12288)')
                if not 248 <= t_peak <= 252:
                    probs.append(f'note {i}: wheel peak at {t_peak:.1f} ms (expected 250)')
                ramp = [(t, v) for t, v in zip(times, vals) if v != 8192]
                if ramp and not (99 <= ramp[0][0] <= 102 and 398 <= ramp[-1][0] <= 401):
                    probs.append(f'note {i}: bend from {ramp[0][0]:.1f} to {ramp[-1][0]:.1f} ms (expected 100..400)')
                up = [v for t, v in zip(times, vals) if t <= t_peak]
                down = [v for t, v in zip(times, vals) if t >= t_peak]
                if any(b < a for a, b in zip(up, up[1:])) or any(b > a for a, b in zip(down, down[1:])):
                    probs.append(f'note {i}: wheel ramp not monotonic')
                gaps = np.diff([w['sample'] for w in during if 0.1 * sr <= w['sample'] - s_on < 0.4 * sr])
                if len(gaps) and (gaps.min() < 64 or gaps.max() > 64):
                    probs.append(f'note {i}: wheel spacing {gaps.min()}..{gaps.max()} samples (expected 64)')
            facts.append({'lat_ms': lat_ms, 'peak': peak if vals else None, 'n_wheel': len(vals)})
    # nothing between the notes
    spans = []
    for on in ons:
        nxt = [o for o in offs if o['sample'] >= on['sample']]
        spans.append((on['sample'], nxt[0]['sample'] if nxt else 10 ** 12))
    stray = [e for e in ev if not any(a <= e['sample'] <= b for a, b in spans)]
    if stray and not bypass:
        probs.append(f'{len(stray)} events outside any note, first {stray[0]}')
    if bypass:
        inside = [e for e in ev if bypass[0] * sr + d['block'] <= e['sample'] < bypass[1] * sr]
        if inside:
            probs.append(f'{len(inside)} events while bypassed, first {inside[0]}')
    return probs, facts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--probe', required=True)
    ap.add_argument('--outdir', required=True)
    ap.add_argument('--vst3', default='D:/VST3')
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    rows, failures, ref = [], 0, {}
    for sr in RATES:
        wav = os.path.join(a.outdir, f'plucks-{sr}.wav')
        make_wav(wav, sr)
        for kind, name in PLUGINS.items():
            plugin = f'{a.vst3}/{name}.vst3'
            for block in BLOCKS:
                d = run_probe(a.probe, plugin, wav, os.path.join(a.outdir, f'{kind}-{sr}-{block}.json'), block)
                probs, facts = check_notes(d, sr)
                if kind == 'Fx' and d['passThroughMaxDiff'] != 0:
                    probs.append(f'audio not passed through (max diff {d["passThroughMaxDiff"]})')
                if kind == 'Inst' and d['outputMaxAbs'] != 0:
                    probs.append(f'instrument output not silent (max {d["outputMaxAbs"]})')
                if d['latencySamples'] != 0:
                    probs.append(f'reported latency {d["latencySamples"]}')
                pos = [(e['type'], e['sample'], e.get('value'), e.get('note')) for e in d['events']]
                key = (kind, sr)
                if key not in ref:
                    ref[key] = pos
                elif pos != ref[key]:
                    probs.append('events differ from the 32-sample-block run (not sample-accurate)')
                lat = [f['lat_ms'] for f in facts]
                rows.append((kind, sr, block, len([e for e in d['events'] if e['type'] == 'on']),
                             f'{np.median(lat):.2f}' if lat else '-', 'OK' if not probs else '; '.join(probs[:3])))
                failures += bool(probs)
    # MIDI channel 5
    sr = 48000
    wav = os.path.join(a.outdir, f'plucks-{sr}.wav')
    for kind, name in PLUGINS.items():
        d = run_probe(a.probe, f'{a.vst3}/{name}.vst3', wav, os.path.join(a.outdir, f'{kind}-ch5.json'), 128, ['--channel', '5'])
        probs, _ = check_notes(d, sr, channel=5)
        rows.append((kind, sr, '128, MIDI CH 5', len([e for e in d['events'] if e['type'] == 'on']), '-', 'OK' if not probs else '; '.join(probs[:3])))
        failures += bool(probs)
    # host bypass in mid-note: from 300 ms into the 3rd note (t = 2.8 s) to 4.2 s
    for kind, name in PLUGINS.items():
        d = run_probe(a.probe, f'{a.vst3}/{name}.vst3', wav, os.path.join(a.outdir, f'{kind}-bypass.json'), 128,
                      ['--bypass-from', '2.8', '--bypass-to', '4.2'])
        probs, _ = check_notes(d, sr, bypass=(2.8, 4.2))
        cut = [e for e in d['events'] if e['type'] == 'off' and abs(e['t'] - 2.8) < 0.01]
        if not cut:
            probs.append('no note-off at the bypass')
        rows.append((kind, sr, '128, bypass 2.8-4.2 s', len([e for e in d['events'] if e['type'] == 'on']), '-',
                     'OK' if not probs else '; '.join(probs[:3])))
        failures += bool(probs)

    print('| plugin | Hz | tampon | notes | attaque → note-on (médiane, ms) | résultat |')
    print('|---|---|---|---|---|---|')
    for r in rows:
        print('| ' + ' | '.join(str(x) for x in r) + ' |')
    print(f'\n{len(rows)} runs, {failures} failing')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
