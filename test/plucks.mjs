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

// ---------------------------------------------------------------------------
// Polyphony generators (POLY bench, docs/polyphonic-plan.md jalon 1). Every
// function below returns a plain Float32Array like pluck(); the exact labels
// (which note starts when) come from chordLabels() / repick's `at` list.

/** Realistic strum spreads (seconds between two consecutive strings). */
export const STRUM_SPREAD = { fast: 0.005, medium: 0.015, slow: 0.035 };

/**
 * Strum order of a chord: low string first (downstroke, the default) or high
 * string first (`up: true`). Notes are given as MIDI numbers, one per string.
 */
export function strumOrder(notes, up = false) {
  const o = [...notes].sort((a, b) => a - b);
  return up ? o.reverse() : o;
}

/**
 * Exact labels of a chord(): [{ midi, at }] in strum order, `at` in seconds
 * from the start of the returned signal.
 */
export function chordLabels(o) {
  const at = o.at ?? 0, spread = o.spread ?? 0;
  return strumOrder(o.notes, o.up).map((midi, i) => ({ midi, at: at + i * spread }));
}

/**
 * A chord: one pluck per note, each on its own string (so the notes overlap
 * and ring together), onsets staggered by `spread` seconds in strum order.
 *   chord(sr, { notes: [midi...], at, dur, spread, up, total, ...pluck options })
 *     at      seconds before the first string is hit (default 0)
 *     dur     how long each string rings (default 1 s)
 *     spread  seconds between two consecutive strings (default 0: all at once)
 *     up      true for an upstroke (high string first)
 *     total   length of the returned signal in seconds (default at + dur + spread·(n-1))
 * Any other option (amp, decay, fundamental, stiffness, cut, ...) is passed
 * to every pluck; `seed` is offset per string so the pick noise differs.
 */
export function chord(sr, o) {
  const { notes: _n, at: _a, dur: _d, spread: _s, up: _u, total: _t, ...rest } = o;
  const dur = o.dur ?? 1;
  const labels = chordLabels(o);
  const last = labels[labels.length - 1].at;
  const totalSec = o.total ?? (last + dur);
  const out = new Float32Array(Math.round(sr * totalSec));
  labels.forEach(({ midi, at }, i) => {
    const p = pluck(sr, { ...rest, midi, dur, seed: (o.seed ?? 7) + 31 * i });
    const off = Math.round(at * sr);
    for (let j = 0; j < p.length && off + j < out.length; j++) out[off + j] += p[j];
  });
  return out;
}

/**
 * chord() with a named strum speed instead of a spread in seconds:
 *   strum(sr, { notes, at, dur, speed: 'fast' | 'medium' | 'slow', up })
 * fast = 5 ms per string (a flat-pick hit), medium = 15 ms (GuitarSet median
 * strum spread is 13-16 ms), slow = 35 ms (GuitarSet 90th centile 34-37 ms).
 */
export function strum(sr, o) {
  const spread = STRUM_SPREAD[o.speed ?? 'medium'];
  if (spread === undefined) throw new Error(`strum: unknown speed "${o.speed}"`);
  const { speed: _s, ...rest } = o;
  return chord(sr, { ...rest, spread });
}

/**
 * The same note re-picked on ONE string: each pick stops the string ~5 ms
 * before the next attack (exactly like seq(), which this wraps).
 *   repick(sr, { midi, at: [t0, t1, ...], dur, total, ...pluck options })
 *     dur    how long the last pick rings (default 0.5 s); earlier picks are
 *            cut by the next one
 *     total  length of the signal (default last at + dur)
 */
export function repick(sr, o) {
  const { midi, at, dur = 0.5, total, ...rest } = o;
  const times = [...at].sort((a, b) => a - b);
  const totalSec = total ?? (times[times.length - 1] + dur);
  return seq(sr, totalSec, times.map((t, i) => ({ ...rest, at: t, midi, dur, seed: (o.seed ?? 7) + 17 * i })));
}

/**
 * Palm mute: damps the whole signal from `at` seconds with a short
 * raised-cosine fade of `release` seconds, then silence (plus the same
 * -80 dBFS floor pluck() carries, so a tracker sees "a string that stopped",
 * not a digital zero). Returns a copy; the input is left intact.
 */
export function muteAt(signal, sr, at, release = 0.01, noiseAmp = 1e-4, seed = 5) {
  const out = new Float32Array(signal.length);
  const start = Math.round(at * sr), fade = Math.max(1, Math.round(release * sr));
  const rng = makeRng(seed);
  for (let i = 0; i < signal.length; i++) {
    if (i < start) { out[i] = signal[i]; continue; }
    const u = (i - start) / fade;
    const g = u >= 1 ? 0 : 0.5 * (1 + Math.cos(Math.PI * u));
    out[i] = signal[i] * g + rng() * noiseAmp;
  }
  return out;
}
