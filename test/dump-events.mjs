// Runs the monophonic tracker (js/audio/guitar/tracker.js) on WAV files and
// writes its note events as JSON for test/score.py (mir_eval scoring).
//
//   node test/dump-events.mjs <file.wav> <out.json>
//   node test/dump-events.mjs --guitarset <dir> --set solo|comp [--takes test/takes.json] --out <out.json>
//
// Output: { label, source, takes: { <take>: { duration, events: [ { onset,
// offset, midi, velocity, why } ] } } } — times in seconds, stamped at the end
// of the 128-sample block that produced the event (what a host would see).
// A note still sounding at the end of the file is closed at the file end.
// TUNING='{"ONSET_AGREE":2}' in the environment applies tracker ablations.
import { readFileSync, writeFileSync } from 'node:fs';
import { join, basename } from 'node:path';
import { GuitarTracker, TUNING } from '../js/audio/guitar/tracker.js';
if (process.env.TUNING) Object.assign(TUNING, JSON.parse(process.env.TUNING));

/** Minimal RIFF/WAVE reader: PCM 16-bit, first channel (same as guitarset-eval.mjs). */
export function readWav(path) {
  const b = readFileSync(path);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('ascii', o, o + 4), size = dv.getUint32(o + 4, true);
    if (id === 'fmt ') fmt = { format: dv.getUint16(o + 8, true), channels: dv.getUint16(o + 10, true), rate: dv.getUint32(o + 12, true), bits: dv.getUint16(o + 22, true) };
    if (id === 'data') { data = { start: o + 8, size: Math.min(size, b.length - o - 8) }; break; }
    o += 8 + size + (size & 1);
  }
  if (!fmt || !data || fmt.format !== 1 || fmt.bits !== 16) throw new Error(`unsupported wav: ${path}`);
  const n = Math.floor(data.size / 2 / fmt.channels);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = dv.getInt16(data.start + i * 2 * fmt.channels, true) / 32768;
  return { rate: fmt.rate, samples: out };
}

/** Feeds the tracker in 128-sample blocks; pairs note-on / note-off into notes. */
export function trackNotes(wav, params = {}) {
  const tr = new GuitarTracker(wav.rate, params);
  const notes = [];
  let open = null;
  for (let o = 0; o < wav.samples.length; o += 128) {
    const chunk = wav.samples.subarray(o, Math.min(wav.samples.length, o + 128));
    const t = (o + chunk.length) / wav.rate;
    for (const e of tr.process(chunk)) {
      if (e.t === 'on') {
        if (open) { open.offset = t; open = null; }   // the tracker sends 'off' first, but be safe
        open = { onset: t, offset: null, midi: e.midi, velocity: e.vel, why: e.why };
        notes.push(open);
      } else if (e.t === 'off' && open && open.midi === e.midi) {
        open.offset = t; open = null;
      }
    }
  }
  const end = wav.samples.length / wav.rate;
  for (const n of notes) if (n.offset === null) n.offset = end;
  return { duration: end, events: notes };
}

function main() {
  const a = process.argv.slice(2);
  const opt = { takes: new URL('./takes.json', import.meta.url).pathname, set: 'solo', label: 'mono' };
  const pos = [];
  for (let i = 0; i < a.length; i++) {
    if (a[i].startsWith('--')) opt[a[i].slice(2)] = a[++i]; else pos.push(a[i]);
  }
  const result = { label: opt.label, source: 'js/audio/guitar/tracker.js', takes: {} };
  let outPath;
  if (opt.guitarset) {
    const list = JSON.parse(readFileSync(opt.takes, 'utf8'))[opt.set];
    if (!list) throw new Error(`no "${opt.set}" list in ${opt.takes}`);
    for (const take of list) {
      const wav = readWav(join(opt.guitarset, 'audio_mono-pickup_mix', take + '_mix.wav'));
      result.takes[take] = trackNotes(wav);
      console.error(`${take}: ${result.takes[take].events.length} notes`);
    }
    result.set = opt.set;
    outPath = opt.out || `events-${opt.label}-${opt.set}.json`;
  } else {
    if (pos.length < 1) {
      console.error('usage: node test/dump-events.mjs <file.wav> [out.json]\n       node test/dump-events.mjs --guitarset <dir> --set solo|comp [--takes test/takes.json] [--label mono] --out <out.json>');
      process.exit(1);
    }
    const wav = readWav(pos[0]);
    result.takes[basename(pos[0]).replace(/(_mix)?\.wav$/i, '')] = trackNotes(wav);
    outPath = pos[1] || opt.out || basename(pos[0]).replace(/\.wav$/i, '') + '.events.json';
  }
  writeFileSync(outPath, JSON.stringify(result));
  const n = Object.values(result.takes).reduce((s, t) => s + t.events.length, 0);
  console.error(`wrote ${outPath}: ${Object.keys(result.takes).length} take(s), ${n} notes`);
}
if (process.argv[1] && import.meta.url.endsWith(basename(process.argv[1]))) main();
