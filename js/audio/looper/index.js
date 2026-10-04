// LOOPER: six synchronised loop tracks recorded from the rack, from one
// module, or from the audio input, each with its own small mixer strip
// (level, pan, low-pass, reverb and delay sends, mute/solo) and the classic
// tricks (reverse, half speed, overdub feedback, undo).
//
// The timing-critical part lives in the AudioWorklet (worklet.js → core.js);
// this module owns the Web Audio plumbing around it and the command API the
// rack card calls.
//
//   source ─→ [worklet: 6 tracks] ─→ per track: lowpass → level → pan → mute ─┬→ master
//                                                                             ├→ reverb send
//                                                                             └→ delay send
//   audio input additionally ─→ monitor level ─→ master (so you hear yourself)

import { TRACKS } from './core.js';

export { TRACKS };

const WORKLET_URL = new URL('./worklet.js', import.meta.url);

export function defaultTrack() {
  return {
    vol: 0.8, pan: 0, cutoff: 1,      // cutoff 0..1 (log) → 40 Hz .. 18 kHz, 1 = open
    rev: 0, del: 0,                   // sends 0..1
    mute: false, solo: false,
    reverse: false, half: false,
    feedback: 1,                      // overdub: 1 = layers never fade
  };
}

/**
 * @param engine      the shared Engine (context, buses)
 * @param handlers    { meter(m), events(list), status({key, detail}) }
 */
export function createLooper(engine, handlers, opts = {}) {
  const ctx = engine.ctx;
  let node = null, ready = null;
  let maxSeconds = opts.maxSeconds || 300;
  const params = Array.from({ length: TRACKS }, defaultTrack);
  const chains = [];
  let source = { kind: 'rack', id: null };
  let srcNode = null;                 // what currently feeds the worklet
  let stream = null, inGain = null, monitor = null;
  let monitorLevel = 1;
  let inputLatencyMs = 0;
  let status = { key: 'off', detail: '' };

  const setStatus = (key, detail = '') => { status = { key, detail }; handlers.status && handlers.status(status); };
  const post = msg => { if (node) node.port.postMessage(msg); };

  function buildChains() {
    for (let i = 0; i < TRACKS; i++) {
      const filter = ctx.createBiquadFilter();
      filter.type = 'lowpass';
      filter.frequency.value = 18000;
      const vol = ctx.createGain();
      const pan = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
      const mute = ctx.createGain();
      const rev = ctx.createGain(), del = ctx.createGain();
      node.connect(filter, i);
      filter.connect(vol);
      if (pan) { vol.connect(pan); pan.connect(mute); } else vol.connect(mute);
      mute.connect(engine.master);
      mute.connect(rev); rev.connect(engine.reverbIn);
      mute.connect(del); del.connect(engine.delayIn);
      chains.push({ filter, vol, pan, mute, rev, del });
      applyTrack(i);
    }
  }

  function applyTrack(i) {
    const c = chains[i], p = params[i];
    if (!c) return;
    const t = ctx.currentTime;
    c.vol.gain.setTargetAtTime(p.vol * p.vol, t, 0.02);
    if (c.pan) c.pan.pan.setTargetAtTime(p.pan, t, 0.02);
    c.filter.frequency.setTargetAtTime(40 * Math.pow(18000 / 40, Math.min(1, Math.max(0, p.cutoff))), t, 0.02);
    c.rev.gain.setTargetAtTime(p.rev, t, 0.02);
    c.del.gain.setTargetAtTime(p.del, t, 0.02);
    const anySolo = params.some(x => x.solo);
    c.mute.gain.setTargetAtTime(p.mute || (anySolo && !p.solo) ? 0 : 1, t, 0.015);
  }

  /** Loads the worklet and builds the graph once; resolves true when usable. */
  function init() {
    if (ready) return ready;
    if (!ctx.audioWorklet) { setStatus('unsupported'); ready = Promise.resolve(false); return ready; }
    ready = ctx.audioWorklet.addModule(WORKLET_URL).then(() => {
      node = new AudioWorkletNode(ctx, 'jamrack-looper', {
        numberOfInputs: 1, numberOfOutputs: TRACKS,
        outputChannelCount: Array.from({ length: TRACKS }, () => 2),
        channelCount: 2, channelCountMode: 'explicit',
        processorOptions: { maxSeconds },
      });
      node.port.onmessage = e => {
        const list = e.data || [];
        const evs = [];
        for (const m of list) { if (m.t === 'meter') handlers.meter && handlers.meter(m); else evs.push(m); }
        if (evs.length && handlers.events) handlers.events(evs);
      };
      buildChains();
      sendLatency();
      setStatus('ready');
      return true;
    }).catch(err => {
      console.warn('Looper: worklet failed to load', err && err.message);
      setStatus('unsupported');
      ready = null;
      return false;
    });
    return ready;
  }

  function outputLatencyMs() {
    const out = (typeof ctx.outputLatency === 'number' && ctx.outputLatency > 0) ? ctx.outputLatency : (ctx.baseLatency || 0);
    return out * 1000;
  }

  function sendLatency() {
    const sr = ctx.sampleRate;
    post({ cmd: 'latency', input: Math.round(inputLatencyMs / 1000 * sr), output: Math.round(outputLatencyMs() / 1000 * sr) });
  }

  function disconnectSource() {
    try { if (srcNode) srcNode.disconnect(node); } catch { /* not connected */ }
    srcNode = null;
    if (stream) {
      stream.getTracks().forEach(t => t.stop());
      try { inGain.disconnect(); monitor.disconnect(); } catch { /* gone */ }
      stream = inGain = monitor = null;
    }
    inputLatencyMs = 0;
  }

  /**
   * Chooses what the looper records: { kind: 'rack' } (every module, dry),
   * { kind: 'module', id } (one module), { kind: 'input', deviceId } (the
   * audio input, raw, with monitoring). Resolves false when the source
   * could not be opened (the status says why).
   */
  async function setSource(src) {
    if (!(await init())) return false;
    disconnectSource();
    source = { kind: src.kind, id: src.id || null, deviceId: src.deviceId || '' };
    if (src.kind === 'rack') {
      srcNode = engine.dry;
    } else if (src.kind === 'module') {
      srcNode = opts.moduleOut ? opts.moduleOut(src.id) : null;
      if (!srcNode) { setStatus('noModule'); return false; }
    } else if (src.kind === 'input') {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) { setStatus('noMic'); return false; }
      try {
        const audio = {
          echoCancellation: false, noiseSuppression: false, autoGainControl: false,
          channelCount: { ideal: 2 }, latency: { ideal: 0 }, sampleRate: { ideal: ctx.sampleRate },
        };
        if (src.deviceId) audio.deviceId = { exact: src.deviceId };
        stream = await navigator.mediaDevices.getUserMedia({ audio });
        if (ctx.state !== 'running') await ctx.resume();
        const track = stream.getAudioTracks()[0];
        const st = track.getSettings ? track.getSettings() : {};
        inputLatencyMs = typeof st.latency === 'number' ? st.latency * 1000 : 0;
        track.onended = () => { if (source.kind === 'input') { disconnectSource(); setStatus('ended'); } };
        const ms = ctx.createMediaStreamSource(stream);
        inGain = ctx.createGain();
        monitor = ctx.createGain();
        monitor.gain.value = monitorLevel;
        ms.connect(inGain);
        inGain.connect(monitor);
        monitor.connect(engine.master);
        srcNode = inGain;
      } catch (err) {
        setStatus(err && err.name === 'NotFoundError' ? 'noMic' : 'denied', (err && err.name) || 'Error');
        return false;
      }
    }
    if (srcNode) srcNode.connect(node, 0, 0);
    sendLatency();
    setStatus('ready');
    return true;
  }

  return {
    init,
    get status() { return status; },
    get source() { return source; },
    get params() { return params; },
    // transport
    toggle: i => post({ cmd: 'toggle', i }),
    play: () => post({ cmd: 'play' }),
    stop: () => post({ cmd: 'stop' }),
    clear: i => post({ cmd: 'clear', i }),
    clearAll: () => post({ cmd: 'clearAll' }),
    undo: i => post({ cmd: 'undo', i }),
    /** Mixer and tricks for one track; any subset of defaultTrack() keys. */
    setTrack(i, p) {
      Object.assign(params[i], p);
      const soloChanged = 'solo' in p;
      if (soloChanged) for (let k = 0; k < TRACKS; k++) applyTrack(k); else applyTrack(i);
      const core = {};
      for (const k of ['reverse', 'half', 'feedback']) if (k in p) core[k] = p[k];
      if (Object.keys(core).length) post({ cmd: 'track', i, params: core });
    },
    setSource,
    /** Beat length from the metronome tempo (0 = free loop length). */
    setSync(bpm) { post({ cmd: 'snap', frames: bpm > 0 ? Math.round(60 / bpm * ctx.sampleRate) : 0 }); },
    setMaxSeconds(sec) { maxSeconds = sec; post({ cmd: 'max', seconds: sec }); },
    setMonitor(v) { monitorLevel = v; if (monitor) monitor.gain.setTargetAtTime(v, ctx.currentTime, 0.02); },
    refreshLatency: sendLatency,
    latency() { return { input: inputLatencyMs, output: outputLatencyMs() }; },
  };
}
