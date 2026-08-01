// SFZ engine entry point: caching, and the player object an Instrument uses.
//
// Mirrors the shape of soundfont.js (an entry with `ready` / `done` / progress
// listeners, plus an LRU cache) so the rack can treat SFZ like any other bank.
// The cache is budgeted in BYTES rather than in instrument count: an SFZ bank
// is orders of magnitude heavier than a midi-js one.

import { SfzInstrument } from './loader.js';
import { matchRegions, SeqState, isKeyswitch } from './regions.js';
import { SfzVoice } from './voice.js';

const cache = new Map();                 // url -> entry
const MAX_BYTES = 320 * 1024 * 1024;     // ~320 MB of decoded audio

let protectedKeys = () => new Set();
export function setProtectedKeysProvider(fn) { protectedKeys = fn; }

function evict() {
  let total = 0;
  for (const e of cache.values()) total += e.instrument ? e.instrument.byteSize() : 0;
  if (total <= MAX_BYTES) return;
  const keep = new Set([...protectedKeys()].map(absolute));
  const candidates = [...cache.entries()]
    .filter(([url]) => !keep.has(url))
    .sort((a, b) => a[1].touched - b[1].touched);
  for (const [url, e] of candidates) {
    if (total <= MAX_BYTES) break;
    total -= e.instrument ? e.instrument.byteSize() : 0;
    cache.delete(url);
  }
}

/** Resolves relative bank paths so the cache key is stable. */
function absolute(url) {
  try { return new URL(url, document.baseURI || location.href).href; }
  catch { return url; }
}

/** Loads (or returns the cached) SFZ instrument at `url`. */
export function loadSfz(ctx, rawUrl, onProgress) {
  const url = absolute(rawUrl);
  let entry = cache.get(url);
  if (entry && !entry.error) {
    entry.touched = Date.now();
    if (onProgress) entry.listeners.push(onProgress);
    return entry;
  }

  entry = {
    url,
    instrument: null,
    error: null,
    progress: 0,
    touched: Date.now(),
    listeners: onProgress ? [onProgress] : [],
  };
  entry.ready = new Promise(res => { entry._readyRes = res; });

  entry.done = (async () => {
    const inst = new SfzInstrument(ctx, url);
    await inst.parse();
    entry.instrument = inst;
    // Playable as soon as a handful of samples are in; the rest streams in.
    await inst.warmUp(12, pct => {
      entry.progress = pct;
      entry.listeners.forEach(fn => { try { fn(pct); } catch { /* ignore */ } });
    });
    entry._readyRes();
    return inst;
  })().catch(err => {
    entry.error = err;
    cache.delete(url);
    entry._readyRes();
    throw err;
  });

  cache.set(url, entry);
  evict();
  return entry;
}

export function touchSfz(url) {
  const e = cache.get(absolute(url));
  if (e) e.touched = Date.now();
}

/**
 * Plays an SFZ instrument. One per rack module.
 *
 * Holds the per-instrument state that outlives a single note: round-robin
 * counters, keyswitch memory, and the voices currently sounding (so choke
 * groups can silence them).
 */
export class SfzPlayer {
  constructor(ctx, dest) {
    this.ctx = ctx;
    this.dest = dest;
    this.instrument = null;
    this.seq = new SeqState();
    this.active = new Map();     // note -> SfzVoice[]
    this.releasing = new Set();
    this.byGroup = new Map();    // choke group -> Set<SfzVoice>
  }

  setInstrument(instrument) {
    this.allNotesOff(true);
    this.instrument = instrument;
    this.seq.reset();
  }

  get ready() {
    return !!(this.instrument && this.instrument.regions.length);
  }

  /** True when this note selects an articulation instead of playing. */
  isKeyswitchNote(note) {
    return this.ready && isKeyswitch(this.instrument.regions, note);
  }

  /** @returns true if the note produced sound (false when still loading). */
  noteOn(note, vel, bendCents = 0) {
    if (!this.ready) return false;
    const regions = this.instrument.regions;

    // Keyswitch keys select an articulation instead of making a sound.
    if (isKeyswitch(regions, note)) {
      this.seq.lastKeyswitch = note;
      return true;
    }

    const matched = matchRegions(regions, note, vel, this.seq);
    if (!matched.length) return false;

    const voices = [];
    for (const region of matched) {
      const entry = this.instrument.buffers.get(region.url);
      if (!entry) {
        // Not decoded yet: fetch it so the next hit works, and skip this layer.
        this.instrument.loadSample(region.url).catch(() => {});
        continue;
      }
      // Choke groups: a closed hi-hat silences the open one.
      if (region.group) this._choke(region.group);

      const voice = new SfzVoice(this.ctx, this.dest, region, entry, note, vel, bendCents);
      voice.onended = v => this._cleanup(v);
      voices.push(voice);
      if (region.offBy) {
        if (!this.byGroup.has(region.offBy)) this.byGroup.set(region.offBy, new Set());
        this.byGroup.get(region.offBy).add(voice);
      }
    }
    if (!voices.length) return false;

    const previous = this.active.get(note);
    if (previous) previous.forEach(v => this._release(v));
    this.active.set(note, voices);
    return true;
  }

  noteOff(note) {
    const voices = this.active.get(note);
    if (voices) {
      this.active.delete(note);
      voices.forEach(v => this._release(v));
    }
    // Release-triggered regions (key noise, pedal samples) fire here.
    if (this.ready) {
      const rel = matchRegions(this.instrument.regions, note, 64, this.seq, { trigger: 'release' });
      for (const region of rel) {
        const entry = this.instrument.buffers.get(region.url);
        if (!entry) continue;
        const v = new SfzVoice(this.ctx, this.dest, region, entry, note, 64);
        v.onended = x => this._cleanup(x);
        this.releasing.add(v);
      }
    }
  }

  setBend(cents) {
    this.active.forEach(vs => vs.forEach(v => v.setBend(cents)));
  }

  allNotesOff(immediate = false) {
    this.active.forEach(vs => vs.forEach(v => immediate ? v.choke() : v.release(0.06)));
    this.active.clear();
    if (immediate) {
      this.releasing.forEach(v => v.choke());
      this.releasing.clear();
      this.byGroup.clear();
    }
  }

  activeCount() {
    let n = 0;
    this.active.forEach(vs => { n += vs.length; });
    return n + this.releasing.size;
  }

  _choke(group) {
    const set = this.byGroup.get(group);
    if (!set) return;
    set.forEach(v => v.choke(v.region.offMode !== 'normal'));
    set.clear();
  }

  _release(voice) {
    voice.release();
    this.releasing.add(voice);
  }

  _cleanup(voice) {
    this.releasing.delete(voice);
    voice.disconnect();
    for (const [note, vs] of this.active) {
      const i = vs.indexOf(voice);
      if (i >= 0) {
        vs.splice(i, 1);
        if (!vs.length) this.active.delete(note);
        break;
      }
    }
    this.byGroup.forEach(set => set.delete(voice));
  }
}

export { SfzInstrument };
