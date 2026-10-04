// AudioWorklet processor hosting the guitar trackers on the audio thread.
// Loaded by js/input/guitar.js with audioWorklet.addModule(); the DSP itself
// lives in tracker.js (MONO) and poly/engine.js (POLY), shared with the tests
// and the ScriptProcessor fallback.
//
// Messages in:  { params }            tracker parameters (both engines)
//               { mode: 'mono'|'poly', bank? }   switch engine; `bank` is a
//                                     buildBank() result made on the main
//                                     thread (rendering it here would stall
//                                     the audio for ~50 ms)
//               { flush: true }       release the sounding notes
//               { capture: bool }     forward the raw input to the main thread
//                                     (calibration assistant)
// Messages out: arrays of tracker events, plus [{ t: 'mode', mode }] after a
//               switch (the notes of the previous engine are flushed first)
//               and [{ t: 'audio', data: Float32Array }] while capturing.
// A new `bank` while POLY runs rebuilds the POLY engine on it (notes flushed).

import { GuitarTracker } from './tracker.js';
import { PolyTracker } from './poly/engine.js';
import { buildBank } from './poly/bank.js';

class GuitarTrackerProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = (options && options.processorOptions) || {};
    this.params = o.params || {};
    this.bank = o.bank || null;
    this.mono = new GuitarTracker(sampleRate, this.params);
    this.poly = null;
    this.engine = this.mono;
    this.mode = 'mono';
    this.capture = false;
    this.capBuf = new Float32Array(2048);
    this.capN = 0;
    if (o.mode === 'poly') this._setMode('poly');
    this.port.onmessage = e => {
      const msg = e.data || {};
      if (msg.bank) {
        this.bank = msg.bank;
        if (this.poly) {            // a calibrated (or generic) bank replaces the running one
          const ev = this.poly.flush();
          if (ev.length) this.port.postMessage(ev);
          this.poly = null;
          if (this.mode === 'poly') { this.mode = ''; this._setMode('poly'); }
        }
      }
      if ('capture' in msg) { this.capture = !!msg.capture; this.capN = 0; }
      if (msg.params) {
        this.params = { ...this.params, ...msg.params };
        this.mono.setParams(msg.params);
        if (this.poly) this.poly.setParams(msg.params);
      }
      if (msg.mode && msg.mode !== this.mode) this._setMode(msg.mode);
      if (msg.flush) {
        const ev = this.engine.flush();
        if (ev.length) this.port.postMessage(ev);
      }
    };
  }

  _setMode(mode) {
    const ev = this.engine.flush();
    if (ev.length) this.port.postMessage(ev);
    if (mode === 'poly') {
      if (!this.poly) {
        this.poly = new PolyTracker(sampleRate, { ...this.params, bank: this.bank || buildBank('medium') });
      } else {
        this.poly.reset();
      }
      this.engine = this.poly;
    } else {
      this.engine = this.mono;
    }
    this.mode = mode;
    this.port.postMessage([{ t: 'mode', mode }]);
  }

  process(inputs) {
    const ch = inputs[0] && inputs[0][0];
    if (ch && ch.length) {
      // postMessage clones synchronously, so the tracker's reused array is safe
      const ev = this.engine.process(ch);
      if (ev.length) this.port.postMessage(ev);
      if (this.capture) {
        this.capBuf.set(ch.subarray(0, Math.min(ch.length, this.capBuf.length - this.capN)), this.capN);
        this.capN += ch.length;
        if (this.capN >= this.capBuf.length) {
          this.port.postMessage([{ t: 'audio', data: this.capBuf.slice(0, this.capBuf.length) }]);
          this.capN = 0;
        }
      }
    }
    return true;   // keep running even while the input is briefly silent
  }
}

registerProcessor('jamrack-guitar-tracker', GuitarTrackerProcessor);
