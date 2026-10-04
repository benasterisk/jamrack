// Measures the monophonic tracker on REAL guitar: GuitarSet (CC BY 4.0,
// https://zenodo.org/records/3371780) mono magnetic-pickup recordings with
// per-string note annotations from a hexaphonic pickup.
//
//   node test/guitarset-eval.mjs <guitarset dir> [takes=24] [filter=solo]
//
// <guitarset dir> holds audio_mono-pickup_mix/*.wav and annotation/*.jams.
// "solo" takes are single-note lines (what a monophonic tracker claims),
// "comp" takes are chord comping (where it is expected to fail).
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { GuitarTracker, TUNING } from '../js/audio/guitar/tracker.js';
// ablations: TUNING='{"ONSET_AGREE":2}' node test/guitarset-eval.mjs ...
if (process.env.TUNING) Object.assign(TUNING, JSON.parse(process.env.TUNING));

const dir = process.argv[2];
const takes = Number(process.argv[3] || 24);
const filter = process.argv[4] || 'solo';
if (!dir) { console.error('usage: node test/guitarset-eval.mjs <guitarset dir> [takes] [solo|comp]'); process.exit(1); }

/** Minimal RIFF/WAVE reader: PCM 16-bit mono (what GuitarSet ships). */
function readWav(path) {
  const b = readFileSync(path);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('ascii', o, o + 4), size = dv.getUint32(o + 4, true);
    if (id === 'fmt ') fmt = { format: dv.getUint16(o + 8, true), channels: dv.getUint16(o + 10, true), rate: dv.getUint32(o + 12, true), bits: dv.getUint16(o + 22, true) };
    if (id === 'data') { data = { start: o + 8, size }; break; }
    o += 8 + size + (size & 1);
  }
  if (!fmt || !data || fmt.format !== 1 || fmt.bits !== 16) throw new Error(`unsupported wav: ${path}`);
  const n = Math.floor(data.size / 2 / fmt.channels);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = dv.getInt16(data.start + i * 2 * fmt.channels, true) / 32768;
  return { rate: fmt.rate, samples: out };
}

/** Reference notes: the six per-string note_midi annotations merged. */
function readNotes(path) {
  const jams = JSON.parse(readFileSync(path, 'utf8'));
  const notes = [];
  for (const a of jams.annotations) {
    if (a.namespace !== 'note_midi') continue;
    for (const d of a.data) notes.push({ t: d.time, dur: d.duration, midi: Math.round(d.value) });
  }
  notes.sort((a, b) => a.t - b.t);
  // a note that starts while another string still rings is polyphony: a
  // monophonic tracker cannot be blamed for it, so it is counted apart
  for (let i = 0; i < notes.length; i++) {
    notes[i].overlapped = notes.some((o, j) => j !== i && o.t < notes[i].t && o.t + o.dur > notes[i].t + 0.03);
  }
  return notes;
}

function runTracker(wav) {
  const tr = new GuitarTracker(wav.rate);
  const ons = [];
  for (let o = 0; o < wav.samples.length; o += 128) {
    const chunk = wav.samples.subarray(o, Math.min(wav.samples.length, o + 128));
    for (const e of tr.process(chunk)) if (e.t === 'on') ons.push({ t: (o + chunk.length) / wav.rate, midi: e.midi, vel: e.vel });
  }
  return ons;
}

/** Greedy one-to-one matching: same pitch class+octave, onset within the window. */
function match(ref, got) {
  const usedRef = new Set(), lat = [];
  let tp = 0, fpOct = 0, fpOther = 0;
  for (const g of got) {
    let best = -1, bestD = Infinity;
    ref.forEach((r, i) => {
      if (usedRef.has(i) || r.midi !== g.midi) return;
      const d = g.t - r.t;
      if (d >= -0.06 && d <= 0.15 && Math.abs(d) < bestD) { best = i; bestD = Math.abs(d); }
    });
    if (best >= 0) { usedRef.add(best); tp++; lat.push(g.t - ref[best].t); continue; }
    const octave = ref.some(r => Math.abs(r.midi - g.midi) === 12 && g.t - r.t >= -0.06 && g.t - r.t <= 0.15);
    if (octave) fpOct++; else fpOther++;
  }
  const fn = ref.map((r, i) => !usedRef.has(i));
  return { tp, fpOct, fpOther, fnIsolated: ref.filter((r, i) => fn[i] && !r.overlapped).length, fnOverlapped: ref.filter((r, i) => fn[i] && r.overlapped).length, lat };
}

const files = readdirSync(join(dir, 'audio_mono-pickup_mix')).filter(f => f.endsWith('.wav') && f.includes(filter)).sort();
const step = Math.max(1, Math.floor(files.length / takes));
const picked = files.filter((_, i) => i % step === 0).slice(0, takes);
const tot = { refIso: 0, refOvl: 0, tp: 0, fpOct: 0, fpOther: 0, fnIso: 0, fnOvl: 0, lat: [], seconds: 0 };
const rows = [];
for (const f of picked) {
  const wav = readWav(join(dir, 'audio_mono-pickup_mix', f));
  const ref = readNotes(join(dir, 'annotation', f.replace('_mix.wav', '.jams')));
  const got = runTracker(wav);
  const m = match(ref, got);
  const iso = ref.filter(r => !r.overlapped).length, ovl = ref.length - iso;
  tot.refIso += iso; tot.refOvl += ovl; tot.tp += m.tp; tot.fpOct += m.fpOct; tot.fpOther += m.fpOther;
  tot.fnIso += m.fnIsolated; tot.fnOvl += m.fnOverlapped; tot.lat.push(...m.lat); tot.seconds += wav.samples.length / wav.rate;
  const med = m.lat.length ? m.lat.sort((a, b) => a - b)[m.lat.length >> 1] * 1000 : NaN;
  rows.push({ take: f.replace('_mix.wav', ''), 'ref notes': ref.length, 'of which overlapped': ovl, emitted: got.length, matched: m.tp, 'octave errs': m.fpOct, 'other false': m.fpOther, 'missed isolated': m.fnIsolated, 'median lat ms': med.toFixed(0) });
}
console.table(rows);
const lat = tot.lat.sort((a, b) => a - b);
const q = p => (lat.length ? lat[Math.min(lat.length - 1, Math.floor(p * lat.length))] * 1000 : NaN).toFixed(0);
const emitted = tot.tp + tot.fpOct + tot.fpOther;
console.log(`\n${picked.length} ${filter} takes, ${tot.seconds.toFixed(0)} s of audio, ${tot.refIso + tot.refOvl} reference notes (${tot.refOvl} overlapped by a still-ringing string)`);
console.log(`precision (emitted notes that are right): ${(100 * tot.tp / emitted).toFixed(1)} %   [octave errors ${(100 * tot.fpOct / emitted).toFixed(1)} %, other false notes ${(100 * tot.fpOther / emitted).toFixed(1)} %]`);
console.log(`recall on isolated notes: ${(100 * (tot.refIso - tot.fnIso) / tot.refIso).toFixed(1)} %   recall on overlapped notes: ${(100 * (tot.refOvl - tot.fnOvl) / Math.max(1, tot.refOvl)).toFixed(1)} %   recall on all: ${(100 * tot.tp / (tot.refIso + tot.refOvl)).toFixed(1)} %`);
console.log(`onset → note-on vs annotation (hex-pickup onsets, ±10 ms): median ${q(0.5)} ms, p25 ${q(0.25)} ms, p75 ${q(0.75)} ms, p90 ${q(0.9)} ms`);
