// Streaming polyphase resampler to the POLY analysis rate (24 kHz).
//
// Same filter as scipy.signal.resample_poly, which the Python prototype
// (test/poly/analyze.py) used on the 44.1 kHz GuitarSet takes: a Kaiser
// (beta 5) windowed sinc, cutoff 1 / max(up, down) of Nyquist, half-length
// 10 x max(up, down) taps on the UPSAMPLED grid, DC gain `up`. The output
// matches resample_poly to float precision, which the equivalence test
// relies on, and the group delay is half the kernel on the upsampled grid:
// 0.42 ms at every usual ratio (48 kHz -> 24 kHz is 1/2 and 41 taps;
// 44.1 kHz -> 24 kHz is 80/147 and 2941 taps, of which each output sample
// touches 37), so the same kernel serves the live engine.
//
// The output is causal: output sample n corresponds to the input seen
// `delay()` input samples ago. An offline harness that wants scipy's
// zero-phase alignment subtracts that delay from its timestamps.

function gcd(a, b) {
  while (b) [a, b] = [b, a % b];
  return a;
}

/** Modified Bessel function of the first kind, order 0 (series). */
function besselI0(x) {
  let sum = 1, term = 1;
  const y = x * x / 4;
  for (let k = 1; k < 60; k++) {
    term *= y / (k * k);
    sum += term;
    if (term < sum * 1e-17) break;
  }
  return sum;
}

/** scipy.signal.firwin(numtaps, cutoff, window=('kaiser', beta)), cutoff in units of Nyquist. */
export function firwinKaiser(numtaps, cutoff, beta) {
  const h = new Float64Array(numtaps);
  const mid = (numtaps - 1) / 2;
  const i0b = besselI0(beta);
  let sum = 0;
  for (let i = 0; i < numtaps; i++) {
    const m = i - mid;
    const sinc = m === 0 ? 1 : Math.sin(Math.PI * cutoff * m) / (Math.PI * cutoff * m);
    const r = 2 * i / (numtaps - 1) - 1;
    const kaiser = besselI0(beta * Math.sqrt(Math.max(0, 1 - r * r))) / i0b;
    h[i] = cutoff * sinc * kaiser;
    sum += h[i];
  }
  for (let i = 0; i < numtaps; i++) h[i] /= sum;   // unit gain at DC
  return h;
}

export class Resampler {
  /**
   * @param {number} srIn   input sample rate
   * @param {number} srOut  output sample rate
   * @param {object} [o]    { halfLenFactor: 10 (scipy) }
   */
  constructor(srIn, srOut, o = {}) {
    const g = gcd(Math.round(srIn), Math.round(srOut));
    this.up = Math.round(srOut) / g;
    this.down = Math.round(srIn) / g;
    this.srIn = srIn;
    this.srOut = srOut;
    const maxRate = Math.max(this.up, this.down);
    const factor = o.halfLenFactor ?? 10;
    this.halfLen = Math.round(factor * maxRate);
    const taps = 2 * this.halfLen + 1;
    const h = firwinKaiser(taps, 1 / maxRate, 5.0);
    for (let i = 0; i < taps; i++) h[i] *= this.up;
    // Polyphase split: output sample n uses phase (n * down) mod up and
    // input index floor(n * down / up), looking back over `perPhase` inputs.
    this.perPhase = Math.ceil(taps / this.up);
    this.phases = [];
    for (let p = 0; p < this.up; p++) {
      const ph = new Float64Array(this.perPhase);
      for (let j = 0; j < this.perPhase; j++) {
        const k = p + j * this.up;           // tap index for input j steps back
        ph[j] = k < taps ? h[k] : 0;
      }
      this.phases.push(ph);
    }
    // history of inputs, newest at `wi - 1`
    let size = 64;
    while (size < 2 * this.perPhase + 16) size *= 2;
    this.hist = new Float64Array(size);
    this.mask = size - 1;
    this.wi = 0;
    this.count = 0;          // input samples consumed so far
    this.nextOut = 0;        // output sample index to produce next
    this.identity = this.up === 1 && this.down === 1;
  }

  /** Group delay of the filter in INPUT samples. */
  delay() {
    return this.halfLen / this.up;
  }

  /** Same delay in seconds. */
  delaySeconds() {
    return this.delay() / this.srIn;
  }

  /**
   * Pushes input samples; calls `onSample(y)` for every output sample. The
   * output n is emitted once input index floor(n * down / up) has arrived,
   * i.e. with a lag of `halfLen` taps (causal).
   */
  push(input, onSample) {
    if (this.identity) {
      for (let i = 0; i < input.length; i++) onSample(input[i]);
      return;
    }
    const hist = this.hist, mask = this.mask, up = this.up, down = this.down, L = this.perPhase;
    for (let i = 0; i < input.length; i++) {
      hist[this.wi] = input[i];
      this.wi = (this.wi + 1) & mask;
      this.count++;
      // every output whose anchor input has arrived
      while (true) {
        const n = this.nextOut;
        const num = n * down;                      // position in the upsampled grid
        const anchor = Math.floor(num / up);        // newest input needed: index anchor
        if (anchor >= this.count) break;
        const phase = num - anchor * up;            // (n * down) mod up
        const ph = this.phases[phase];
        let acc = 0;
        // input j steps back from `anchor`: hist index of input (anchor - j)
        let idx = (this.wi - 1 - (this.count - 1 - anchor)) & mask;
        for (let j = 0; j < L; j++) {
          acc += ph[j] * hist[idx];
          idx = (idx - 1) & mask;
        }
        onSample(acc);
        this.nextOut++;
      }
    }
  }
}
