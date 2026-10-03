# Guitar → MIDI: how it works, where the latency goes, and the native option

This note documents the GUITAR → MIDI section (`js/input/guitar.js`,
`js/audio/guitar/`) for whoever works on it next — including the question it
was born from: *is a real-time guitar-to-MIDI converter realistic in a browser,
and how low can the latency go?*

## Short answer

Yes for monophonic playing (one note at a time), and the browser is **not**
the main cost. Three budgets add up:

| Stage | What it is | Typical cost |
|---|---|---|
| **Tracking** | hearing enough of the note to name it: 1-2 periods of the fundamental, plus skipping the pick transient | ~9 ms top frets · 12 ms high E · 17-20 ms G/B · 31-36 ms low E |
| **Input buffer** | interface → OS → browser | 2-5 ms with a proper interface on macOS/CoreAudio or ASIO; ~10 ms WASAPI shared on Windows; worse through a built-in mic |
| **Output buffer** | the synth's audio context → OS → interface | 3-10 ms (`baseLatency` / `outputLatency`, shown on the card as OUT) |

The tracking line is physics, identical for a native app, Jam Origin's
MIDI Guitar, or a Roland GR unit on a standard pickup (hex pickups cheat by
knowing which string vibrates). The measured numbers above come from the
synthetic-string tests in `test/guitar-tracker.test.mjs`; run them with
`node --test test/guitar-tracker.test.mjs`.

What a native app would gain is only the input/output buffering: ASIO or
CoreAudio at 64 samples takes IN + OUT from ~20 ms down to ~5 ms on Windows.
On macOS Chrome is already close. So: **build it in the browser first, tune it
with a real guitar, and add the native helper only if the IN/OUT readout on
the card is the part that still hurts.**

## What the browser must do to get there

All of it is in `js/input/guitar.js`:

- `getUserMedia` with `echoCancellation`, `noiseSuppression` and
  `autoGainControl` **off** and `latency: 0`. The three "smart" processors
  are built for voice calls: they add 10-20 ms and chew an instrument signal.
  Measured on Chrome/macOS by Jeff Kaufman: ~67 ms round trip by default,
  ~19 ms with these settings and `latencyHint: 0`.
- The tracker runs in an **AudioWorklet** (audio thread, 128-sample blocks),
  not on the main thread. A ScriptProcessor fallback exists for old browsers.
- The engine's `AudioContext` asks for `latencyHint: 0` on desktops (the
  smallest output buffer the device supports; Chrome clamps it) and keeps
  `'interactive'` on phones.
- Use a **real audio interface** (any USB one with a Hi-Z input) rather than
  a laptop mic, and headphones — the synth will otherwise feed back into the mic.

## The tracker (`js/audio/guitar/tracker.js`)

Pure JavaScript over `Float32Array` blocks; no DOM, no Web Audio objects, so
the same file runs in the worklet, in the fallback, and in Node tests.

1. **Per sample**: DC block → RMS (full band) and RMS of a 2.5 kHz high-pass
   (pick transients live there, a sustaining string does not) → two 3 kHz
   low-pass biquads → decimate by 2 (to ~24 kHz, every pitch sits below 1.5 kHz).
2. **Per frame** (64 decimated samples ≈ 2.7 ms):
   - *Onset*: the level, or the high band alone, rises by 6-12 dB (SENS) over
     where it was 8-16 ms ago. A louder attack may override the 27 ms
     refractory period (the pick scratches the string before releasing it).
   - *Pitch*: **YIN** (de Cheveigné & Kawahara 2002) in its "newest-anchored"
     form: the latest W samples are compared with the same samples one lag
     earlier, so a note is visible as soon as it is two periods old. Right after
     an onset three windows (128 / 256 / 515 samples) are tried and the shortest
     confident one wins — that is what gives the high strings ~10 ms. While a
     note sounds, a 3-period window around it follows bends, hammer-ons and
     slides in a few ms. Parabolic interpolation, an octave-up guard, and
     restriction to the guitar's lag range (70-1400 Hz).
   - *Notes*: an onset becomes a note-on once two consecutive frames agree on
     the pitch (velocity from the attack peak, shaped by DYN); a pitch jump
     ≥ 0.5 semitone within 8 ms is a hammer-on/pull-off (legato retrigger);
     slower movement is pitch bend (BEND on) or chromatic stepping (BEND off);
     note-off when the level falls RELEASE dB under the note's peak or the
     period is lost for 32 ms. A re-pick of the same note is accepted only with
     a convincing attack (level dip then rise, or the old note's periodicity
     breaking) so finger squeaks don't double-trigger.
3. CPU: ~12 % of one core in Node for continuous notes, nothing while idle.

Monophonic only. Polyphonic transcription from a single pickup is a
research-grade problem (neural models like Spotify's Basic Pitch need
~100 ms windows, far from real time); a hex pickup would be the honest route.

Tuning aid: `node test/trace.mjs repick|run|hammer [fromMs] [toMs]` prints a
frame-by-frame trace of levels, confidence and decisions.

## If the IN/OUT buffers turn out to be the problem: the native bridge

Same tracker, different plumbing — a small helper that owns the audio input
and sends MIDI, while JAMRACK keeps playing the sounds:

```
guitar → interface → [helper: ASIO / CoreAudio / ALSA @ 64 samples → tracker]
       → virtual MIDI port → browser (Web MIDI, already supported by JAMRACK)
```

- **Language**: Rust. `cpal` gives ASIO (feature flag + Steinberg SDK), WASAPI,
  CoreAudio and ALSA/JACK; `midir` creates a virtual MIDI *source* on macOS
  (CoreMIDI) and Linux (ALSA seq). Windows cannot create virtual ports
  without a driver: the helper sends to a **loopMIDI** port instead (free,
  one-time install), which Chrome then sees as a MIDI input.
- **Port the tracker** line by line (it is ~400 lines of plain arithmetic);
  keep the Node tests as the reference and feed the Rust build the same
  synthetic WAVs to prove it identical before touching hardware.
- **Expected gain**: IN from ~10 ms (WASAPI shared) to ~2 ms (ASIO 64 @ 48 k);
  OUT unchanged unless the synth also moves out of the browser. Total gain
  ~8-15 ms on Windows, ~3-5 ms on macOS. The tracking budget stays.
- **Not possible / not worth it**: there is no ASIO in a browser, and WebMIDI
  itself costs well under 1 ms. A WASM build of the tracker would not help
  either — the JS version already uses a tenth of one core.

Dev test of the native helper needs real hardware (an interface and a guitar),
which the cloud session used to build this did not have; its DSP core, however,
can be validated against the same test signals before any hardware.
