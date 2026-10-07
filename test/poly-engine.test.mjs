// Run with:  node --test test/poly-engine.test.mjs
// Node tests of the POLY engine (js/audio/guitar/poly/): the building blocks
// against closed forms, and the whole engine on synthetic plucks. The
// equivalence with the Python prototype on real guitar is measured by
// test/poly-dump-events.mjs + test/poly/merge.py (see docs/poly-implementation.md).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RealFFT } from '../js/audio/guitar/poly/fft.js';
import { Resampler, firwinKaiser } from '../js/audio/guitar/poly/resample.js';
import { buildBank, hannWindow, NBINS, BIN_HZ, midiToHz, OPEN_MIDI, PITCH_LO } from '../js/audio/guitar/poly/bank.js';
import { Decomposer } from '../js/audio/guitar/poly/nmf.js';
import { PolyTracker, ANALYSIS_RATE, ANALYSIS_HOP } from '../js/audio/guitar/poly/engine.js';
import { pluck, silence, concat } from './plucks.mjs';

const SR = 48000;
const HOP_MS = 1000 * ANALYSIS_HOP / ANALYSIS_RATE;
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const name = m => NAMES[m % 12] + (Math.floor(m / 12) - 1);
const bank = buildBank('medium');

/** Feeds a 48 kHz signal in 128-sample blocks; returns events with ms stamps. */
function run(tr, sig) {
  const ev = [];
  for (let o = 0; o < sig.length; o += 128) {
    for (const e of tr.process(sig.subarray(o, Math.min(sig.length, o + 128)))) {
      ev.push({ ...e, ms: (e.hop + 1) * HOP_MS + 1000 * tr.delaySeconds });
    }
  }
  for (const e of tr.flush()) ev.push({ ...e, ms: (e.hop + 1) * HOP_MS });
  return ev;
}
const ons = ev => ev.filter(e => e.t === 'on');
const offs = ev => ev.filter(e => e.t === 'off');

test('FFT magnitude matches a direct DFT', () => {
  const n = 2048;
  const x = new Float64Array(1024);
  for (let i = 0; i < x.length; i++) x[i] = Math.sin(2 * Math.PI * 100.5 * i / 1024) + 0.3 * Math.cos(2 * Math.PI * 7 * i / 1024);
  const mag = new RealFFT(n).magnitude(x, new Float64Array(n / 2 + 1));
  for (const k of [0, 7 * 2, 201, 500, 1024]) {
    let re = 0, im = 0;
    for (let i = 0; i < x.length; i++) { re += x[i] * Math.cos(2 * Math.PI * k * i / n); im -= x[i] * Math.sin(2 * Math.PI * k * i / n); }
    assert.ok(Math.abs(mag[k] - Math.hypot(re, im)) < 1e-9, `bin ${k}: ${mag[k]} vs ${Math.hypot(re, im)}`);
  }
});

test('resampler: scipy kernel (unit DC gain x up, symmetric), 48 kHz -> 24 kHz passes a 1 kHz sine untouched', () => {
  const h = firwinKaiser(41, 0.5, 5.0);
  assert.ok(Math.abs(h.reduce((a, b) => a + b, 0) - 1) < 1e-12, 'unit DC gain');
  for (let i = 0; i < 20; i++) assert.ok(Math.abs(h[i] - h[40 - i]) < 1e-15, 'symmetric');
  const rs = new Resampler(48000, 24000);
  assert.equal(rs.up, 1); assert.equal(rs.down, 2); assert.equal(rs.halfLen, 20);
  const n = 48000;
  const x = Float64Array.from({ length: n }, (_, i) => Math.sin(2 * Math.PI * 1000 * i / 48000));
  const y = [];
  rs.push(x, v => y.push(v));
  assert.equal(y.length, n / 2);
  // steady state, delay = halfLen input samples = 10 output samples; the
  // 41-tap Kaiser-5 kernel has ~0.1 % passband ripple (scipy's too)
  let err = 0;
  for (let i = 100; i < y.length - 100; i++) {
    const expect = Math.sin(2 * Math.PI * 1000 * (i - 10) / 24000);
    err = Math.max(err, Math.abs(y[i] - expect));
  }
  assert.ok(err < 5e-3, `max error ${err}`);
  const rs2 = new Resampler(44100, 24000);
  assert.equal(rs2.up, 80); assert.equal(rs2.down, 147);
  assert.ok(Math.abs(rs2.delaySeconds() * 1000 - 0.4167) < 0.01, 'same 0.42 ms delay at 44.1 kHz');
});

test('bank: 122 unit-L2 columns, every open string peaks at its fundamental', () => {
  assert.equal(bank.K, 122);
  for (let k = 0; k < bank.K; k++) {
    let ss = 0;
    for (let b = 0; b < NBINS; b++) ss += bank.W[k * NBINS + b] ** 2;
    assert.ok(Math.abs(ss - 1) < 1e-5, `column ${k} norm ${ss}`);
  }
  for (let s = 0; s < 6; s++) {
    const k = s * 20;                       // fret 0
    let best = 0;
    for (let b = 1; b < NBINS; b++) if (bank.W[k * NBINS + b] > bank.W[k * NBINS + best]) best = b;
    const f0 = midiToHz(OPEN_MIDI[s]);
    assert.ok(Math.abs(best * BIN_HZ - f0) <= BIN_HZ, `string ${s}: peak at ${(best * BIN_HZ).toFixed(0)} Hz, f0 ${f0.toFixed(0)}`);
  }
  // the window of the bank is the 1024-sample analysis window
  assert.equal(bank.window.length, 1024);
  assert.ok(Math.abs(hannWindow(1024).reduce((a, b) => a + b, 0) - 2) < 1e-9);
});

test('decomposer: a single template spectrum activates its own pitch, and almost nothing else', () => {
  const dec = new Decomposer(bank);
  const k = 2 * 20 + 5;                     // D string, fret 5 = G3 (midi 55)
  const v = new Float64Array(NBINS);
  for (let b = 0; b < NBINS; b++) v[b] = 0.05 * bank.W[k * NBINS + b];
  let P;
  for (let i = 0; i < 6; i++) P = dec.step(v);   // warm start settles in a few hops
  const j = 55 - PITCH_LO;
  const total = P.reduce((a, b) => a + b, 0);
  assert.ok(P[j] > 0.8 * total, `G3 holds ${(100 * P[j] / total).toFixed(0)} % of the activation`);
  assert.ok(dec.nAct >= 60 && dec.nAct <= 62, `active set ${dec.nAct}`);
});

test('silence and a -54 dBFS hiss never trigger', () => {
  const tr = new PolyTracker(SR, { bank });
  const ev = run(tr, concat(silence(SR, 1.0, 0.002), silence(SR, 0.5, 0.0)));
  assert.equal(ons(ev).length, 0, `got ${ons(ev).map(e => name(e.midi)).join(' ')}`);
});

test('single plucks on every string: the right note, once, released after the mute', () => {
  const rows = [], problems = [];
  for (const [label, midi] of [['E2', 40], ['A2', 45], ['D3', 50], ['G3', 55], ['B3', 59], ['E4', 64], ['A4', 69], ['E5', 76]]) {
    const tr = new PolyTracker(SR, { bank });
    const sig = concat(silence(SR, 0.1), pluck(SR, { midi, dur: 0.6, cut: 0.5, stiffness: 1e-4 }), silence(SR, 0.2));
    const ev = run(tr, sig);
    const on = ons(ev), off = offs(ev);
    const got = ev.filter(e => e.t !== 'meter').map(e => e.t + name(e.midi) + '@' + e.ms.toFixed(0)).join(' ');
    const lat = on.length ? on[0].ms - 100 : NaN;
    const offLat = off.length ? off[off.length - 1].ms - 600 : NaN;
    rows.push({ string: label, got, 'on latency ms': lat.toFixed(1), 'off after mute ms': offLat.toFixed(1) });
    const right = on.filter(e => e.midi === midi).length;
    if (right !== 1) problems.push(`${label}: expected one ${name(midi)}, got ${got}`);
    else if (on.length > 2) problems.push(`${label}: ${on.length - 1} extra notes: ${got}`);
    else if (!(lat > 0 && lat < 60)) problems.push(`${label}: note-on latency ${lat.toFixed(1)} ms`);
    else if (!(offLat > 0 && offLat < 80)) problems.push(`${label}: note-off ${offLat.toFixed(1)} ms after the mute`);
  }
  console.table(rows);
  assert.deepEqual(problems, []);
});

test('a dyad (two plucks at once) yields both notes', () => {
  const tr = new PolyTracker(SR, { bank });
  const a = pluck(SR, { midi: 48, dur: 0.8, cut: 0.7, stiffness: 1e-4, seed: 2 });
  const b = pluck(SR, { midi: 55, dur: 0.8, cut: 0.7, stiffness: 1e-4, seed: 3 });
  const mix = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) mix[i] = 0.6 * (a[i] + b[i]);
  const ev = run(tr, concat(silence(SR, 0.1), mix, silence(SR, 0.2)));
  const got = ons(ev).map(e => e.midi);
  assert.ok(got.includes(48) && got.includes(55), `expected C3 + G3, got ${got.map(name).join(' ')}`);
  assert.ok(got.length <= 3, `too many notes: ${got.map(name).join(' ')}`);
});

test('cost per hop (informative; the budget is one hop = 2.67 ms, measured for real by test/poly-dump-events.mjs)', () => {
  const tr = new PolyTracker(SR, { bank });
  const sig = concat(silence(SR, 0.05), pluck(SR, { midi: 45, dur: 1.0, stiffness: 1e-4 }));
  run(tr, sig);
  console.log(`decomposer ${tr.nmfMs.toFixed(3)} ms/hop, whole hop ${tr.hopMs.toFixed(3)} ms (avg), max ${tr.maxHopMs.toFixed(2)} ms`);
  // loose bound: a loaded machine must not fail the suite, a 10x regression must
  assert.ok(tr.hopMs < 10 * 1000 * ANALYSIS_HOP / ANALYSIS_RATE, `whole hop ${tr.hopMs} ms`);
});
