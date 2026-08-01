// Loads an SFZ instrument: fetches the .sfz, resolves #include, then fetches
// and decodes the samples it references.
//
// An SFZ bank can hold hundreds of files and hundreds of megabytes, so nothing
// is preloaded wholesale: the region map is built immediately (the .sfz itself
// is a few kB), a small batch of central notes is warmed up so the instrument
// becomes playable fast, and everything else is fetched on demand.
//
// Two Web Audio traps are handled here, both invisible in native players:
//
//  1. decodeAudioData RESAMPLES to the context rate (a 44.1 kHz file becomes a
//     48 kHz buffer). SFZ expresses offset/loop points in frames of the ORIGINAL
//     file, so those indices must be scaled — otherwise loops drift by ~9%.
//  2. decodeAudioData DISCARDS the WAV `smpl` chunk, which is where embedded
//     loop points live. We read the RIFF header ourselves before decoding.

import { parseSfz, samplePath } from './parser.js';

const MAX_INCLUDE_DEPTH = 8;

/** Reads sampleRate and loop points straight from the RIFF header. */
function readWavInfo(arrayBuffer) {
  const info = { sampleRate: null, loopStart: null, loopEnd: null };
  try {
    const dv = new DataView(arrayBuffer);
    if (dv.byteLength < 12) return info;
    const tag = (o) => String.fromCharCode(
      dv.getUint8(o), dv.getUint8(o + 1), dv.getUint8(o + 2), dv.getUint8(o + 3));
    if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return info;   // not a WAV
    let pos = 12;
    while (pos + 8 <= dv.byteLength) {
      const id = tag(pos);
      const size = dv.getUint32(pos + 4, true);
      const body = pos + 8;
      if (id === 'fmt ' && body + 8 <= dv.byteLength) {
        info.sampleRate = dv.getUint32(body + 4, true);
      } else if (id === 'smpl' && body + 44 <= dv.byteLength) {
        const loops = dv.getUint32(body + 28, true);
        if (loops > 0 && body + 36 + 24 <= dv.byteLength) {
          info.loopStart = dv.getUint32(body + 36 + 8, true);
          info.loopEnd = dv.getUint32(body + 36 + 12, true);
        }
      }
      pos = body + size + (size % 2);        // chunks are word-aligned
    }
  } catch { /* not a RIFF file (flac/ogg): defaults are fine */ }
  return info;
}

/** decodeAudioData in promise or callback form (old webkit is callback-only). */
function decodeAudio(ctx, arrayBuffer) {
  return new Promise((resolve, reject) => {
    try {
      const p = ctx.decodeAudioData(arrayBuffer, resolve, reject);
      if (p && typeof p.then === 'function') p.then(resolve, reject);
    } catch (e) { reject(e); }
  });
}

/** Fetches a .sfz and inlines its #include directives. */
async function fetchSfzText(url, depth = 0) {
  const res = await fetch(url, { mode: 'cors' });
  if (!res.ok) throw new Error(`HTTP ${res.status} on ${url}`);
  let text = await res.text();
  if (depth >= MAX_INCLUDE_DEPTH || !/#include/i.test(text)) return text;

  const includes = [...text.matchAll(/#include\s+"([^"]+)"/gi)];
  for (const inc of includes) {
    const incUrl = new URL(inc[1].replace(/\\/g, '/'), url).href;
    let body = '';
    try {
      body = await fetchSfzText(incUrl, depth + 1);
    } catch { /* a missing include must not sink the whole instrument */ }
    text = text.replace(inc[0], body);
  }
  return text;
}

/**
 * An SFZ instrument: the parsed regions plus the buffers fetched so far.
 * Buffers are keyed by absolute URL, so a file shared by several regions is
 * only fetched once.
 */
export class SfzInstrument {
  constructor(ctx, url) {
    this.ctx = ctx;
    // Built-in banks are referenced relatively ("sfz/808/808.sfz"), but a
    // relative string cannot act as the base when resolving sample paths.
    // Make it absolute once, here.
    this.url = new URL(url, document.baseURI || location.href).href;
    this.regions = [];
    this.control = {};
    this.buffers = new Map();    // url -> { buffer, sampleRate, loopStart, loopEnd }
    this.pending = new Map();    // url -> Promise (de-duplicates in-flight fetches)
    this.error = null;
    this.progress = 0;
  }

  /** Parses the .sfz and resolves every region's sample URL. */
  async parse() {
    const text = await fetchSfzText(this.url);
    const { control, regions } = parseSfz(text);
    this.control = control;
    this.regions = regions
      .filter(r => r.sample)                       // regions without a sample are noise
      .map(r => ({ ...r, url: samplePath(r, control, this.url) }));
    if (!this.regions.length) throw new Error('No playable region in this SFZ');
    return this;
  }

  /** Distinct sample URLs, ordered so the middle of the keyboard loads first. */
  sampleUrls() {
    const seen = new Map();
    for (const r of this.regions) {
      const centre = (r.lokey + r.hikey) / 2;
      const d = Math.abs(centre - 60);
      if (!seen.has(r.url) || d < seen.get(r.url)) seen.set(r.url, d);
    }
    return [...seen.entries()].sort((a, b) => a[1] - b[1]).map(e => e[0]);
  }

  /** Fetches + decodes one sample (idempotent, de-duplicated). */
  loadSample(url) {
    if (this.buffers.has(url)) return Promise.resolve(this.buffers.get(url));
    const inFlight = this.pending.get(url);
    if (inFlight) return inFlight;

    const job = (async () => {
      const res = await fetch(url, { mode: 'cors' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const raw = await res.arrayBuffer();
      // Read the header BEFORE decoding: decodeAudioData detaches the buffer
      // and drops both the true sample rate and the smpl loop points.
      // The whole file has to be scanned, not just its head: `smpl` normally
      // sits AFTER the audio data.
      const info = readWavInfo(raw);
      const buffer = await decodeAudio(this.ctx, raw);
      const entry = {
        buffer,
        // Fall back to the context rate when the format has no readable header
        // (flac/ogg): frame indices then need no scaling, which is the safest
        // assumption we can make.
        sampleRate: info.sampleRate || buffer.sampleRate,
        loopStart: info.loopStart,
        loopEnd: info.loopEnd,
      };
      this.buffers.set(url, entry);
      this.pending.delete(url);
      return entry;
    })().catch(err => {
      this.pending.delete(url);
      throw err;
    });

    this.pending.set(url, job);
    return job;
  }

  /**
   * Warms up the instrument: loads the first `count` samples (centre of the
   * keyboard first) so it can be played almost immediately, then keeps loading
   * the rest in the background.
   */
  async warmUp(count = 12, onProgress) {
    const urls = this.sampleUrls();
    const head = urls.slice(0, count);
    let done = 0;
    const step = () => {
      done++;
      this.progress = done / urls.length;
      if (onProgress) onProgress(this.progress);
    };
    // Load the first batch in parallel; a failing sample must not block the rest.
    await Promise.all(head.map(u => this.loadSample(u).then(step, step)));

    // The tail streams in quietly afterwards, in small chunks so the network
    // (and the decoder) stay responsive while playing.
    const tail = urls.slice(count);
    (async () => {
      for (let i = 0; i < tail.length; i += 6) {
        const chunk = tail.slice(i, i + 6);
        await Promise.all(chunk.map(u => this.loadSample(u).then(step, step)));
      }
    })();
    return this;
  }

  /** Rough memory footprint, for the LRU cache. */
  byteSize() {
    let bytes = 0;
    for (const { buffer } of this.buffers.values()) {
      bytes += buffer.length * buffer.numberOfChannels * 4;
    }
    return bytes;
  }
}

export { readWavInfo };
