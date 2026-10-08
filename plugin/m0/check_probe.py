#!/usr/bin/env python3
"""Checks the MIDI that the installed M0 prototypes emit, as recorded by
plugin/tools/vst3_probe.cpp, against what plugin/m0/PluginProcessor.cpp is
meant to do (docs/plugin-plan.md, M0 task 2). No DAW, no person needed.

  python plugin/m0/check_probe.py --probe <vst3_probe.exe> --outdir <dir> [--vst3 D:/VST3]

Builds test WAVs, runs both plugins through the probe and checks, per note:
  - note-on 60, velocity 100, on the requested channel, preceded at the same
    sample by a pitch-wheel reset to 8192, within a few ms of a real attack,
    exactly one note per attack that arrives while no note sounds;
  - pitch wheel only DURING the note, one message every 64 samples from the
    note-on: none before 100 ms, every value between 100 and 400 ms within 1
    of the straight line 8192 -> 12288 (250 ms) -> 8192, then exactly one
    8192 within 64 samples after 400 ms and nothing else;
  - note-off 60 on the note's channel 600 ms after the note-on (to the
    sample), or exactly where the host cut it, with the wheel back to 8192
    at the same sample;
  - no event at all outside the notes, no hanging note at the end;
  - effect: audio passes through bit-exactly; instrument: silent output;
  - identical events whatever the block size (sample-accurate);
  - host bypass and deactivate/reactivate in mid-note: note-off and wheel
    reset at that block, nothing more until the first attack that comes
    after, and from there the same events as without the interruption;
  - detector cases: a string left ringing and plucked again every second,
    an attack during a held note, a tone too quiet to trigger, a sustained
    tone, an attack on the very first sample.
It also checks that the installed DLLs are the ones in plugin/build.
Exit code 1 on any failure; a Markdown summary is printed.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

ONSETS = [0.5 + k * 1.0 for k in range(10)]
PLUGINS = {'Fx': ('JAMRACK GTM Fx', 'jamrack_gtm_fx'), 'Inst': ('JAMRACK GTM Inst', 'jamrack_gtm_inst')}
RATES = [44100, 48000, 96000]
BLOCKS = [32, 64, 128, 256, 512, 1024]
NOTE_S, RAMP_S = 0.600, (0.100, 0.250, 0.400)
EVERY = 64


def synth(path, sr, onsets, tau=0.06, length=11.0, replace=False):
    """Plucks (110 Hz, decay tau, a click on the attack), peak -6 dBFS.
    replace: each pluck stops the previous one (the same string plucked again)."""
    x = np.zeros(int(sr * length))
    rng = np.random.default_rng(1)
    for i, t0 in enumerate(onsets):
        n0 = int(round(t0 * sr))
        if replace:
            n = (int(round(onsets[i + 1] * sr)) if i + 1 < len(onsets) else len(x)) - n0
        else:
            n = min(int(0.6 * sr), len(x) - n0)
        t = np.arange(n) / sr
        s = sum((1.0 / k) * np.sin(2 * np.pi * k * 110.0 * t) for k in range(1, 15))
        env = np.minimum(1, t / 0.0005) * np.exp(-t / tau)
        click = np.exp(-t / 0.0008) * rng.standard_normal(n) * 0.5 * np.minimum(1, t / 0.0002)
        if replace:
            x[n0:n0 + n] = 0.35 * env * s + click
        else:
            x[n0:n0 + n] += 0.35 * env * s + click
    x /= np.max(np.abs(x)) / 0.5
    sf.write(path, x.astype(np.float32), sr, subtype='PCM_16')


def tones(path, sr, spans, rms_db, length):
    """220 Hz sine bursts at a given RMS level, 1 ms fade-in, 10 ms fade-out."""
    x = np.zeros(int(sr * length))
    a = np.sqrt(2) * 10 ** (rms_db / 20)
    for t0, t1 in spans:
        n0, n1 = int(round(t0 * sr)), int(round(t1 * sr))
        t = np.arange(n1 - n0) / sr
        fade = np.minimum(1, t / 0.001) * np.minimum(1, (t[-1] - t) / 0.01)
        x[n0:n1] = a * fade * np.sin(2 * np.pi * 220.0 * t)
    sf.write(path, x.astype(np.float32), sr, subtype='FLOAT')


def run_probe(probe, plugin, wav, out, block, extra=()):
    r = subprocess.run([probe, '--plugin', plugin, '--wav', wav, '--out', out, '--block', str(block), *extra],
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f'probe failed ({plugin}, {block}): {r.stderr.strip()}')
    return json.load(open(out, encoding='utf-8'))


def ideal_wheel(rel, rs, rp, re):
    if rel < rp:
        return 8192 + 4096.0 * (rel - rs) / (rp - rs)
    return 12288 - 4096.0 * (rel - rp) / (re - rp)


def check_notes(d, sr, attacks, channel=1, max_lat_ms=2.0, cuts=()):
    """Returns (problems, note-on latencies in ms). attacks: times (s) of the
    attacks that must each give exactly one note; cuts: samples where the host
    interrupted processing (the sounding note must end exactly there)."""
    probs, lats = [], []
    ev = d['events']
    other = [e for e in ev if e['type'] not in ('on', 'off', 'wheel')]
    if other:
        probs.append(f'{len(other)} unexpected messages: {other[:2]}')
    if any(e['ch'] != channel for e in ev):
        probs.append(f'events on another channel than {channel}: {sorted({e["ch"] for e in ev})}')
    L = int(round(NOTE_S * sr))
    rs, rp, re = (int(round(t * sr)) for t in RAMP_S)
    ons = [i for i, e in enumerate(ev) if e['type'] == 'on']
    covered = set()
    matched = set()
    for k, i in enumerate(ons):
        on = ev[i]
        s_on = on['sample']
        if on['note'] != 60 or on['vel'] != 100:
            probs.append(f'note {k}: note-on {on["note"]} velocity {on["vel"]} (expected 60, 100)')
        if not (i > 0 and ev[i - 1]['type'] == 'wheel' and ev[i - 1]['sample'] == s_on and ev[i - 1]['value'] == 8192):
            probs.append(f'note {k}: no wheel reset just before the note-on')
        else:
            covered.add(i - 1)
        covered.add(i)
        # which attack is it?
        near = [a for a in attacks if 0 <= (on['t'] - a) * 1000 <= max_lat_ms]
        if not near:
            probs.append(f'note {k}: note-on at {on["t"]:.4f} s matches no expected attack')
        else:
            matched.add(near[0])
            lats.append(1000 * (on['t'] - near[0]))
        # its note-off
        offs = [j for j in range(i + 1, len(ev)) if ev[j]['type'] == 'off']
        if not offs:
            probs.append(f'note {k}: no note-off (hanging note)')
            continue
        j = offs[0]
        off = ev[j]
        s_off = off['sample']
        nxt_on = [x for x in ons if x > i]
        if nxt_on and nxt_on[0] < j:
            probs.append(f'note {k}: a note-on before its note-off')
        cut = [c for c in cuts if s_on < c < s_on + L]
        want_off = cut[0] if cut else s_on + L
        if s_off != want_off:
            probs.append(f'note {k}: note-off at +{s_off - s_on} samples (expected +{want_off - s_on})')
        if off['note'] != 60 or off['ch'] != on['ch']:
            probs.append(f'note {k}: note-off {off["note"]} on channel {off["ch"]} (expected 60 on {on["ch"]})')
        covered.add(j)
        if not (j + 1 < len(ev) and ev[j + 1]['type'] == 'wheel' and ev[j + 1]['sample'] == s_off and ev[j + 1]['value'] == 8192):
            probs.append(f'note {k}: wheel not reset just after the note-off')
        else:
            covered.add(j + 1)
        # the wheel during the note
        during = [(x, ev[x]) for x in range(i + 1, j) if ev[x]['type'] == 'wheel']
        covered.update(x for x, _ in during)
        if any(e['type'] != 'wheel' for e in ev[i + 1:j]):
            probs.append(f'note {k}: other events inside the note')
        rel = [(e['sample'] - s_on, e['value']) for _, e in during]
        if any(r % EVERY for r, _ in rel):
            probs.append(f'note {k}: wheel off the 64-sample grid')
        if any(r < rs for r, _ in rel):
            probs.append(f'note {k}: wheel before 100 ms')
        span = s_off - s_on
        ramp = [(r, v) for r, v in rel if rs <= r < re]
        want = [r for r in range(EVERY, span, EVERY) if rs <= r < re]
        if [r for r, _ in ramp] != want:
            probs.append(f'note {k}: {len(ramp)} ramp messages, expected {len(want)} (one per 64 samples)')
        bad = [(r, v) for r, v in ramp if abs(v - ideal_wheel(r, rs, rp, re)) > 1]
        if bad:
            probs.append(f'note {k}: wheel off the line at {1000 * bad[0][0] / sr:.1f} ms ({bad[0][1]})')
        after = [(r, v) for r, v in rel if r >= re]
        if span > re + EVERY:
            if len(after) != 1 or after[0][1] != 8192 or after[0][0] >= re + EVERY:
                probs.append(f'note {k}: after 400 ms expected one 8192 within 64 samples, got {after[:3]}')
        elif after and any(v != 8192 for _, v in after):
            probs.append(f'note {k}: wheel not back to 8192 after 400 ms')
    missing = [a for a in attacks if a not in matched]
    if missing:
        probs.append(f'{len(missing)} attacks without a note, first at {missing[0]} s')
    if len(ons) != len(attacks):
        probs.append(f'{len(ons)} notes for {len(attacks)} attacks')
    stray = [e for x, e in enumerate(ev) if x not in covered]
    if stray:
        probs.append(f'{len(stray)} events outside any note, first {stray[0]}')
    return probs, lats


def audio_checks(kind, d):
    p = []
    if kind == 'Fx' and d['passThroughMaxDiff'] != 0:
        p.append(f'audio not passed through (max diff {d["passThroughMaxDiff"]})')
    if kind == 'Inst' and d['outputMaxAbs'] != 0:
        p.append(f'instrument output not silent (max {d["outputMaxAbs"]})')
    if d['latencySamples'] != 0:
        p.append(f'reported latency {d["latencySamples"]}')
    if not (d['layoutAccepted'] and d['inputChannels'] == 2 and d['outputChannels'] == 2):
        p.append(f'layout {d["layoutAccepted"]} {d["inputChannels"]}/{d["outputChannels"]}')
    if d['isInstrument'] != (kind == 'Inst') or not d['producesMidi'] or not d['hasBypassParameter']:
        p.append('instrument/MIDI/bypass flags not as expected')
    # Live 12 refuses an instrument without an event input bus ("no valid event input bus")
    if kind == 'Inst' and not d.get('acceptsMidi'):
        p.append('instrument without a MIDI input bus (Live refuses to open it)')
    return p


def key(e):
    return (e['type'], e['sample'], e['ch'], e.get('note'), e.get('vel'), e.get('value'))


def interrupted(d, ref, cut, resume, block):
    """Events of a run interrupted at sample `cut` (bypass or re-prepare)
    until `resume`, against the uninterrupted run `ref`: identical before the
    cut; at the cut, a note-off + wheel 8192 if a note was sounding; nothing
    until the first note-on of `ref` at or after `resume`; identical from
    there. The cut and resume happen at block starts."""
    c = -(-cut // block) * block
    r = -(-resume // block) * block
    ev = [key(e) for e in d['events']]
    rf = [key(e) for e in ref['events']]
    first = min([e[1] for e in rf if e[0] == 'on' and e[1] >= r], default=10 ** 12)
    p = []
    if [e for e in ev if e[1] < c] != [e for e in rf if e[1] < c]:
        p.append('events before the interruption differ from the normal run')
    sounding = any(e[0] == 'on' and e[1] < c for e in rf) and \
        max([e[1] for e in rf if e[0] == 'on' and e[1] < c], default=-1) > max([e[1] for e in rf if e[0] == 'off' and e[1] <= c], default=-1)
    at = [e for e in ev if c <= e[1] < first]
    want = [('off', c, 1, 60, None, None), ('wheel', c, 1, None, None, 8192)] if sounding else []
    if [(e[0], e[1], e[3], e[5]) for e in at] != [(w[0], w[1], w[3], w[5]) for w in want]:
        p.append(f'between the cut and the next attack: {at[:3]} (expected {"note-off + wheel 8192 at the cut" if sounding else "nothing"})')
    if [e for e in ev if e[1] >= first] != [e for e in rf if e[1] >= first]:
        p.append('events after the interruption differ from the normal run')
    return p, c, sounding


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--probe', required=True)
    ap.add_argument('--outdir', required=True)
    ap.add_argument('--vst3', default='D:/VST3')
    ap.add_argument('--build', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build'))
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')   # the Windows console is not UTF-8
    os.makedirs(a.outdir, exist_ok=True)
    rows, failures = [], 0

    def row(test, kind, sr, block, d, probs, lats):
        nonlocal failures
        n = len([e for e in d['events'] if e['type'] == 'on']) if d else '-'
        rows.append((test, kind, sr, block, n, f'{np.median(lats):.2f}' if lats else '-',
                     'OK' if not probs else '; '.join(probs[:3])))
        failures += bool(probs)

    def bundle(kind):
        return f'{a.vst3}/{PLUGINS[kind][0]}.vst3'

    # 0. the installed DLLs are the ones just built
    for kind, (name, target) in PLUGINS.items():
        inst = f'{a.vst3}/{name}.vst3/Contents/x86_64-win/{name}.vst3'
        built = os.path.join(a.build, f'{target}_artefacts', 'Release', 'VST3', f'{name}.vst3', 'Contents', 'x86_64-win', f'{name}.vst3')
        p = []
        if not os.path.exists(inst):
            p.append(f'{inst} missing')
        elif os.path.exists(built) and md5(inst) != md5(built):
            p.append('installed DLL differs from plugin/build (copy failed? a host holds it?)')
        rows.append(('installed = built', kind, '-', '-', '-', '-', 'OK' if not p else '; '.join(p)))
        failures += bool(p)

    # 1. ten plucks, every rate and block size
    ref = {}
    for sr in RATES:
        wav = os.path.join(a.outdir, f'plucks-{sr}.wav')
        synth(wav, sr, ONSETS)
        for kind in PLUGINS:
            for block in BLOCKS:
                d = run_probe(a.probe, bundle(kind), wav, os.path.join(a.outdir, f'{kind}-{sr}-{block}.json'), block)
                probs, lats = check_notes(d, sr, ONSETS)
                probs += audio_checks(kind, d)
                pos = [key(e) for e in d['events']]
                if (kind, sr) not in ref:
                    ref[(kind, sr)] = d
                elif pos != [key(e) for e in ref[(kind, sr)]['events']]:
                    probs.append('events differ from the 32-sample-block run (not sample-accurate)')
                if kind == 'Inst' and (sr, block) == (48000, 128):
                    fx = [key(e) for e in ref[('Fx', sr)]['events']]
                    if pos != fx:
                        probs.append('Inst events differ from Fx events')
                row('10 plucks', kind, sr, block, d, probs, lats)

    sr, block = 48000, 128
    plucks = os.path.join(a.outdir, f'plucks-{sr}.wav')
    # 2. MIDI channel 5
    for kind in PLUGINS:
        d = run_probe(a.probe, bundle(kind), plucks, os.path.join(a.outdir, f'{kind}-ch5.json'), block, ['--channel', '5'])
        probs, lats = check_notes(d, sr, ONSETS, channel=5)
        if [k[1] for k in map(key, d['events'])] != [k[1] for k in map(key, ref[(kind, sr)]['events'])]:
            probs.append('channel 5 events at other samples than channel 1')
        row('MIDI CH 5', kind, sr, block, d, probs, lats)

    # 3. detector cases (48 kHz, block 128)
    cases = []
    w = os.path.join(a.outdir, 'ringing.wav')
    synth(w, sr, ONSETS, tau=1.5, replace=True)
    cases.append(('string left ringing, plucked every 1 s', w, ONSETS, 10.0))
    w = os.path.join(a.outdir, 'held.wav')
    synth(w, sr, [0.5, 0.8, 2.0, 2.3, 3.5], length=5.0)
    cases.append(('attacks during a held note ignored', w, [0.5, 2.0, 3.5], 2.0))
    w = os.path.join(a.outdir, 'first-sample.wav')
    synth(w, sr, [0.0, 1.0], length=2.0)
    cases.append(('attack on the first sample', w, [0.0, 1.0], 2.0))
    w = os.path.join(a.outdir, 'quiet.wav')
    tones(w, sr, [(0.5, 1.5), (2.5, 3.5)], -55.0, 4.0)
    cases.append(('tone at -55 dBFS RMS (under the -45 floor): no note', w, [], 2.0))
    w = os.path.join(a.outdir, 'soft.wav')
    tones(w, sr, [(0.5, 1.5), (2.5, 3.5)], -40.0, 4.0)
    cases.append(('tone at -40 dBFS RMS (over the floor): a note each', w, [0.5, 2.5], 2.0))
    w = os.path.join(a.outdir, 'nan.wav')
    synth(w, sr, ONSETS)
    x, _ = sf.read(w, dtype='float32')
    x[int(3.0 * sr)] = np.nan                    # inside the 2.5 s note
    x[int(6.0 * sr)] = np.inf                    # inside the 5.5 s note
    sf.write(w, x, sr, subtype='FLOAT')
    cases.append(('one NaN and one Inf sample: all notes still', w, ONSETS, 2.0))
    w = os.path.join(a.outdir, 'sustain.wav')
    tones(w, sr, [(0.5, 4.0)], -9.0, 5.0)
    cases.append(('3.5 s sustained tone: one note', w, [0.5], 2.0))
    for test, wav, attacks, lat in cases:
        for kind in PLUGINS:
            d = run_probe(a.probe, bundle(kind), wav, os.path.join(a.outdir, f'{kind}-{os.path.basename(wav)[:-4]}.json'), block)
            probs, lats = check_notes(d, sr, attacks, max_lat_ms=lat)
            probs += audio_checks(kind, d)
            row(test, kind, sr, block, d, probs, lats)
            if wav.endswith('ringing.wav'):
                ref[(kind, 'ringing')] = d

    # 4. interruptions in mid-note (48 kHz, block 128)
    ringing = os.path.join(a.outdir, 'ringing.wav')
    inter = [
        ('bypass 2.8-4.2 s', plucks, ['--bypass-from', '2.8', '--bypass-to', '4.2'], 2.8, 4.2, None),
        ('bypass ends 5 ms after an attack', plucks, ['--bypass-from', '2.8', '--bypass-to', '3.505'], 2.8, 3.505, None),
        ('bypass 2.8-3.2 s, string ringing', ringing, ['--bypass-from', '2.8', '--bypass-to', '3.2'], 2.8, 3.2, 'ringing'),
        ('off/on (re-prepare) at 2.8 s, string ringing', ringing, ['--reprepare-at', '2.8'], 2.8, 2.8, 'ringing'),
    ]
    for n_test, (test, wav, extra, t_cut, t_resume, refname) in enumerate(inter):
        for kind in PLUGINS:
            d = run_probe(a.probe, bundle(kind), wav, os.path.join(a.outdir, f'{kind}-interrupt{n_test}.json'), block, extra)
            r = ref[(kind, refname)] if refname else ref[(kind, sr)]
            probs, c, sounding = interrupted(d, r, int(round(t_cut * sr)), int(round(t_resume * sr)), block)
            if not sounding:
                probs.append('test setup: no note sounding at the cut')
            # the generic per-note checks, with the cut note ending at the cut
            first = min([e['t'] for e in r['events'] if e['type'] == 'on' and e['sample'] >= -(-int(round(t_resume * sr)) // block) * block], default=99)
            att = [t for t in ONSETS if t * sr < c or t >= first - 0.011]
            p2, lats = check_notes(d, sr, att, max_lat_ms=10.0, cuts=[c])
            probs += p2 + audio_checks(kind, d)
            row(test, kind, sr, block, d, probs, lats)

    print('| test | plugin | Hz | tampon | notes | attaque → note-on (médiane, ms) | résultat |')
    print('|---|---|---|---|---|---|---|')
    for r in rows:
        print('| ' + ' | '.join(str(x) for x in r) + ' |')
    print(f'\n{len(rows)} checks, {failures} failing')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
