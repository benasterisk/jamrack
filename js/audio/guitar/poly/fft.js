// Real-input FFT for the POLY engine: magnitude spectrum of a zero-padded
// frame. Plain JavaScript over typed arrays (AudioWorklet, Node tests).
//
// Radix-2 iterative complex FFT with precomputed twiddles and bit-reversal
// table; a real frame is fed with a zero imaginary part. The POLY engine only
// needs two sizes (2048 per hop, 65536 once to render the template bank), so
// the simplicity wins over a split-radix or real-packing scheme: a 2048-point
// transform costs ~25 µs here, far under the decomposer's budget.

export class RealFFT {
  /** `n` must be a power of two. */
  constructor(n) {
    if (n < 2 || (n & (n - 1))) throw new Error(`RealFFT: size ${n} is not a power of two`);
    this.n = n;
    this.re = new Float64Array(n);
    this.im = new Float64Array(n);
    this.cos = new Float64Array(n / 2);
    this.sin = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) {
      const a = -2 * Math.PI * i / n;
      this.cos[i] = Math.cos(a);
      this.sin[i] = Math.sin(a);
    }
    this.rev = new Uint32Array(n);
    const bits = Math.log2(n);
    for (let i = 0; i < n; i++) {
      let r = 0;
      for (let b = 0; b < bits; b++) r |= ((i >>> b) & 1) << (bits - 1 - b);
      this.rev[i] = r;
    }
  }

  /**
   * Magnitude spectrum |X[k]| for k = 0..n/2 of the real signal `x`
   * (length <= n, zero-padded at the end), written into `out` (n/2 + 1).
   */
  magnitude(x, out) {
    const n = this.n, re = this.re, im = this.im, rev = this.rev;
    const len = Math.min(x.length, n);
    for (let i = 0; i < n; i++) {
      const j = rev[i];
      re[i] = j < len ? x[j] : 0;
      im[i] = 0;
    }
    this._transform();
    for (let k = 0; k <= n / 2; k++) out[k] = Math.hypot(re[k], im[k]);
    return out;
  }

  _transform() {
    const n = this.n, re = this.re, im = this.im, cos = this.cos, sin = this.sin;
    for (let size = 2; size <= n; size <<= 1) {
      const half = size >> 1, step = n / size;
      for (let start = 0; start < n; start += size) {
        for (let j = 0, t = 0; j < half; j++, t += step) {
          const wr = cos[t], wi = sin[t];
          const a = start + j, b = a + half;
          const xr = re[b] * wr - im[b] * wi;
          const xi = re[b] * wi + im[b] * wr;
          re[b] = re[a] - xr; im[b] = im[a] - xi;
          re[a] += xr; im[a] += xi;
        }
      }
    }
  }
}
