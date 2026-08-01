// One rack instance = a playable instrument with its own audio chain:
//   voices -> filter -> volume -> pan -> mute -> out (dry + reverb/delay sends)
// Two engines: 'bank' (samples from the sound bank) and 'analog' (a modern
// subtractive synth: unison/supersaw, second oscillator, sub, per-voice filter
// with its own envelope, LFO and drive — see _buildAnalogVoice).

import { clamp, midiToFreq } from '../util.js';

const MAX_VOICES = 48;

// Waveshaper curves are expensive to build and only depend on the amount, so
// the handful of distinct values in use are cached.
const driveCurves = new Map();

/** Soft-clipping curve. amount 0..1 — this is what makes the sound "fat". */
function makeDriveCurve(amount) {
  const key = Math.round(amount * 40);          // 0.025 steps is plenty
  const cached = driveCurves.get(key);
  if (cached) return cached;
  const k = amount * 60;                        // 0 = clean, 60 = squashed
  const n = 2048;
  const curve = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * 2 - 1;
    // Classic arctangent-style soft clip: rounds the peaks instead of
    // chopping them, which is what saturation (rather than distortion) is.
    curve[i] = k === 0 ? x : ((1 + k) * x) / (1 + k * Math.abs(x));
  }
  driveCurves.set(key, curve);
  return curve;
}

// Measured against MusyngKite: +26 dB puts a maxed six-note chord just under
// 0 dBFS with no clipping (+28 already clips). The analog engine reaches full
// scale on its own (see the per-voice peak in noteOn) and needs no lift.
// SFZ banks are mastered at normal levels (unlike midi-js ones), so they only
// need a little headroom trim rather than the large lift `bank` requires.
const MAKEUP_DB = { bank: 26, analog: 0, sfz: 6, sampler: 5 };

export class Instrument {
  constructor(engine, settings) {
    this.engine = engine;
    const ctx = engine.ctx;

    this.input = ctx.createGain();
    this.filter = ctx.createBiquadFilter();
    this.filter.type = 'lowpass';
    this.volGain = ctx.createGain();
    this.panner = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
    this.muteGain = ctx.createGain();
    this.out = ctx.createGain();

    this.input.connect(this.filter);
    this.filter.connect(this.volGain);
    if (this.panner) {
      this.volGain.connect(this.panner);
      this.panner.connect(this.muteGain);
    } else {
      this.volGain.connect(this.muteGain);
    }
    this.muteGain.connect(this.out);

    this.out.connect(engine.master);
    this.revSend = ctx.createGain();
    this.delSend = ctx.createGain();
    this.out.connect(this.revSend);
    this.out.connect(this.delSend);
    this.revSend.connect(engine.reverbIn);
    this.delSend.connect(engine.delayIn);

    this.voices = new Map();     // input note -> active voice (key held down)
    this.releasing = new Set();  // released or sustained voices
    this.sustained = new Set();
    this.sustainOn = false;
    this.bendCents = 0;
    this.buffers = null;         // Map midi -> AudioBuffer ('bank' engine)
    this.sfz = null;             // SfzPlayer ('sfz' engine), see setSfzPlayer

    this.applySettings(settings);
  }

  applySettings(s) {
    this.s = s;
    const t = this.engine.now;
    // Sampled banks are mastered far below full scale (MusyngKite peaks near
    // -23 dBFS), so they need make-up gain to reach a normal loudness; the
    // analog engine already runs hot. Applying it per instrument — rather than
    // on the shared bus — lets both engines coexist at a sane level.
    const makeup = Math.pow(10, (MAKEUP_DB[s.engine] ?? MAKEUP_DB.bank) / 20);
    this.volGain.gain.setTargetAtTime(s.volume * s.volume * makeup, t, 0.02);
    if (this.panner) this.panner.pan.setTargetAtTime(s.pan, t, 0.02);
    // 0..1 -> 40 Hz .. 18 kHz (log)
    const freq = 40 * Math.pow(18000 / 40, clamp(s.filterCut, 0, 1));
    this.filter.frequency.setTargetAtTime(freq, t, 0.02);
    this.filter.Q.setTargetAtTime(0.3 + Math.pow(s.filterRes, 2) * 17, t, 0.02);
    this.revSend.gain.setTargetAtTime(s.revSend, t, 0.02);
    this.delSend.gain.setTargetAtTime(s.delSend, t, 0.02);
  }

  setMuted(m) {
    this.muteGain.gain.setTargetAtTime(m ? 0 : 1, this.engine.now, 0.015);
  }

  setBuffers(buffers) {
    this.buffers = buffers;
  }

  /** Attaches the user sample of the SAMPLER engine; null clears it. */
  setSampler(buffer) {
    this.smp = buffer ? { buffer, rev: null } : null;
  }

  /** Reversed copy of the sample, built once on first use. */
  _smpReversed() {
    const s = this.smp;
    if (!s) return null;
    if (!s.rev) {
      const b = s.buffer;
      const r = this.engine.ctx.createBuffer(b.numberOfChannels, b.length, b.sampleRate);
      for (let ch = 0; ch < b.numberOfChannels; ch++) {
        const src = b.getChannelData(ch);
        const dst = r.getChannelData(ch);
        for (let i = 0; i < b.length; i++) dst[i] = src[b.length - 1 - i];
      }
      s.rev = r;
    }
    return s.rev;
  }

  /**
   * Granular playback: a stream of short overlapping grains, each pitched by
   * `pitch`, while the read position walks the buffer at `stretch` speed.
   * That decoupling is exactly what a BufferSource cannot do on its own.
   *
   * Returns a facade shaped like an audio source (stop()/onended) so the
   * shared voice lifecycle (release, cleanup, stealing) treats it normally.
   */
  _spawnGranular(ctx, t0, dest, buf, o) {
    const dur = buf.duration;
    const grain = o.grain;
    const period = grain / 2;                // 50% overlap, triangular window
    let pos = o.a * dur;                     // read head, in seconds
    let next = Math.max(t0, ctx.currentTime);
    let stopAt = Infinity;
    let done = false;
    let lastEnd = next + grain;

    const facade = {
      onended: null,
      stop(at) { stopAt = Math.min(stopAt, at ?? ctx.currentTime); },
    };

    const spawn = (when) => {
      const src = ctx.createBufferSource();
      src.buffer = buf;
      src.playbackRate.value = o.pitch;
      const g = ctx.createGain();
      g.gain.setValueAtTime(0, when);
      g.gain.linearRampToValueAtTime(1, when + period);
      g.gain.linearRampToValueAtTime(0, when + grain);
      src.connect(g);
      g.connect(dest);
      const offset = Math.min(pos, dur - 0.002);
      src.start(when, offset, Math.min(grain * o.pitch + 0.01, dur - offset));
      src.stop(when + grain + 0.01);
      src.onended = () => {
        try { src.disconnect(); g.disconnect(); } catch { /* done */ }
      };
      lastEnd = when + grain;
    };

    const tick = () => {
      const horizon = ctx.currentTime + 0.12;
      while (next < horizon && next < stopAt && !done) {
        spawn(next);
        next += period;
        pos += period * o.stretch;
        if (o.loopOn) {
          if (pos >= o.lb * dur) pos = o.la * dur + (pos - o.lb * dur);
        } else if (pos >= o.b * dur) {
          done = true;
        }
      }
      if ((done || ctx.currentTime >= stopAt) && ctx.currentTime > lastEnd) {
        clearInterval(timer);
        if (facade.onended) facade.onended();
      }
    };
    const timer = setInterval(tick, 25);
    tick();
    return facade;
  }

  /** Attaches an SFZ player (engine 'sfz'); null detaches it. */
  setSfzPlayer(player) {
    if (this.sfz && this.sfz !== player) this.sfz.allNotesOff(true);
    this.sfz = player || null;
  }

  // input note (before this instance's octave/transpose), velocity 0..1
  noteOn(inputMidi, vel) {
    const s = this.s;
    const midi = clamp(inputMidi + s.octave * 12 + s.transpose, 0, 127);

    // SFZ has its own voice model (several regions may sound per note, with
    // their own envelopes and loops), so it bypasses the shared voice pool.
    if (s.engine === 'sfz') {
      if (!this.sfz) return;
      // A keyswitch names a fixed key in the bank: octave/transpose must not
      // move it, or the articulation would be selected by the wrong key.
      const target = this.sfz.isKeyswitchNote(inputMidi) ? inputMidi : midi;
      // vel arrives 0..1 here but SFZ reasons in MIDI velocity.
      this.sfz.noteOn(target, Math.max(1, Math.round(vel * 127)), this.bendCents);
      return target;
    }

    const existing = this.voices.get(inputMidi);
    if (existing) this._release(existing, 0.04);

    const ctx = this.engine.ctx;
    const t = this.engine.now;
    const gain = ctx.createGain();
    gain.connect(this.input);

    const sources = [];
    // Extra per-voice nodes (filter, LFO, shaper…) kept so the voice can be
    // torn down cleanly: oscillators need stop(), gains need disconnect().
    const extra = { nodes: [], stoppables: [] };
    let peak;
    let oneShot = false;      // sampler: play through, ignore note-off

    if (s.engine === 'analog') {
      peak = this._buildAnalogVoice(ctx, t, midi, vel, gain, sources, extra);
    } else if (s.engine === 'sampler') {
      if (!this.smp) return;
      const p = s.sampler;
      const full = this.smp.buffer;
      const dur = full.duration;
      let a = clamp(p.start, 0, 1), b = clamp(p.end, 0, 1);
      if (b - a < 0.005) b = Math.min(1, a + 0.005);
      let rate = Math.pow(2, (p.tune || 0) / 1200);
      let loopOn = p.loopOn;
      oneShot = p.oneShot;

      if (p.chop > 0) {
        // MPC-style chop: the trimmed region is cut into N equal slices, one
        // key per slice starting at chopBase, each fired one-shot at raw pitch.
        const idx = midi - (p.chopBase ?? 48);
        if (idx < 0 || idx >= p.chop) return;
        const w = (b - a) / p.chop;
        a = a + idx * w;
        b = a + w;
        loopOn = false;
        oneShot = true;
      } else if (p.keytrack) {
        rate *= Math.pow(2, (midi - (p.rootMidi ?? 60)) / 12);
      }

      // Reverse plays a flipped copy: positions mirror around the middle.
      const buf = p.reverse ? this._smpReversed() : full;
      if (p.reverse) { const na = 1 - b; b = 1 - a; a = na; }
      let la = clamp(p.loopStart, 0, 1), lb = clamp(p.loopEnd, 0, 1);
      if (lb - la < 0.005) lb = Math.min(1, la + 0.005);
      if (p.reverse) { const t2 = 1 - lb; lb = 1 - la; la = t2; }

      peak = Math.pow(vel, 1.5) * clamp(p.gain ?? 1, 0, 2);

      if (p.stretchOn) {
        // Granular engine: pitch (rate) and time (stretch) are independent.
        // Keys shift the pitch without changing duration; `stretch` slows or
        // speeds the sample without touching its pitch.
        const facade = this._spawnGranular(ctx, t, gain, buf, {
          a, b, la, lb, loopOn,
          pitch: rate,
          stretch: clamp(p.stretch ?? 1, 0.25, 4),
          grain: clamp(p.grain ?? 0.09, 0.03, 0.2),
        });
        sources.push({ node: facade, baseDetune: 0 });
      } else {
        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.playbackRate.value = rate;
        if (src.detune) src.detune.value = this.bendCents;
        if (loopOn) {
          src.loop = true;
          src.loopStart = la * dur;
          src.loopEnd = lb * dur;
        }
        src.connect(gain);
        if (loopOn) src.start(t, a * dur);
        else src.start(t, a * dur, Math.max(0.005, (b - a) * dur / rate));
        sources.push({ node: src, baseDetune: 0 });
      }
    } else {
      if (!this.buffers || this.buffers.size === 0) return; // not loaded yet
      const found = this._nearestBuffer(midi);
      if (!found) return;
      peak = Math.pow(vel, 1.5);
      const src = ctx.createBufferSource();
      src.buffer = found.buffer;
      src.playbackRate.value = Math.pow(2, (midi - found.midi) / 12);
      if (src.detune) src.detune.value = this.bendCents; // missing on old webkit
      src.connect(gain);
      src.start(t);
      sources.push({ node: src, baseDetune: 0 });
    }

    const a = Math.max(0.001, s.adsr.a);
    gain.gain.setValueAtTime(0, t);
    gain.gain.linearRampToValueAtTime(peak, t + a);
    if (s.adsr.s < 0.999) {
      gain.gain.setTargetAtTime(peak * s.adsr.s, t + a, Math.max(0.015, s.adsr.d / 3));
    }

    const voice = { gain, sources, peak, startedAt: t, released: false, extra, oneShot };
    // samples sometimes end on their own (end of the sample)
    sources[0].node.onended = () => this._cleanup(voice);
    this.voices.set(inputMidi, voice);
    this._stealIfNeeded(voice);
    return midi;
  }

  noteOff(inputMidi) {
    if (this.s.engine === 'sfz') {
      if (!this.sfz) return;
      const midi = clamp(inputMidi + this.s.octave * 12 + this.s.transpose, 0, 127);
      // The sustain pedal holds SFZ notes the same way it holds the others.
      if (this.sustainOn) this.sfzSustained = (this.sfzSustained || new Set()).add(midi);
      else this.sfz.noteOff(midi);
      return;
    }
    const voice = this.voices.get(inputMidi);
    if (!voice) return;
    this.voices.delete(inputMidi);
    if (this.sustainOn) {
      this.sustained.add(voice);
      this.releasing.add(voice);
    } else if (voice.oneShot) {
      // One-shot: the sample plays to its end; onended does the cleanup.
      this.releasing.add(voice);
    } else {
      this._release(voice);
    }
  }

  setSustain(on) {
    this.sustainOn = on;
    if (!on) {
      this.sustained.forEach(v => { if (!v.released) this._release(v); });
      this.sustained.clear();
      // Notes the pedal was holding on the SFZ side.
      if (this.sfz && this.sfzSustained) {
        this.sfzSustained.forEach(m => this.sfz.noteOff(m));
        this.sfzSustained.clear();
      }
    }
  }

  setBend(semitones) {
    this.bendCents = semitones * 100;
    if (this.sfz) this.sfz.setBend(this.bendCents);
    const t = this.engine.now;
    const apply = v => v.sources.forEach(src => {
      if (src.node.detune) {
        src.node.detune.setTargetAtTime(src.baseDetune + this.bendCents, t, 0.01);
      }
    });
    this.voices.forEach(apply);
    this.releasing.forEach(apply);
  }

  allNotesOff() {
    this.voices.forEach(v => this._release(v, 0.06));
    this.voices.clear();
    this.sustained.forEach(v => { if (!v.released) this._release(v, 0.06); });
    this.sustained.clear();
    if (this.sfz) this.sfz.allNotesOff();
    if (this.sfzSustained) this.sfzSustained.clear();
  }

  activeCount() {
    return this.voices.size + this.releasing.size
      + (this.sfz ? this.sfz.activeCount() : 0);
  }

  // ---------------------------------------------------------------- analog
  //
  // Voice layout (built fresh for every note):
  //
  //   [unison saws] ─┐
  //   [osc 2]        ├─→ (drive) ─→ [voice filter] ─→ gain ─→ instance input
  //   [sub osc]     ─┘                    ↑
  //                              filter envelope + LFO
  //
  // The module-wide filter still sits after `gain`; this per-voice one is what
  // makes plucks, acid lines and wobbles possible — a shared filter cannot
  // reopen for each new note.
  _buildAnalogVoice(ctx, t, midi, vel, gain, sources, extra) {
    const s = this.s;
    const syn = s.synth || {};
    const freq = midiToFreq(midi);
    const uni = clamp(Math.round(syn.unison || 1), 1, 7);

    // Stacked saws sum nearly in phase, so the more voices the lower the peak
    // must be to keep the limiter out of it.
    const stack = uni + (syn.osc2 && syn.osc2 !== 'off' ? 1 : 0) + (syn.sub > 0 ? 1 : 0);
    let peak = Math.pow(vel, 1.5) * 0.18 / Math.sqrt(Math.max(1, stack / 2));

    // Where the oscillators land: straight into the filter when there is one.
    const useFilter = (syn.fEnvAmt || 0) > 0 || (syn.lfoAmt > 0 && syn.lfoTarget === 'filter');
    const drive = clamp(syn.drive || 0, 0, 1);

    let head = gain;                       // node the oscillators feed into
    let voiceFilter = null;

    if (useFilter) {
      voiceFilter = ctx.createBiquadFilter();
      voiceFilter.type = 'lowpass';
      voiceFilter.Q.value = 0.5 + Math.pow(clamp(s.filterRes, 0, 1), 2) * 22;
      voiceFilter.connect(head);
      extra.nodes.push(voiceFilter);
      head = voiceFilter;
    }

    if (drive > 0) {
      const shaper = ctx.createWaveShaper();
      shaper.curve = makeDriveCurve(drive);
      shaper.oversample = '2x';
      // Saturation raises the perceived level a lot: compensate.
      const trim = ctx.createGain();
      trim.gain.value = 1 / (1 + drive * 2.2);
      shaper.connect(trim);
      trim.connect(head);
      extra.nodes.push(shaper, trim);
      head = shaper;
    }

    // --- oscillator 1: unison stack ---------------------------------------
    // Detune spreads symmetrically around the note; odd stacks keep one voice
    // dead centre so the pitch stays defined.
    const maxCents = 4 + (syn.uniDetune || 0) * 46;
    const width = clamp(syn.uniSpread ?? 0.7, 0, 1);
    for (let i = 0; i < uni; i++) {
      const pos = uni === 1 ? 0 : (i / (uni - 1)) * 2 - 1;   // -1..1
      const cents = pos * maxCents + (uni > 1 ? 0 : (s.detune || 0) / 2);
      const osc = ctx.createOscillator();
      osc.type = s.wave;
      osc.frequency.value = freq;
      osc.detune.value = cents + this.bendCents;
      // Spread the stack across the stereo field (skipped on old webkit).
      if (uni > 1 && width > 0 && ctx.createStereoPanner) {
        const p = ctx.createStereoPanner();
        p.pan.value = pos * width;
        osc.connect(p);
        p.connect(head);
        extra.nodes.push(p);
      } else {
        osc.connect(head);
      }
      osc.start(t);
      sources.push({ node: osc, baseDetune: cents });
      extra.stoppables.push(osc);
    }

    // --- oscillator 2 -------------------------------------------------------
    if (syn.osc2 && syn.osc2 !== 'off') {
      const o2 = ctx.createOscillator();
      o2.type = syn.osc2;
      o2.frequency.value = freq;
      const semi = (syn.osc2Semi || 0) * 100;
      o2.detune.value = semi + this.bendCents;
      const g2 = ctx.createGain();
      g2.gain.value = clamp(syn.osc2Mix ?? 0.5, 0, 1);
      o2.connect(g2);
      g2.connect(head);
      o2.start(t);
      sources.push({ node: o2, baseDetune: semi });
      extra.nodes.push(g2);
      extra.stoppables.push(o2);
    }

    // --- sub oscillator (one octave down) ----------------------------------
    if ((syn.sub || 0) > 0) {
      const sub = ctx.createOscillator();
      sub.type = syn.subWave === 'square' ? 'square' : 'sine';
      sub.frequency.value = freq / 2;
      sub.detune.value = this.bendCents;
      const gs = ctx.createGain();
      gs.gain.value = clamp(syn.sub, 0, 1) * 0.9;
      sub.connect(gs);
      // The sub carries the weight: keep it out of the drive/filter path so it
      // stays clean and solid, which is exactly how hardware basses do it.
      gs.connect(gain);
      sub.start(t);
      sources.push({ node: sub, baseDetune: 0 });
      extra.nodes.push(gs);
      extra.stoppables.push(sub);
    }

    // --- filter envelope ----------------------------------------------------
    if (voiceFilter) {
      const base = 40 * Math.pow(18000 / 40, clamp(s.filterCut, 0, 1));
      const amt = clamp(syn.fEnvAmt || 0, 0, 1);
      // The envelope opens the cutoff up to ~5 octaves above the base setting.
      const top = Math.min(18000, base * Math.pow(2, amt * 5));
      const sus = base + (top - base) * clamp(syn.fSustain ?? 0.2, 0, 1);
      const fa = Math.max(0.001, syn.fAttack ?? 0.005);
      const fd = Math.max(0.01, syn.fDecay ?? 0.25);
      const f = voiceFilter.frequency;
      f.setValueAtTime(base, t);
      f.linearRampToValueAtTime(top, t + fa);
      f.setTargetAtTime(sus, t + fa, fd / 3);
    }

    // --- LFO ----------------------------------------------------------------
    const lfoAmt = clamp(syn.lfoAmt || 0, 0, 1);
    if (lfoAmt > 0) {
      const lfo = ctx.createOscillator();
      lfo.type = 'sine';
      lfo.frequency.value = clamp(syn.lfoRate || 5, 0.05, 40);
      const depth = ctx.createGain();
      const target = syn.lfoTarget || 'filter';
      if (target === 'filter' && voiceFilter) {
        // Modulate the cutoff in Hz, scaled to the current cutoff so the
        // movement stays musical whatever the base setting.
        const base = 40 * Math.pow(18000 / 40, clamp(s.filterCut, 0, 1));
        depth.gain.value = base * lfoAmt * 1.5;
        depth.connect(voiceFilter.frequency);
      } else if (target === 'pitch') {
        depth.gain.value = lfoAmt * 50;               // ±50 cents max
        sources.forEach(src => { if (src.node.detune) depth.connect(src.node.detune); });
      } else {
        depth.gain.value = lfoAmt * 0.5;              // tremolo
        depth.connect(gain.gain);
      }
      lfo.connect(depth);
      lfo.start(t);
      extra.nodes.push(depth);
      extra.stoppables.push(lfo);
    }

    // --- glide (portamento) -------------------------------------------------
    const glide = syn.glide || 0;
    if (glide > 0 && this._lastFreq) {
      sources.forEach(src => {
        if (!src.node.frequency) return;
        const targetF = src.node.frequency.value;
        const ratio = targetF / freq;                  // keeps sub/osc2 offsets
        src.node.frequency.setValueAtTime(this._lastFreq * ratio, t);
        src.node.frequency.exponentialRampToValueAtTime(targetF, t + glide);
      });
    }
    this._lastFreq = freq;

    return peak;
  }

  _nearestBuffer(midi) {
    const exact = this.buffers.get(midi);
    if (exact) return { midi, buffer: exact };
    for (let d = 1; d <= 36; d++) {
      const lo = this.buffers.get(midi - d);
      if (lo) return { midi: midi - d, buffer: lo };
      const hi = this.buffers.get(midi + d);
      if (hi) return { midi: midi + d, buffer: hi };
    }
    return null;
  }

  _release(voice, forcedRelease) {
    if (voice.released) return;
    voice.released = true;
    this.releasing.add(voice);
    const t = this.engine.now;
    const r = forcedRelease ?? Math.max(0.02, this.s.adsr.r);
    const g = voice.gain.gain;
    if (typeof g.cancelAndHoldAtTime === 'function') {
      g.cancelAndHoldAtTime(t);
    } else {
      const current = g.value;
      g.cancelScheduledValues(t);
      g.setValueAtTime(current, t);
    }
    g.setTargetAtTime(0, t, Math.max(0.008, r / 4));
    const stopAt = t + r + 0.1;
    voice.sources.forEach(src => {
      try { src.node.stop(stopAt); } catch { /* already stopped */ }
    });
    // LFOs live outside `sources` (they are modulators, not audio sources) and
    // would otherwise keep running forever.
    if (voice.extra) {
      voice.extra.stoppables.forEach(n => {
        try { n.stop(stopAt); } catch { /* already stopped */ }
      });
    }
  }

  _cleanup(voice) {
    this.releasing.delete(voice);
    this.sustained.delete(voice);
    try { voice.gain.disconnect(); } catch { /* already done */ }
    // Per-voice analog nodes (filter, shaper, panners, LFO gain): without this
    // every note would leak a small graph for the lifetime of the page.
    if (voice.extra) {
      voice.extra.nodes.forEach(n => {
        try { n.disconnect(); } catch { /* already done */ }
      });
    }
    // in case the voice ended by itself (sample ran out) with no noteOff
    for (const [k, v] of this.voices) {
      if (v === voice) { this.voices.delete(k); break; }
    }
  }

  // Steals a voice when polyphony overflows, least audible first:
  // 1) release tails, 2) oldest voice held by the sustain pedal,
  // 3) oldest held key — never the note that was just pressed.
  _stealIfNeeded(newVoice) {
    if (this.activeCount() <= MAX_VOICES) return;
    const t = this.engine.now;

    let tail = null;
    for (const v of this.releasing) {
      if (this.sustained.has(v)) continue;
      if (!tail || v.startedAt < tail.startedAt) tail = v;
    }
    if (tail) {
      tail.gain.gain.setTargetAtTime(0, t, 0.008);
      tail.sources.forEach(src => { try { src.node.stop(t + 0.05); } catch { /* already stopped */ } });
      return;
    }

    let oldSus = null;
    for (const v of this.sustained) {
      if (v.released) continue;
      if (!oldSus || v.startedAt < oldSus.startedAt) oldSus = v;
    }
    if (oldSus) {
      this.sustained.delete(oldSus);
      this._release(oldSus, 0.03);
      return;
    }

    let oldest = null, oldestKey = null;
    for (const [k, v] of this.voices) {
      if (v === newVoice) continue;
      if (!oldest || v.startedAt < oldest.startedAt) { oldest = v; oldestKey = k; }
    }
    if (oldest) {
      this.voices.delete(oldestKey);
      this._release(oldest, 0.03);
    }
  }

  dispose() {
    this.allNotesOff();
    const nodes = [this.input, this.filter, this.volGain, this.panner,
      this.muteGain, this.out, this.revSend, this.delSend];
    setTimeout(() => nodes.forEach(n => { try { n && n.disconnect(); } catch { /* ok */ } }),
      (this.s.adsr.r + 0.3) * 1000);
  }
}
