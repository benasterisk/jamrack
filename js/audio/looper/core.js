// LOOPER core: six synchronised loop tracks over one shared transport.
//
// Pure JavaScript over Float32Array blocks (no Web Audio objects), so the
// same code runs in the AudioWorklet (worklet.js) and in the Node tests
// (test/looper-core.test.mjs).
//
// Model (the one hardware loopers made familiar):
//   - the FIRST recording sets the loop length: press to start, press to
//     close; with a beat quantum (SYNC) the length snaps to whole beats and
//     what was played past the snapped end wraps into the start of the loop
//   - every other track records exactly one loop length, in sync, from
//     wherever the play head is when it starts; after a full cycle it
//     continues as an overdub
//   - a track button cycles REC → DUB → PLAY → DUB → PLAY…; an overdub mixes
//     the input onto the existing audio, scaled by the track's feedback
//     (1 = layers pile up forever, lower = older layers fade)
//   - UNDO swaps the audio back to what it was before the last overdub pass
//     (pressing again redoes)
//   - half speed and reverse act on playback only
//   - latency: a player plays along with what they HEAR, which the output
//     buffer delays by `outputLatency`; an external input then arrives
//     `inputLatency` later still. The recorder therefore writes
//     inputLatency + outputLatency frames behind the play head, and keeps
//     writing for that long after a pass ends so the tail is not lost. The
//     first loop (nothing to play along to yet) is only shifted by the input
//     latency
//
// Memory: a 5-minute stereo loop is 115 MB per track (float32), twice that
// with an undo layer. The host chooses maxSeconds per device.

export const TRACKS = 6;
const CHUNK_SECONDS = 5;          // growth step while the first loop records
const MIN_LOOP_SECONDS = 0.1;

export class LooperCore {
  constructor(sampleRate, { maxSeconds = 300 } = {}) {
    this.sr = sampleRate;
    this.maxFrames = Math.round(maxSeconds * sampleRate);
    this.chunk = Math.round(CHUNK_SECONDS * sampleRate);
    this.length = 0;               // frames; 0 = no loop yet
    this.pos = 0;                  // play head, 0..length-1
    this.running = false;
    this.inputLatency = 0;         // frames: source → engine (0 for internal sources)
    this.outputLatency = 0;        // frames: engine → ears
    this.writeLat = 0;             // inputLatency + outputLatency
    this.snap = 0;                 // beat quantum in frames; 0 = free length
    this.pendingLength = 0;        // first loop waiting for enough frames to close
    this.firstTrack = -1;          // track recording the first loop, or -1
    this.tracks = [];
    for (let i = 0; i < TRACKS; i++) {
      this.tracks.push({
        mode: 'idle',              // 'idle' | 'rec' | 'dub' | 'play'
        has: false,
        L: null, R: null,
        chunksL: [], chunksR: [], stored: 0,   // first-loop growth storage
        undoL: null, undoR: null, undoValid: false,
        writing: 'none',           // 'none' | 'over' | 'dub' — what the recorder does
        written: 0,                // frames written in the current pass
        tail: 0,                   // frames still to write after the pass ended (latency)
        reverse: false, half: false, feedback: 1, mute: false,
        rh: 0,                     // read head for half speed (fractional)
        rms: 0,
      });
    }
    this.inputRms = 0;
    this._events = [];
  }

  // ---------------------------------------------------------------- commands

  setMaxSeconds(sec) { this.maxFrames = Math.round(Math.max(MIN_LOOP_SECONDS, sec) * this.sr); }
  setLatency(inputFrames, outputFrames = 0) {
    this.inputLatency = Math.max(0, Math.round(inputFrames));
    this.outputLatency = Math.max(0, Math.round(outputFrames));
    this.writeLat = this.inputLatency + this.outputLatency;
  }
  setSnap(frames) { this.snap = Math.max(0, Math.round(frames)); }

  setTrack(i, p) {
    const t = this.tracks[i];
    if (!t) return;
    if (p.feedback !== undefined) t.feedback = Math.min(1, Math.max(0, p.feedback));
    if (p.mute !== undefined) t.mute = !!p.mute;
    if (p.half !== undefined && !!p.half !== t.half) { t.half = !!p.half; t.rh = this.pos; }
    if (p.reverse !== undefined) t.reverse = !!p.reverse;
  }

  /** The track button: REC → DUB → PLAY → DUB … (see the header). */
  toggle(i) {
    const t = this.tracks[i];
    if (!t) return;
    if (this.length === 0) {
      if (this.firstTrack === i) { this._requestClose(); return; }
      if (this.firstTrack >= 0) return;          // one first loop at a time
      this._startFirst(i);
      return;
    }
    if (t.mode === 'rec' || t.mode === 'dub') {
      this._endPass(t, 'play');
    } else if (t.has) {
      this._startDub(t);
    } else {
      this._startRec(t);
    }
    if (!this.running) { this.running = true; this.pos = 0; }
    this._emitTrack(i);
  }

  play() {
    if (this.length === 0) return;
    this.pos = 0;
    this.running = true;
    this.tracks.forEach(t => { t.rh = 0; });
    this._emit({ t: 'transport', running: true });
  }

  stop() {
    if (this.firstTrack >= 0) { this._requestClose(); }
    this.tracks.forEach((t, i) => {
      if (t.mode === 'rec' || t.mode === 'dub') { this._endPass(t, 'play'); this._emitTrack(i); }
    });
    this.running = false;
    this._emit({ t: 'transport', running: false });
  }

  clear(i) {
    const t = this.tracks[i];
    if (!t) return;
    if (this.firstTrack === i) { this.firstTrack = -1; this.pendingLength = 0; }
    Object.assign(t, { mode: 'idle', has: false, L: null, R: null, chunksL: [], chunksR: [], stored: 0,
      undoL: null, undoR: null, undoValid: false, writing: 'none', written: 0, tail: 0, rms: 0 });
    if (!this.tracks.some(x => x.has) && this.firstTrack < 0) {
      this.length = 0; this.pos = 0; this.running = false;
      this._emit({ t: 'length', length: 0 });
    }
    this._emitTrack(i);
  }

  clearAll() { for (let i = 0; i < TRACKS; i++) this.clear(i); }

  /** Swaps the audio with the copy taken before the last overdub pass. */
  undo(i) {
    const t = this.tracks[i];
    if (!t || !t.undoValid) return;
    [t.L, t.undoL] = [t.undoL, t.L];
    [t.R, t.undoR] = [t.undoR, t.R];
    this._emitTrack(i);
  }

  // ------------------------------------------------------------- internals

  _emit(e) { this._events.push(e); }
  _emitTrack(i) { const t = this.tracks[i]; this._emit({ t: 'track', i, mode: t.mode, has: t.has, undo: t.undoValid }); }

  _startFirst(i) {
    const t = this.tracks[i];
    t.chunksL = []; t.chunksR = []; t.stored = 0;
    t.mode = 'rec'; t.writing = 'over'; t.written = 0; t.tail = 0;
    this.firstTrack = i;
    this.pendingLength = 0;
    this.running = true;
    this.pos = 0;
    this._emitTrack(i);
    this._emit({ t: 'transport', running: true });
  }

  /** The first loop's closing press: decide the length, close when possible. */
  _requestClose() {
    const t = this.tracks[this.firstTrack];
    // press-to-press is the length the player meant, whatever the latency
    let len = Math.max(Math.round(MIN_LOOP_SECONDS * this.sr), t.stored);
    if (this.snap > 0) len = Math.max(this.snap, Math.round(len / this.snap) * this.snap);
    len = Math.min(len, this.maxFrames);
    this.pendingLength = len;
    this._tryCloseFirst();
  }

  _tryCloseFirst() {
    const i = this.firstTrack;
    if (i < 0 || !this.pendingLength) return;
    const t = this.tracks[i];
    const lat = this.inputLatency;
    if (t.stored < lat + this.pendingLength) return;     // not enough arrived yet
    const len = this.pendingLength;
    t.L = new Float32Array(len); t.R = new Float32Array(len);
    this._copyStorage(t, lat, len, t.L, t.R, 0, false);
    // what was played past the end belongs to the start of the second cycle
    const extra = t.stored - lat - len;
    if (extra > 0) this._copyStorage(t, lat + len, extra, t.L, t.R, 0, true);
    t.chunksL = []; t.chunksR = []; t.stored = 0;
    t.has = true;
    this.length = len;
    this.pos = extra % len;
    this.firstTrack = -1;
    this.pendingLength = 0;
    t.mode = 'dub'; t.writing = 'dub'; t.written = 0; t.tail = 0;
    this._snapshot(t);
    this._emit({ t: 'length', length: len });
    this._emitTrack(i);
  }

  /** Copies `n` frames of first-loop storage from `from` into L/R at `to` (adding if dub). */
  _copyStorage(t, from, n, L, R, to, add) {
    for (let j = 0; j < n; j++) {
      const s = from + j, c = (s / this.chunk) | 0, k = s - c * this.chunk;
      const l = t.chunksL[c][k], r = t.chunksR[c][k];
      const d = (to + j) % L.length;
      if (add) { L[d] += l; R[d] += r; } else { L[d] = l; R[d] = r; }
    }
  }

  _startRec(t) {
    t.L = new Float32Array(this.length); t.R = new Float32Array(this.length);
    t.has = true; t.undoValid = false;
    t.mode = 'rec'; t.writing = 'over'; t.written = 0; t.tail = 0;
  }

  _startDub(t) {
    this._snapshot(t);
    t.mode = 'dub'; t.writing = 'dub'; t.written = 0; t.tail = 0;
  }

  _snapshot(t) {
    if (!t.undoL || t.undoL.length !== t.L.length) { t.undoL = new Float32Array(t.L.length); t.undoR = new Float32Array(t.R.length); }
    t.undoL.set(t.L); t.undoR.set(t.R);
    t.undoValid = true;
  }

  /** Ends a rec/dub pass; the recorder keeps writing for the latency tail. */
  _endPass(t, mode) {
    t.mode = mode;
    t.tail = this.writeLat;
    if (t.tail === 0) t.writing = 'none';
  }

  _store(t, l, r) {
    const c = (t.stored / this.chunk) | 0;
    if (c >= t.chunksL.length) { t.chunksL.push(new Float32Array(this.chunk)); t.chunksR.push(new Float32Array(this.chunk)); }
    const k = t.stored - c * this.chunk;
    t.chunksL[c][k] = l; t.chunksR[c][k] = r;
    t.stored++;
  }

  // ---------------------------------------------------------------- audio

  /**
   * Renders one block. `outs[i]` = [Float32Array L, Float32Array R] per
   * track (overwritten). Returns the state events produced during the
   * block (the array is reused: consume it before the next call).
   */
  process(inL, inR, outs) {
    const ev = this._events;
    ev.length = 0;
    const n = inL.length;
    const tracks = this.tracks;
    let inSq = 0;
    for (let i = 0; i < TRACKS; i++) { outs[i][0].fill(0); outs[i][1].fill(0); }
    const sq = new Float64Array(TRACKS);

    for (let s = 0; s < n; s++) {
      const l = inL[s], r = inR[s];
      inSq += l * l + r * r;

      // ---- first loop: just store what comes in
      if (this.firstTrack >= 0) {
        const t = tracks[this.firstTrack];
        if (t.stored < this.maxFrames + this.inputLatency) this._store(t, l, r);
        else if (!this.pendingLength) { this.pendingLength = this.maxFrames; }
        if (this.pendingLength) this._tryCloseFirst();
        // nothing plays until the loop exists (other tracks cannot have audio)
        continue;
      }
      if (this.length === 0) continue;

      const len = this.length, p = this.pos;
      const w = (p - this.writeLat + len) % len;
      for (let i = 0; i < TRACKS; i++) {
        const t = tracks[i];
        if (!t.has) continue;
        // write
        if (t.writing !== 'none') {
          if (t.writing === 'dub') { t.L[w] = t.L[w] * t.feedback + l; t.R[w] = t.R[w] * t.feedback + r; }
          else { t.L[w] = l; t.R[w] = r; }
          t.written++;
          if (t.tail > 0 && --t.tail === 0) t.writing = 'none';
          else if (t.mode === 'rec' && t.written >= len) { t.mode = 'dub'; t.writing = 'dub'; this._snapshot(t); this._emitTrack(i); }
        }
        // read
        if (!this.running || t.mute) continue;
        let ol, or;
        if (t.half) {
          const a = t.rh | 0, b = (a + 1) % len, f = t.rh - a;
          ol = t.L[a] + (t.L[b] - t.L[a]) * f; or = t.R[a] + (t.R[b] - t.R[a]) * f;
          t.rh += t.reverse ? -0.5 : 0.5;
          if (t.rh >= len) t.rh -= len; else if (t.rh < 0) t.rh += len;
        } else {
          const q = t.reverse ? len - 1 - p : p;
          ol = t.L[q]; or = t.R[q];
        }
        outs[i][0][s] = ol; outs[i][1][s] = or;
        sq[i] += ol * ol + or * or;
      }
      if (this.running && ++this.pos >= len) this.pos = 0;
    }
    this.inputRms = Math.sqrt(inSq / (2 * n));
    for (let i = 0; i < TRACKS; i++) tracks[i].rms = Math.sqrt(sq[i] / (2 * n));
    return ev;
  }

  /** Snapshot for the UI. */
  meter() {
    return {
      t: 'meter', length: this.length, pos: this.firstTrack >= 0 ? this.tracks[this.firstTrack].stored : this.pos,
      running: this.running, recordingFirst: this.firstTrack >= 0,
      input: this.inputRms, levels: this.tracks.map(t => t.rms),
      tracks: this.tracks.map(t => ({ mode: t.mode, has: t.has, undo: t.undoValid })),
    };
  }
}
