// Measures (does not test) the latency of the analysis windows the POLY
// engine may use — docs/polyphonic-plan.md, section 8, "latence réelle des
// fenêtres asymétriques (à mesurer, pas déduite)".
//
//   node test/poly-latency.mjs            # markdown tables on stdout
//   node test/poly-latency.mjs --json     # same numbers as JSON
//
// Setup, mirroring the tracker's front-end: 48 kHz synthetic pluck (test/
// plucks.mjs, no decay so each partial has a true steady level), anti-alias
// low-pass and ÷2 decimation to 24 kHz (POLY's second ring: no 3 kHz
// low-pass), FFT 2048 every HOP = 64 samples (2.67 ms). Each frame's window
// ends on the newest sample; "time after the pluck" is the time of that
// newest sample minus the pluck time. Nothing else is added: hop
// granularity (0-2.7 ms), FFT cost and the browser buffers come on top.
//
// Windows:
//   hann L       symmetric Hann over the last L ms (centre of gravity L/2 back)
//   plateau L    newest-anchored: half-Hann raise of 5 ms at the OLD edge, then
//                a plateau of weight 1 right up to the newest sample
//   exp tau      exponential: weight exp(-age/tau), newest sample = 1
//
// Measured per window and per open string:
//   note-on   time after the pluck at which the energy of the first three
//             partials (±1 bin each) reaches 50 % and 90 % of its steady
//             level (RMS magnitude, i.e. -6 dB and -0.9 dB)
//   note-off  time after a palm mute (muteAt, 10 ms fade) at which that
//             energy has dropped by 20 dB below its pre-mute level

import { pluck, silence, concat, muteAt } from './plucks.mjs';

const SR = 48000, D = 2, SRD = SR / D, HOP = 64, N = 2048;
const STRINGS = [['E2', 40], ['A2', 45], ['D3', 50], ['G3', 55], ['B3', 59], ['E4', 64]];
const PARTIALS = 3;
const midiToHz = m => 440 * Math.pow(2, (m - 69) / 12);
const ms = sec => sec * 1000;

// ---- windows (length n samples at SRD, index n-1 = newest sample) ----------
const hann = n => Float32Array.from({ length: n }, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * (i + 0.5) / n));
const plateau = (n, raise) => Float32Array.from({ length: n }, (_, i) => i < raise ? 0.5 - 0.5 * Math.cos(Math.PI * (i + 0.5) / raise) : 1);
const expo = (n, tau) => Float32Array.from({ length: n }, (_, i) => Math.exp(-(n - 1 - i) / tau));
const samples = msec => Math.round(msec / 1000 * SRD);

const WINDOWS = [
  ['hann 21 ms', hann(samples(21))],
  ['hann 43 ms', hann(samples(43))],
  ['hann 85 ms', hann(samples(85))],
  ['plateau 21 ms (5 ms raise)', plateau(samples(21), samples(5))],
  ['plateau 43 ms (5 ms raise)', plateau(samples(43), samples(5))],
  ['plateau 85 ms (5 ms raise)', plateau(samples(85), samples(5))],
  ['exp τ 8 ms', expo(N, samples(8))],
  ['exp τ 10 ms', expo(N, samples(10))],
  ['exp τ 12 ms', expo(N, samples(12))],
];

// ---- front-end: anti-alias + ÷2 (the tracker's biquad, 10 kHz instead of 3) -
class Biquad {
  constructor(sr, fc, q) {
    const w0 = 2 * Math.PI * fc / sr, cos = Math.cos(w0), alpha = Math.sin(w0) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cos) / 2 / a0; this.b1 = (1 - cos) / a0; this.b2 = this.b0;
    this.a1 = -2 * cos / a0; this.a2 = (1 - alpha) / a0; this.z1 = 0; this.z2 = 0;
  }
  run(x) { const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
}
function decimate(sig) {
  const lp1 = new Biquad(SR, 10000, 0.7), lp2 = new Biquad(SR, 10000, 0.7);
  const out = new Float32Array(Math.floor(sig.length / D));
  for (let i = 0, j = 0; i < sig.length; i++) { const y = lp2.run(lp1.run(sig[i])); if (i % D === D - 1) out[j++] = y; }
  return out;
}

// ---- radix-2 FFT (in place, re/im) -----------------------------------------
const REV = new Uint16Array(N), COS = new Float64Array(N / 2), SIN = new Float64Array(N / 2);
for (let i = 0, bits = Math.log2(N); i < N; i++) { let r = 0; for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); REV[i] = r; }
for (let i = 0; i < N / 2; i++) { COS[i] = Math.cos(2 * Math.PI * i / N); SIN[i] = -Math.sin(2 * Math.PI * i / N); }
function fft(re, im) {
  for (let i = 0; i < N; i++) { const j = REV[i]; if (j > i) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let size = 2; size <= N; size <<= 1) {
    const half = size >> 1, step = N / size;
    for (let s = 0; s < N; s += size) for (let k = 0; k < half; k++) {
      const wr = COS[k * step], wi = SIN[k * step], a = s + k, b = a + half;
      const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
      re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
    }
  }
}

/** Energy of the first PARTIALS partials (±1 bin) per hop: [{ t, e }] (t = newest sample, seconds). */
function partialEnergy(sigD, win, f0) {
  const re = new Float64Array(N), im = new Float64Array(N);
  const bins = [];
  for (let k = 1; k <= PARTIALS; k++) { const b = Math.round(k * f0 * N / SRD); bins.push(b - 1, b, b + 1); }
  const n = win.length, out = [];
  for (let end = n; end <= sigD.length; end += HOP) {
    re.fill(0); im.fill(0);
    for (let i = 0; i < n; i++) re[N - n + i] = sigD[end - n + i] * win[i];
    fft(re, im);
    let e = 0;
    for (const b of bins) e += re[b] * re[b] + im[b] * im[b];
    out.push({ t: end / SRD, e });
  }
  return out;
}

/**
 * First frame time (relative to `ref`) at which pred(e) holds and keeps
 * holding for `persist` frames (a crossing "for good", not a flicker).
 */
function crossing(frames, ref, pred, persist = 4) {
  for (let i = 0; i < frames.length; i++) {
    if (frames[i].t < ref) continue;
    if (frames.slice(i, i + persist).every(f => pred(f.e))) return ms(frames[i].t - ref);
  }
  return NaN;
}
const mean = a => a.reduce((x, y) => x + y, 0) / a.length;

const PLUCK_AT = 0.2, MUTE_AT = 0.6, TOTAL = 1.0;   // hop-aligned: 0.2 s = 75 hops of 64 at 24 kHz
const results = [];
for (const [label, win] of WINDOWS) {
  for (const [str, midi] of STRINGS) {
    const f0 = midiToHz(midi);
    // decay 0: every partial holds its level, so "steady" is well defined
    const sig = concat(silence(SR, PLUCK_AT), pluck(SR, { midi, dur: TOTAL - PLUCK_AT, decay: 0 }));
    const muted = muteAt(sig, SR, MUTE_AT);
    const on = partialEnergy(decimate(sig), win, f0);
    const off = partialEnergy(decimate(muted), win, f0);
    const steadyFrames = on.filter(f => f.t > PLUCK_AT + 0.15 && f.t < MUTE_AT).map(f => f.e);
    const steady = mean(steadyFrames);
    // A window too short to separate the partials of a low string lets them
    // beat inside the ±1 bin bands: the "steady" level then ripples by a few
    // dB and a 90 % crossing "for good" never happens. Flag it and report the
    // first crossing instead.
    const rippleDb = 10 * Math.log10(Math.max(...steadyFrames) / Math.min(...steadyFrames));
    const unresolved = rippleDb > 1.5;
    const persist = unresolved ? 1 : 4;
    const preMute = mean(off.filter(f => f.t > MUTE_AT - 0.05 && f.t <= MUTE_AT).map(f => f.e));
    results.push({
      window: label, string: str, midi, rippleDb, unresolved,
      t50: crossing(on, PLUCK_AT, e => e >= 0.25 * steady, persist),   // 50 % of the RMS magnitude = 25 % of the energy
      t90: crossing(on, PLUCK_AT, e => e >= 0.81 * steady, persist),
      tOff20: crossing(off, MUTE_AT, e => e <= 0.01 * preMute),
    });
  }
}

if (process.argv.includes('--json')) {
  console.log(JSON.stringify(results, null, 1));
} else {
  const fmt = v => Number.isNaN(v) ? '—' : v.toFixed(1);
  const head = `| window | ${STRINGS.map(s => s[0]).join(' | ')} |\n|---|${STRINGS.map(() => '---:').join('|')}|`;
  const rowsOf = cell => WINDOWS.map(([label]) =>
    `| ${label} | ${STRINGS.map(([str]) => cell(results.find(r => r.window === label && r.string === str))).join(' | ')} |`).join('\n');
  console.log(`Analysis: ${SR / 1000} kHz → ÷${D} → ${SRD / 1000} kHz, FFT ${N}, hop ${HOP} (${ms(HOP / SRD).toFixed(2)} ms); window ends on the newest sample, pluck and mute are hop-aligned.`);
  console.log(`Energy = first ${PARTIALS} partials ±1 bin. Times in ms after the pluck / after the mute, hop granularity (0-${ms(HOP / SRD).toFixed(1)} ms) not included.\n`);
  console.log('**Note-on: partials at 50 % / 90 % of their steady level (ms after the pluck)**\n');
  console.log(head); console.log(rowsOf(r => `${fmt(r.t50)} / ${fmt(r.t90)}${r.unresolved ? '\u2009*' : ''}`));
  const flagged = results.filter(r => r.unresolved);
  if (flagged.length) console.log(`\n\\* window does not separate this string's partials (steady level ripples by ${Math.min(...flagged.map(r => r.rippleDb)).toFixed(1)}-${Math.max(...flagged.map(r => r.rippleDb)).toFixed(1)} dB): first crossing shown, not a crossing for good.`);
  console.log('\n**Note-off: partial energy −20 dB (ms after a palm mute with a 10 ms fade)**\n');
  console.log(head); console.log(rowsOf(r => fmt(r.tOff20)));
}
