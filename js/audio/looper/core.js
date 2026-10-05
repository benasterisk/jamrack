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
// Audio-thread discipline: process() has a 2.7 ms budget per 128-frame block
// at 48 kHz, so nothing in here walks a whole loop at once, and nothing is
// allocated or freed inside the sample loop (large buffers churn the GC).
// The first recording writes into one buffer of MAX LENGTH, allocated when
// it starts (the OS maps its pages as they are touched), and the loop is a
// view on it at close: no copy, only the overrun is added onto the start.
// The undo layer builds itself while a pass runs (see _startUndo).
//
// Memory: a 5-minute stereo loop is 115 MB per track (float32), twice that
// with an undo layer; the first track holds MAX LENGTH worth whatever its
// loop length. The host chooses maxSeconds per device.

export const TRACKS = 6;
const MIN_LOOP_SECONDS = 0.1;
const UNDO_FILL = 1 << 14;        // frames of undo layer copied per block (64 KB per channel)

export class LooperCore {
  constructor(sampleRate, { maxSeconds = 300 } = {}) {
    this.sr = sampleRate;
    this.maxFrames = Math.round(maxSeconds * sampleRate);
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
        storeL: null, storeR: null, stored: 0,  // first-loop storage (MAX LENGTH + input latency)
        undoL: null, undoR: null, undoValid: false,
        undoBuilding: false,       // the undo layer is still being completed (see _startUndo)
        undoStart: 0, undoFilled: 0,
        writing: 'none',           // 'none' | 'over' | 'dub' — what the recorder does
        written: 0,                // frames written in the current pass
        tail: 0,                   // frames still to write after the pass ended (latency)
        reverse: false, half: false, feedback: 1, mute: false,
        paused: false,             // PAUSE: the track is silent and records nothing; resumes in sync
        rh: 0,                     // read head for half speed (fractional)
        rms: 0,
      });
    }
    this.inputRms = 0;
    this._sq = new Float64Array(TRACKS);
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
    if (p.paused !== undefined && !!p.paused !== t.paused) {
      t.paused = !!p.paused;
      // pausing ends a recording/overdub pass: the track keeps what it has
      if (t.paused && (t.mode === 'rec' || t.mode === 'dub')) { this._endPass(t, 'play'); this._emitTrack(i); }
      this._emit({ t: 'pause', i, paused: t.paused });
    }
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
    if (!this.running) { this.running = true; this.pos = 0; }
    if (t.mode === 'rec' || t.mode === 'dub') {
      this._endPass(t, 'play');
    } else if (t.has) {
      this._startDub(t);
    } else {
      this._startRec(t);
    }
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
    // Stopped first: a first loop that closes now (or once its last frames
    // arrive) must land in PLAY, not start an overdub on a frozen head.
    this.running = false;
    if (this.firstTrack >= 0) { this._requestClose(); }
    this.tracks.forEach((t, i) => {
      // The head no longer moves, so the latency tail has nowhere to go:
      // flushing it would pile every late sample onto one frame.
      if (t.mode === 'rec' || t.mode === 'dub') { this._dropPass(t); this._emitTrack(i); }
    });
    this._emit({ t: 'transport', running: false });
  }

  clear(i) {
    const t = this.tracks[i];
    if (!t) return;
    if (this.firstTrack === i) { this.firstTrack = -1; this.pendingLength = 0; }
    Object.assign(t, { mode: 'idle', has: false, L: null, R: null, storeL: null, storeR: null, stored: 0,
      undoL: null, undoR: null, undoValid: false, undoBuilding: false, writing: 'none', written: 0, tail: 0, rms: 0 });
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

  /** Where the next input sample is written: `writeLat` frames behind the play head. */
  _writeHead() {
    const len = this.length;
    // a double modulo: the latency may exceed a very short loop
    return ((this.pos - this.writeLat) % len + len) % len;
  }

  _startFirst(i) {
    const t = this.tracks[i];
    // one allocation, between blocks; what gets stored is capped by it
    const cap = this.maxFrames + this.inputLatency;
    t.storeL = new Float32Array(cap); t.storeR = new Float32Array(cap); t.stored = 0;
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
    // the loop is a view on the storage: nothing to copy at close
    t.L = t.storeL.subarray(lat, lat + len); t.R = t.storeR.subarray(lat, lat + len);
    // what was played past the end (under a beat) belongs to the start of the second cycle
    const extra = t.stored - lat - len;
    for (let j = 0; j < extra; j++) { const d = j % len; t.L[d] += t.storeL[lat + len + j]; t.R[d] += t.storeR[lat + len + j]; }
    t.storeL = t.storeR = null; t.stored = 0;
    t.has = true;
    this._allocUndo(t);
    this.length = len;
    this.pos = extra % len;
    this.firstTrack = -1;
    this.pendingLength = 0;
    if (this.running) {
      t.mode = 'dub'; t.writing = 'dub'; t.written = 0; t.tail = 0;
      this._startUndo(t, this._writeHead());
    } else {
      // STOP closed it: the loop exists but nothing moves until PLAY
      t.mode = 'play'; t.writing = 'none'; t.written = 0; t.tail = 0;
    }
    this._emit({ t: 'length', length: len });
    this._emitTrack(i);
  }

  _startRec(t) {
    t.L = new Float32Array(this.length); t.R = new Float32Array(this.length);
    t.has = true; t.undoValid = false; t.undoBuilding = false;
    t.mode = 'rec'; t.writing = 'over'; t.written = 0; t.tail = 0;
    this._allocUndo(t);
  }

  /** The undo layer is sized once, with the loop, so no pass start allocates. */
  _allocUndo(t) {
    if (!t.undoL || t.undoL.length !== t.L.length) { t.undoL = new Float32Array(t.L.length); t.undoR = new Float32Array(t.R.length); }
  }

  _startDub(t) {
    t.mode = 'dub'; t.writing = 'dub'; t.written = 0; t.tail = 0;
    this._startUndo(t, this._writeHead());
  }

  /**
   * Begins the undo layer of a pass whose first write lands at `start`.
   * Copying the whole loop here would stall the audio thread (13 ms for
   * 5 minutes), so the layer is built in two directions: the recorder saves
   * each frame just before overwriting it (forward from `start`), and
   * _fillUndo() copies the untouched frames backwards from the end of the
   * pass, a bounded slice per block. The two meet, and only then is the
   * layer valid; a pass that ends early is completed the same way.
   */
  _startUndo(t, start) {
    this._allocUndo(t);
    t.undoValid = false;
    t.undoBuilding = true;
    t.undoStart = start;
    t.undoFilled = 0;
  }

  _fillUndo(t, i) {
    const len = t.L.length;
    const left = len - t.written - t.undoFilled;     // neither saved by the recorder nor filled yet
    if (left > 0) {
      const n = Math.min(left, UNDO_FILL);
      // the slice just below the filled region, in unwrapped loop coordinates
      let end = t.undoStart + len - t.undoFilled;
      let start = end - n;
      while (start < end) {
        const a = start % len, m = Math.min(end - start, len - a);
        t.undoL.set(t.L.subarray(a, a + m), a);
        t.undoR.set(t.R.subarray(a, a + m), a);
        start += m;
      }
      t.undoFilled += n;
    }
    if (t.written + t.undoFilled >= len) {
      t.undoBuilding = false;
      t.undoValid = true;
      this._emitTrack(i);
    }
  }

  /** Ends a rec/dub pass; the recorder keeps writing for the latency tail. */
  _endPass(t, mode) {
    t.mode = mode;
    t.tail = this.writeLat;
    if (t.tail === 0) t.writing = 'none';
  }

  /** Ends a pass at once, tail included (the transport stopped). */
  _dropPass(t) {
    t.mode = 'play'; t.writing = 'none'; t.tail = 0;
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
    const sq = this._sq;
    sq.fill(0);

    for (let s = 0; s < n; s++) {
      const l = inL[s], r = inR[s];
      inSq += l * l + r * r;

      // ---- first loop: just store what comes in
      if (this.firstTrack >= 0) {
        const t = tracks[this.firstTrack];
        // the storage was sized when recording started: MAX changed since is honoured when smaller
        if (t.stored < t.storeL.length && t.stored < this.maxFrames + this.inputLatency) { t.storeL[t.stored] = l; t.storeR[t.stored] = r; t.stored++; }
        else if (!this.pendingLength) { this.pendingLength = Math.min(this.maxFrames, t.stored - this.inputLatency); }
        if (this.pendingLength) this._tryCloseFirst();
        // nothing plays until the loop exists (other tracks cannot have audio)
        continue;
      }
      if (this.length === 0) continue;

      const len = this.length, p = this.pos;
      const w = this._writeHead();
      for (let i = 0; i < TRACKS; i++) {
        const t = tracks[i];
        if (!t.has) continue;
        // write — only while the head moves: stopped, every sample would land on one frame
        if (this.running && t.writing !== 'none') {
          if (t.writing === 'dub') {
            if (t.undoBuilding) { t.undoL[w] = t.L[w]; t.undoR[w] = t.R[w]; }
            t.L[w] = t.L[w] * t.feedback + l; t.R[w] = t.R[w] * t.feedback + r;
          } else { t.L[w] = l; t.R[w] = r; }
          t.written++;
          if (t.tail > 0 && --t.tail === 0) t.writing = 'none';
          else if (t.mode === 'rec' && t.written >= len) {
            t.mode = 'dub'; t.writing = 'dub'; t.written = 0;
            this._startUndo(t, (w + 1) % len);
            this._emitTrack(i);
          }
        }
        // read
        if (!this.running || t.mute || t.paused) continue;
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
    for (let i = 0; i < TRACKS; i++) if (tracks[i].undoBuilding) this._fillUndo(tracks[i], i);
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
