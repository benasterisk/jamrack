// GUITAR → MIDI input: captures an audio input (interface or mic) with every
// browser "smart" processing switched off, runs the tracker in an AudioWorklet
// on the audio thread, and turns its events into note-on/off and pitch bend —
// exactly like a MIDI keyboard would.
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

const WORKLET_URL = new URL('../audio/guitar/worklet.js', import.meta.url);

export function createGuitarInput(ctx, handlers) {
  let running = false, starting = false;
  let stream = null, src = null, gainNode = null, node = null, mute = null;
  let fallback = null;           // GuitarTracker on the main thread (no worklet)
  let workletReady = null;       // Promise, resolved once addModule succeeded
  let params = {};
  let gain = 1;
  let devices = [];
  let deviceId = '';
  let status = { key: 'off', detail: '' };
  let inputLatency = NaN;
  let sounding = -1;             // note currently held by the tracker, or -1

  const setStatus = (key, detail = '') => {
    status = { key, detail };
    handlers.status && handlers.status(status);
  };

  function dispatch(events) {
    for (const e of events) {
      if (e.t === 'on') { sounding = e.midi; handlers.noteOn(e.midi, e.vel); }
      else if (e.t === 'off') { if (sounding === e.midi) sounding = -1; handlers.noteOff(e.midi); }
      else if (e.t === 'bend') handlers.bend(e.semis);
      else if (e.t === 'meter') handlers.meter && handlers.meter(e);
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
      // iOS: opening the microphone flips the system audio session to
      // "play and record", which by default treats the page like a phone
      // call (earpiece, lowered volume). The Audio Session API (iOS 17+)
      // lets the page state its intent explicitly; it is the only lever a
      // web page has over the output route. Harmless elsewhere.
      setAudioSession('play-and-record');
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
        node = new AudioWorkletNode(ctx, 'jamrack-guitar-tracker', {
          numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1],
          channelCount: 1, channelCountMode: 'explicit',
          processorOptions: { params },
        });
        node.port.onmessage = e => dispatch(e.data);
      } catch (err) {
        // Old browser: the same tracker on the main thread. Works, with the
        // extra jitter of a 256-sample ScriptProcessor.
        console.warn('Guitar tracker: AudioWorklet unavailable, using ScriptProcessor', err && err.message);
        fallback = new GuitarTracker(ctx.sampleRate, params);
        node = ctx.createScriptProcessor(256, 1, 1);
        node.onaudioprocess = ev => dispatch(fallback.process(ev.inputBuffer.getChannelData(0)));
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

  function setAudioSession(type) {
    try {
      if (navigator.audioSession && 'type' in navigator.audioSession) navigator.audioSession.type = type;
    } catch { /* not supported */ }
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
    setAudioSession('auto');
  }

  function stop() {
    if (!running) return;
    running = false;
    // Release whatever note is sounding before the graph goes away. The main
    // thread knows it (it dispatched it), so no round trip to the worklet.
    if (sounding >= 0) { handlers.noteOff(sounding); sounding = -1; }
    handlers.bend(0);
    teardown();
    handlers.running && handlers.running(false);
    setStatus('off');
  }

  function setParams(p) {
    params = { ...params, ...p };
    if (fallback) fallback.setParams(p);
    else if (node && node.port) node.port.postMessage({ params: p });
  }

  function setGain(v) {
    gain = v;
    if (gainNode) gainNode.gain.setTargetAtTime(v, ctx.currentTime, 0.02);
  }

  if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
    navigator.mediaDevices.addEventListener('devicechange', () => { if (running) refreshDevices(); });
  }

  return {
    start, stop, setParams, setGain,
    get running() { return running; },
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
