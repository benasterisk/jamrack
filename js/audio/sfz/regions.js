// Region selection: which regions of an SFZ instrument answer a given note.
//
// The rule that makes SFZ different from a one-sample-per-note bank: matching
// is a CONJUNCTION and **every** surviving region sounds. That is precisely how
// velocity layers, multi-mic kits and crossfades work — several regions play at
// once, some of them at zero gain. Picking a single "best" region would break
// all of it.

/** Round-robin counters, kept per (choke group, key) rather than globally. */
export class SeqState {
  constructor() {
    this.counters = new Map();
    this.lastKeyswitch = null;
  }

  /** Advances and returns the round-robin step for this note. */
  next(group, key) {
    const k = `${group}|${key}`;
    const n = (this.counters.get(k) || 0) + 1;
    this.counters.set(k, n);
    return n;
  }

  reset() {
    this.counters.clear();
  }
}

/**
 * Regions triggered by a note-on.
 *
 * @param regions  parsed regions (see parser.js)
 * @param note     MIDI note
 * @param vel      MIDI velocity 1..127
 * @param seq      SeqState instance (round robin + keyswitch memory)
 * @param opts     { trigger: 'attack' | 'release' }
 */
export function matchRegions(regions, note, vel, seq, opts = {}) {
  const wantTrigger = opts.trigger || 'attack';
  // One random draw per note-on, tested against every region, so that
  // lorand/hirand ranges partition the probability space correctly.
  const roll = Math.random();

  const out = [];
  for (const r of regions) {
    if (note < r.lokey || note > r.hikey) continue;
    if (vel < r.lovel || vel > r.hivel) continue;
    if (roll < r.loRand || roll >= r.hiRand) continue;
    if (r.end === -1) continue;                 // silent region (choke helper)

    // trigger=attack is the default; release regions only fire on note-off.
    const trig = r.trigger === 'first' || r.trigger === 'legato' ? 'attack' : r.trigger;
    const wanted = wantTrigger === 'release' ? ['release', 'release_key'] : ['attack'];
    if (!wanted.includes(trig)) continue;

    // Keyswitches: a region bound to sw_last only plays when that key was the
    // last one pressed in the switch range. sw_default gives the initial state,
    // without which such an instrument would be silent until the user finds it.
    if (r.swLast !== null) {
      const current = seq.lastKeyswitch !== null ? seq.lastKeyswitch : r.swDefault;
      if (current !== r.swLast) continue;
    }
    out.push(r);
  }

  // Round robin is applied last: it must not consume a step for regions that
  // were already filtered out.
  const rr = out.filter(r => r.seqLength > 1);
  if (!rr.length) return out;

  const steps = new Map();     // group -> step drawn for this note-on
  return out.filter(r => {
    if (r.seqLength <= 1) return true;
    let step = steps.get(r.group);
    if (step === undefined) {
      step = ((seq.next(r.group, note) - 1) % r.seqLength) + 1;
      steps.set(r.group, step);
    }
    return step === r.seqPosition;
  });
}

/** True when the note falls inside the instrument's keyswitch range. */
export function isKeyswitch(regions, note) {
  return regions.some(r =>
    r.swLokey !== null && r.swHikey !== null
    && note >= r.swLokey && note <= r.swHikey);
}

/**
 * Playback gain for a region at a given velocity.
 *
 * Mirrors sfizz: the default curve is (v/127)², blended by amp_veltrack, then
 * multiplied by the velocity crossfades and the region's own `volume` in dB.
 */
export function regionGain(region, vel) {
  const v = Math.max(0, Math.min(127, vel)) / 127;
  const track = region.ampVeltrack / 100;          // -1..1

  let g = v * v;
  g = Math.abs(track) * (1 - g);
  g = track < 0 ? g : 1 - g;

  // Velocity crossfades: fade in below xfin_hivel, fade out above xfout_lovel.
  if (region.xfinHivel > region.xfinLovel) {
    if (vel <= region.xfinLovel) g = 0;
    else if (vel < region.xfinHivel) {
      g *= Math.sqrt((vel - region.xfinLovel) / (region.xfinHivel - region.xfinLovel));
    }
  }
  if (region.xfoutHivel > region.xfoutLovel) {
    if (vel >= region.xfoutHivel) g = 0;
    else if (vel > region.xfoutLovel) {
      g *= Math.sqrt(1 - (vel - region.xfoutLovel) / (region.xfoutHivel - region.xfoutLovel));
    }
  }

  return g * Math.pow(10, region.volume / 20);
}

/**
 * Playback rate for a region at a given note.
 * `entrySampleRate` is the file's real rate; buffers decoded at another rate
 * must be compensated, otherwise everything plays sharp or flat.
 */
export function playbackRate(region, note, entrySampleRate, ctxSampleRate) {
  const cents = region.keytrack * (note - region.keycenter)
    + region.tune + 100 * region.transpose;
  let rate = Math.pow(2, cents / 1200);
  if (entrySampleRate && ctxSampleRate && entrySampleRate !== ctxSampleRate) {
    // decodeAudioData already resampled the audio to the context rate, so the
    // buffer plays at the right pitch at rate 1 — no correction needed here.
    // (Kept explicit so the intent is clear to the next reader.)
  }
  return rate;
}

/**
 * Converts a frame index expressed in the FILE's sample rate into the decoded
 * buffer's timeline (seconds). This is the fix for the classic Web Audio SFZ
 * bug: decodeAudioData resamples, so raw frame indices no longer line up.
 */
export function frameToSeconds(frame, entry) {
  const fileRate = entry.sampleRate || entry.buffer.sampleRate;
  return frame / fileRate;
}
