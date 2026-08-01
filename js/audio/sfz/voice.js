// Turns a matched SFZ region into sounding audio.
//
// One region = one voice:
//   AudioBufferSource -> [filter if cutoff] -> gain (amp EG x velocity) -> out
//
// A single note may spawn several voices at once (velocity layers, multiple
// mics), so they are grouped by note on the engine side.

import { regionGain, playbackRate, frameToSeconds } from './regions.js';

// Below this the release is inaudible but the click is not: SFZ's 1 ms spec
// default is what makes strict players click on real banks.
const MIN_RELEASE = 0.02;

export class SfzVoice {
  /**
   * @param ctx      AudioContext
   * @param dest     node to connect to
   * @param region   parsed region
   * @param entry    { buffer, sampleRate, loopStart, loopEnd } from the loader
   * @param note     MIDI note
   * @param vel      MIDI velocity 1..127
   * @param bendCents current pitch bend, in cents
   */
  constructor(ctx, dest, region, entry, note, vel, bendCents = 0) {
    this.ctx = ctx;
    this.region = region;
    this.released = false;
    this.startedAt = ctx.currentTime;

    const t = ctx.currentTime + (region.delay || 0);
    const src = ctx.createBufferSource();
    src.buffer = entry.buffer;
    src.playbackRate.value = playbackRate(region, note, entry.sampleRate, ctx.sampleRate);
    if (src.detune) src.detune.value = bendCents;   // absent on old webkit

    // --- looping -----------------------------------------------------------
    // SFZ gives frames in the FILE's rate; AudioBufferSourceNode wants seconds.
    const wantsLoop = region.loopMode === 'loop_continuous'
      || region.loopMode === 'loop_sustain'
      || (!region.loopMode && entry.loopStart != null);
    if (wantsLoop) {
      const ls = region.loopStart != null ? region.loopStart : entry.loopStart;
      const le = region.loopEnd != null ? region.loopEnd : entry.loopEnd;
      if (ls != null && le != null && le > ls) {
        src.loop = true;
        src.loopStart = frameToSeconds(ls, entry);
        src.loopEnd = frameToSeconds(le, entry);
      }
    }
    this.loopSustain = region.loopMode === 'loop_sustain';
    // one_shot ignores note-off entirely: standard for percussion.
    this.oneShot = region.loopMode === 'one_shot';

    // --- gain + amplitude envelope ----------------------------------------
    const gain = ctx.createGain();
    const peak = regionGain(region, vel);
    const eg = region.ampeg;
    const a = Math.max(0.0005, eg.attack);
    const g = gain.gain;
    const t0 = t + eg.delay;

    g.setValueAtTime(0, t0);
    g.linearRampToValueAtTime(peak, t0 + a);        // attack is linear in amplitude
    const holdEnd = t0 + a + eg.hold;
    if (eg.hold > 0) g.setValueAtTime(peak, holdEnd);
    if (eg.sustain < 0.999) {
      // Decay towards the sustain level. setTargetAtTime is exponential, which
      // is what SFZ specifies (linear in dB).
      const target = peak * eg.sustain;
      g.setTargetAtTime(target, holdEnd, Math.max(0.01, eg.decay / 3));
    }
    this.peak = peak;
    this.gain = gain;

    // --- optional per-region filter ---------------------------------------
    let head = gain;
    this.filter = null;
    if (region.cutoff != null && region.cutoff > 0) {
      const f = ctx.createBiquadFilter();
      f.type = filterType(region.filType);
      f.frequency.value = Math.min(region.cutoff, ctx.sampleRate / 2 - 100);
      // SFZ resonance is in dB; BiquadFilter wants Q.
      f.Q.value = Math.max(0.0001, Math.pow(10, region.resonance / 20));
      f.connect(gain);
      head = f;
      this.filter = f;
    }

    // --- panning ----------------------------------------------------------
    // SFZ uses a constant-power cosine law; StereoPanner's law differs, but the
    // audible difference is small and the node is far cheaper than a manual
    // splitter/merger pair per voice.
    let tail = gain;
    if (region.pan && ctx.createStereoPanner) {
      const p = ctx.createStereoPanner();
      p.pan.value = Math.max(-1, Math.min(1, region.pan / 100));
      gain.connect(p);
      tail = p;
      this.panner = p;
    }
    tail.connect(dest);

    src.connect(head);
    // offset is a frame index in the file's rate, like the loop points.
    const offset = region.offset > 0 ? frameToSeconds(region.offset, entry) : 0;
    try {
      src.start(t, offset);
    } catch {
      src.start(t);                                  // offset past end of buffer
    }
    this.src = src;
    this.onended = null;
    src.onended = () => { if (this.onended) this.onended(this); };
  }

  setBend(cents) {
    if (this.src.detune) {
      this.src.detune.setTargetAtTime(cents, this.ctx.currentTime, 0.01);
    }
  }

  /** Note-off: runs the release stage, unless the region is one_shot. */
  release(forced) {
    if (this.released) return;
    // A one_shot region plays to completion and ignores note-off — that is what
    // makes drum hits sound whole instead of being cut short.
    if (this.oneShot && forced === undefined) return;
    this.released = true;

    const t = this.ctx.currentTime;
    const r = Math.max(MIN_RELEASE, forced ?? this.region.ampeg.release);
    const g = this.gain.gain;
    if (typeof g.cancelAndHoldAtTime === 'function') {
      g.cancelAndHoldAtTime(t);
    } else {
      const now = g.value;
      g.cancelScheduledValues(t);
      g.setValueAtTime(now, t);
    }
    g.setTargetAtTime(0, t, r / 4);

    // loop_sustain leaves the loop at note-off and plays the tail out.
    if (this.loopSustain) this.src.loop = false;

    try { this.src.stop(t + r + 0.05); } catch { /* already stopped */ }
  }

  /** Immediate stop for choke groups, with a tiny fade to avoid a click. */
  choke(fast = true) {
    if (fast) {
      const t = this.ctx.currentTime;
      const g = this.gain.gain;
      if (typeof g.cancelAndHoldAtTime === 'function') g.cancelAndHoldAtTime(t);
      g.setTargetAtTime(0, t, 0.004);
      try { this.src.stop(t + 0.03); } catch { /* already stopped */ }
      this.released = true;
    } else {
      this.release();
    }
  }

  disconnect() {
    try { this.src.disconnect(); } catch { /* done */ }
    try { this.gain.disconnect(); } catch { /* done */ }
    if (this.filter) { try { this.filter.disconnect(); } catch { /* done */ } }
    if (this.panner) { try { this.panner.disconnect(); } catch { /* done */ } }
  }
}

/** Maps an SFZ filter type onto the closest BiquadFilterNode type. */
function filterType(sfzType) {
  if (!sfzType) return 'lowpass';
  if (sfzType.startsWith('hpf')) return 'highpass';
  if (sfzType.startsWith('bpf')) return 'bandpass';
  if (sfzType.startsWith('brf')) return 'notch';
  if (sfzType.startsWith('apf')) return 'allpass';
  if (sfzType.startsWith('pkf') || sfzType.startsWith('peq')) return 'peaking';
  if (sfzType.startsWith('lsh')) return 'lowshelf';
  if (sfzType.startsWith('hsh')) return 'highshelf';
  return 'lowpass';
}
