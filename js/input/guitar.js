// GUITAR → MIDI input: captures an audio input (interface or mic) with every
// browser "smart" processing switched off, runs a tracker in an AudioWorklet
// on the audio thread, and turns its events into note-on/off and pitch bend —
// exactly like a MIDI keyboard would.
//
// Two engines, chosen by `mode` (state.guitar.mode):
//   mono  js/audio/guitar/tracker.js — one note at a time, follows bends
//   poly  js/audio/guitar/poly/engine.js — several strings at once (beta),
//         no pitch bend yet; its template bank is rendered here, on the main
//         thread, and handed to the worklet
//
// Latency budget, in order of size (see docs/guitar-to-midi.md):
//   1. the tracker itself: 1-2 periods of the note + the pick transient
//      (≈10 ms on the high strings, 20-35 ms on the low ones) — physics
//   2. the OS input path: a few ms with a proper interface on macOS, ~10 ms
//      WASAPI on Windows, more without an interface
//   3. the synth output: the audio context's buffer (shown as OUT)
// What the browser *adds* over a native app is mostly (2): raw constraints
// (no echo cancellation, noise suppression or auto-gain) take out the
// biggest chunk, the rest needs ASIO/CoreAudio from a native helper.

import { GuitarTracker } from '../audio/guitar/tracker.js';
import { PolyTracker } from '../audio/guitar/poly/engine.js';
import { buildBank } from '../audio/guitar/poly/bank.js';
import { captureOpened, captureClosed } from '../util.js';

const WORKLET_URL = new URL('../audio/guitar/worklet.js', import.meta.url);

export function createGuitarInput(ctx, handlers) {
  let running = false, starting = false;
  let stream = null, src = null, gainNode = null, node = null, mute = null;
  let fallback = null;           // tracker on the main thread (no worklet)
  let workletReady = null;       // Promise, resolved once addModule succeeded
  let params = {};
  let mode = 'mono';
  let bank = null;               // POLY template bank, built once on demand
  let bankSent = false;          // ... already handed to the current worklet node
  let profile = null;            // calibrated profile the bank is rendered from (null = generic)
  let onAudio = null;            // raw-input listener while the calibration assistant runs
  let gain = 1;
  let devices = [];
  let deviceId = '';
  let status = { key: 'off', detail: '' };
  let inputLatency = NaN;
  const sounding = new Set();    // notes currently held by the tracker

  const setStatus = (key, detail = '') => {
    status = { key, detail };
    handlers.status && handlers.status(status);
  };

  function dispatch(events) {
    for (const e of events) {
      if (e.t === 'on') { sounding.add(e.midi); handlers.noteOn(e.midi, e.vel); }
      else if (e.t === 'off') { sounding.delete(e.midi); handlers.noteOff(e.midi); }
      else if (e.t === 'bend') handlers.bend(e.semis);
      else if (e.t === 'meter') handlers.meter && handlers.meter(e);
      else if (e.t === 'mode') handlers.mode && handlers.mode(e.mode);
      else if (e.t === 'audio') { if (onAudio) onAudio(e.data); }
    }
  }

  async function refreshDevices() {
    try {
      const all = await navigator.mediaDevices.enumerateDevices();
      devices = all.filter(d => d.kind === 'audioinput')
        .map((d, i) => ({ id: d.deviceId, label: d.label || `Input ${i + 1}` }));
    } catch {
      devices = [];
    }
    handlers.devices && handlers.devices(devices, deviceId);
  }

  function loadWorklet() {
    if (!ctx.audioWorklet) return Promise.reject(new Error('no AudioWorklet'));
    if (!workletReady) {
      workletReady = ctx.audioWorklet.addModule(WORKLET_URL).catch(err => {
        workletReady = null;
        throw err;
      });
    }
    return workletReady;
  }

  function polyBank() {
    if (!bank) bank = profile ? buildBank('medium', { bLaw: profile.bLaw, prof: profile.prof }) : buildBank('medium');   // ~50 ms
    return bank;
  }

  /** Renders the POLY bank from a calibrated profile (null = generic) and hands it to the running engine. */
  function setProfile(p) {
    profile = p || null;
    bank = null;
    bankSent = false;
    if (!running || mode !== 'poly') return;
    if (fallback) {
      dispatch(fallback.flush());
      fallback = makeFallback();
    } else if (node && node.port) {
      node.port.postMessage({ bank: polyBank() });
      bankSent = true;
    }
  }

  /** Streams the raw input (blocks of the context rate) to `fn` until stopCapture(). */
  function startCapture(fn) {
    onAudio = fn;
    if (node && node.port && !fallback) node.port.postMessage({ capture: true });
  }

  function stopCapture() {
    onAudio = null;
    if (node && node.port && !fallback) node.port.postMessage({ capture: false });
  }

  function makeFallback() {
    return mode === 'poly'
      ? new PolyTracker(ctx.sampleRate, { ...params, bank: polyBank() })
      : new GuitarTracker(ctx.sampleRate, params);
  }

  async function start(id = deviceId) {
    if (starting) return false;
    if (running) stop();
    starting = true;
    deviceId = id || '';
    setStatus('starting');
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      starting = false;
      setStatus('noMic');
      return false;
    }
    try {
      // iOS: state the intent before the microphone opens, or the page is
      // routed like a phone call (see captureOpened in util.js)
      captureOpened('guitar');
      // Raw audio: the three "smart" processors add 10-20 ms and mangle an
      // instrument signal; a 0 latency hint asks the OS for its smallest input buffer.
      const audio = {
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
        channelCount: { ideal: 1 },
        latency: { ideal: 0 },
        sampleRate: { ideal: ctx.sampleRate },
      };
      if (deviceId) audio.deviceId = { exact: deviceId };
      stream = await navigator.mediaDevices.getUserMedia({ audio });
      if (ctx.state !== 'running') await ctx.resume();

      const track = stream.getAudioTracks()[0];
      const settings = track.getSettings ? track.getSettings() : {};
      if (settings.deviceId) deviceId = settings.deviceId;
      inputLatency = typeof settings.latency === 'number' ? settings.latency * 1000 : NaN;
      track.onended = () => { if (running) { stop(); setStatus('ended'); } };

      src = ctx.createMediaStreamSource(stream);
      gainNode = ctx.createGain();
      gainNode.gain.value = gain;
      // The analysis node must reach the destination to be pulled; a muted
      // gain keeps the guitar itself out of the speakers.
      mute = ctx.createGain();
      mute.gain.value = 0;

      let compat = false;
      try {
        await loadWorklet();
        const processorOptions = { params, mode };
        if (mode === 'poly') processorOptions.bank = polyBank();
        node = new AudioWorkletNode(ctx, 'jamrack-guitar-tracker', {
          numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
          channelCount: 1, channelCountMode: 'explicit',
          processorOptions,
        });
        bankSent = mode === 'poly';
        node.port.onmessage = e => dispatch(e.data);
      } catch (err) {
        // Old browser: the same tracker on the main thread. Works, with the
        // extra jitter of a 256-sample ScriptProcessor.
        console.warn('Guitar tracker: AudioWorklet unavailable, using ScriptProcessor', err && err.message);
        fallback = makeFallback();
        node = ctx.createScriptProcessor(256, 1, 1);
        node.onaudioprocess = ev => {
          const data = ev.inputBuffer.getChannelData(0);
          dispatch(fallback.process(data));
          if (onAudio) onAudio(Float32Array.from(data));
        };
        compat = true;
      }
      src.connect(gainNode);
      gainNode.connect(node);
      node.connect(mute);
      mute.connect(ctx.destination);

      running = true;
      starting = false;
      handlers.running && handlers.running(true);
      await refreshDevices();
      setStatus(compat ? 'compat' : 'listening', currentLabel());
      return true;
    } catch (err) {
      starting = false;
      teardown();
      setStatus(err && err.name === 'NotFoundError' ? 'noMic' : 'denied', (err && err.name) || 'Error');
      return false;
    }
  }

  function currentLabel() {
    const d = devices.find(x => x.id === deviceId);
    return d ? d.label : '';
  }

  function teardown() {
    try { if (node) node.disconnect(); } catch { /* already gone */ }
    try { if (gainNode) gainNode.disconnect(); } catch { /* already gone */ }
    try { if (src) src.disconnect(); } catch { /* already gone */ }
    try { if (mute) mute.disconnect(); } catch { /* already gone */ }
    if (node && node.port) node.port.onmessage = null;
    if (node && 'onaudioprocess' in node) node.onaudioprocess = null;
    if (stream) stream.getTracks().forEach(t => t.stop());
    stream = src = gainNode = node = mute = null;
    fallback = null;
    bankSent = false;
    captureClosed('guitar');
  }

  function releaseAll() {
    // The main thread knows the held notes (it dispatched them), so no round
    // trip to the worklet is needed.
    for (const m of sounding) handlers.noteOff(m);
    sounding.clear();
    handlers.bend(0);
  }

  function stop() {
    if (!running) return;
    running = false;
    releaseAll();
    teardown();
    handlers.running && handlers.running(false);
    setStatus('off');
  }

  function setParams(p) {
    params = { ...params, ...p };
    if (fallback) fallback.setParams(p);
    else if (node && node.port) node.port.postMessage({ params: p });
  }

  /** Switches the engine ('mono' | 'poly'); the held notes are released. */
  function setMode(m) {
    if (m !== 'mono' && m !== 'poly') m = 'mono';
    if (m === mode) return;
    mode = m;
    if (!running) return;
    if (fallback) {
      dispatch(fallback.flush());
      fallback = makeFallback();
      handlers.mode && handlers.mode(mode);
    } else if (node && node.port) {
      const msg = { mode };
      if (mode === 'poly' && !bankSent) { msg.bank = polyBank(); bankSent = true; }
      node.port.postMessage(msg);   // the worklet flushes, switches and reports
    }
  }

  function setGain(v) {
    gain = v;
    if (gainNode) gainNode.gain.setTargetAtTime(v, ctx.currentTime, 0.02);
  }

  if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
    navigator.mediaDevices.addEventListener('devicechange', () => { if (running) refreshDevices(); });
  }

  return {
    start, stop, setParams, setMode, setGain, setProfile, startCapture, stopCapture,
    get running() { return running; },
    get mode() { return mode; },
    get profile() { return profile; },
    get devices() { return devices; },
    get deviceId() { return deviceId; },
    get status() { return status; },
    /** Input/output buffering as reported by the browser, in ms (NaN if unknown). */
    latency() {
      const out = (typeof ctx.outputLatency === 'number' && ctx.outputLatency > 0)
        ? ctx.outputLatency : ctx.baseLatency;
      return { input: inputLatency, output: typeof out === 'number' ? out * 1000 : NaN };
    },
  };
}
