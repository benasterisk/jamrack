// Per-hop note rule of the POLY engine: turns the pitch activations of the
// decomposer, the level and the spectral flux into note-on / note-off
// decisions. A causal port of test/poly/notes.py run_take() with the
// thresholds of test/poly/params-merged.json ("best", tuned on the DEV
// players only).
//
//   onset     log-spectral flux >= fluxThr AND the level rose >= riseDb over
//             where it was 3-6 hops (8-16 ms) ago, or flux >= 2 fluxThr alone;
//             refractory `refractory` hops that a 1.5x stronger flux may
//             override during the first 4. An onset opens a decision window
//             of `windowHops`.
//   note-on   inside the window, pitch m fires when for `agree` consecutive
//             hops P[m] > absOn and P[m] > frac * sum(P), when P[m] has risen
//             riseX over its value 4 hops before the onset, when its odd /
//             even partial ratio clears the sub-octave guard, and when no
//             sounding or candidate note 12 / 19 / 24 / 28 / 31 semitones
//             below explains it (harmUp). One note per pitch and per onset;
//             at most maxVoices voices (the weakest is released otherwise);
//             a sounding pitch is re-struck by the same rule (repick).
//   note-off  when the activation stays releaseDb under its peak, or under
//             absOn / 4, for offHops consecutive hops.
//
// Events carry the hop index that decided them; the host stamps them at the
// end of that hop, (hop + 1) * 2.67 ms, like the prototype.

import { N_PITCH, PITCH_LO } from './bank.js';

/** Thresholds (params-merged.json "best"). Exported mutable for ablations. */
export const NOTE_RULE = {
  absOn: 0.008,
  frac: 0.12,
  riseX: 1.2,
  fluxThr: 2.0,
  riseDb: 6.0,
  releaseDb: 20.0,
  offHops: 9,
  windowHops: 15,
  refractory: 10,
  octOdd: 0.25,
  octUp: 0.5,
  maxVoices: 6,
  agree: 2,
  minDelay: 0,
  lowGuard: 0.0,
  harmUp: 1.0,
};

const HARMONIC_INTERVALS = [12, 19, 24, 28, 31];
const P_RING = 8;      // hops of P kept for the "before the onset" reference
const B_RING = 4;      // hops of the base condition kept for `agree`

export class NoteRule {
  constructor(params = {}) {
    this.p = { ...NOTE_RULE, ...params };
    this.levels = new Float64Array(8).fill(Infinity);
    this.Phist = new Float64Array(P_RING * N_PITCH);
    this.baseHist = new Uint8Array(B_RING * N_PITCH);
    this.before = new Float64Array(N_PITCH);
    this.fired = new Uint8Array(N_PITCH);
    this.cond = new Uint8Array(N_PITCH);
    this.voices = new Map();     // pitch index -> { on, peak, below, vel }
    this.reset();
  }

  reset() {
    this.t = 0;
    this.levels.fill(Infinity);
    this.Phist.fill(0);
    this.baseHist.fill(0);
    this.before.fill(0);
    this.fired.fill(0);
    this.lastOnset = -1e9;
    this.lastFlux = 0;
    this.onsetHop = -1e6;
    this.voices.clear();
  }

  setParams(params) {
    Object.assign(this.p, params);
  }

  /**
   * One hop. P: pitch activations (N_PITCH), level in dBFS, flux, odd and low
   * partial ratios per pitch. Appends { t: 'on', midi, vel, hop } and
   * { t: 'off', midi, hop } to `out`.
   */
  step(P, level, flux, odd, low, out) {
    const p = this.p, t = this.t;
    const voices = this.voices;

    // ---- onset detection
    this.levels[t & 7] = level;
    let past = Infinity;
    for (let d = 3; d <= 6; d++) if (t - d >= 0) past = Math.min(past, this.levels[(t - d) & 7]);
    const rise = level - past;
    const raw = (flux >= p.fluxThr && rise >= p.riseDb) || flux >= 2 * p.fluxThr;
    // P of this hop into the ring before reading "4 hops ago"
    const pRow = (t % P_RING) * N_PITCH;
    this.Phist.set(P, pRow);
    if (raw && (t - this.lastOnset >= p.refractory || (t - this.lastOnset <= 4 && flux >= 1.5 * this.lastFlux))) {
      this.lastOnset = t;
      this.lastFlux = flux;
      this.onsetHop = t;
      this.fired.fill(0);
      const bh = Math.max(0, t - 4);
      this.before.set(this.Phist.subarray((bh % P_RING) * N_PITCH, (bh % P_RING + 1) * N_PITCH));
    }

    // ---- candidate condition
    let S = 0;
    for (let m = 0; m < N_PITCH; m++) S += P[m];
    const bRow = (t % B_RING) * N_PITCH;
    const base = this.baseHist;
    for (let m = 0; m < N_PITCH; m++) base[bRow + m] = (P[m] > p.absOn && P[m] > p.frac * S) ? 1 : 0;
    const since = t - this.onsetHop;
    const inWin = this.onsetHop >= 0 && since <= p.windowHops && since >= p.minDelay;
    const cond = this.cond;
    let any = false;
    if (inWin) {
      const riseFloor = p.absOn / 4;
      for (let m = 0; m < N_PITCH; m++) {
        let c = 1;
        for (let d = 0; d < p.agree && c; d++) {
          if (t - d < 0) { c = 0; break; }
          c = base[((t - d) % B_RING) * N_PITCH + m];
        }
        if (c && p.lowGuard > 0 && !(low[m] >= p.lowGuard)) c = 0;
        if (c && !(P[m] >= p.riseX * Math.max(this.before[m], riseFloor))) c = 0;
        if (c && !(odd[m] >= p.octOdd)) c = 0;
        cond[m] = c;
        if (c) any = true;
      }
    } else {
      cond.fill(0);
    }

    // ---- note-off (before note-on, like the prototype)
    if (voices.size) {
      const offGain = Math.pow(10, -p.releaseDb / 20);
      const floor = p.absOn / 4;
      for (const [m, v] of voices) {
        const x = P[m];
        if (x > v.peak) v.peak = x;
        if (x < v.peak * offGain || x < floor) {
          if (++v.below >= p.offHops) this._noteOff(m, t, out);
        } else {
          v.below = 0;
        }
      }
    }

    // ---- note-on
    if (any) {
      for (let m = 0; m < N_PITCH; m++) {
        if (!cond[m] || this.fired[m]) continue;
        const v = voices.get(m);
        if (v && t - v.on < p.refractory) continue;
        if (m - 12 >= 0 && p.octUp > 0 && voices.has(m - 12) && P[m] < p.octUp * P[m - 12]) continue;
        if (p.harmUp > 0) {
          let ghost = false;
          for (const d of HARMONIC_INTERVALS) {
            const q = m - d;
            if (q >= 0 && (voices.has(q) || cond[q]) && P[q] > p.harmUp * P[m]) { ghost = true; break; }
          }
          if (ghost) continue;
        }
        if (v) this._noteOff(m, t, out);           // repick
        if (voices.size >= p.maxVoices) {
          let weakest = -1, wval = Infinity;
          for (const q of voices.keys()) if (P[q] < wval) { wval = P[q]; weakest = q; }
          if (wval >= P[m]) continue;
          this._noteOff(weakest, t, out);
        }
        const vel = Math.min(1, Math.max(0.1, (20 * Math.log10(Math.max(P[m], 1e-9)) + 50) / 30));
        voices.set(m, { on: t, peak: P[m], below: 0, vel });
        this.fired[m] = 1;
        out.push({ t: 'on', midi: m + PITCH_LO, vel, why: 'poly', hop: t });
      }
    }
    this.t = t + 1;
  }

  /** Releases every voice at hop `hop` (input stopped / end of file). */
  flush(out, hop = this.t - 1) {
    for (const m of [...this.voices.keys()]) this._noteOff(m, Math.max(0, hop), out);
  }

  _noteOff(m, t, out) {
    this.voices.delete(m);
    out.push({ t: 'off', midi: m + PITCH_LO, hop: t });
  }
}
