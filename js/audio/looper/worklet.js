// AudioWorklet host for the LOOPER core: one stereo input (the chosen source),
// six stereo outputs (one per track, mixed and effected by Web Audio nodes
// on the main thread, see index.js).

import { LooperCore, TRACKS } from './core.js';

class LooperProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = (options && options.processorOptions) || {};
    this.core = new LooperCore(sampleRate, { maxSeconds: o.maxSeconds || 300 });
    this.blocks = 0;
    this.silence = new Float32Array(128);
    this.port.onmessage = e => {
      const m = e.data || {};
      const c = this.core;
      switch (m.cmd) {
        case 'toggle': c.toggle(m.i); break;
        case 'play': c.play(); break;
        case 'stop': c.stop(); break;
        case 'clear': c.clear(m.i); break;
        case 'clearAll': c.clearAll(); break;
        case 'undo': c.undo(m.i); break;
        case 'track': c.setTrack(m.i, m.params || {}); break;
        case 'latency': c.setLatency(m.input || 0, m.output || 0); break;
        case 'snap': c.setSnap(m.frames || 0); break;
        case 'max': c.setMaxSeconds(m.seconds || 300); break;
        default: break;
      }
      // commands change state: answer with the events they produced plus a meter
      const ev = c._events.splice(0);
      if (ev.length) this.port.postMessage(ev);
      this.port.postMessage([c.meter()]);
    };
  }

  process(inputs, outputs) {
    const inp = inputs[0] || [];
    const n = (outputs[0] && outputs[0][0] && outputs[0][0].length) || 128;
    if (this.silence.length !== n) this.silence = new Float32Array(n);
    const inL = inp[0] || this.silence;
    const inR = inp[1] || inL;
    const outs = [];
    for (let i = 0; i < TRACKS; i++) outs.push([outputs[i][0], outputs[i][1] || outputs[i][0]]);
    const ev = this.core.process(inL, inR, outs);
    if (ev.length) this.port.postMessage(ev.slice());
    if (++this.blocks % 12 === 0) this.port.postMessage([this.core.meter()]);   // ~30 ms
    return true;
  }
}

registerProcessor('jamrack-looper', LooperProcessor);
