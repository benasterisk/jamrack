// Sparse beta-divergence NMF step of the POLY engine: explains one magnitude
// spectrum v (1025 bins) as W h with the fixed template bank W, h >= 0.
// Mirrors test/poly/decomp/decomp.py nmf_run (beta 0.5, multiplicative
// updates, L1 penalty, warm start from the previous hop, active set).
//
// Per hop:
//   1. salience s = W^T v of every template
//   2. active set: the templates sounding at the previous hop (h above a
//      floor), the strongest `active` of them if there are more, otherwise
//      completed to `active` by salience; the noise templates always in
//   3. h_act starts from the previous hop, floored at 1e-4 max(v)
//   4. `iter` updates  h <- h * (W^T (v y^-1.5)) / (W^T y^-0.5 + lambda),
//      y = W h, on the active columns only
//   5. the pitch activation P[m] = sum of h over the templates of midi m
//
// Cost is what matters here (budget: one hop = 2.67 ms, see
// docs/polyphonic-plan.md section 14): the columns are stored contiguously
// and each update is one accumulate pass (y) plus one fused dot pass
// (numerator and denominator together) over the active columns.

import { NBINS, N_PITCH } from './bank.js';

const H_FLOOR = 1e-6;
const EPS = 1e-9;

export class Decomposer {
  /**
   * @param bank    buildBank() result
   * @param o       { active: 60, iter: 15, lambda: 400, initSal: 0 }
   */
  constructor(bank, o = {}) {
    this.bank = bank;
    this.K = bank.K;
    this.W = bank.W;
    this.active = o.active ?? 60;
    this.iter = o.iter ?? 15;
    this.lambda = o.lambda ?? 400;
    this.initSal = o.initSal ?? 0;
    this.noise = bank.meta.map((m, i) => (m.midi < 0 ? i : -1)).filter(i => i >= 0);
    this.h = new Float64Array(this.K);        // activations carried from hop to hop
    this.sal = new Float64Array(this.K);
    this.ha = new Float64Array(this.K);
    this.act = new Int32Array(this.K);
    this.nAct = 0;
    this.y = new Float64Array(NBINS);
    this.g1 = new Float64Array(NBINS);        // v * y^-1.5
    this.g2 = new Float64Array(NBINS);        // y^-0.5
    this.P = new Float64Array(N_PITCH);
    this._order = new Int32Array(this.K);
    this._inAct = new Uint8Array(this.K);
  }

  reset() {
    this.h.fill(0);
  }

  /** One hop. `v`: magnitude spectrum (NBINS). Returns this.P (reused). */
  step(v) {
    const K = this.K, W = this.W, h = this.h, sal = this.sal, ha = this.ha, act = this.act;
    const y = this.y, g1 = this.g1, g2 = this.g2, lam = this.lambda;

    // 1. salience and the frame maximum
    let vmax = 0;
    for (let b = 0; b < NBINS; b++) if (v[b] > vmax) vmax = v[b];
    for (let k = 0; k < K; k++) {
      let s = 0;
      const base = k * NBINS;
      for (let b = 0; b < NBINS; b++) s += W[base + b] * v[b];
      sal[k] = s;
    }

    // 2. active set
    const nmax = Math.min(this.active, K);
    const inAct = this._inAct;
    inAct.fill(0);
    let n = 0;
    if (nmax >= K) {
      for (let k = 0; k < K; k++) act[n++] = k;
    } else {
      const order = this._order;
      let ns = 0;
      for (let k = 0; k < K; k++) if (h[k] > H_FLOOR) order[ns++] = k;
      if (ns >= nmax) {
        // the strongest nmax of the sounding templates
        const sub = Array.from(order.subarray(0, ns)).sort((a, b) => h[b] - h[a]);
        for (let i = 0; i < nmax; i++) { act[n++] = sub[i]; inAct[sub[i]] = 1; }
      } else {
        for (let i = 0; i < ns; i++) { act[n++] = order[i]; inAct[order[i]] = 1; }
        // fill with the most salient of the rest
        const rest = [];
        for (let k = 0; k < K; k++) if (!inAct[k]) rest.push(k);
        rest.sort((a, b) => sal[b] - sal[a]);
        const need = nmax - ns;
        for (let i = 0; i < need; i++) { act[n++] = rest[i]; inAct[rest[i]] = 1; }
      }
      for (const k of this.noise) if (!inAct[k]) { act[n++] = k; inAct[k] = 1; }
      // ascending column order, like np.union1d (summation order only)
      const sub = Array.from(act.subarray(0, n)).sort((a, b) => a - b);
      for (let i = 0; i < n; i++) act[i] = sub[i];
    }
    this.nAct = n;

    // 3. warm start
    const floor = 1e-4 * Math.max(vmax, 1e-6);
    for (let i = 0; i < n; i++) {
      const k = act[i];
      let start = Math.max(h[k], floor);
      if (this.initSal > 0) start = Math.max(start, this.initSal * sal[k]);
      ha[i] = start;
    }

    // 4. multiplicative updates on the active columns
    for (let it = 0; it < this.iter; it++) {
      y.fill(EPS);
      for (let i = 0; i < n; i++) {
        const a = ha[i];
        if (a === 0) continue;
        const base = act[i] * NBINS;
        for (let b = 0; b < NBINS; b++) y[b] += a * W[base + b];
      }
      for (let b = 0; b < NBINS; b++) {
        const r = 1 / Math.sqrt(y[b]);     // y^-0.5
        g2[b] = r;
        g1[b] = v[b] * r / y[b];           // v * y^-1.5
      }
      for (let i = 0; i < n; i++) {
        const base = act[i] * NBINS;
        let num = 0, den = 0;
        for (let b = 0; b < NBINS; b++) {
          const w = W[base + b];
          num += w * g1[b];
          den += w * g2[b];
        }
        ha[i] *= num / (den + lam);
      }
    }

    // 5. write back, pitch activations
    h.fill(0);
    for (let i = 0; i < n; i++) h[act[i]] = ha[i];
    const P = this.P, cols = this.bank.pitchCols;
    for (let j = 0; j < N_PITCH; j++) {
      let s = 0;
      const c = cols[j];
      for (let i = 0; i < c.length; i++) s += h[c[i]];
      P[j] = s;
    }
    return P;
  }
}
