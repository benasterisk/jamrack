// Loads midi-js format sound banks
// ({instrument}-mp3.js files holding { "A0": "data:audio/mp3;base64,..." })
// Progressive decoding: notes closest to the middle of the keyboard first, so
// the instrument becomes playable as soon as a handful of notes are decoded.

import { nameToMidi, base64ToArrayBuffer } from '../util.js';
import { GM } from '../data/gm.js';

const cache = new Map();      // "url|instrument" -> { buffers, ready, done, error, progress }
const namesCache = new Map(); // bank url -> list of instrument ids
const MAX_CACHED = 12;

// Keys shielded from eviction (instruments currently assigned to a module)
let protectedKeys = () => new Set();
export function setProtectedKeysProvider(fn) { protectedKeys = fn; }

export function instrumentKey(bankUrl, instrument) {
  return `${bankUrl}|${instrument}`;
}

// Instruments available in a bank (names.json, or the full GM list as fallback)
export async function fetchBankInstruments(bankUrl) {
  if (namesCache.has(bankUrl)) return namesCache.get(bankUrl);
  try {
    const res = await fetch(new URL('names.json', bankUrl), { mode: 'cors' });
    if (res.ok) {
      const json = await res.json();
      const list = Array.isArray(json) ? json : Object.values(json);
      namesCache.set(bankUrl, list);
      return list;
    }
    if (res.status === 404) {
      // this bank simply has no names.json: standard GM list, safe to cache
      const list = GM.map(g => g.id);
      namesCache.set(bankUrl, list);
      return list;
    }
  } catch { /* transient network failure: do not poison the cache */ }
  return GM.map(g => g.id);
}

// Checks that a custom bank responds (names.json OR a known instrument file)
export async function probeBank(bankUrl) {
  try {
    const res = await fetch(new URL('names.json', bankUrl), { mode: 'cors' });
    if (res.ok) return { ok: true, hasNames: true };
  } catch { /* try the other probe */ }
  try {
    const res = await fetch(new URL('acoustic_grand_piano-mp3.js', bankUrl), {
      method: 'HEAD', mode: 'cors',
    });
    if (res.ok) return { ok: true, hasNames: false };
  } catch { /* network or CORS failure */ }
  return { ok: false };
}

// decodeAudioData comes in promise or callback form depending on browser age
// (old webkit — the ones that only expose webkitAudioContext — are callback-only)
function decodeAudio(ctx, arrayBuffer) {
  return new Promise((resolve, reject) => {
    try {
      const p = ctx.decodeAudioData(arrayBuffer, resolve, reject);
      if (p && typeof p.then === 'function') p.then(resolve, reject);
    } catch (e) {
      reject(e);
    }
  });
}

function parseMidiJs(text) {
  const anchor = text.lastIndexOf('MIDI.Soundfont.');
  if (anchor < 0) throw new Error('Not a midi-js soundfont');
  const start = text.indexOf('{', anchor);
  const end = text.lastIndexOf('}');
  if (start < 0 || end <= start) throw new Error('No JSON object found');
  const json = text.slice(start, end + 1).replace(/,\s*}/g, '}');
  return JSON.parse(json);
}

// Loads an instrument. Returns the cache entry right away:
// entry.ready resolves once the instrument is playable (first notes decoded),
// entry.done once everything is decoded. onProgress(pct) is optional.
export function loadInstrument(ctx, bankUrl, instrument, onProgress) {
  const key = instrumentKey(bankUrl, instrument);
  let entry = cache.get(key);
  if (entry && !entry.error) {
    if (onProgress) entry.listeners.push(onProgress);
    return entry;
  }

  entry = {
    key,
    buffers: new Map(),   // midi -> AudioBuffer
    progress: 0,
    error: null,
    listeners: onProgress ? [onProgress] : [],
  };
  entry.ready = new Promise(res => { entry._readyRes = res; });
  entry.done = load(ctx, bankUrl, instrument, entry)
    .catch(err => {
      entry.error = err;
      cache.delete(key);
      entry._readyRes();
      throw err;
    });
  cache.set(key, entry);
  evictOld();
  return entry;
}

async function load(ctx, bankUrl, instrument, entry) {
  const url = new URL(`${instrument}-mp3.js`, bankUrl);
  const res = await fetch(url, { mode: 'cors' });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const notes = parseMidiJs(await res.text());

  const items = Object.entries(notes)
    .map(([name, uri]) => ({ midi: nameToMidi(name), uri }))
    .filter(it => it.midi !== null);
  if (!items.length) throw new Error('No notes in the file');

  // middle notes first, so playback can start as early as possible
  items.sort((a, b) => Math.abs(a.midi - 60) - Math.abs(b.midi - 60));

  let decoded = 0;
  let readyFired = false;
  const CHUNK = 8;
  for (let i = 0; i < items.length; i += CHUNK) {
    await Promise.all(items.slice(i, i + CHUNK).map(async it => {
      try {
        const buf = await decodeAudio(ctx, base64ToArrayBuffer(it.uri));
        if (buf) entry.buffers.set(it.midi, buf);
      } catch { /* unreadable note: the nearest-buffer fallback covers it */ }
      decoded++;
    }));
    entry.progress = decoded / items.length;
    entry.listeners.forEach(fn => fn(entry.progress));
    if (!readyFired && (decoded >= 12 || decoded >= items.length)) {
      readyFired = true;
      entry._readyRes();
    }
  }
  if (entry.buffers.size === 0) {
    // every decode failed: never report a silent "ready"
    throw new Error('No decodable notes');
  }
  if (!readyFired) entry._readyRes();
  entry.progress = 1;
  entry.lastUsed = Date.now();
  return entry.buffers;
}

export function touchInstrument(bankUrl, instrument) {
  const entry = cache.get(instrumentKey(bankUrl, instrument));
  if (entry) entry.lastUsed = Date.now();
}

function evictOld() {
  if (cache.size <= MAX_CACHED) return;
  const keep = protectedKeys();
  const entries = [...cache.values()]
    .filter(e => e.progress === 1 && !keep.has(e.key))
    .sort((a, b) => (a.lastUsed || 0) - (b.lastUsed || 0));
  while (cache.size > MAX_CACHED && entries.length) {
    const e = entries.shift();
    cache.delete(e.key);
  }
}
