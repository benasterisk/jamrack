// Run with:  node --test test/poly-calibrate.test.mjs
// The POLY calibration measurement on synthetic plucks whose inharmonicity
// and tuning are known, and the bank built from a measured profile.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { analysePluck, fitInharmonicity, PluckCapture, makeProfile } from '../js/audio/guitar/poly/calibrate.js';
import { Resampler } from '../js/audio/guitar/poly/resample.js';
import { buildBank, NBINS, BIN_HZ, midiToHz } from '../js/audio/guitar/poly/bank.js';
import { B_LAW } from '../js/audio/guitar/poly/profile.js';
import { pluck, silence, concat } from './plucks.mjs';

const SR = 48000;
const STRINGS = [[40, 1.5e-4], [45, 1.0e-4], [50, 5e-5], [55, 3e-5], [59, 7e-5], [64, 2e-5]];

function to24k(sig48) {
  const rs = new Resampler(SR, 24000);
  const out = [];
  rs.push(sig48, v => out.push(v));
  return Float32Array.from(out);
}

test('fitInharmonicity recovers B and f0 of a synthetic string', () => {
  for (const [midi, B] of STRINGS) {
    const sig = pluck(SR, { midi, dur: 1.0, harmonics: 30, stiffness: B, decay: 0.8, amp: 0.4 });
    const x = to24k(sig);
    const seg = x.subarray(Math.round(0.06 * 24000), Math.round(0.56 * 24000));
    const fit = fitInharmonicity(seg, midiToHz(midi));
    assert.ok(fit, `midi ${midi}: no fit`);
    const errB = Math.abs(fit.B - B) / B;
    const cents = 1200 * Math.log2(fit.f0 / midiToHz(midi));
    assert.ok(errB < 0.12, `midi ${midi}: B ${fit.B.toExponential(2)} vs ${B.toExponential(2)} (${(100 * errB).toFixed(0)} %)`);
    assert.ok(Math.abs(cents) < 3, `midi ${midi}: f0 off by ${cents.toFixed(1)} cents`);
    assert.ok(fit.n >= 8, `midi ${midi}: only ${fit.n} partials`);
  }
});

test('analysePluck: profile from the attack, tuning offset, and the three refusals', () => {
  const sig = concat(silence(SR, 0.1), pluck(SR, { midi: 45, cents: 12, dur: 0.9, harmonics: 30, stiffness: 1e-4, amp: 0.4 }));
  const x = to24k(sig);
  const r = analysePluck(x, Math.round(0.1 * 24000), 45);
  assert.ok(r.ok, JSON.stringify(r));
  assert.ok(Math.abs(r.cents - 12) < 3, `tuning ${r.cents.toFixed(1)} cents, expected +12`);
  assert.equal(r.amps.length, 40);
  assert.ok(Math.abs(r.amps.reduce((s, a) => s + a * a, 0) - 1) < 1e-6, 'unit L2 profile');
  assert.ok(r.amps[0] > r.amps[5], 'fundamental above the 6th partial');
  // wrong string
  const w = analysePluck(x, Math.round(0.1 * 24000), 50);
  assert.equal(w.ok, false); assert.equal(w.error, 'wrongNote'); assert.equal(w.heardMidi, 45);
  // too quiet
  const q = analysePluck(to24k(concat(silence(SR, 0.1), pluck(SR, { midi: 45, dur: 0.9, amp: 0.001 }))), Math.round(0.1 * 24000), 45);
  assert.equal(q.ok, false); assert.equal(q.error, 'tooQuiet');
  // noise instead of a note
  const n = analysePluck(to24k(concat(silence(SR, 0.1), silence(SR, 0.9, 0.2, 5))), Math.round(0.1 * 24000), 45);
  assert.equal(n.ok, false); assert.ok(['noPartials', 'wrongNote'].includes(n.error), n.error);
});

test('PluckCapture finds the onset by itself and reports the pluck', () => {
  const cap = new PluckCapture(SR, 50);
  const sig = concat(silence(SR, 0.4, 0.0005), pluck(SR, { midi: 50, dur: 1.0, harmonics: 30, stiffness: 5e-5, amp: 0.3 }), silence(SR, 0.2));
  let res = null;
  for (let o = 0; o < sig.length && !res; o += 128) res = cap.push(sig.subarray(o, Math.min(sig.length, o + 128)));
  assert.ok(res && res.ok, JSON.stringify(res));
  assert.equal(cap.state, 'captured');
  assert.ok(Math.abs(cap.onset / 24000 - 0.4) < 0.02, `onset at ${(cap.onset / 24000).toFixed(3)} s`);
  assert.ok(Math.abs(res.B - 5e-5) / 5e-5 < 0.15, `B ${res.B.toExponential(2)}`);
  // the capture arms only after 0.3 s of silence: a string still ringing keeps it 'quiet'
  const busy = new PluckCapture(SR, 50);
  const ringing = pluck(SR, { midi: 45, dur: 0.25, amp: 0.3 });
  for (let o = 0; o < ringing.length; o += 128) busy.push(ringing.subarray(o, Math.min(ringing.length, o + 128)));
  assert.equal(busy.state, 'quiet');
  const hush = silence(SR, 0.35, 0.0005);
  for (let o = 0; o < hush.length; o += 128) busy.push(hush.subarray(o, Math.min(hush.length, o + 128)));
  assert.equal(busy.state, 'armed');
  // nothing played: timeout (30 s)
  const idle = new PluckCapture(SR, 50);
  let t = null;
  const quiet = silence(SR, 32, 0.0005);
  for (let o = 0; o < quiet.length && !t; o += 4096) t = idle.push(quiet.subarray(o, Math.min(quiet.length, o + 4096)));
  assert.ok(t && t.error === 'timeout');
});

test('makeProfile fills unmeasured strings with the generic values, fret 12 sets the slope; the bank builds from it', () => {
  const open = STRINGS.map(([midi, B], s) => (s === 3 ? null : { ok: true, B, f0: midiToHz(midi), cents: 0, n: 10, amps: Float64Array.from({ length: 40 }, (_, i) => 1 / (i + 1)) }));
  const fret12 = [null, { ok: true, B: 1.0e-4 * 4 }, null, null, null, null];   // doubling every 6 frets = slope 1
  const p = makeProfile(open, fret12);
  assert.deepEqual(p.measured, [true, true, true, false, true, true]);
  assert.deepEqual(p.bLaw[3], B_LAW[3]);
  assert.ok(Math.abs(p.bLaw[1][1] - 1) < 1e-9, `slope ${p.bLaw[1][1]}`);
  assert.equal(p.bLaw[0][1], B_LAW[0][1]);
  const bank = buildBank('medium', { bLaw: p.bLaw, prof: p.prof });
  assert.equal(bank.K, 122);
  let best = 0;
  for (let b = 1; b < NBINS; b++) if (bank.W[b] > bank.W[best]) best = b;
  assert.ok(Math.abs(best * BIN_HZ - midiToHz(40)) <= BIN_HZ, 'low E template peaks at its fundamental');
});
