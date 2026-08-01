// Note and math helpers

export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

export function isBlack(midi) {
  return [1, 3, 6, 8, 10].includes(midi % 12);
}

// "Bb3", "C#4", "A0" -> midi number (the note naming used by midi-js banks)
const NAME_RE = /^([A-Ga-g])(#{1,2}|b{1,2})?(-?\d+)$/;
const BASE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

export function nameToMidi(name) {
  const m = NAME_RE.exec(String(name).trim());
  if (!m) return null;
  let semis = BASE[m[1].toUpperCase()];
  if (m[2]) semis += (m[2][0] === '#' ? 1 : -1) * m[2].length;
  return semis + (parseInt(m[3], 10) + 1) * 12;
}

export function midiToFreq(midi) {
  return 440 * Math.pow(2, (midi - 69) / 12);
}

export function base64ToArrayBuffer(dataUri) {
  const b64 = dataUri.slice(dataUri.indexOf(',') + 1);
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes.buffer;
}

export function debounce(fn, ms) {
  let t = 0;
  return (...args) => {
    clearTimeout(t);
    t = setTimeout(() => fn(...args), ms);
  };
}

export function formatTime(sec) {
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${String(s).padStart(2, '0')}`;
}

export function uid() {
  return Math.random().toString(36).slice(2, 9);
}

// HTML-escape anything external that goes through innerHTML
export function esc(s) {
  return String(s).replace(/[&<>"']/g, c => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
  ));
}

export function el(html) {
  const t = document.createElement('template');
  t.innerHTML = html.trim();
  return t.content.firstElementChild;
}
