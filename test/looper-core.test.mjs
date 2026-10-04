// Run with:  node --test test/looper-core.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { LooperCore, TRACKS } from '../js/audio/looper/core.js';

const SR = 48000, B = 128;           // SR = 375 blocks: every length below is block-aligned
const HALF = 188 * B, QUARTER = 94 * B;
const ramp = f => f / 1e6;           // a signal where the value tells the frame

/**
 * Drives the core block by block. `input(frame)` → [l, r]; `actions` maps a
 * frame (multiple of B) to a function(core). Returns per-track outputs over
 * the whole run, the events with their frame, and snapshots taken by actions.
 */
function drive(core, frames, input, actions = {}) {
  if (frames % B) throw new Error(`drive(): ${frames} frames is not a multiple of ${B}`);
  for (const k of Object.keys(actions)) if (Number(k) % B) throw new Error(`action at ${k} is not block-aligned`);
  const outs = Array.from({ length: TRACKS }, () => [new Float32Array(frames), new Float32Array(frames)]);
  const events = [];
  const inL = new Float32Array(B), inR = new Float32Array(B);
  const blk = Array.from({ length: TRACKS }, () => [new Float32Array(B), new Float32Array(B)]);
  for (let f = 0; f < frames; f += B) {
    if (actions[f]) actions[f](core, f);
    for (const e of core._events.splice(0)) events.push({ ...e, frame: f });
    for (let s = 0; s < B; s++) { const [l, r] = input(f + s); inL[s] = l; inR[s] = r; }
    const ev = core.process(inL, inR, blk);
    for (const e of ev) events.push({ ...e, frame: f });
    for (let i = 0; i < TRACKS; i++) { outs[i][0].set(blk[i][0], f); outs[i][1].set(blk[i][1], f); }
  }
  return { outs, events };
}
const near = (a, b, eps = 1e-6) => Math.abs(a - b) <= eps;

test('first loop: press-to-press sets the length, audio lands where it was played', () => {
  const core = new LooperCore(SR);
  const { events, outs } = drive(core, 3 * SR, f => [ramp(f), -ramp(f)], {
    0: c => c.toggle(0),
    [SR]: c => c.toggle(0),            // close after exactly 1 s
    [SR + 10 * B]: c => c.toggle(0),   // DUB → PLAY
  });
  const len = events.find(e => e.t === 'length');
  assert.equal(len.length, SR);
  assert.equal(len.frame, SR);
  const t = core.tracks[0];
  assert.equal(t.mode, 'play');
  assert.ok(t.has);
  for (let q = 10 * B; q < SR; q += 997) assert.ok(near(t.L[q], ramp(q)) && near(t.R[q], -ramp(q)), `L[${q}]`);
  // playback: at absolute frame SR + k (k ≥ 10 B, after the dub pass) the output is L[k]
  for (let k = 10 * B; k < SR; k += 1001) assert.ok(near(outs[0][0][SR + k], ramp(k)), `out at ${k}`);
  // the dub pass (first 10 blocks after the close) added the input onto the loop
  assert.ok(near(t.L[5], ramp(5) + ramp(SR + 5)));
});

test('external input: latency compensation puts the first loop and later passes in place', () => {
  const core = new LooperCore(SR);
  const IN = 480, OUT = 240;
  core.setLatency(IN, OUT);
  // the player plays `intended`; the engine receives it IN frames later
  const intended = f => (f >= 0 ? ramp(f) : 0);
  let snapshot = null;
  const { events } = drive(core, 4 * SR, f => [intended(f - IN), 0], {
    0: c => c.toggle(0),
    [SR]: c => c.toggle(0),                                     // press at 1 s
    [SR + 8 * B]: c => { snapshot = Float32Array.from(c.tracks[0].L); },
  });
  const len = events.find(e => e.t === 'length');
  assert.equal(len.length, SR, 'press-to-press length');
  assert.ok(len.frame >= SR + IN - B && len.frame <= SR + IN + B, `closed once the tail arrived (frame ${len.frame})`);
  // L[q] must hold intended(q) over the whole loop, including the in-flight tail
  for (let q = 0; q < SR; q += 499) {
    if (q >= SR - IN - OUT) continue;   // the seam gets the dub of the second cycle: checked below
    assert.ok(near(snapshot[q], intended(q), 1e-6), `L[${q}] = ${snapshot[q]} expected ${intended(q)}`);
  }
  // steady state: a marker played at loop position P (as heard) arrives IN+OUT late
  // and must be written at P. Track 1 records a full cycle.
  const core2 = new LooperCore(SR);
  core2.setLatency(IN, OUT);
  const { events: ev2 } = drive(core2, 2 * SR, f => [intended(f - IN), 0], { 0: c => c.toggle(0), [SR]: c => c.toggle(0) });
  const closeFrame = SR + IN;                      // the tail of the first loop arrived here
  assert.ok(Math.abs(ev2.find(e => e.t === 'length').frame - closeFrame) < B);
  const A0 = Math.ceil(closeFrame / B) * B + 16 * B;   // track 1 starts recording here
  const P = 20000;                                 // marker at loop position P (what the player hears)
  const posAt = f => (f - closeFrame) % SR;         // play head at absolute frame f (pos 0 at closeFrame)
  // find the absolute frame where the heard position is P, then the marker arrives IN+OUT later
  let markerFrame = -1;
  for (let f = A0; f < A0 + SR; f++) if (posAt(f - OUT) === P && f > A0) { markerFrame = f + IN; break; }
  const core3 = new LooperCore(SR);
  core3.setLatency(IN, OUT);
  drive(core3, 4 * SR, f => [f === markerFrame ? 1 : 0, 0], {
    0: c => c.toggle(0), [SR]: c => c.toggle(0),
    [A0]: c => c.toggle(1),
  });
  const L1 = core3.tracks[1].L;
  let peak = -1, where = -1;
  for (let q = 0; q < SR; q++) if (L1[q] > peak) { peak = L1[q]; where = q; }
  assert.equal(peak, 1);
  assert.equal(where, P, `marker written at ${where}, expected ${P}`);
});

test('a second track records one cycle in sync, then overdubs', () => {
  const core = new LooperCore(SR);
  const A = SR + 100 * B;                           // track 1 starts recording at pos 100 B
  const { events } = drive(core, A + SR + 50 * B, f => [ramp(f), 0], {
    0: c => c.toggle(0), [SR]: c => c.toggle(0),
    [A]: c => c.toggle(1),
  });
  const t1 = core.tracks[1];
  assert.equal(t1.mode, 'dub', 'after a full cycle the track overdubs');
  const auto = events.find(e => e.t === 'track' && e.i === 1 && e.mode === 'dub');
  assert.ok(auto.frame >= A + SR - B && auto.frame <= A + SR + B, `auto-dub at ${auto.frame}`);
  // loop position (100 B + j) % SR holds the input of frame A + j; the first
  // 50 blocks were then overdubbed once more (frame A + SR + j)
  for (let j = 0; j < SR; j += 777) {
    const q = (100 * B + j) % SR;
    const want = ramp(A + j) + (j < 50 * B ? ramp(A + SR + j) : 0);
    assert.ok(near(t1.L[q], want), `L1[${q}] = ${t1.L[q]} expected ${want}`);
  }
});

test('overdub with feedback scales the old layer; undo swaps it back', () => {
  const core = new LooperCore(SR);
  drive(core, 2 * SR, () => [1, 1], { 0: c => c.toggle(0), [HALF]: c => { c.toggle(0); c.toggle(0); } });   // close, then PLAY at once
  const t = core.tracks[0];
  assert.equal(t.mode, 'play');
  assert.ok(t.L.every(v => near(v, 1)), 'loop holds 1.0');
  core.setTrack(0, { feedback: 0.5 });
  core.play();
  drive(core, SR, () => [0.25, 0.25], { 0: c => c.toggle(0) /* → dub */, [HALF]: c => c.toggle(0) /* → play after exactly one cycle */ });
  assert.ok(t.L.every(v => near(v, 0.75)), `after one dub pass: ${t.L[0]}`);
  assert.ok(t.undoValid);
  core.undo(0);
  assert.ok(t.L.every(v => near(v, 1)), 'undo restored the layer');
  core.undo(0);
  assert.ok(t.L.every(v => near(v, 0.75)), 'undo again redoes');
});

test('reverse and half speed act on playback only', () => {
  const core = new LooperCore(SR);
  const LEN = QUARTER;
  drive(core, SR, f => [ramp(f), 0], { 0: c => c.toggle(0), [LEN]: c => c.toggle(0), [LEN + B]: c => c.toggle(0) });
  core.setTrack(0, { reverse: true });
  core.play();
  let r = drive(core, LEN, () => [0, 0]);
  for (let p = 0; p < LEN; p += 101) assert.ok(near(r.outs[0][0][p], core.tracks[0].L[LEN - 1 - p]), `reverse at ${p}`);
  core.setTrack(0, { reverse: false, half: true });
  core.play();
  r = drive(core, LEN, () => [0, 0]);
  for (let p = 0; p < LEN - 2; p += 101) {
    const want = core.tracks[0].L[Math.floor(p / 2)] * (p % 2 ? 0.5 : 1) + core.tracks[0].L[Math.floor(p / 2) + 1] * (p % 2 ? 0.5 : 0);
    assert.ok(near(r.outs[0][0][p], want, 1e-6), `half speed at ${p}`);
  }
});

test('SYNC: the first loop snaps to whole beats, the overrun wraps into the start', () => {
  const core = new LooperCore(SR);
  const BEAT = QUARTER;                                 // ≈ 239 BPM, block-aligned
  core.setSnap(BEAT);
  const { events } = drive(core, 4 * BEAT, f => [ramp(f), 0], {
    0: c => c.toggle(0), [BEAT + 8 * B]: c => c.toggle(0) /* close → dub */, [BEAT + 16 * B]: c => c.toggle(0) /* → play */,
  });
  const len = events.find(e => e.t === 'length');
  assert.equal(len.length, BEAT);
  const t = core.tracks[0];
  // positions 0..8B were played twice (end of cycle 1 overrun = start of cycle 2)
  assert.ok(near(t.L[5], ramp(5) + ramp(BEAT + 5)));
  assert.ok(near(t.L[9 * B], ramp(9 * B) + ramp(BEAT + 9 * B)), 'the dub pass continued in sync');
  // a press just short of the beat rounds up and waits for the beat
  const core2 = new LooperCore(SR);
  core2.setSnap(BEAT);
  const { events: ev2 } = drive(core2, 4 * BEAT, f => [ramp(f), 0], { 0: c => c.toggle(0), [BEAT - 8 * B]: c => c.toggle(0) });
  assert.equal(core2.tracks[0].mode, 'dub');
  const len2 = ev2.find(e => e.t === 'length');
  assert.equal(len2.length, BEAT);
  assert.ok(len2.frame >= BEAT - B && len2.frame <= BEAT, `closed at the beat (frame ${len2.frame})`);
});

test('the maximum length closes the first loop by itself', () => {
  const core = new LooperCore(SR, { maxSeconds: 0.5 });
  const { events } = drive(core, 2 * SR, f => [ramp(f), 0], { 0: c => c.toggle(0) });
  const len = events.find(e => e.t === 'length');
  assert.equal(len.length, SR / 2);
  assert.equal(core.tracks[0].mode, 'dub');
});

test('stop, play, clear and clearAll', () => {
  const core = new LooperCore(SR);
  drive(core, SR, f => [ramp(f), 0], { 0: c => c.toggle(0), [HALF]: c => { c.toggle(0); c.toggle(0); } });
  core.stop();
  assert.equal(core.running, false);
  let r = drive(core, B * 4, () => [0, 0]);
  assert.ok(r.outs[0][0].every(v => v === 0), 'silent while stopped');
  core.play();
  assert.equal(core.pos, 0);
  r = drive(core, B, () => [0, 0]);
  assert.ok(near(r.outs[0][0][7], core.tracks[0].L[7]), 'PLAY restarts from the top');
  // STOP while recording the first loop closes it
  const c2 = new LooperCore(SR);
  drive(c2, QUARTER, () => [0.5, 0.5], { 0: c => c.toggle(1) });
  c2.stop();
  drive(c2, B, () => [0, 0]);
  assert.equal(c2.length, QUARTER);
  assert.equal(c2.tracks[1].mode, 'play');
  core.clear(0);
  assert.equal(core.length, 0, 'clearing the only track resets the loop');
  assert.equal(core.tracks[0].has, false);
  drive(core, SR, () => [1, 0], { 0: c => c.toggle(2), [HALF]: c => c.toggle(2), [HALF + B]: c => c.toggle(3) });
  assert.ok(core.tracks[2].has && core.tracks[3].has);
  core.clearAll();
  assert.ok(core.tracks.every(t => !t.has) && core.length === 0 && !core.running);
});

test('mute and meters', () => {
  const core = new LooperCore(SR);
  drive(core, SR, () => [0.5, 0.5], { 0: c => c.toggle(0), [HALF]: c => { c.toggle(0); c.toggle(0); } });
  const m = core.meter();
  assert.equal(m.length, HALF);
  assert.ok(m.levels[0] > 0.49 && m.input > 0.49);
  core.setTrack(0, { mute: true });
  const r = drive(core, B, () => [0, 0]);
  assert.ok(r.outs[0][0].every(v => v === 0));
});

test('STOP drops the latency tail: a stopped head records nothing', () => {
  const core = new LooperCore(SR);
  core.setLatency(0, 500);
  drive(core, SR, () => [0.1, 0.1], { 0: c => c.toggle(0), [HALF]: c => c.toggle(0) /* close → dub */ });
  core.stop();                                 // mid-overdub, with a 500-frame tail pending
  const t = core.tracks[0];
  assert.equal(t.mode, 'play');
  assert.equal(t.writing, 'none');
  drive(core, 40 * B, () => [0.1, 0.1]);       // the synth keeps ringing after STOP
  assert.ok(t.L.every(v => Math.abs(v) <= 0.2 + 1e-6), `peak ${Math.max(...t.L.map(Math.abs))}`);
});

test('STOP while the first loop waits for its beat closes it stopped', () => {
  const core = new LooperCore(SR);
  const BEAT = QUARTER;
  core.setSnap(BEAT);
  drive(core, BEAT + 60 * B, () => [0.5, 0.5], { 0: c => c.toggle(0) });   // in the second half of beat 2
  core.stop();                                 // rounds up to 2 beats: the close waits for the frames
  assert.equal(core.running, false);
  const { events } = drive(core, BEAT, () => [0.5, 0.5]);
  const t = core.tracks[0];
  assert.equal(core.length, 2 * BEAT);
  assert.ok(events.some(e => e.t === 'track' && e.i === 0 && e.mode === 'play'));
  assert.equal(t.mode, 'play');
  assert.equal(t.writing, 'none');
  assert.equal(core.running, false);
  drive(core, 40 * B, () => [0.5, 0.5]);       // input keeps coming while stopped
  assert.ok(t.L.every(v => Math.abs(v) <= 1 + 1e-6), `peak ${Math.max(...t.L.map(Math.abs))}`);
  core.play();
  const r = drive(core, B, () => [0, 0]);
  assert.ok(near(r.outs[0][0][3], t.L[3]), 'PLAY then plays the closed loop');
});

test('a latency longer than the loop still writes every frame', () => {
  const core = new LooperCore(SR);
  const LEN = QUARTER;
  drive(core, LEN + B, () => [0, 0], { 0: c => c.toggle(0), [LEN]: c => { c.toggle(0); c.toggle(0); } });
  core.setLatency(0, LEN + 3000);              // write head further back than one loop
  drive(core, LEN, () => [1, 1], { 0: c => c.toggle(1) });   // exactly one cycle: each frame written once
  const t = core.tracks[1];
  assert.ok(t.L.every(v => v === 1), `unwritten frames: ${t.L.filter(v => v !== 1).length}`);
});

test('the undo layer builds itself and survives a partial pass', () => {
  const core = new LooperCore(SR);
  drive(core, SR, f => [ramp(f), 0], { 0: c => c.toggle(0), [HALF]: c => { c.toggle(0); c.toggle(0); } });
  const t = core.tracks[0];
  const before = Float32Array.from(t.L);
  core.play();
  // overdub a third of the loop only, then PLAY
  drive(core, QUARTER, () => [1, 0], { 0: c => c.toggle(0), [QUARTER - 30 * B]: c => c.toggle(0) });
  assert.equal(t.mode, 'play');
  assert.ok(t.undoValid, 'the layer completed within a few blocks of the pass');
  const layered = Float32Array.from(t.L);
  assert.ok(near(layered[5], before[5] + 1) && near(layered[HALF - 5], before[HALF - 5]), 'only the written range changed');
  core.undo(0);
  assert.ok(t.L.every((v, q) => v === before[q]), 'undo restored the exact pre-pass audio');
  core.undo(0);
  assert.ok(t.L.every((v, q) => v === layered[q]), 'undo again redoes');
});
