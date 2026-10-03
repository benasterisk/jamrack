// Frame-by-frame trace of the tracker on a synthetic phrase — a tuning aid,
// not a test. Usage: node test/trace.mjs repick|run|hammer [fromMs] [toMs]
import { GuitarTracker } from '../js/audio/guitar/tracker.js';
import { pluck, silence, concat, seq, run } from './plucks.mjs';

const SR = 48000;
const which = process.argv[2] || 'repick';
const from = Number(process.argv[3] || 0), to = Number(process.argv[4] || 1e9);
const sig = {
  repick: () => seq(SR, 1.2, [0, 1, 2, 3, 4].map(i => ({ at: 0.1 + i * 0.15, midi: 57, seed: i + 1 }))),
  run: () => seq(SR, 1.5, [60, 61, 62, 63, 64, 65, 66, 67].map((m, i) => ({ at: 0.1 + i * 0.12, midi: m, seed: i + 20 }))),
  hammer: () => concat(silence(SR, 0.1), pluck(SR, { midi: 64, dur: 0.8, hammer: { at: 0.3, semis: 2 } })),
}[which]();

const tr = new GuitarTracker(SR);
const f = (v, n = 1) => (Number.isNaN(v) ? '  --' : v.toFixed(n).padStart(5));
tr.onFrame = fr => {
  const ms = fr.k * tr.frameMs;
  if (ms < from || ms > to) return;
  console.log(`${ms.toFixed(0).padStart(5)}ms rms${f(fr.rmsDb, 0)} ref${f(fr.ref, 0)} hf${f(fr.hfDb, 0)} hfref${f(fr.hfRef, 0)} lvl${f(fr.lvl, 0)}`
    + ` midi${f(fr.midiF, 2)} c${fr.conf.toFixed(2)} note${String(fr.note).padStart(3)}${fr.pend ? ' PEND' : ''}${fr.onset ? (fr.strong ? ' ONSET!' : ' onset') : ''}`);
};
const ev = run(tr, sig);
console.log(ev.filter(e => e.t !== 'meter').map(e => `${e.t}${e.midi ?? ''}@${e.ms.toFixed(0)}`).join(' '));
