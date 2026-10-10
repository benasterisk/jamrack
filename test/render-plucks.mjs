// Synthetic equivalence corpus of the MidPluck plugin (docs/plugin-plan.md 4.1).
//
//   node test/render-plucks.mjs [--out <dir>] [--engines mono,poly,dense]
//   node test/render-plucks.mjs --check --dump <dump_events.exe> [--out <dir>] [--engines ...]
//
// Renders its own list of recipes (explicit copies of the signals built in the
// bodies of test/guitar-tracker.test.mjs and test/poly-engine.test.mjs, plus
// strums and a palm mute) with the generators of test/plucks.mjs, at 44.1, 48
// and 96 kHz, into 16-bit mono WAV files:
//   <out>/<rate>/audio_mono-pickup_mix/<recipe>_mix.wav   <out>/<rate>/takes.txt
// (the --mixdir layout of dump_events and test/poly-dump-events.mjs). Each WAV
// is READ BACK with readWav() (int16 / 32768) and the JS reference dumps are
// computed on those samples, never on the float signal:
//   MONO   trackNotes() of test/dump-events.mjs, at every rate   -> js-mono-<rate>.json
//   POLY   trackNotesPoly(wav, 'live') at 48 kHz                 -> js-poly-48000.json
//   dense  the same with DECOMPOSER { sparse: 0, iter: 15 }, on the recipes
//          of <out>/48000/takes-dense.txt (one seed per chord / dyad family)
//                                                                 -> js-poly-dense-48000.json
// With --check, runs dump_events on the same WAVs (MONO --stamp block, POLY
// --align live, dense --dense) and diffs note for note (test/diff-events.mjs,
// hop MONO 64 * round(rate / 24000) / rate, POLY 64 / 24000). Exit code 1 if
// any note is missing, extra, shifted or off-diverge, or if dump_events fails.
//
// The ECO load control of the JS POLY engine is switched off here: offline it
// reacts to the wall clock (the dense decomposer averages ~2.3 ms per hop in
// Node, above 80 % of the 2.67 ms budget, so ECO would switch on after 375
// hops and flap), which would make the reference depend on the machine's load.
// dump_events must not run ECO either.
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { spawnSync } from 'node:child_process';
import { pathToFileURL } from 'node:url';
import { pluck, silence, concat, seq, chord, strum, muteAt } from './plucks.mjs';
import { diffEvents, formatDiff } from './diff-events.mjs';

const RATES = [44100, 48000, 96000];
const POLY_RATE = 48000;
const POLY_HOP = 64 / 24000;
const monoHop = rate => 64 * Math.round(rate / 24000) / rate;

// ---------------------------------------------------------------------------
// Recipes: name -> (sr) => Float32Array. Copies of the test signals; the
// comments name the test each one comes from.

const MONO_STRINGS = [   // guitar-tracker.test.mjs STRINGS
  ['D2-dropD', 38], ['E2', 40], ['A2', 45], ['D3', 50], ['G3', 55],
  ['B3', 59], ['E4', 64], ['A4-fret5', 69], ['E5-fret12', 76], ['A5-fret17', 81],
];
const POLY_STRINGS = [['E2', 40], ['A2', 45], ['D3', 50], ['G3', 55], ['B3', 59], ['E4', 64], ['A4', 69], ['E5', 76]];
const CHORDS = { E: [40, 47, 52], Am: [45, 52, 57], D: [50, 57, 66], G: [55, 59, 62], C: [48, 52, 55] };

export const RECIPES = [];
// dense: false keeps a recipe out of the dense group (2.3 ms per hop in Node,
// ~3.5x the shipped decomposer): one seed per chord / dyad family is enough there.
const add = (name, build, o = {}) => RECIPES.push({ name, build, dense: o.dense !== false });

// guitar-tracker.test.mjs, 'single plucks: right note, one note-on, latency per string'
for (const [label, midi] of MONO_STRINGS) {
  add(`pluck-${label}`, sr => concat(silence(sr, 0.1), pluck(sr, { midi, dur: 0.6, cut: 0.5 }), silence(sr, 0.2)));
}
// 'noise floor and silence never trigger' (also poly-engine 'silence and a -54 dBFS hiss')
add('hiss-then-silence', sr => concat(silence(sr, 1.0, 0.002), silence(sr, 0.5, 0.0)));
// 'a knock (noise burst) does not become a note'
add('knock', sr => concat(silence(sr, 0.1), silence(sr, 0.03, 0.2, 11), silence(sr, 0.3)));
// 'repicking the same note retriggers every time'
add('repick-x5', sr => seq(sr, 1.2, [0, 1, 2, 3, 4].map(i => ({ at: 0.1 + i * 0.15, midi: 57, seed: i + 1 }))));
// 'hammer-on without a new pluck changes the note (legato)'
add('legato-hammer', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 64, dur: 0.8, hammer: { at: 0.3, semis: 2 } })));
// 'a whole-step bend is pitch bend, not a new note' (and the chromatic-mode test's signal)
add('bend-whole-step', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 55, dur: 1.0, bend: { at: 0.3, dur: 0.3, semis: 2 } })));
// 'missing fundamental (pickup near the bridge) still finds the right octave'
for (const midi of [40, 45, 50]) {
  add(`missing-fundamental-${midi}`, sr => concat(silence(sr, 0.1), pluck(sr, { midi, dur: 0.5, fundamental: 0.15 })));
}
// 'pitch accuracy on sustained notes (cents)'
for (const [midi, cents, stiffness] of [[40, -15, 0], [40, -15, 2e-4], [50, 0, 2e-4], [64, 20, 2e-4], [76, -30, 2e-4], [81, 10, 2e-4]]) {
  add(`sustain-${midi}${cents < 0 ? '' : '+'}${cents}c-B${stiffness}`, sr => concat(silence(sr, 0.1), pluck(sr, { midi, cents, dur: 0.8, stiffness })));
}
// 'velocity follows pick strength' (and 'octave and transpose' at amp 0.3)
for (const amp of [0.03, 0.12, 0.5]) {
  add(`velocity-${amp}`, sr => concat(silence(sr, 0.1), pluck(sr, { midi: 52, dur: 0.4, amp })));
}
add('pluck-E3', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 52, dur: 0.4 })));
// 'a fast chromatic run is tracked note for note'
add('chromatic-run', sr => seq(sr, 1.5, [60, 61, 62, 63, 64, 65, 66, 67].map((m, i) => ({ at: 0.1 + i * 0.12, midi: m, seed: i + 20 }))));
// 'note-off when the string decays below the release threshold'
add('decay', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 57, dur: 2.5, decay: 2 })));
// '44.1 kHz and 256-sample blocks give the same notes'
add('rates-A2', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 45, dur: 0.5 }), silence(sr, 0.1)));
// 'flush() releases a sounding note': the note is still open at the end of the file
add('open-at-end', sr => concat(silence(sr, 0.1), pluck(sr, { midi: 45, dur: 0.3 })));
// 'CPU: one second of audio...': a pluck from the very first sample
add('cpu-E2', sr => pluck(sr, { midi: 40, dur: 1.0 }));
// 'mono on a 3-note chord (15 ms strum)'
for (const [label, ns] of Object.entries(CHORDS)) for (const seed of [1, 7, 42]) for (const up of [false, true]) {
  add(`chord-${label}-s${seed}-${up ? 'up' : 'down'}`, sr => chord(sr, { notes: ns, at: 0.1, dur: 1, spread: 0.015, seed, up, total: 0.9 }), { dense: seed === 7 });
}
// 'mono on a dyad (A2+E3)'
for (const spread of [0, 0.005, 0.015, 0.035]) for (const seed of [1, 7, 42, 99]) for (const up of [false, true]) {
  add(`dyad-A2E3-${Math.round(spread * 1000)}ms-s${seed}-${up ? 'up' : 'down'}`, sr => chord(sr, { notes: [45, 52], at: 0.1, dur: 1, spread, seed, up, total: 0.9 }), { dense: seed === 7 });
}
// poly-engine.test.mjs, 'single plucks on every string'
for (const [label, midi] of POLY_STRINGS) {
  add(`poly-pluck-${label}`, sr => concat(silence(sr, 0.1), pluck(sr, { midi, dur: 0.6, cut: 0.5, stiffness: 1e-4 }), silence(sr, 0.2)));
}
// 'a dyad (two plucks at once) yields both notes'
add('poly-dyad-C3G3', sr => {
  const a = pluck(sr, { midi: 48, dur: 0.8, cut: 0.7, stiffness: 1e-4, seed: 2 });
  const b = pluck(sr, { midi: 55, dur: 0.8, cut: 0.7, stiffness: 1e-4, seed: 3 });
  const m = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) m[i] = 0.6 * (a[i] + b[i]);
  return concat(silence(sr, 0.1), m, silence(sr, 0.2));
});
// 'cost per hop'
add('poly-cost-A2', sr => concat(silence(sr, 0.05), pluck(sr, { midi: 45, dur: 1.0, stiffness: 1e-4 })));
// strums fast / medium / slow (plucks.mjs strum(), open E major, all six strings)
for (const speed of ['fast', 'medium', 'slow']) for (const up of [false, true]) {
  add(`strum-E-${speed}-${up ? 'up' : 'down'}`, sr => strum(sr, { notes: [40, 47, 52, 56, 59, 64], at: 0.1, dur: 1.2, speed, up, total: 1.4 }));
}
// palm mute (plucks.mjs muteAt())
add('palm-mute', sr => muteAt(concat(silence(sr, 0.1), pluck(sr, { midi: 50, dur: 0.8 })), sr, 0.4));

// ---------------------------------------------------------------------------

/** 16-bit PCM mono WAV, s = max(-32768, min(32767, round(x * 32768))). */
export function wav16(signal, rate) {
  const n = signal.length;
  const b = Buffer.alloc(44 + 2 * n);
  b.write('RIFF', 0, 'ascii'); b.writeUInt32LE(36 + 2 * n, 4); b.write('WAVE', 8, 'ascii');
  b.write('fmt ', 12, 'ascii'); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
  b.writeUInt32LE(rate, 24); b.writeUInt32LE(2 * rate, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36, 'ascii'); b.writeUInt32LE(2 * n, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(signal[i] * 32768))), 44 + 2 * i);
  return b;
}

function parseArgs(argv) {
  const opt = { out: join(tmpdir(), 'midpluck-plucks'), engines: 'mono,poly,dense', check: false, dump: null };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--check') opt.check = true;
    else if (a === '--out' || a === '--dump' || a === '--engines') {
      if (i + 1 >= argv.length) throw new Error(`missing value after ${a}`);
      opt[a.slice(2)] = argv[++i];
    } else throw new Error(`unknown argument ${a}`);
  }
  opt.engines = opt.engines.split(',').map(s => s.trim()).filter(Boolean);
  for (const e of opt.engines) if (!['mono', 'poly', 'dense'].includes(e)) throw new Error(`unknown engine "${e}" (mono, poly, dense)`);
  if (opt.check && !opt.dump) throw new Error('--check needs --dump <dump_events executable>');
  opt.out = resolve(opt.out);
  return opt;
}

/** Runs dump_events; returns the parsed JSON, or throws with its output. */
function runDump(exe, args, outJson) {
  const r = spawnSync(exe, [...args, '--out', outJson], { encoding: 'utf8', maxBuffer: 64 << 20 });
  const log = `${r.stdout || ''}${r.stderr || ''}`.trim();
  if (r.error) throw new Error(`cannot run ${exe}: ${r.error.message}`);
  if (r.status !== 0) throw new Error(`${exe} ${args.join(' ')} exited with ${r.status}:\n${log}`);
  const last = log.split(/\r?\n/).filter(l => l.startsWith('wrote ')).pop();
  return { log, summary: last || '' };
}

async function main() {
  const opt = parseArgs(process.argv.slice(2));

  // The JS dump modules apply these at import time; dump_events is not given
  // them, so a forgotten variable would compare two different engines.
  const stray = ['TUNING', 'DECOMP', 'RULE'].filter(k => process.env[k]);
  if (stray.length) {
    console.error(`render-plucks: unset ${stray.join(', ')} first (the JS dumps would apply ${stray.length > 1 ? 'them' : 'it'}, dump_events would not)`);
    process.exit(2);
  }
  const { readWav, trackNotes } = await import('./dump-events.mjs');
  const { trackNotesPoly } = await import('./poly-dump-events.mjs');
  const { PolyTracker, DECOMPOSER } = await import('../js/audio/guitar/poly/engine.js');
  PolyTracker.prototype._loadControl = function () {};   // no ECO offline (see the header)

  const names = RECIPES.map(r => r.name);
  const denseNames = RECIPES.filter(r => r.dense).map(r => r.name);
  if (new Set(names).size !== names.length) throw new Error('duplicate recipe names');
  const t0 = performance.now();

  // 1. render, write, read back
  const wavs = {};   // rate -> name -> { rate, samples }
  const rates = opt.engines.includes('mono') ? RATES : [POLY_RATE];
  for (const rate of rates) {
    const dir = join(opt.out, String(rate), 'audio_mono-pickup_mix');
    mkdirSync(dir, { recursive: true });
    wavs[rate] = {};
    for (const r of RECIPES) {
      const path = join(dir, `${r.name}_mix.wav`);
      writeFileSync(path, wav16(r.build(rate), rate));
      wavs[rate][r.name] = readWav(path);
    }
    writeFileSync(join(opt.out, String(rate), 'takes.txt'), names.join('\n') + '\n', 'ascii');
    writeFileSync(join(opt.out, String(rate), 'takes-dense.txt'), denseNames.join('\n') + '\n', 'ascii');
  }
  console.log(`rendered ${RECIPES.length} recipes at ${rates.join(' / ')} Hz into ${opt.out} (${((performance.now() - t0) / 1000).toFixed(1)} s)`);

  // 2. JS reference dumps on the re-read samples
  const groups = [];
  const dumpJs = (label, source, set, extra, list, fn) => {
    const t1 = performance.now();
    const result = { label, source, ...extra, takes: {}, set };
    for (const name of list) {
      const t = fn(name);
      delete t.cost;
      result.takes[name] = t;
    }
    Object.defineProperty(result, 'seconds', { value: (performance.now() - t1) / 1000 });   // not serialised
    return result;
  };
  if (opt.engines.includes('mono')) {
    for (const rate of RATES) {
      const js = dumpJs('mono', 'js/audio/guitar/tracker.js', `synthetic-${rate}`, {}, names, n => trackNotes(wavs[rate][n]));
      groups.push({ id: `mono-${rate}`, rate, hop: monoHop(rate), js, takes: 'takes.txt', args: ['--mode', 'mono', '--stamp', 'block', '--block', '128'] });
    }
  }
  if (opt.engines.includes('poly')) {
    const js = dumpJs('poly-js', 'js/audio/guitar/poly/engine.js', `synthetic-${POLY_RATE}`, { align: 'live' }, names, n => trackNotesPoly(wavs[POLY_RATE][n], 'live'));
    groups.push({ id: `poly-${POLY_RATE}`, rate: POLY_RATE, hop: POLY_HOP, js, takes: 'takes.txt', args: ['--mode', 'poly', '--align', 'live'] });
  }
  if (opt.engines.includes('dense')) {
    const saved = { ...DECOMPOSER };
    Object.assign(DECOMPOSER, { sparse: 0, iter: 15 });
    try {
      const js = dumpJs('poly-js-dense', 'js/audio/guitar/poly/engine.js', `synthetic-${POLY_RATE}`, { align: 'live' }, denseNames, n => trackNotesPoly(wavs[POLY_RATE][n], 'live'));
      groups.push({ id: `poly-dense-${POLY_RATE}`, rate: POLY_RATE, hop: POLY_HOP, js, takes: 'takes-dense.txt', args: ['--mode', 'poly', '--align', 'live', '--dense'] });
    } finally {
      for (const k of Object.keys(DECOMPOSER)) delete DECOMPOSER[k];
      Object.assign(DECOMPOSER, saved);
    }
  }
  for (const g of groups) {
    writeFileSync(join(opt.out, `js-${g.id}.json`), JSON.stringify(g.js));
    const n = Object.values(g.js.takes).reduce((s, t) => s + t.events.length, 0);
    console.log(`JS ${g.id}: ${n} notes over ${Object.keys(g.js.takes).length} recipes in ${g.js.seconds.toFixed(1)} s -> ${join(opt.out, `js-${g.id}.json`)}`);
  }
  console.log(`JS reference dumps done (${((performance.now() - t0) / 1000).toFixed(1)} s)`);
  if (!opt.check) return 0;

  // 3. dump_events on the same WAVs, then the diff
  const rows = [];
  let failed = false;
  for (const g of groups) {
    const outJson = join(opt.out, `cpp-${g.id}.json`);
    const dir = join(opt.out, String(g.rate));
    let cpp;
    try {
      const t1 = performance.now();
      const r = runDump(opt.dump, [...g.args, '--mixdir', dir, '--set', `synthetic-${g.rate}`, '--takes', join(dir, g.takes), '--label', `cpp-${g.id}`], outJson);
      console.log(`C++ ${g.id} (${((performance.now() - t1) / 1000).toFixed(1)} s): ${r.summary}`);
      cpp = JSON.parse(readFileSync(outJson, 'utf8'));
    } catch (e) {
      console.log(`C++ ${g.id}: FAILED\n${e.message}`);
      rows.push({ group: g.id, js: '-', cpp: '-', identical: '-', shifted: '-', 'off-div': '-', missing: '-', extra: '-', 'why!=': '-', 'max dOn ms': '-', verdict: 'dump_events failed' });
      failed = true;
      continue;
    }
    const d = diffEvents(g.js, cpp, { hop: g.hop });
    const t = d.total;
    const bad = t.missing + t.extra + t.shifted + t.offDiverge;
    rows.push({ group: g.id, js: t.ref, cpp: t.cand, identical: t.identical, shifted: t.shifted, 'off-div': t.offDiverge, missing: t.missing, extra: t.extra, 'why!=': t.whyDiff, 'max dOn ms': (1000 * t.maxOnset).toFixed(3), verdict: bad ? 'DIFF' : 'ok' });
    if (bad) {
      failed = true;
      console.log(formatDiff(d, { title: `--- ${g.id}: recipes with differences (hop ${g.hop.toFixed(7)} s)`, onlyDiff: true }));
    }
  }
  console.table(rows);
  console.log(failed ? 'render-plucks: FAIL (C++ and JS differ, or dump_events failed)' : 'render-plucks: every note identical');
  console.log(`total ${((performance.now() - t0) / 1000).toFixed(1)} s`);
  return failed ? 1 : 0;
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  main().then(code => process.exit(code), e => { console.error(`render-plucks: ${e.stack || e.message}`); process.exit(2); });
}
