// Calibration of the POLY bank to one guitar: measures, from a single pluck
// of each string, the inharmonicity B (string stiffness) and the attack
// partial profile — the two things profile.js holds for the generic bank.
// Pure DSP over typed arrays (main thread or Node tests); the dialog that
// drives it lives in js/ui/rack.js, the storage in profiles.js.
//
// The measurement mirrors test/poly/bank/extract.py so that a calibrated
// profile is of the same nature as the shipped one:
//   attack spectrum   mean of the medium-window magnitude spectra whose
//                     newest sample lies 10..40 ms after the onset (unit L2)
//   inharmonicity     65536-point Hann FFT of 60..560 ms after the onset;
//                     the partials n = 1..30 under 10 kHz are peak-picked
//                     (parabolic interpolation on the log magnitude, >= 15 dB
//                     over the median of the band) in three rounds of
//                     widening n with the B fitted so far; least squares on
//                     (f_n / n)^2 = f0^2 + f0^2 B n^2, outliers > 1 % dropped
//   partial profile   max within ±1 bin of each fitted partial in the attack
//                     spectrum, n = 1..40, unit L2
// A pluck is accepted when it sits within ±100 cents of the string asked
// for, is louder than -50 dBFS over the attack and shows at least 5 partials.

import { Resampler } from './resample.js';
import { RealFFT } from './fft.js';
import { SR, HOP, NFFT, NBINS, BIN_HZ, OPEN_MIDI, hannWindow, midiToHz } from './bank.js';
import { B_LAW, PROF_ATT } from './profile.js';

const NFIT = 65536;
const N_MAX = 40, N_FIT = 30;
const F_MAX = 10000;
const PEAK_DB = 15;
const MIN_LEVEL_DB = -50;
const ATT_MS = [10, 40];
const FIT_MS = [60, 560];
const MAX_CENTS = 100;
const ONSET_RISE_DB = 12;
const ONSET_MIN_DB = -45;
const CAPTURE_S = 0.62;        // after the onset: covers the fit window
const TIMEOUT_S = 20;

const dB = x => 20 * Math.log10(Math.max(x, 1e-9));
const hzToCents = (f, ref) => 1200 * Math.log2(f / ref);

let fitFFT = null, hopFFT = null, winM = null;
function tools() {
  if (!fitFFT) { fitFFT = new RealFFT(NFIT); hopFFT = new RealFFT(NFFT); winM = hannWindow(1024); }
}

/** Interpolated peak of mag[lo..hi) or null (extract.py pick_peak). */
export function pickPeak(mag, lo, hi) {
  lo = Math.max(lo, 1); hi = Math.min(hi, mag.length - 2);
  if (hi - lo < 3) return null;
  let i = lo;
  for (let b = lo + 1; b < hi; b++) if (mag[b] > mag[i]) i = b;
  const w = hi - lo;
  const band = Array.from(mag.subarray(Math.max(lo - w, 1), Math.min(hi + w, mag.length - 1))).sort((a, b) => a - b);
  const median = band[band.length >> 1];
  if (mag[i] < Math.pow(10, PEAK_DB / 20) * median) return null;
  const y0 = Math.log(mag[i - 1] + 1e-12), y1 = Math.log(mag[i] + 1e-12), y2 = Math.log(mag[i + 1] + 1e-12);
  const den = y0 - 2 * y1 + y2;
  const delta = den < 0 ? Math.max(-1, Math.min(1, 0.5 * (y0 - y2) / den)) : 0;
  return { bin: i + delta, mag: mag[i] };
}

/** High-resolution magnitude spectrum (Hann, 65536-point FFT) of a 24 kHz segment. */
export function highResSpectrum(seg) {
  tools();
  const N = seg.length;
  const x = new Float64Array(N);
  for (let i = 0; i < N; i++) x[i] = seg[i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)));
  return fitFFT.magnitude(x, new Float64Array(NFIT / 2 + 1));
}

/**
 * B and f0 of a decaying note (extract.py fit_inharmonicity) from its 24 kHz
 * segment, or from a highResSpectrum() of it. Returns { B, f0, n } or null.
 */
export function fitInharmonicity(segOrMag, f0Guess) {
  const mag = segOrMag.length === NFIT / 2 + 1 ? segOrMag : highResSpectrum(segOrMag);
  const hz = SR / NFIT;
  let f0 = f0Guess, B = 0;
  const found = new Map();
  for (const [nHi, tol] of [[6, 0.015], [14, 0.012], [N_FIT, 0.010]]) {
    for (let n = 1; n <= nHi; n++) {
      if (found.has(n)) continue;
      const fn = n * f0 * Math.sqrt(1 + B * n * n);
      if (fn > F_MAX) break;
      const r = pickPeak(mag, Math.floor(fn * (1 - tol) / hz), Math.floor(fn * (1 + tol) / hz) + 1);
      if (r) found.set(n, r.bin * hz);
    }
    if (found.size < 3) return null;
    // least squares  y = a + b n^2,  y = (f_n / n)^2
    let s0 = 0, s1 = 0, s2 = 0, sy = 0, sxy = 0;
    for (const [n, f] of found) {
      const u = n * n, y = (f / n) ** 2;
      s0++; s1 += u; s2 += u * u; sy += y; sxy += u * y;
    }
    const det = s0 * s2 - s1 * s1;
    if (Math.abs(det) < 1e-12) return null;
    const a = (s2 * sy - s1 * sxy) / det, b = (s0 * sxy - s1 * sy) / det;
    if (a <= 0) return null;
    f0 = Math.sqrt(a);
    B = Math.max(b / a, 0);
    for (const [n, f] of [...found]) {
      const pred = n * f0 * Math.sqrt(1 + B * n * n);
      if (Math.abs(f / pred - 1) > 0.01) found.delete(n);
    }
  }
  if (found.size < 5) return null;
  return { B, f0, n: found.size };
}

/** Frequency of the strongest peak of a high-resolution spectrum in [lo, hi] Hz, sub-octave checked. */
export function strongestPitch(mag, lo = 70, hi = 1400) {
  const hz = SR / NFIT;
  let best = Math.round(lo / hz);
  for (let b = best; b <= Math.round(hi / hz); b++) if (mag[b] > mag[best]) best = b;
  const half = Math.round(best / 2);
  if (half * hz >= lo) {
    let hb = half;
    for (let b = half - 3; b <= half + 3; b++) if (b > 0 && mag[b] > mag[hb]) hb = b;
    if (mag[hb] > 0.3 * mag[best]) best = hb;
  }
  const r = pickPeak(mag, best - 2, best + 3);
  return (r ? r.bin : best) * hz;
}

/** Unit-L2 partial amplitudes 1..N_MAX read in a medium-window spectrum. */
export function partialAmps(spec, f0, B) {
  const a = new Float64Array(N_MAX);
  let ss = 0;
  for (let n = 1; n <= N_MAX; n++) {
    const fn = n * f0 * Math.sqrt(1 + B * n * n);
    if (fn > F_MAX) break;
    const b = Math.round(fn / BIN_HZ);
    let m = 0;
    for (let k = Math.max(b - 1, 0); k <= Math.min(b + 1, NBINS - 1); k++) if (spec[k] > m) m = spec[k];
    a[n - 1] = m;
    ss += m * m;
  }
  if (ss > 0) { const inv = 1 / Math.sqrt(ss); for (let i = 0; i < N_MAX; i++) a[i] *= inv; }
  return a;
}

/**
 * Analyses one pluck in a 24 kHz buffer. `onset` = sample index of the
 * attack, `expectMidi` = the note asked for. Returns
 * { ok: true, B, f0, cents, n, amps, levelDb } or { ok: false, error, ... }
 * with error 'tooQuiet' | 'noPartials' | 'wrongNote'.
 */
export function analysePluck(x, onset, expectMidi) {
  tools();
  const ms = v => Math.round(v * SR / 1000);
  // attack spectrum (hop-aligned frames whose newest sample is 10..40 ms after the onset)
  const att = new Float64Array(NBINS), frame = new Float64Array(1024), spec = new Float64Array(NBINS);
  let nAtt = 0, sumSq = 0, nSq = 0;
  const k0 = Math.ceil((onset + ms(ATT_MS[0])) / HOP), k1 = Math.floor((onset + ms(ATT_MS[1])) / HOP);
  for (let k = k0; k <= k1; k++) {
    const end = k * HOP;
    if (end > x.length) break;
    for (let i = 0; i < 1024; i++) { const j = end - 1024 + i; frame[i] = (j >= 0 ? x[j] : 0) * winM[i]; }
    hopFFT.magnitude(frame, spec);
    for (let b = 0; b < NBINS; b++) att[b] += spec[b];
    nAtt++;
  }
  for (let i = onset + ms(ATT_MS[0]); i < Math.min(x.length, onset + ms(ATT_MS[1])); i++) { sumSq += x[i] * x[i]; nSq++; }
  const levelDb = dB(Math.sqrt(sumSq / Math.max(1, nSq)));
  if (!nAtt || levelDb < MIN_LEVEL_DB) return { ok: false, error: 'tooQuiet', levelDb };
  let ss = 0;
  for (let b = 0; b < NBINS; b++) ss += att[b] * att[b];
  for (let b = 0; b < NBINS; b++) att[b] /= Math.sqrt(ss) || 1;
  // inharmonicity from the decaying part
  const a0 = onset + ms(FIT_MS[0]), a1 = Math.min(x.length, onset + ms(FIT_MS[1]));
  if (a1 - a0 < ms(200)) return { ok: false, error: 'tooQuiet', levelDb };
  const mag = highResSpectrum(x.subarray(a0, a1));
  const fit = fitInharmonicity(mag, midiToHz(expectMidi));
  if (!fit) {
    // not the note asked for? try again from the strongest low peak, to
    // name what was heard
    const other = fitInharmonicity(mag, strongestPitch(mag));
    if (!other) return { ok: false, error: 'noPartials', levelDb };
    const c = hzToCents(other.f0, midiToHz(expectMidi));
    const heard = Math.round(expectMidi + c / 100);
    return { ok: false, error: 'wrongNote', levelDb, heardMidi: heard, cents: c - 100 * (heard - expectMidi) };
  }
  const cents = hzToCents(fit.f0, midiToHz(expectMidi));
  if (Math.abs(cents) > MAX_CENTS) {
    const heard = Math.round(expectMidi + cents / 100);
    return { ok: false, error: 'wrongNote', levelDb, heardMidi: heard, cents: cents - 100 * (heard - expectMidi) };
  }
  return { ok: true, B: fit.B, f0: fit.f0, cents, n: fit.n, amps: partialAmps(att, fit.f0, fit.B), levelDb };
}

/**
 * Live capture of one pluck: feed the input blocks, read `result` once set.
 * Detects the onset itself (hop level rising 12 dB over the previous 100 ms
 * and above -45 dBFS), keeps 0.62 s after it, then analyses.
 */
export class PluckCapture {
  constructor(sampleRate, expectMidi) {
    this.rs = new Resampler(sampleRate, SR);
    this.expectMidi = expectMidi;
    this.buf = new Float32Array(Math.round(SR * (TIMEOUT_S + 1)));
    this.n = 0;
    this.levels = [];           // hop levels in dB
    this.onset = -1;
    this.result = null;
    this.levelDb = -120;
    this.sumSq = 0; this.inHop = 0;
    this._onSample = s => this._sample(s);
  }

  /** Feeds a block at the input rate; returns the result when available. */
  push(input) {
    if (this.result) return this.result;
    this.rs.push(input, this._onSample);
    return this.result;
  }

  _sample(s) {
    if (this.n >= this.buf.length) {
      if (!this.result) this.result = { ok: false, error: 'timeout' };
      return;
    }
    this.buf[this.n++] = s;
    this.sumSq += s * s;
    if (++this.inHop < HOP) return;
    this.inHop = 0;
    const lvl = dB(Math.sqrt(this.sumSq / HOP));
    this.sumSq = 0;
    this.levels.push(lvl);
    this.levelDb = lvl;
    const k = this.levels.length - 1;
    if (this.onset < 0) {
      if (k >= 40 && lvl >= ONSET_MIN_DB) {
        let past = Infinity;
        for (let d = 3; d <= 40; d++) past = Math.min(past, this.levels[k - d]);
        if (lvl - past >= ONSET_RISE_DB) this.onset = Math.max(0, (k - 1) * HOP);
      }
      if (this.n > TIMEOUT_S * SR) this.result = { ok: false, error: 'timeout' };
    } else if (this.n >= this.onset + Math.round(CAPTURE_S * SR)) {
      this.result = analysePluck(this.buf.subarray(0, this.n), this.onset, this.expectMidi);
    }
  }
}

/**
 * Builds a bank profile { bLaw, prof, tuning } from per-string measurements.
 * `open[s]` = analysePluck result of the open string s (or null to keep the
 * generic values), `fret12[s]` = optional result at the 12th fret, which
 * gives the slope of the B law (generic slope otherwise).
 */
export function makeProfile(open, fret12 = []) {
  const bLaw = [], prof = [], tuning = [], measured = [];
  for (let s = 0; s < 6; s++) {
    const o = open[s];
    if (!o || !o.ok) {
      bLaw.push([...B_LAW[s]]);
      prof.push([...PROF_ATT[s]]);
      tuning.push(null);
      measured.push(false);
      continue;
    }
    let slope = B_LAW[s][1];
    const f = fret12[s];
    if (f && f.ok && f.B > 0 && o.B > 0) {
      // B(fret) = B_open 2^(fret slope / 6)  ->  slope = log2(B12 / B0) / 2
      slope = Math.min(1.3, Math.max(0.6, Math.log2(f.B / o.B) / 2));
    }
    bLaw.push([o.B, slope]);
    prof.push(Array.from(o.amps));
    tuning.push(o.cents);
    measured.push(true);
  }
  return { bLaw, prof, tuning, measured };
}

export const OPEN_STRING_MIDI = OPEN_MIDI;
