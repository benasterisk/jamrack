// Run with:  node --test test/guitar-tracker.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { GuitarTracker } from '../js/audio/guitar/tracker.js';
import { pluck, silence, concat, seq, run, ons, offs, bends, meters, notes, chord } from './plucks.mjs';

const SR = 48000;
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const name = m => NAMES[m % 12] + (Math.floor(m / 12) - 1);

// Every open string, a few fretted notes, and drop D.
const STRINGS = [
  ['D2 (drop D)', 38], ['E2', 40], ['A2', 45], ['D3', 50], ['G3', 55],
  ['B3', 59], ['E4', 64], ['A4 (fret 5, high E)', 69], ['E5 (fret 12)', 76], ['A5 (fret 17)', 81],
];

test('single plucks: right note, one note-on, latency per string', () => {
  const rows = [], problems = [];
  for (const [label, midi] of STRINGS) {
    const tr = new GuitarTracker(SR);
    // pluck at 100 ms, hard mute at 600 ms
    const sig = concat(silence(SR, 0.1), pluck(SR, { midi, dur: 0.6, cut: 0.5 }), silence(SR, 0.2));
    const ev = run(tr, sig);
    const on = ons(ev), off = offs(ev);
    const got = notes(ev).map(e => e.t + name(e.midi) + '@' + e.ms.toFixed(0)).join(' ');
    const lat = on.length ? on[0].ms - 100 : NaN;
    const offLat = off.length ? off[off.length - 1].ms - 600 : NaN;
    rows.push({ string: label, got, 'on latency ms': lat.toFixed(1), 'off after mute ms': offLat.toFixed(1), vel: on.length ? on[0].vel.toFixed(2) : '-' });
    if (on.length !== 1 || on[0].midi !== midi) problems.push(`${label}: expected one ${name(midi)}, got ${got}`);
    else if (off.length !== 1) problems.push(`${label}: expected one note-off, got ${got}`);
    else if (!(lat > 0 && lat < 45)) problems.push(`${label}: note-on latency ${lat.toFixed(1)} ms`);
    else if (!(offLat > 0 && offLat < 40)) problems.push(`${label}: note-off ${offLat.toFixed(1)} ms after the mute`);
  }
  console.table(rows);
  assert.deepEqual(problems, []);
});

test('noise floor and silence never trigger', () => {
  const tr = new GuitarTracker(SR);
  const ev = run(tr, concat(silence(SR, 1.0, 0.002), silence(SR, 0.5, 0.0)));   // -54 dBFS hiss
  assert.equal(notes(ev).length, 0);
});

test('a knock (noise burst) does not become a note', () => {
  const tr = new GuitarTracker(SR);
  const burst = silence(SR, 0.03, 0.2, 11);
  const ev = run(tr, concat(silence(SR, 0.1), burst, silence(SR, 0.3)));
  assert.equal(notes(ev).length, 0, `got ${notes(ev).map(e => e.t + name(e.midi)).join(' ')}`);
});

test('repicking the same note retriggers every time', () => {
  const tr = new GuitarTracker(SR);
  const sig = seq(SR, 1.2, [0, 1, 2, 3, 4].map(i => ({ at: 0.1 + i * 0.15, midi: 57, seed: i + 1 })));
  const ev = run(tr, sig);
  const on = ons(ev);
  assert.equal(on.length, 5, `expected 5 note-ons, got ${on.length}: ${notes(ev).map(e => e.t + '@' + e.ms.toFixed(0)).join(' ')}`);
  assert.ok(on.every(e => e.midi === 57));
  // every note-on but the first is preceded by the note-off of the previous one
  const order = notes(ev).map(e => e.t);
  for (let i = 1; i < order.length; i++) assert.notEqual(order[i - 1] + order[i], 'onon');
});

test('hammer-on without a new pluck changes the note (legato)', () => {
  const tr = new GuitarTracker(SR);
  const sig = concat(silence(SR, 0.1), pluck(SR, { midi: 64, dur: 0.8, hammer: { at: 0.3, semis: 2 } }));
  const ev = run(tr, sig);
  const on = ons(ev);
  assert.deepEqual(on.map(e => e.midi), [64, 66], `got ${on.map(e => name(e.midi) + '@' + e.ms.toFixed(0)).join(' ')}`);
  const lat = on[1].ms - 400;
  assert.ok(lat > 0 && lat < 25, `legato change took ${lat.toFixed(1)} ms`);
  const off = offs(ev);
  assert.equal(off[0].midi, 64);
  assert.ok(off[0].ms <= on[1].ms);
});

test('a whole-step bend is pitch bend, not a new note', () => {
  const tr = new GuitarTracker(SR);
  const sig = concat(silence(SR, 0.1), pluck(SR, { midi: 55, dur: 1.0, bend: { at: 0.3, dur: 0.3, semis: 2 } }));
  const ev = run(tr, sig);
  assert.deepEqual(ons(ev).map(e => e.midi), [55], `got ${ons(ev).map(e => name(e.midi) + '@' + e.ms.toFixed(0)).join(' ')}`);
  const b = bends(ev);
  assert.ok(b.length >= 8, `expected a stream of bend events, got ${b.length}`);
  const last = b[b.length - 1].semis;
  assert.ok(Math.abs(last - 2) < 0.12, `final bend ${last.toFixed(3)} semitones, expected ~2`);
  // monotonic-ish: never goes backwards by more than the smoothing allows
  for (let i = 1; i < b.length; i++) assert.ok(b[i].semis >= b[i - 1].semis - 0.1);
});

test('chromatic mode: a bend past half a semitone steps to the next note', () => {
  const tr = new GuitarTracker(SR, { bend: false });
  const sig = concat(silence(SR, 0.1), pluck(SR, { midi: 55, dur: 1.0, bend: { at: 0.3, dur: 0.3, semis: 2 } }));
  const ev = run(tr, sig);
  assert.deepEqual(ons(ev).map(e => e.midi), [55, 56, 57], `got ${ons(ev).map(e => name(e.midi)).join(' ')}`);
  assert.equal(bends(ev).length, 0);
});

test('missing fundamental (pickup near the bridge) still finds the right octave', () => {
  for (const midi of [40, 45, 50]) {
    const tr = new GuitarTracker(SR);
    const ev = run(tr, concat(silence(SR, 0.1), pluck(SR, { midi, dur: 0.5, fundamental: 0.15 })));
    assert.deepEqual(ons(ev).map(e => e.midi), [midi], `${name(midi)}: got ${ons(ev).map(e => name(e.midi)).join(' ')}`);
  }
});

test('pitch accuracy on sustained notes (cents)', () => {
  const rows = [];
  for (const [midi, cents, stiffness] of [[40, -15, 0], [40, -15, 2e-4], [50, 0, 2e-4], [64, 20, 2e-4], [76, -30, 2e-4], [81, 10, 2e-4]]) {
    const tr = new GuitarTracker(SR);
    const ev = run(tr, concat(silence(SR, 0.1), pluck(SR, { midi, cents, dur: 0.8, stiffness })));
    const m = meters(ev).filter(e => e.ms > 250 && e.ms < 700 && !Number.isNaN(e.midiF));
    assert.ok(m.length > 10, `${name(midi)}: too few confident meter frames (${m.length})`);
    const errs = m.map(e => (e.midiF - midi) * 100 - cents);
    const mean = errs.reduce((a, b) => a + b, 0) / errs.length;
    const worst = Math.max(...errs.map(Math.abs));
    rows.push({ note: name(midi), offset: cents, stiffness, 'mean error ¢': mean.toFixed(1), 'worst ¢': worst.toFixed(1) });
    // a stiff (inharmonic) string reads a few cents sharp on any estimator — tuners show the same
    const tol = stiffness ? 6 : 2;
    assert.ok(Math.abs(mean) < tol, `${name(midi)}: mean error ${mean.toFixed(1)} cents`);
    assert.ok(worst < tol + 6, `${name(midi)}: worst error ${worst.toFixed(1)} cents`);
  }
  console.table(rows);
});

test('velocity follows pick strength', () => {
  const vel = amp => {
    const tr = new GuitarTracker(SR, { dyn: 1 });
    return ons(run(tr, concat(silence(SR, 0.1), pluck(SR, { midi: 52, dur: 0.4, amp }))))[0].vel;
  };
  const soft = vel(0.03), mid = vel(0.12), hard = vel(0.5);
  console.log(`velocity soft ${soft.toFixed(2)} · mid ${mid.toFixed(2)} · hard ${hard.toFixed(2)}`);
  assert.ok(soft < mid && mid < hard);
  assert.ok(hard > 0.8 && soft < 0.45);
});

test('octave and transpose shift the emitted notes, not the tuner', () => {
  const tr = new GuitarTracker(SR, { octave: 1, transpose: -2 });
  const ev = run(tr, concat(silence(SR, 0.1), pluck(SR, { midi: 52, dur: 0.4 })));
  assert.deepEqual(ons(ev).map(e => e.midi), [52 + 12 - 2]);
  const m = meters(ev).filter(e => !Number.isNaN(e.midiF));
  assert.ok(Math.abs(m[m.length - 1].midiF - 52) < 0.1);
});

test('a fast chromatic run is tracked note for note', () => {
  const tr = new GuitarTracker(SR);
  const run8 = [60, 61, 62, 63, 64, 65, 66, 67];
  const sig = seq(SR, 1.5, run8.map((m, i) => ({ at: 0.1 + i * 0.12, midi: m, seed: i + 20 })));
  const ev = run(tr, sig);
  assert.deepEqual(ons(ev).map(e => e.midi), run8, `got ${ons(ev).map(e => name(e.midi) + '@' + e.ms.toFixed(0)).join(' ')}`);
});

test('note-off when the string decays below the release threshold', () => {
  const tr = new GuitarTracker(SR, { release: 0 });   // 15 dB under the peak
  const ev = run(tr, concat(silence(SR, 0.1), pluck(SR, { midi: 57, dur: 2.5, decay: 2 })));
  assert.equal(ons(ev).length, 1);
  assert.equal(offs(ev).length, 1, 'the decaying note must end on its own');
  console.log(`decay note-off after ${(offs(ev)[0].ms - 100).toFixed(0)} ms`);
  assert.ok(offs(ev)[0].ms < 2500);
});

test('44.1 kHz and 256-sample blocks give the same notes', () => {
  for (const [sr, block] of [[44100, 128], [44100, 256], [48000, 256], [96000, 128]]) {
    const tr = new GuitarTracker(sr);
    const ev = run(tr, concat(silence(sr, 0.1), pluck(sr, { midi: 45, dur: 0.5 }), silence(sr, 0.1)), block);
    assert.deepEqual(ons(ev).map(e => e.midi), [45], `${sr} Hz / ${block}: got ${ons(ev).map(e => name(e.midi)).join(' ')}`);
    assert.equal(offs(ev).length, 1);
  }
});

test('flush() releases a sounding note', () => {
  const tr = new GuitarTracker(SR);
  run(tr, concat(silence(SR, 0.1), pluck(SR, { midi: 45, dur: 0.3 })));
  const ev = tr.flush();
  assert.equal(ev[0].t, 'off');
  assert.equal(ev[0].midi, 45);
  // a pending pitch bend is zeroed with the note
  assert.ok(ev.slice(1).every(e => e.t === 'bend' && e.semis === 0));
  assert.deepEqual([...tr.flush()], []);
});

test('CPU: one second of audio analyses in a small fraction of a second', () => {
  const tr = new GuitarTracker(SR);
  const sig = pluck(SR, { midi: 40, dur: 1.0 });
  run(tr, sig); // warm-up (JIT)
  const t0 = performance.now();
  run(tr, sig);
  const ms = performance.now() - t0;
  console.log(`1 s of audio tracked in ${ms.toFixed(0)} ms (${(ms / 10).toFixed(1)} % of one core)`);
  assert.ok(ms < 300);
});

// ---------------------------------------------------------------------------
// Polyphony: what the MONO tracker does today when several strings ring.
// These two tests DOCUMENT the expected behaviour of a monophonic tracker
// (one `note` state, one YIN period), they are not regressions: they are the
// baseline the POLY engine (docs/polyphonic-plan.md) must beat, measured on
// the same synthetic signals (test/plucks.mjs chord()). The numbers in the
// comments come from the scratch run that set the thresholds; the tables the
// tests print show them again.

/** Sounding-notes bookkeeping over an event list. */
function polyStats(ev) {
  let sounding = 0, maxSounding = 0;
  for (const e of notes(ev)) { sounding += e.t === 'on' ? 1 : -1; maxSounding = Math.max(maxSounding, sounding); }
  const on = ons(ev);
  let maxOnsIn100 = 0;
  for (let i = 0; i < on.length; i++) {
    let c = 0;
    for (let j = i; j < on.length && on[j].ms - on[i].ms <= 100; j++) c++;
    maxOnsIn100 = Math.max(maxOnsIn100, c);
  }
  return { maxSounding, maxOnsIn100, on };
}

test('mono on a 3-note chord (15 ms strum): at most one sounding note, at most one note-on — POLY baseline', () => {
  // Measured (5 open-position triads × 3 seeds × down/up strum, 15 ms spread):
  // 0 or 1 note-on per chord, never 2; the one note named is the last/first
  // string of the strum or a common sub-period (D major → D2). Recall on the
  // chord's notes: ≤ 1/3. POLY target (plan, jalon 3): 2-3 notes ≥ 90 %,
  // chord completion p90 < 60 ms.
  const CHORDS = { 'E (E2 B2 E3)': [40, 47, 52], 'Am (A2 E3 A3)': [45, 52, 57], 'D (D3 A3 F#4)': [50, 57, 66], 'G (G3 B3 D4)': [55, 59, 62], 'C (C3 E3 G3)': [48, 52, 55] };
  const rows = [], problems = [];
  let found = 0, total = 0;
  for (const [label, ns] of Object.entries(CHORDS)) for (const seed of [1, 7, 42]) for (const up of [false, true]) {
    // dur > total: every string is still ringing when the signal ends, so the
    // end of the signal never leaves one string alone (that would be mono)
    const sig = chord(SR, { notes: ns, at: 0.1, dur: 1, spread: 0.015, seed, up, total: 0.9 });
    const ev = run(new GuitarTracker(SR), sig);
    const { maxSounding, on } = polyStats(ev);
    const hits = new Set(on.map(e => e.midi).filter(m => ns.includes(m))).size;
    found += hits; total += ns.length;
    rows.push({ chord: label, seed, strum: up ? 'up' : 'down', 'note-ons': on.map(e => name(e.midi) + '@' + e.ms.toFixed(0)).join(' ') || '-', 'max sounding': maxSounding, 'notes found': `${hits}/${ns.length}` });
    if (maxSounding > 1) problems.push(`${label} seed ${seed} ${up ? 'up' : 'down'}: ${maxSounding} notes sounding at once`);
    if (on.length > 1) problems.push(`${label} seed ${seed} ${up ? 'up' : 'down'}: ${on.length} note-ons`);
  }
  console.table(rows);
  console.log(`mono recall on 3-note chords: ${found}/${total} notes (${(100 * found / total).toFixed(0)} %) — the POLY baseline to beat`);
  assert.deepEqual(problems, []);
  // the documented baseline: a monophonic tracker cannot hear more than a third of a triad
  assert.ok(found / total <= 1 / 3, `mono recall ${found}/${total} on chords: if this grew, update the POLY baseline`);
});

test('mono on a dyad (A2+E3): never both notes, at most one note-on per 100 ms — POLY baseline', () => {
  // Measured (spreads 0 / 5 / 15 / 35 ms × 4 seeds × down/up): 0 note-ons when
  // the two strings start within 15 ms (the two periods make YIN's dip too
  // shallow to pass CONF_ON) and exactly 1 at 35 ms (the first string is
  // named before the second lands, then goes silent when it does); never 2.
  // POLY target: both notes within 40 ms of their pluck.
  const rows = [], problems = [];
  for (const spread of [0, 0.005, 0.015, 0.035]) for (const seed of [1, 7, 42, 99]) for (const up of [false, true]) {
    const sig = chord(SR, { notes: [45, 52], at: 0.1, dur: 1, spread, seed, up, total: 0.9 });
    const ev = run(new GuitarTracker(SR), sig);
    const { maxSounding, maxOnsIn100, on } = polyStats(ev);
    rows.push({ 'spread ms': spread * 1000, seed, strum: up ? 'up' : 'down', 'note-ons': on.map(e => name(e.midi) + '@' + e.ms.toFixed(0)).join(' ') || '-', 'max sounding': maxSounding, 'note-ons / 100 ms': maxOnsIn100 });
    if (maxSounding > 1) problems.push(`spread ${spread * 1000} ms seed ${seed}: ${maxSounding} notes sounding at once`);
    if (maxOnsIn100 > 1) problems.push(`spread ${spread * 1000} ms seed ${seed}: ${maxOnsIn100} note-ons within 100 ms`);
  }
  console.table(rows);
  assert.deepEqual(problems, []);
});
