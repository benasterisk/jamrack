// Global audio engine: context, master bus, shared reverb and delay.
//
// Routing:
//   instance.out ─┬─→ dry ─→ master ─→ limiter ─→ destination
//                 ├─→ (send) reverbIn ─→ convolver ─→ reverbWet ─→ master
//                 └─→ (send) delayIn ─→ delay(+feedback+tone) ─→ delayWet ─→ master

import { debounce } from '../util.js';

export class Engine {
  constructor(opts = {}) {
    const AC = window.AudioContext || window.webkitAudioContext;
    // 'interactive' is the platform's usual low-latency buffer; a numeric 0
    // asks for the smallest one the device supports (Chrome clamps it to what
    // it considers safe), which matters for the GUITAR → MIDI section. Phones
    // keep the default: their minimum glitches under a loaded rack.
    const desktop = window.matchMedia && window.matchMedia('(hover: hover) and (pointer: fine)').matches;
    // Buffer size: 'min' asks for the smallest buffer (lowest latency; a loaded
    // machine or a demanding audio driver may crackle), 'balanced' 20 ms,
    // 'safe' 30 ms. Measured in Chrome on Windows: hint 0 and 'interactive'
    // both give 10 ms (the WASAPI floor), 0.02 -> 20 ms, 0.03 -> 30 ms.
    const buffer = (opts && opts.buffer) || 'min';
    const latencyHint = buffer === 'safe' ? 0.03 : buffer === 'balanced' ? 0.02 : (desktop ? 0 : 'interactive');
    this.ctx = new AC({ latencyHint });

    this.master = this.ctx.createGain();
    // Instruments sum here (dry, before the shared effects) so the LOOPER can
    // record "the rack" without the metronome, the effect returns or itself.
    this.dry = this.ctx.createGain();
    this.dry.connect(this.master);
    // Instruments apply their own make-up gain (sampled banks are mastered
    // very quiet), so this limiter catches the peaks when voices stack up.
    this.limiter = this.ctx.createDynamicsCompressor();
    this.limiter.threshold.value = -1.5;
    this.limiter.knee.value = 3;
    this.limiter.ratio.value = 20;
    this.limiter.attack.value = 0.003;
    this.limiter.release.value = 0.25;

    this.analyser = this.ctx.createAnalyser();
    this.analyser.fftSize = 512;

    this.master.connect(this.limiter);
    this.limiter.connect(this.analyser);
    this.analyser.connect(this.ctx.destination);

    // Reverb (generated impulse response)
    this.reverbIn = this.ctx.createGain();
    this.convolver = this.ctx.createConvolver();
    this.reverbWet = this.ctx.createGain();
    this.reverbWet.gain.value = 1;
    this.reverbIn.connect(this.convolver);
    this.convolver.connect(this.reverbWet);
    this.reverbWet.connect(this.master);
    this.setReverbDecay(2.2);

    // Delay
    this.delayIn = this.ctx.createGain();
    this.delay = this.ctx.createDelay(2.5);
    this.delayFb = this.ctx.createGain();
    this.delayTone = this.ctx.createBiquadFilter();
    this.delayTone.type = 'lowpass';
    this.delayTone.frequency.value = 3800;
    this.delayWet = this.ctx.createGain();
    this.delayWet.gain.value = 1;
    this.delayIn.connect(this.delay);
    this.delay.connect(this.delayWet);
    this.delay.connect(this.delayTone);
    this.delayTone.connect(this.delayFb);
    this.delayFb.connect(this.delay);
    this.delayWet.connect(this.master);
    this.setDelay(0.35, 0.35);

    this._regenIR = debounce(sec => { this.convolver.buffer = this._makeIR(sec); }, 180);
  }

  get now() { return this.ctx.currentTime; }

  async resume() {
    if (this.ctx.state !== 'running') {
      try { await this.ctx.resume(); } catch { /* needs a user gesture */ }
    }
    return this.ctx.state === 'running';
  }

  setMasterVolume(v) {
    this.master.gain.setTargetAtTime(v * v, this.now, 0.02);
  }


  setReverbDecay(seconds) {
    // Regenerating the IR swaps in fresh random noise, which cuts the reverb
    // tail — only do it when the decay time actually changes.
    if (seconds === this._irSeconds) return;
    this._irSeconds = seconds;
    if (this.convolver.buffer) this._regenIR(seconds);
    else this.convolver.buffer = this._makeIR(seconds);
  }

  _makeIR(seconds) {
    const sr = this.ctx.sampleRate;
    const len = Math.max(1, Math.floor(sr * seconds));
    const buf = this.ctx.createBuffer(2, len, sr);
    for (let ch = 0; ch < 2; ch++) {
      const data = buf.getChannelData(ch);
      for (let i = 0; i < len; i++) {
        // noise with an exponential decay, denser at the start
        data[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 2.8);
      }
    }
    return buf;
  }

  setDelay(time, feedback) {
    const t = this.now;
    this.delay.delayTime.setTargetAtTime(time, t, 0.05);
    this.delayFb.gain.setTargetAtTime(Math.min(feedback, 0.9), t, 0.05);
  }
}
