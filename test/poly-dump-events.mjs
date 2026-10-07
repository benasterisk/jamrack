// Runs the POLY engine (js/audio/guitar/poly/engine.js) on WAV files and
// writes its note events as JSON for test/score.py and for the equivalence
// check against the Python prototype (test/poly/merge.py events).
//
//   node test/poly-dump-events.mjs <file.wav> [out.json]
//   node test/poly-dump-events.mjs --guitarset <dir> --set solo|comp [--takes test/takes.json] --out <out.json>
//   node test/poly-dump-events.mjs --mixdir <dir> --set mix2 --takes <dir>/takes.json --out <out.json>
//
// Same JSON as test/dump-events.mjs. Timestamps: (hop + 1) * 2.67 ms of
// analysis time, like the prototype. By default the WAV is resampled with the
// engine's own resampler and the first `delay` output samples are dropped so
// that the analysis hops fall exactly where scipy's zero-phase resample_poly
// put them in the prototype (--align live keeps the causal stream instead,
// i.e. what a browser hears, 0.42 ms later).
//
// Prints the decomposer and whole-hop cost per hop (mean and p99) per take:
// the budget is one hop = 2.667 ms.
import { readFileSync, writeFileSync } from 'node:fs';
import { join, basename } from 'node:path';
import { readWav } from './dump-events.mjs';
import { PolyTracker, ANALYSIS_RATE, ANALYSIS_HOP } from '../js/audio/guitar/poly/engine.js';
import { Resampler } from '../js/audio/guitar/poly/resample.js';
import { buildBank } from '../js/audio/guitar/poly/bank.js';
import { NOTE_RULE } from '../js/audio/guitar/poly/notes.js';
import { DECOMPOSER } from '../js/audio/guitar/poly/engine.js';
if (process.env.RULE) Object.assign(NOTE_RULE, JSON.parse(process.env.RULE));
if (process.env.DECOMP) Object.assign(DECOMPOSER, JSON.parse(process.env.DECOMP));

const HOP_S = ANALYSIS_HOP / ANALYSIS_RATE;
let bank = null;

/** Note events of one WAV through the engine; `align` = 'scipy' | 'live'. */
export function trackNotesPoly(wav, align = 'scipy') {
  bank = bank || buildBank('medium');
  const tr = new PolyTracker(ANALYSIS_RATE, { bank });    // fed at the analysis rate below
  const rs = new Resampler(wav.rate, ANALYSIS_RATE);
  const lag = align === 'scipy' ? Math.round(rs.halfLen / rs.down) : 0;
  const open = new Map();
  const notes = [];
  const hopMs = [], nmfMs = [];
  let skipped = 0;
  const buf = new Float64Array(4096);
  let n = 0;
  const feed = () => {
    const t0 = performance.now();
    for (const e of tr.pushAnalysisRate(buf.subarray(0, n))) {
      if (e.t === 'on') {
        const prev = open.get(e.midi);
        if (prev) { prev.offset = (e.hop + 1) * HOP_S; open.delete(e.midi); }
        const note = { onset: (e.hop + 1) * HOP_S, offset: null, midi: e.midi, velocity: e.vel, why: e.why };
        notes.push(note);
        open.set(e.midi, note);
      } else if (e.t === 'off') {
        const prev = open.get(e.midi);
        if (prev) { prev.offset = (e.hop + 1) * HOP_S; open.delete(e.midi); }
      } else if (e.t === 'meter') {
        hopMs.push(e.hopMs); nmfMs.push(e.nmfMs);
      }
    }
    void t0;
    n = 0;
  };
  for (let o = 0; o < wav.samples.length; o += 4096) {
    rs.push(wav.samples.subarray(o, Math.min(wav.samples.length, o + 4096)), y => {
      if (skipped < lag) { skipped++; return; }
      buf[n++] = y;
      if (n === buf.length) feed();
    });
  }
  feed();
  const lastHop = tr.hop - 1;
  for (const e of tr.flush()) {
    if (e.t === 'off') {
      const prev = open.get(e.midi);
      if (prev) { prev.offset = (e.hop + 1) * HOP_S; open.delete(e.midi); }
    }
  }
  const end = (lastHop + 1) * HOP_S;
  for (const nt of notes) if (nt.offset === null) nt.offset = end;
  notes.sort((a, b) => a.onset - b.onset);
  const q = (arr, p) => { const s = [...arr].sort((a, b) => a - b); return s.length ? s[Math.min(s.length - 1, Math.floor(p * s.length))] : NaN; };
  const mean = arr => arr.reduce((a, b) => a + b, 0) / Math.max(1, arr.length);
  return {
    duration: wav.samples.length / wav.rate,
    events: notes,
    cost: { hops: tr.hop, nmfMeanMs: mean(nmfMs), hopMeanMs: mean(hopMs), hopP99Ms: q(hopMs, 0.99), hopMaxMs: tr.maxHopMs },
  };
}

function main() {
  const a = process.argv.slice(2);
  const opt = { takes: new URL('./takes.json', import.meta.url).pathname, set: 'solo', label: 'poly-js', align: 'scipy' };
  const pos = [];
  for (let i = 0; i < a.length; i++) {
    if (a[i].startsWith('--')) opt[a[i].slice(2)] = a[++i]; else pos.push(a[i]);
  }
  const result = { label: opt.label, source: 'js/audio/guitar/poly/engine.js', align: opt.align, takes: {} };
  let outPath;
  const costs = [];
  const report = (take, r) => {
    costs.push(r.cost);
    console.error(`${take}: ${r.events.length} notes, ${r.cost.hops} hops, NMF ${r.cost.nmfMeanMs.toFixed(3)} ms/hop, hop ${r.cost.hopMeanMs.toFixed(3)} ms mean / ${r.cost.hopP99Ms.toFixed(3)} p99 / ${r.cost.hopMaxMs.toFixed(2)} max`);
    delete r.cost;
  };
  const dir = opt.guitarset || opt.mixdir;
  if (dir) {
    const takesPath = opt.takes.startsWith('/') && /^\/[A-Za-z]:/.test(opt.takes) ? opt.takes.slice(1) : opt.takes;
    const list = JSON.parse(readFileSync(takesPath, 'utf8'))[opt.set];
    if (!list) throw new Error(`no "${opt.set}" list in ${takesPath}`);
    for (const take of list) {
      const wav = readWav(join(dir, 'audio_mono-pickup_mix', take + '_mix.wav'));
      result.takes[take] = trackNotesPoly(wav, opt.align);
      report(take, result.takes[take]);
    }
    result.set = opt.set;
    outPath = opt.out || `events-${opt.label}-${opt.set}.json`;
  } else {
    if (pos.length < 1) {
      console.error('usage: node test/poly-dump-events.mjs <file.wav> [out.json]\n       node test/poly-dump-events.mjs --guitarset <dir> --set solo|comp [--takes test/takes.json] [--align scipy|live] --out <out.json>');
      process.exit(1);
    }
    const wav = readWav(pos[0]);
    const take = basename(pos[0]).replace(/(_mix)?\.wav$/i, '');
    result.takes[take] = trackNotesPoly(wav, opt.align);
    report(take, result.takes[take]);
    outPath = pos[1] || opt.out || basename(pos[0]).replace(/\.wav$/i, '') + '.poly.json';
  }
  writeFileSync(outPath, JSON.stringify(result));
  const n = Object.values(result.takes).reduce((s, t) => s + t.events.length, 0);
  const avg = k => costs.reduce((s, c) => s + c[k], 0) / costs.length;
  console.error(`wrote ${outPath}: ${Object.keys(result.takes).length} take(s), ${n} notes; cost over the set: NMF ${avg('nmfMeanMs').toFixed(3)} ms/hop, hop ${avg('hopMeanMs').toFixed(3)} ms mean (budget ${(1000 * HOP_S).toFixed(3)} ms)`);
}
if (process.argv[1] && import.meta.url.endsWith(basename(process.argv[1]))) main();
