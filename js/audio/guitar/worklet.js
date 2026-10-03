// AudioWorklet processor hosting the guitar tracker on the audio thread.
// Loaded by js/input/guitar.js with audioWorklet.addModule(); the DSP itself
// lives in tracker.js (shared with the tests and the ScriptProcessor fallback).

import { GuitarTracker } from './tracker.js';

class GuitarTrackerProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const params = (options && options.processorOptions && options.processorOptions.params) || {};
    this.tracker = new GuitarTracker(sampleRate, params);
    this.port.onmessage = e => {
      const msg = e.data || {};
      if (msg.params) this.tracker.setParams(msg.params);
      if (msg.flush) {
        const ev = this.tracker.flush();
        if (ev.length) this.port.postMessage(ev);
      }
    };
  }

  process(inputs) {
    const ch = inputs[0] && inputs[0][0];
    if (ch && ch.length) {
      // postMessage clones synchronously, so the tracker's reused array is safe
      const ev = this.tracker.process(ch);
      if (ev.length) this.port.postMessage(ev);
    }
    return true;   // keep running even while the input is briefly silent
  }
}

registerProcessor('jamrack-guitar-tracker', GuitarTrackerProcessor);
