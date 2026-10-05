// Generic template bank of the POLY engine ("fitatt" in the prototype):
// one magnitude-spectrum template per (string, fret) rendered from the
// measured per-string partial profile and inharmonicity law of profile.js,
// plus two noise templates for the pick transient. Mirrors
// test/poly/bank/make_bank.py (kind fit, phase att) and test/poly/templates.py.
//
// A template is the spectrum the analysis would see for that note: the sum
// over partials n of a_n |W(f - f_n)|, f_n = n f0 sqrt(1 + B n^2), where W is
// the transform of the analysis window sampled 32x finer than a bin. Columns
// are unit L2. Built once at start-up (~30 ms); nothing is shipped
// precomputed, so a calibrated profile can be rendered the same way later.

import { RealFFT } from './fft.js';
import { B_LAW, PROF_ATT } from './profile.js';

export const SR = 24000;            // analysis rate
export const HOP = 64;              // samples per hop (2.67 ms)
export const NFFT = 2048;
export const NBINS = NFFT / 2 + 1;  // 1025
export const BIN_HZ = SR / NFFT;    // 11.72 Hz
export const WINDOWS = { medium: 1024, short: 512 };
export const OPEN_MIDI = [40, 45, 50, 55, 59, 64];   // E2 A2 D3 G3 B3 E4
export const FRETS = 20;
export const PITCH_LO = 40, N_PITCH = 44;            // E2 .. B5 (midi 40..83)
const F_MAX = 10000;
const OVERSAMPLE = 32;

export const midiToHz = m => 440 * Math.pow(2, (m - 69) / 12);

/**
 * Analysis window of `n` samples, index n-1 = newest sample, scaled so a
 * full-scale sine shows a peak magnitude of 1 in the spectrum.
 */
export function hannWindow(n) {
  const w = new Float64Array(n);
  let sum = 0;
  for (let i = 0; i < n; i++) {
    w[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * (i + 0.5) / n);
    sum += w[i];
  }
  for (let i = 0; i < n; i++) w[i] *= 2 / sum;
  return w;
}

/** |W(f)| of the window on a grid of 1/OVERSAMPLE bin, normalised to W(0) = 1. */
export function windowKernel(w) {
  const n = NFFT * OVERSAMPLE;
  const fft = new RealFFT(n);
  const mag = fft.magnitude(w, new Float64Array(n / 2 + 1));
  const k0 = mag[0];
  for (let i = 0; i < mag.length; i++) mag[i] /= k0;
  return mag;
}

/** Adds the partials of one note (amplitudes amps[n-1]) into `spec` (NBINS). */
export function renderProfile(f0, B, amps, kern, spec) {
  const last = kern.length - 2;
  for (let n = 1; n <= amps.length; n++) {
    const a = amps[n - 1];
    const fn = n * f0 * Math.sqrt(1 + B * n * n);
    if (fn > F_MAX || a <= 0) continue;
    const centre = fn / BIN_HZ;
    for (let b = 0; b < NBINS; b++) {
      const off = Math.abs(b - centre) * OVERSAMPLE;
      let idx = Math.floor(off);
      if (idx > last) idx = last;
      const frac = off - idx;
      spec[b] += a * (kern[idx] * (1 - frac) + kern[idx + 1] * frac);
    }
  }
  return spec;
}

/**
 * Builds the bank for one analysis window.
 * Returns { K, W, meta, window, pitchCols } where W is column-major
 * Float32Array (column k at W.subarray(k * NBINS, (k + 1) * NBINS)), meta[k]
 * = { string, fret, midi } (midi -1 for the noise columns) and
 * pitchCols[j] lists the columns of pitch PITCH_LO + j.
 */
export function buildBank(winName = 'medium', profile = { bLaw: B_LAW, prof: PROF_ATT }) {
  const n = WINDOWS[winName];
  const w = hannWindow(n);
  const kern = windowKernel(w);
  const K = 6 * FRETS + 2;
  const W = new Float32Array(K * NBINS);
  const meta = [];
  const col = new Float64Array(NBINS);
  let k = 0;
  const store = () => {
    let ss = 0;
    for (let b = 0; b < NBINS; b++) ss += col[b] * col[b];
    const inv = 1 / Math.sqrt(ss);
    const base = k * NBINS;
    for (let b = 0; b < NBINS; b++) W[base + b] = col[b] * inv;
    col.fill(0);
    k++;
  };
  for (let s = 0; s < 6; s++) {
    const [bOpen, slope] = profile.bLaw[s];
    for (let f = 0; f < FRETS; f++) {
      const midi = OPEN_MIDI[s] + f;
      const B = bOpen * Math.pow(2, f * slope / 6);
      renderProfile(midiToHz(midi), B, profile.prof[s], kern, col);
      meta.push({ string: s, fret: f, midi });
      store();
    }
  }
  // noise templates: white, and pink (1/sqrt f, floored at 50 Hz)
  col.fill(1);
  meta.push({ string: -1, fret: -1, midi: -1, name: 'noise-white' });
  store();
  for (let b = 0; b < NBINS; b++) col[b] = 1 / Math.sqrt(Math.max(b * BIN_HZ, 50));
  meta.push({ string: -1, fret: -1, midi: -1, name: 'noise-pink' });
  store();
  const pitchCols = [];
  for (let j = 0; j < N_PITCH; j++) {
    pitchCols.push(meta.map((m, i) => (m.midi === PITCH_LO + j ? i : -1)).filter(i => i >= 0));
  }
  return { K, W, meta, window: w, pitchCols };
}
