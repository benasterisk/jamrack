// Synthetic plucked-string signals for the tracker tests: additive harmonics
// with per-harmonic decay, slight inharmonicity, a noisy pick transient, and
// optional bends (continuous) or hammer-ons (instant pitch steps).

const midiToHz = m => 440 * Math.pow(2, (m - 69) / 12);

/** Deterministic noise so a failing test fails the same way every run. */
export function makeRng(seed = 1) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296 * 2 - 1;
  };
}

/**
 * One plucked note.
 *   f0           fundamental in Hz (or `midi` + `cents`)
 *   dur          seconds
 *   amp          peak-ish amplitude (0..1)
 *   fundamental  gain of the 1st harmonic (0.15 = "missing fundamental" pickup)
 *   decay        1 = electric clean, 3 = palm-muted
 *   bend         { at, dur, semis }   linear glide starting at `at` seconds
 *   hammer       { at, semis }        instant pitch step at `at` seconds
 *   cut          seconds: hard mute (signal ends) at that time
 */
export function pluck(sr, o) {
  const f0 = o.f0 ?? midiToHz(o.midi) * Math.pow(2, (o.cents || 0) / 1200);
  const n = Math.round(sr * o.dur);
  const out = new Float32Array(n);
  const amp = o.amp ?? 0.3;
  const fund = o.fundamental ?? 1;
  const decay = o.decay ?? 1;
  const K = o.harmonics ?? 14;
  const rng = makeRng(o.seed ?? 7);
  const B = o.stiffness ?? 2e-4;           // string stiffness (inharmonicity)
  const phase = new Float64Array(K + 1);
  const cutN = o.cut ? Math.round(o.cut * sr) : n;
  for (let i = 0; i < Math.min(n, cutN); i++) {
    const t = i / sr;
    let f = f0;
    if (o.bend && t >= o.bend.at) {
      const u = Math.min(1, (t - o.bend.at) / o.bend.dur);
      f = f0 * Math.pow(2, o.bend.semis * u / 12);
    }
    if (o.hammer && t >= o.hammer.at) f = f0 * Math.pow(2, o.hammer.semis / 12);
    // hammer-on: a small amplitude bump, no pick noise
    const hammerBoost = (o.hammer && t >= o.hammer.at && t < o.hammer.at + 0.03) ? 1.3 : 1;
    const attack = Math.min(1, t / 0.0015);
    let s = 0;
    for (let k = 1; k <= K; k++) {
      const fk = k * f * Math.sqrt(1 + B * k * k);
      phase[k] += 2 * Math.PI * fk / sr;
      const a = (k === 1 ? fund : 1 / k) * Math.exp(-t * decay * (0.3 + 1.2 * k));
      s += a * Math.sin(phase[k]);
    }
    let pick = Math.exp(-t / 0.0025) * rng() * 0.8;
    if (o.hammer && t >= o.hammer.at) pick += Math.exp(-(t - o.hammer.at) / 0.0015) * rng() * 0.25;
    out[i] = amp * attack * hammerBoost * (s * 0.55 + pick) + rng() * 1e-4;
  }
  return out;
}

export function silence(sr, dur, noiseAmp = 1e-4, seed = 3) {
  const rng = makeRng(seed);
  const out = new Float32Array(Math.round(sr * dur));
  for (let i = 0; i < out.length; i++) out[i] = rng() * noiseAmp;
  return out;
}

export function concat(...parts) {
  const n = parts.reduce((a, p) => a + p.length, 0);
  const out = new Float32Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

/**
 * Notes played one after another on the SAME string: the pick stops the
 * string ~5 ms before each new attack, so the previous note is cut there
 * (overlapping notes on different strings are polyphony, which a monophonic
 * tracker does not claim to handle).
 *   seq(sr, total, [{ at, midi, ...pluck options }])
 */
export function seq(sr, totalSec, notes) {
  const out = new Float32Array(Math.round(sr * totalSec));
  notes.forEach((n, i) => {
    const next = notes[i + 1];
    const cut = next ? Math.max(0.01, next.at - n.at - 0.005) : undefined;
    const p = pluck(sr, { ...n, dur: n.dur ?? (totalSec - n.at), cut });
    const off = Math.round(n.at * sr);
    for (let j = 0; j < p.length && off + j < out.length; j++) out[off + j] += p[j];
  });
  return out;
}

/** Sums `b` into `a` at offset `atSec` (overlapping notes). */
export function mix(sr, a, b, atSec) {
  const off = Math.round(atSec * sr);
  const out = new Float32Array(Math.max(a.length, off + b.length));
  out.set(a);
  for (let i = 0; i < b.length; i++) out[off + i] += b[i];
  return out;
}

/** Runs the tracker over the signal in blocks; events get a time in ms. */
export function run(tracker, signal, block = 128) {
  const sr = tracker.sr;
  const events = [];
  for (let o = 0; o < signal.length; o += block) {
    const chunk = signal.subarray(o, Math.min(signal.length, o + block));
    const ev = tracker.process(chunk);
    for (const e of ev) events.push({ ...e, ms: (o + chunk.length) / sr * 1000 });
  }
  return events;
}

export const notes = ev => ev.filter(e => e.t === 'on' || e.t === 'off');
export const ons = ev => ev.filter(e => e.t === 'on');
export const offs = ev => ev.filter(e => e.t === 'off');
export const bends = ev => ev.filter(e => e.t === 'bend');
export const meters = ev => ev.filter(e => e.t === 'meter');
