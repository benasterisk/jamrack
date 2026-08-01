// Metronome: look-ahead scheduling for a rock-steady tempo

export class Metronome {
  constructor(engine) {
    this.engine = engine;
    this.gain = engine.ctx.createGain();
    this.gain.gain.value = 0.5;
    this.gain.connect(engine.master);
    this.bpm = 100;
    this.running = false;
    this._timer = 0;
    this._nextBeat = 0;
    this._count = 0;
  }

  setBpm(bpm) {
    this.bpm = Math.min(300, Math.max(30, bpm || 100));
  }

  start() {
    if (this.running) return;
    this.running = true;
    this._count = 0;
    this._nextBeat = this.engine.now + 0.08;
    this._timer = setInterval(() => this._schedule(), 25);
  }

  stop() {
    this.running = false;
    clearInterval(this._timer);
  }

  toggle() {
    this.running ? this.stop() : this.start();
    return this.running;
  }

  _schedule() {
    const now = this.engine.now;
    if (this._nextBeat < now) {
      // Missed beats (frozen or throttled tab): skip ahead to the next beat,
      // keeping phase and accent, instead of firing a burst of late clicks.
      const period = 60 / this.bpm;
      const missed = Math.ceil((now - this._nextBeat) / period);
      this._nextBeat += missed * period;
      this._count += missed;
    }
    const horizon = now + 0.12;
    while (this._nextBeat < horizon) {
      this._click(this._nextBeat, this._count % 4 === 0);
      this._nextBeat += 60 / this.bpm;
      this._count++;
    }
  }

  _click(t, accent) {
    const ctx = this.engine.ctx;
    const osc = ctx.createOscillator();
    const g = ctx.createGain();
    osc.type = 'square';
    osc.frequency.value = accent ? 1900 : 1300;
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(accent ? 0.5 : 0.32, t + 0.001);
    g.gain.exponentialRampToValueAtTime(0.001, t + 0.05);
    osc.connect(g);
    g.connect(this.gain);
    osc.start(t);
    osc.stop(t + 0.07);
  }
}
