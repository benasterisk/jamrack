// Monophonic guitar → note tracker: the DSP behind the GUITAR section.
//
// Pure JavaScript over Float32Array blocks — no DOM, no Web Audio objects — so
// the exact same code runs inside the AudioWorklet (worklet.js), in the
// main-thread ScriptProcessor fallback (js/input/guitar.js) and in the Node
// tests (test/guitar-tracker.test.mjs).
//
// Per sample:  DC block → level (RMS) and high-band level (pick transients)
//              → 2× low-pass 3 kHz → ÷D (≈24 kHz, where every pitch lives)
// Per frame (64 decimated samples ≈ 2.7 ms):
//   1. onset: the level, or the high band alone, rises by `riseDb` over
//      where it was 8-16 ms ago (a pick is broadband; a sustaining string
//      has almost nothing above 2.5 kHz, so repicking a loud note still
//      shows up clearly in the high band)
//   2. pitch: YIN (de Cheveigné & Kawahara, 2002) with parabolic
//      interpolation and a guard against octave-up errors. A pluck is
//      identified over the full guitar range with a long window; a sounding
//      note is then followed with a short window (3 periods) around it,
//      which reacts to hammer-ons, pull-offs, slides and bends in a few ms
//   3. a small state machine turning (level, pitch, confidence) into
//      note-on / note-off / pitch-bend events
//
// Latency is bounded by physics, not by this code: a pitch cannot be named
// before 1-2 periods of it have been heard (12-24 ms on the low E) and the
// pick transient (noise) must be skipped. Expect ~10 ms on the high strings
// and 20-30 ms on the low ones — the same ballpark as commercial trackers
// working from a standard (non-hexaphonic) pickup.

export const DEFAULT_PARAMS = {
  sens: 0.5,       // 0..1 onset sensitivity: gate level and required rise
  release: 0.5,    // 0..1 how far a note may decay before note-off (15..45 dB)
  dyn: 0.7,        // 0..1 velocity dynamics (0 = every note at full velocity)
  bend: true,      // true: pitch bend follows bends/vibrato; false: chromatic
  octave: 0,       // added to the detected note
  transpose: 0,    // semitones added to the detected note
  fmin: 70,        // Hz — drop D is 73.4; set ~30 for a bass guitar
  fmax: 1400,      // Hz — 24th fret of the high E is 1319
  a4: 440,         // Hz — tuning reference
};

// Analysis frame = HOP decimated samples (≈2.7 ms at 24 kHz)
const HOP = 64;
const METER_EVERY = 8;       // frames between meter messages (~21 ms)
const VEL_LO_DB = -48, VEL_HI_DB = -12;   // RMS dBFS → velocity 0..1

/**
 * Decision thresholds. Exported (and mutable) so the evaluation scripts can
 * ablate them; the values are the ones tuned on synthetic strings and on
 * real guitar (GuitarSet solo takes, see test/guitarset-eval.mjs).
 */
export const TUNING = {
  YIN_THRESHOLD: 0.15,  // first dip below this = the period
  CONF_ON: 0.85,        // confidence (1 - dip depth) to start a note
  CONF_TRACK: 0.7,      // confidence to keep tracking a sounding note
  JUMP: 0.5,            // semitones within JUMP_SPAN frames = hammer-on, not a bend
  JUMP_SPAN: 3,         // frames (~8 ms): faster than any finger can bend
  HOLD: 6,              // frames (~16 ms) of peak-hold for the level: one period of the low E
  BEND_RANGE: 2,        // semitones (what the synth engines accept)
  BEND_RETRIG: 2.35,    // beyond this the note is re-struck (slide)
  REFRACTORY: 10,       // frames (~27 ms): one onset per pluck
  PEND_MAX: 20,         // frames (~53 ms) to find a pitch after an onset
  LOWCONF_MAX: 12,      // frames (~32 ms) without a period = string muted
  SOFT_AGREE: 4,        // frames of stable pitch for an onset-less (swell) note
  ONSET_AGREE: 3,       // frames agreeing on the pitch before a plucked note starts
  LEGATO_AGREE: 4,      // ... before a hammer-on / pull-off / slide retriggers (~11 ms)
  OCTAVE_AGREE: 6,      // ... for an octave change without a pluck (rare: usually two strings)
  GRID: 0.35,           // semitones: a note must sit this close to the grid to be named
  SAME_NOTE_HOLDOFF: 19,// frames (~50 ms): a note cannot be re-picked right after it started
  EARLY_FIX: 22,        // frames (~60 ms): a different stable pitch this soon after a pluck
                        // is a wrong note to correct, not a bend to follow
  HARMONIC_GUARD: 1,    // 1: check 1.5x/2x/3x the period for a clearly deeper dip
};
const T = TUNING;

const dB = x => 20 * Math.log10(Math.max(x, 1e-9));
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const midiToHz = m => 440 * Math.pow(2, (m - 69) / 12);

/** RBJ-cookbook biquad (low-pass or high-pass), transposed direct form II. */
class Biquad {
  constructor(type, sr, fc, q) {
    const w0 = 2 * Math.PI * fc / sr;
    const cos = Math.cos(w0), alpha = Math.sin(w0) / (2 * q);
    const a0 = 1 + alpha;
    if (type === 'lp') {
      this.b0 = (1 - cos) / 2 / a0; this.b1 = (1 - cos) / a0;
    } else {
      this.b0 = (1 + cos) / 2 / a0; this.b1 = -(1 + cos) / a0;
    }
    this.b2 = this.b0;
    this.a1 = -2 * cos / a0;
    this.a2 = (1 - alpha) / a0;
    this.z1 = 0; this.z2 = 0;
  }
  run(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
}

export class GuitarTracker {
  constructor(sampleRate, params = {}) {
    this.sr = sampleRate;
    // Decimate to ~24 kHz: every pitch of interest sits below 1.5 kHz, and the
    // cost of YIN grows with the square of the sample rate.
    this.D = Math.max(1, Math.round(sampleRate / 24000));
    this.srD = sampleRate / this.D;
    this.frameMs = HOP / this.srD * 1000;
    this.p = { ...DEFAULT_PARAMS };
    this._derive();
    this._setupYin();
    this.setParams(params);

    this.lp1 = new Biquad('lp', sampleRate, 3000, 0.7);
    this.lp2 = new Biquad('lp', sampleRate, 3000, 0.7);
    this.hp = new Biquad('hp', sampleRate, 2500, 0.7);
    this.dcX = 0; this.dcY = 0;
    this.decPhase = 0;
    this.since = 0;
    this.sumSq = 0; this.sumHf = 0; this.nSq = 0;

    this.frame = 0;
    this.rmsHist = new Float32Array(16).fill(-200);
    this.hfHist = new Float32Array(16).fill(-200);
    this.lastOnset = -1e9;
    this.lastOnsetPeak = -200;
    this.note = -1;          // sounding note (raw, before octave/transpose) or -1
    this.noteVel = 0;
    this.noteK = -1e9;       // frame of the note-on
    this.noteWhy = '';       // 'pluck' | 'legato' | 'swell' | 'fix'
    this.peakDb = -200;      // loudest frame of the sounding note
    this.pend = null;        // onset waiting for its pitch
    this.lowConf = 0; this.lowLevel = 0;
    this.legCand = -1; this.legAgree = 0;
    this.softCand = -1; this.softAgree = 0;
    this.armed = true;       // may an onset-less note start?
    this.bendOut = 0;        // last pitch bend sent (semitones)
    this.bendSmooth = 0;
    this.midiHist = new Float32Array(8).fill(NaN);   // recent confident pitches
    this.lastLatMs = 0;      // onset → note-on time of the last note
    this.lastRmsDb = -200;
    this._out = [];
  }

  /** Changes any subset of DEFAULT_PARAMS. */
  setParams(params) {
    const before = this.p.fmin + ':' + this.p.fmax;
    for (const k of Object.keys(params)) {
      if (k in DEFAULT_PARAMS && params[k] !== undefined) this.p[k] = params[k];
    }
    this._derive();
    if (this.p.fmin + ':' + this.p.fmax !== before) this._setupYin();
  }

  _derive() {
    const p = this.p;
    this.gateDb = -36 - 24 * clamp(p.sens, 0, 1);   // -36 .. -60 dBFS
    this.riseDb = 12 - 6 * clamp(p.sens, 0, 1);     // 12 .. 6 dB
    this.relDb = 15 + 30 * clamp(p.release, 0, 1);  // 15 .. 45 dB below the peak
    this.shift = 12 * (p.octave | 0) + (p.transpose | 0);
  }

  _setupYin() {
    const p = this.p;
    this.tmin = Math.max(2, Math.floor(this.srD / p.fmax));
    this.tmax = Math.ceil(this.srD / p.fmin);
    // The integration window must cover at least one period of the lowest
    // note; 1.5 periods keeps the dips sharp while the note decays.
    this.W = Math.max(512, Math.round(1.5 * this.tmax));
    const need = this.W + this.tmax;
    let size = 1024;
    while (size < 2 * need) size *= 2;
    this.ring = new Float32Array(size);
    this.mask = size - 1;
    this.wi = 0;
    this.win = new Float32Array(need);
    this.d = new Float32Array(this.tmax + 1);
    this.cmnd = new Float32Array(this.tmax + 1);
  }

  /**
   * Feeds a block of mono samples and returns the events it produced:
   *   { t: 'on', midi, vel, why }  { t: 'off', midi }  { t: 'bend', semis }
   *   (why: 'pluck' | 'legato' | 'swell' | 'fix' — how the note was decided)
   *   { t: 'meter', db, hz, midiF, conf, note, latMs }  (~50 per second)
   * The returned array is reused: consume it before the next call.
   */
  process(input) {
    const out = this._out;
    out.length = 0;
    const ring = this.ring, mask = this.mask, D = this.D;
    for (let i = 0; i < input.length; i++) {
      const x = input[i];
      // DC blocker (one pole at ~40 Hz)
      const y = x - this.dcX + 0.995 * this.dcY;
      this.dcX = x; this.dcY = y;
      this.sumSq += y * y; this.nSq++;
      const h = this.hp.run(y);
      this.sumHf += h * h;
      const f = this.lp2.run(this.lp1.run(y));
      if (++this.decPhase >= D) {
        this.decPhase = 0;
        ring[this.wi] = f;
        this.wi = (this.wi + 1) & mask;
        if (++this.since >= HOP) {
          this.since = 0;
          this._frame(out);
        }
      }
    }
    return out;
  }

  /** Releases a sounding note (when the input stops). Returns the events. */
  flush() {
    const out = this._out;
    out.length = 0;
    if (this.note >= 0) this._noteOff(out);
    this.pend = null;
    return out;
  }

  // ------------------------------------------------------------------ frame

  _frame(out) {
    const p = this.p;
    const n = Math.max(1, this.nSq);
    const rmsDb = dB(Math.sqrt(this.sumSq / n));
    const hfDb = dB(Math.sqrt(this.sumHf / n));
    this.sumSq = 0; this.sumHf = 0; this.nSq = 0;
    this.lastRmsDb = rmsDb;
    const k = ++this.frame;
    const H = this.rmsHist, HF = this.hfHist, HM = H.length - 1;
    // The level 8-16 ms ago: just before a pluck, after the previous one has
    // settled. Comparing with the immediate past would miss fast repicking.
    let ref = -200, hfRef = -200;
    for (let i = 3; i <= 6; i++) {
      ref = Math.max(ref, H[(k - i) & HM]);
      hfRef = Math.max(hfRef, HF[(k - i) & HM]);
    }
    H[k & HM] = rmsDb;
    HF[k & HM] = hfDb;
    // A frame is shorter than one period of the low strings, so the frame
    // RMS swings with the waveform: hold the peak over one period for every
    // decision about the sustained level.
    let lvl = rmsDb;
    for (let i = 1; i < T.HOLD; i++) lvl = Math.max(lvl, H[(k - i) & HM]);

    const gate = this.gateDb, rise = this.riseDb;
    let onset = false, strong = false;
    // One onset per pluck (the refractory period) — unless a louder attack
    // follows: the pick touching the string scratches a few ms before it
    // releases it, and that scratch must not mask the real attack.
    const free = k - this.lastOnset >= T.REFRACTORY || rmsDb >= this.lastOnsetPeak + 3;
    if (rmsDb > gate && rmsDb > ref - 6 && free) {
      // a pick transient is shorter than a frame and may straddle two
      const broad = rmsDb - ref, high = Math.max(hfDb, HF[(k - 1) & HM]) - hfRef;
      // The high band is nearly empty while a string sustains, so a pick
      // shows there with a smaller rise than in the full band.
      if (broad >= rise || (high >= rise - 3 && hfDb > gate - 12)) {
        onset = true;
        this.lastOnset = k;
        this.lastOnsetPeak = rmsDb;
        // A re-strike of the note already sounding needs a convincing attack,
        // or a finger squeak (all high band) would double-trigger it.
        strong = broad >= rise || high >= rise + 6;
      }
    }

    if (onset) {
      // A pluck: the note starts once its pitch is known. The velocity is the
      // loudest of the first frames (the pick transient peaks within ~10 ms).
      this.pend = { k, peak: rmsDb, cand: -1, agree: 0, strong };
      this.armed = true;
      this.legAgree = 0;
      this.softAgree = 0;
    }

    // ---- pitch (skipped in silence, so an idle rack costs nothing)
    let hz = 0, conf = 0, midiF = NaN;
    if (this.pend) {
      const r = this._pitchAtOnset();
      hz = r.hz; conf = r.conf;
      // A re-struck string breaks its own periodicity (the new pick restarts
      // it at an unrelated phase), which the window following the sounding
      // note sees as a collapse of confidence; a squeak leaves it intact.
      if (this.note >= 0 && k - this.pend.k <= 4 && !this.pend.strong) {
        if (this._track().conf < 0.6) this.pend.strong = true;
      }
    } else if (this.note < 0 && rmsDb > gate - 6) {
      const r = this._yin(this.tmin, this.tmax, this.W);
      hz = r.hz; conf = r.conf;
    } else if (this.note >= 0) {
      const r = this._track();
      hz = r.hz; conf = r.conf;
    }
    if (hz > 0) midiF = 69 + 12 * Math.log2(hz / p.a4);
    const confident = conf >= T.CONF_TRACK && !Number.isNaN(midiF);
    const m = confident ? Math.round(midiF) : -1;

    if (this.pend) {
      const pd = this.pend;
      if (k - pd.k <= 4) {
        pd.peak = Math.max(pd.peak, rmsDb);
        this.lastOnsetPeak = Math.max(this.lastOnsetPeak, rmsDb);
        // A re-struck string is damped by the pick first: a dip well under
        // the sustain, then a sharp rise. A squeak has no such dip. (The dip
        // is read over two frames so the waveform of a low note, whose
        // period is longer than a frame, cannot fake one.)
        let dip = 200;
        for (let i = 1; i <= 3; i++) dip = Math.min(dip, Math.max(H[(k - i) & HM], H[(k - i - 1) & HM]));
        if (rmsDb - dip >= rise && dip <= ref - 6) pd.strong = true;
      }
      // the very first frame is the pick transient: noise, not pitch
      // (a transient's estimate wanders between semitones; a note sits on the grid)
      if (k - pd.k >= 1 && conf >= T.CONF_ON && m >= 0 && lvl > gate && Math.abs(midiF - m) <= T.GRID) {
        pd.agree = (m === pd.cand) ? pd.agree + 1 : 1;
        pd.cand = m;
        if (pd.agree >= T.ONSET_AGREE) {
          if (m !== this.note || (pd.strong && k - this.noteK >= T.SAME_NOTE_HOLDOFF)) {
            this.lastLatMs = (k - pd.k + 1) * this.frameMs;
            this._noteOn(out, m, this._vel(pd.peak), Math.max(pd.peak, rmsDb), 'pluck');
          }
          this.pend = null;
        }
      } else if (k - pd.k > T.PEND_MAX) {
        this.pend = null;   // a knock or a scrape, not a note
      }
    } else if (this.note >= 0) {
      // ---- a note is sounding: follow it
      if (confident) {
        this.lowConf = 0;
        const dev = midiF - this.note;
        const onGrid = Math.abs(midiF - m) <= T.GRID;
        let jump = false;
        for (let i = 1; i <= T.JUMP_SPAN; i++) {
          const past = this.midiHist[(k - i) & 7];
          if (!Number.isNaN(past) && Math.abs(midiF - past) >= T.JUMP) jump = true;
        }
        // Right after a pluck, a stable pitch on another semitone means the
        // attack transient fooled the onset search: correct the note rather
        // than letting the pitch bend quietly absorb a wrong note.
        const early = this.noteWhy === 'pluck' && k - this.noteK <= T.EARLY_FIX;
        const octave = Math.abs(dev) >= 11.5 && Math.abs(dev) <= 12.5;
        if (m !== this.note && onGrid && (jump || early || !p.bend || Math.abs(dev) > T.BEND_RETRIG)) {
          // Hammer-on, pull-off or slide: a new note without a new pluck.
          // Several frames must agree so a transition cannot leave ghost notes.
          this.legAgree = (m === this.legCand) ? this.legAgree + 1 : 1;
          this.legCand = m;
          const need = octave ? T.OCTAVE_AGREE : T.LEGATO_AGREE;
          if (this.legAgree >= need) {
            this.lastLatMs = need * this.frameMs;
            this._noteOn(out, m, Math.max(this._vel(rmsDb), this.noteVel * 0.6), rmsDb, early ? 'fix' : 'legato');
          }
        } else {
          this.legAgree = 0;
          if (p.bend) this._bend(out, early ? clamp(dev, -0.5, 0.5) : dev);
        }
      } else if (++this.lowConf >= T.LOWCONF_MAX) {
        this._noteOff(out);   // no period left: the string was muted
      }
    } else if (this.armed && lvl > gate && conf >= T.CONF_ON && m >= 0 && Math.abs(midiF - m) <= T.GRID) {
      // ---- idle: a note that swelled in without a detectable attack
      // (volume pedal, very soft touch). Needs a longer agreement than a pluck.
      this.softAgree = (m === this.softCand) ? this.softAgree + 1 : 1;
      this.softCand = m;
      if (this.softAgree >= T.SOFT_AGREE) {
        this.lastLatMs = T.SOFT_AGREE * this.frameMs;
        this._noteOn(out, m, this._vel(rmsDb), rmsDb, 'swell');
      }
    } else {
      this.softAgree = 0;
    }

    // ---- level-based note-off, whatever else is going on
    if (this.note >= 0) {
      this.peakDb = Math.max(this.peakDb, lvl);
      if (lvl < Math.max(this.peakDb - this.relDb, gate - 6)) {
        if (++this.lowLevel >= 2) this._noteOff(out);
      } else {
        this.lowLevel = 0;
      }
    }
    // Silence re-arms the onset-less path (it is disarmed by every note-off,
    // otherwise a decayed string would retrigger the moment its note ends).
    if (lvl < gate - 3) this.armed = true;

    this.midiHist[k & 7] = confident ? midiF : NaN;

    // Optional per-frame hook for tuning (see test/trace.mjs); unset in production.
    if (this.onFrame) {
      this.onFrame({ k, rmsDb, hfDb, ref, hfRef, lvl, onset, strong, hz, conf, midiF, note: this.note, pend: !!this.pend });
    }

    if (k % METER_EVERY === 0) {
      out.push({
        t: 'meter', db: rmsDb, hz, midiF: confident ? midiF : NaN, conf,
        note: this.note >= 0 ? this.note + this.shift : -1, latMs: this.lastLatMs,
      });
    }
  }

  _vel(db) {
    const raw = clamp((db - VEL_LO_DB) / (VEL_HI_DB - VEL_LO_DB), 0, 1);
    return clamp(1 - this.p.dyn + this.p.dyn * raw, 0.05, 1);
  }

  _noteOn(out, m, vel, peakDb, why) {
    if (this.note >= 0) out.push({ t: 'off', midi: this.note + this.shift });
    this.noteK = this.frame;
    this.noteWhy = why;
    if (this.bendOut !== 0) out.push({ t: 'bend', semis: 0 });
    this.bendOut = 0; this.bendSmooth = 0;
    this.note = m;
    this.noteVel = vel;
    this.peakDb = peakDb;
    this.lowConf = 0; this.lowLevel = 0;
    this.legAgree = 0; this.softAgree = 0;
    this.midiHist.fill(NaN);
    out.push({ t: 'on', midi: m + this.shift, vel, why });
  }

  _noteOff(out) {
    out.push({ t: 'off', midi: this.note + this.shift });
    if (this.bendOut !== 0) out.push({ t: 'bend', semis: 0 });
    this.bendOut = 0; this.bendSmooth = 0;
    this.note = -1;
    this.armed = false;
    this.legAgree = 0;
  }

  _bend(out, dev) {
    // Light smoothing kills estimator jitter without lagging a real bend.
    this.bendSmooth += (dev - this.bendSmooth) * 0.35;
    const b = clamp(this.bendSmooth, -T.BEND_RANGE, T.BEND_RANGE);
    if (Math.abs(b - this.bendOut) > 0.03) {
      this.bendOut = b;
      out.push({ t: 'bend', semis: b });
    }
  }

  // -------------------------------------------------------------------- YIN

  /**
   * Pitch of a note that has just started. A fresh note is only visible to
   * the lags it is older than, so the shortest window that is confident wins:
   * the high strings are named after a few milliseconds instead of waiting
   * for the long window the low E needs. Each window searches only the lags
   * it can also verify at twice the period (the octave guard).
   */
  _pitchAtOnset() {
    const { tmin, tmax, W } = this;
    for (const w of [128, 256]) {
      if (w >= W) break;
      const tHi = Math.min(tmax, Math.round(w * 1.33));
      const r = this._yin(tmin, tHi, w, Math.floor(tHi / 2));
      if (r.conf >= T.CONF_ON) return r;
    }
    return this._yin(tmin, tmax, W);
  }

  /** Follows the sounding note: a short window (3 periods), ±14 semitones. */
  _track() {
    const tau0 = this.srD / midiToHz(this.note);
    return this._yin(
      Math.max(this.tmin, Math.floor(tau0 / 2.2)),
      Math.min(this.tmax, Math.ceil(tau0 * 2.2)),
      clamp(Math.round(3 * tau0), 96, this.W),
    );
  }

  /**
   * Period search: the newest W decimated samples are compared with the
   * same samples a lag earlier (this way a note is seen as soon as it is two
   * periods old). Dips of the cumulative-mean-normalised difference are
   * looked up between the lags tLo and searchHi; the difference is computed
   * up to tHi so the octave guard can look at twice the period.
   * Returns { hz, conf } — hz 0 when nothing periodic was found.
   */
  _yin(tLo, tHi, W, searchHi = tHi) {
    const win = this.win, d = this.d, cmnd = this.cmnd;
    const n = W + tHi;
    // the last n decimated samples, oldest first; the reference is the last W
    let idx = (this.wi - n) & this.mask;
    for (let i = 0; i < n; i++) { win[i] = this.ring[idx]; idx = (idx + 1) & this.mask; }

    // difference function (lags from 1: the normalisation needs them all)
    for (let tau = 1; tau <= tHi; tau++) {
      let sum = 0;
      const base = tHi - tau;
      for (let j = 0; j < W; j++) {
        const diff = win[tHi + j] - win[base + j];
        sum += diff * diff;
      }
      d[tau] = sum;
    }
    // cumulative mean normalised difference
    cmnd[0] = 1;
    let run = 0;
    for (let tau = 1; tau <= tHi; tau++) {
      run += d[tau];
      cmnd[tau] = run > 0 ? d[tau] * tau / run : 1;
    }
    // first dip below the threshold, walked down to its local minimum
    let tau = -1;
    for (let t = tLo; t < searchHi; t++) {
      if (cmnd[t] < T.YIN_THRESHOLD) {
        while (t + 1 <= tHi && cmnd[t + 1] < cmnd[t]) t++;
        tau = t;
        break;
      }
    }
    if (tau < 0) {
      // nothing periodic enough: report the best candidate with its (low) confidence
      let best = 1;
      for (let t = tLo; t <= searchHi; t++) if (cmnd[t] < best) { best = cmnd[t]; tau = t; }
      if (tau < 0) return { hz: 0, conf: 0 };
    }
    // Harmonic guard: a strong 2nd or 3rd harmonic (or the attack transient)
    // can dip below the threshold at 1/2, 1/3 or 2/3 of the period. The true
    // period then shows a clearly deeper dip at 2x, 3x or 1.5x the lag.
    let guardTau = tau, guardBest = cmnd[tau] - 0.1;
    for (const mult of (T.HARMONIC_GUARD ? [1.5, 2, 3] : [2])) {
      const c = Math.round(mult * tau);
      if (c + 2 > tHi) continue;
      for (let t = c - 2; t <= c + 2; t++) if (cmnd[t] < guardBest) { guardBest = cmnd[t]; guardTau = t; }
    }
    tau = guardTau;
    const conf = 1 - cmnd[tau];
    // parabolic interpolation of the minimum
    let tauF = tau;
    if (tau > 1 && tau < tHi) {
      const a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
      const den = a - 2 * b + c;
      if (den > 0) {
        const delta = 0.5 * (a - c) / den;
        if (Math.abs(delta) < 1) tauF = tau + delta;
      }
    }
    return { hz: this.srD / tauF, conf };
  }
}
