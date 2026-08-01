// Records the master bus and downloads an audio file (webm/opus or m4a)

export class Recorder {
  constructor(engine) {
    this.engine = engine;
    this.recording = false;
    this.startedAt = 0;
    this._mr = null;
    this._chunks = [];
    this._dest = null;
  }

  static supported() {
    return typeof MediaRecorder !== 'undefined';
  }

  start() {
    if (this.recording || !Recorder.supported()) return false;
    const ctx = this.engine.ctx;
    if (!this._dest) {
      this._dest = ctx.createMediaStreamDestination();
      // tapped after the limiter: exactly what comes out of the speakers
      this.engine.analyser.connect(this._dest);
    }
    const mime = ['audio/webm;codecs=opus', 'audio/webm', 'audio/mp4']
      .find(m => MediaRecorder.isTypeSupported(m)) || '';
    // array bound to THIS recorder, so a late stop of the previous one can
    // neither empty nor contaminate the new session
    const chunks = [];
    this._chunks = chunks;
    try {
      this._mr = new MediaRecorder(this._dest.stream, mime ? { mimeType: mime } : undefined);
    } catch {
      return false;
    }
    this._mr.ondataavailable = e => { if (e.data.size) chunks.push(e.data); };
    this._mr.start(250);
    this.recording = true;
    this.startedAt = performance.now();
    return true;
  }

  stop() {
    if (!this.recording || !this._mr) return;
    const mr = this._mr;
    const chunks = this._chunks; // captured now: immune to a following start()
    this.recording = false;
    return new Promise(resolve => {
      mr.onstop = () => {
        const type = mr.mimeType || 'audio/webm';
        const blob = new Blob(chunks, { type });
        const ext = type.includes('mp4') ? 'm4a' : 'webm';
        const d = new Date();
        const pad = n => String(n).padStart(2, '0');
        const name = `jamrack-${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}.${ext}`;
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = name;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 30000);
        resolve(name);
      };
      mr.stop();
    });
  }

  elapsed() {
    return this.recording ? (performance.now() - this.startedAt) / 1000 : 0;
  }
}
