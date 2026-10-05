// POLY engine: polyphonic guitar → note tracker (plan docs/polyphonic-plan.md,
// section 14, milestone 1). Pure JavaScript over typed arrays, like the
// monophonic tracker, so the same code runs in the AudioWorklet, in the
// main-thread fallback and in the Node tests and benchmarks.
//
// Per input sample: polyphase resampling to 24 kHz (resample.js).
// Per hop of 64 analysis samples (2.67 ms):
//   1. level: RMS of the hop, in dBFS
//   2. two newest-anchored Hann windows of the last 1024 (medium, 42.7 ms)
//      and 512 (short, 21.3 ms) samples, 2048-point FFT → magnitude spectra
//   3. onset feature: log-spectral flux of the short window (mean rise in dB
//      of the bins 47 Hz..5 kHz over 3 hops, floored at -80 dBFS, each bin's
//      rise clipped at 20 dB)
//   4. octave guards from the medium spectrum: odd / even partial energy and
//      (p1 + p2) / (p3..p6) of every pitch
//   5. decomposer: sparse beta-NMF of the medium spectrum on the template
//      bank → activation of every pitch (nmf.js)
//   6. note rule → note-on / note-off (notes.js)
//
// Events: { t: 'on', midi, vel, why: 'poly', hop }, { t: 'off', midi, hop },
// { t: 'meter', db, hop, nmfMs, hopMs } (every 8 hops). `hop` is the index of
// the hop that decided the event; a host stamps it at (hop + 1) * 64 / 24000 s
// of analysis time plus the resampler delay.

import { Resampler } from './resample.js';
import { RealFFT } from './fft.js';
import {
  buildBank, hannWindow, SR, HOP, NFFT, NBINS, BIN_HZ, WINDOWS, N_PITCH, PITCH_LO, midiToHz,
} from './bank.js';
import { Decomposer } from './nmf.js';
import { NoteRule, NOTE_RULE } from './notes.js';

export { SR as ANALYSIS_RATE, HOP as ANALYSIS_HOP };

export const POLY_DEFAULTS = {
  octave: 0,          // added to the detected note
  transpose: 0,       // semitones added to the detected note
};

/** Decomposer settings. Exported mutable for ablations (test/poly-dump-events.mjs).
 *  The prototype ran dense / 15 iterations; shipping is sparse 1e-3 / 8
 *  iterations, 2.4x cheaper for -0.1 pt F1 on single notes and -1.6 pt on
 *  triads over the 13 624-note bench (docs/poly-implementation.md). ECO is
 *  the emergency setting the engine falls back to while the audio thread
 *  saturates (see PolyTracker.eco); `sparse` 0 = exact prototype arithmetic. */
export const DECOMPOSER = { active: 60, iter: 8, lambda: 400, initSal: 0, sparse: 1e-3 };
export const ECO = { iter: 5, active: 40 };
// Load control: the hop cost (exponential average) against the hop budget.
// Above ECO_IN for ECO_HOLD hops the decomposer drops to ECO; below ECO_OUT
// for ECO_HOLD hops it comes back. Hysteresis keeps it from flapping.
const ECO_IN = 0.80, ECO_OUT = 0.45, ECO_HOLD = 375;   // 375 hops = 1 s

// Input-level normalisation. The note rule's thresholds and the decomposer's
// L1 penalty are absolute, tuned at GuitarSet levels (active hops: median
// -27 dBFS, 95th percentile -17 dBFS). A guitar plugged straight into an
// interface often sits 10-20 dB lower, and the decomposer then crushes its
// activations. So the spectrum fed to the decomposer is scaled so that the
// recent peak level (slow envelope) meets AGC_REF_DB; never attenuated, at
// most +AGC_MAX_DB. Level and flux (dB differences) are left alone.
const AGC_REF_DB = -17;
const AGC_MAX_DB = 30;
const AGC_DECAY_DB_PER_HOP = 6 / 375;   // the envelope falls 6 dB per second
const AGC_FLOOR_DB = -70;               // below this nothing drives the envelope

const FLUX_LAG = 3;
const FLUX_LO = 4, FLUX_HI = 427;        // bins: 47 Hz .. 5 kHz
const FLUX_BINS = FLUX_HI - FLUX_LO;
const FLUX_FLOOR_DB = -80;
const METER_EVERY = 8;
const EPS = 1e-9;
const RING = 2048;                        // >= medium window

const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

export class PolyTracker {
  /**
   * @param sampleRate input rate
   * @param params     POLY_DEFAULTS subset; `bank` = a buildBank() result to
   *                   reuse (building one takes ~50 ms), `rule` = NoteRule
   *                   threshold overrides, `decomposer` = DECOMPOSER overrides
   */
  constructor(sampleRate, params = {}) {
    this.sr = sampleRate;
    this.p = { ...POLY_DEFAULTS };
    this.rs = new Resampler(sampleRate, SR);
    this.bank = params.bank || buildBank('medium');
    this.fft = new RealFFT(NFFT);
    this.winM = this.bank.window;
    this.winS = hannWindow(WINDOWS.short);
    this.nM = WINDOWS.medium;
    this.nS = WINDOWS.short;
    this.ring = new Float64Array(RING);
    this.wi = 0;
    this.since = 0;
    this.sumSq = 0;
    this.frameM = new Float64Array(this.nM);
    this.frameS = new Float64Array(this.nS);
    this.V = new Float64Array(NBINS);
    this.Vs = new Float64Array(NBINS);
    this.dbHist = new Float64Array((FLUX_LAG + 1) * FLUX_BINS);
    this.odd = new Float64Array(N_PITCH);
    this.low = new Float64Array(N_PITCH);
    this._pk = new Float64Array(6);          // scratch: partial peaks of one pitch (no per-hop allocation)
    // bins of partials 1..6 of every pitch (centre of the ±1-bin max)
    this.pbin = new Int32Array(N_PITCH * 6);
    for (let j = 0; j < N_PITCH; j++) {
      const f0 = midiToHz(PITCH_LO + j);
      for (let n = 1; n <= 6; n++) {
        const r = Math.round(n * f0 / BIN_HZ) - 1;
        this.pbin[j * 6 + n - 1] = Math.min(NBINS - 3, Math.max(0, r)) + 1;
      }
    }
    this.decomposer = new Decomposer(this.bank, { ...DECOMPOSER, ...(params.decomposer || {}) });
    this.rule = new NoteRule({ ...NOTE_RULE, ...(params.rule || {}) });
    this.hop = 0;
    this.shift = 0;
    this.nmfMs = 0;           // exponential average of the decomposer cost per hop
    this.hopMs = 0;           // ... of the whole hop
    this.maxHopMs = 0;
    this.agc = params.agc !== false;
    this.envDb = AGC_FLOOR_DB;  // slow peak of the hop level, for the AGC
    this.gainDb = 0;            // gain applied to the spectrum this hop
    this.eco = false;         // decomposer running at the ECO setting (overload)
    this.autoEco = params.autoEco !== false;
    this._ecoCount = 0;
    this._full = { iter: this.decomposer.iter, active: this.decomposer.active };
    this._out = [];
    this._raw = [];
    this._sent = new Map();     // raw pitch -> midi actually sent (shift at note-on time)
    this._onSample = s => this._sample(s);
    this.setParams(params);
  }

  setParams(params) {
    for (const k of Object.keys(params)) {
      if (k in POLY_DEFAULTS && params[k] !== undefined) this.p[k] = params[k];
    }
    this.shift = 12 * (this.p.octave | 0) + (this.p.transpose | 0);
    if (params.rule) this.rule.setParams(params.rule);
  }

  /** Delay added by the resampler, in seconds (0.42 ms at the usual rates). */
  get delaySeconds() {
    return this.rs.delaySeconds();
  }

  /** Feeds a block at the input rate; returns the events (array reused). */
  process(input) {
    this._out.length = 0;
    this.rs.push(input, this._onSample);
    return this._out;
  }

  /** Feeds samples already at the analysis rate (tests, offline harnesses). */
  pushAnalysisRate(samples) {
    this._out.length = 0;
    for (let i = 0; i < samples.length; i++) this._sample(samples[i]);
    return this._out;
  }

  /** Releases every sounding note (input stopped). Returns the events. */
  flush() {
    this._out.length = 0;
    this._raw.length = 0;
    this.rule.flush(this._raw, this.hop - 1);
    this._emit(this._raw);
    return this._out;
  }

  /** Forgets the past (new input): voices, activations, histories. */
  reset() {
    this.ring.fill(0);
    this.wi = 0; this.since = 0; this.sumSq = 0; this.hop = 0;
    this.dbHist.fill(0);
    this.decomposer.reset();
    this.rule.reset();
  }

  _sample(s) {
    this.ring[this.wi] = s;
    this.wi = (this.wi + 1) & (RING - 1);
    this.sumSq += s * s;
    if (++this.since >= HOP) {
      this.since = 0;
      this._hop();
    }
  }

  _hop() {
    const t0 = now();
    const t = this.hop;
    const level = 20 * Math.log10(Math.sqrt(this.sumSq / HOP) + EPS);
    this.sumSq = 0;

    // windows anchored on the newest sample
    const ring = this.ring, mask = RING - 1, wi = this.wi;
    const fM = this.frameM, nM = this.nM, wM = this.winM;
    for (let i = 0; i < nM; i++) fM[i] = ring[(wi - nM + i) & mask] * wM[i];
    this.fft.magnitude(fM, this.V);
    const fS = this.frameS, nS = this.nS, wS = this.winS;
    for (let i = 0; i < nS; i++) fS[i] = ring[(wi - nS + i) & mask] * wS[i];
    this.fft.magnitude(fS, this.Vs);

    // spectral flux of the short window
    const row = (t % (FLUX_LAG + 1)) * FLUX_BINS;
    const old = ((t + 1) % (FLUX_LAG + 1)) * FLUX_BINS;   // the row of hop t - 3
    const db = this.dbHist, Vs = this.Vs;
    let flux = 0;
    for (let b = 0; b < FLUX_BINS; b++) {
      const d = 20 * Math.log10(Math.max(Vs[FLUX_LO + b], 1e-4));
      if (t >= FLUX_LAG) {
        const r = d - db[old + b];
        flux += r <= 0 ? 0 : (r >= 20 ? 20 : r);
      }
      db[row + b] = d;
    }
    flux = t >= FLUX_LAG ? flux / FLUX_BINS : 0;
    void FLUX_FLOOR_DB;

    // partial ratios (sub-octave and sub-harmonic guards)
    const V = this.V, pb = this.pbin, odd = this.odd, low = this.low, pk = this._pk;
    for (let j = 0; j < N_PITCH; j++) {
      for (let n = 0; n < 6; n++) {
        const c = pb[j * 6 + n];
        const a = V[c - 1], b2 = V[c], c2 = V[c + 1];
        pk[n] = a > b2 ? (a > c2 ? a : c2) : (b2 > c2 ? b2 : c2);
      }
      odd[j] = (pk[0] + pk[2] + pk[4]) / (pk[1] + pk[3] + pk[5] + EPS);
      low[j] = (pk[0] + pk[1]) / (pk[2] + pk[3] + pk[4] + pk[5] + EPS);
    }

    // input-level normalisation (see AGC_REF_DB)
    if (this.agc) {
      this.envDb = Math.max(level, this.envDb - AGC_DECAY_DB_PER_HOP, AGC_FLOOR_DB);
      this.gainDb = Math.min(AGC_MAX_DB, Math.max(0, AGC_REF_DB - this.envDb));
      if (this.gainDb > 0) {
        const g = Math.pow(10, this.gainDb / 20);
        for (let b = 0; b < NBINS; b++) V[b] *= g;
      }
    }

    // decomposer + note rule
    const t1 = now();
    const P = this.decomposer.step(V);
    const t2 = now();
    const raw = this._raw;
    raw.length = 0;
    this.rule.step(P, level, flux, odd, low, raw);
    this._emit(raw);

    const hopMs = now() - t0, nmfMs = t2 - t1;
    this.nmfMs += (nmfMs - this.nmfMs) * 0.05;
    this.hopMs += (hopMs - this.hopMs) * 0.05;
    if (hopMs > this.maxHopMs) this.maxHopMs = hopMs;
    if (this.autoEco) this._loadControl();
    if (t % METER_EVERY === 0) {
      this._out.push({ t: 'meter', db: level, hop: t, nmfMs: this.nmfMs, hopMs: this.hopMs, voices: this.rule.voices.size, eco: this.eco, gainDb: this.gainDb });
    }
    this.hop = t + 1;
  }

  /** Drops to the ECO decomposer while the hop cost saturates the budget, and back. */
  _loadControl() {
    const budget = 1000 * HOP / SR;
    const ratio = this.hopMs / budget;
    if (!this.eco && ratio > ECO_IN) {
      if (++this._ecoCount >= ECO_HOLD) this.setEco(true);
    } else if (this.eco && ratio < ECO_OUT) {
      if (++this._ecoCount >= ECO_HOLD) this.setEco(false);
    } else {
      this._ecoCount = 0;
    }
  }

  setEco(on) {
    this.eco = !!on;
    this._ecoCount = 0;
    this.decomposer.iter = on ? ECO.iter : this._full.iter;
    this.decomposer.active = on ? ECO.active : this._full.active;
  }

  // The octave / transpose shift is applied here. A note-off must carry the
  // same midi as its note-on even if the shift changed in between, or the
  // host would keep the first note sounding forever: remember what was sent.
  _emit(raw) {
    for (let i = 0; i < raw.length; i++) {
      const e = raw[i];
      if (e.t === 'on') {
        const midi = e.midi + this.shift;
        if (midi < 0 || midi > 127) continue;
        this._sent.set(e.midi, midi);
        this._out.push({ t: 'on', midi, vel: e.vel, why: 'poly', hop: e.hop });
      } else {
        const midi = this._sent.get(e.midi);
        if (midi === undefined) continue;
        this._sent.delete(e.midi);
        this._out.push({ t: 'off', midi, hop: e.hop });
      }
    }
  }
}
