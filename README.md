# 🎹 JAMRACK

> **No piano? Jam anyway.** A rack of synths, samplers and pianos that runs entirely in your browser.

**Live: [jamrack.openmindlab.fr](https://jamrack.openmindlab.fr)**

JAMRACK is a playable instrument rack — plain **HTML/CSS/JS + the Web Audio API**, with **no build step, no framework, no backend and zero dependencies**. Drop the folder on any static host and it is online.

![no build](https://img.shields.io/badge/build-none-brightgreen) ![dependencies](https://img.shields.io/badge/dependencies-0-brightgreen) ![languages](https://img.shields.io/badge/UI%20languages-12-blue) ![license](https://img.shields.io/badge/runtime-Web%20Audio%20API-orange)

---

## Features

### Four sound engines, stackable as rack modules

| Module | What it is |
|---|---|
| **BANK** | 128 sampled General MIDI instruments (midi-js-soundfonts over CDN, progressive decoding — playable before fully loaded) |
| **ANALOG** | A real subtractive synth: up to 7-voice detuned **unison** with stereo spread, dual oscillators, **sub-osc**, per-voice filter with its own envelope, **LFO** (filter / pitch / volume), soft-clip **drive**, glide — plus 12 presets (supersaw, hoover, reese, acid, wobble, 808 sub…) |
| **SFZ** | A hand-written [SFZ](https://sfzformat.com/) player: `#define`/`#include`, header inheritance, velocity layers, round-robins, keyswitches, loop points. Three built-in banks (808, analog drums, basses) and any SFZ bank by URL |
| **SAMPLER** | Record from the **mic** or load a file, then: start/end trim with a draggable waveform, loop points, reverse, one-shot, **CHOP** (4/8/16 slices spread across the keys), **time-stretch & pitch-shift** (granular), tune/gain — and a patch library to save the sample *with* its settings |

### A rack, not a single instrument

- Add as many modules as you like; each has volume, pan, octave/transpose, **key range** (build splits: bass left, piano right), ADSR, filter, reverb/delay sends, solo/mute, and a per-module **note repeat** (hold a key, it retriggers in sync with the metronome tempo).
- **MASTER** section with reverb size, delay time/feedback and a VU meter; **METRO** and a session **REC** that downloads an audio file.
- Every knob and section has a **reset**; the whole state persists in `localStorage` (samples and patches in IndexedDB).

### Plays with whatever you have

- **Computer keyboard** — two rows form a piano mapped by **physical key position**, so AZERTY/QWERTY/QWERTZ all work. `Z/X` octave, `C/V` velocity, `Space` sustain.
- **Touch** — multi-touch with glissando; dedicated **PLAY / SOUNDS** views on phones (portrait *and* landscape).
- **MIDI** — USB/Bluetooth keyboards auto-detected (Chrome/Edge): notes, velocity, sustain, pitch bend.

### 12 languages, RTL included

English, Chinese, Hindi, Spanish, Arabic, French, Bengali, Portuguese, Russian, Urdu, Indonesian, German — auto-detected, switchable, remembered. Arabic and Urdu flip the layout to right-to-left (the piano stays bass-to-treble). Note names follow local tradition (`C D E F G A B`, fixed-do `Do Ré Mi…`, German `H`).

A built-in multilingual **manual** (the `?` button) covers playing, all four engines and the rack.

---

## Run it locally

```bash
node serve.mjs
```

Open <http://localhost:4173>. Any static server works — one is needed only because the app uses ES modules.

## Deploy

It is a folder of static files. Serve it with anything (GitHub Pages, Netlify, nginx, Caddy…). Two practical notes:

- Serve `.js` as `text/javascript` (ES modules refuse to load otherwise).
- Prefer `Cache-Control: no-cache` so browsers revalidate after you update (files still ship as cheap 304s thanks to ETags).

## Browser notes

- Audio starts on the first tap/click — a browser requirement on iOS and Android.
- Mic recording needs OS-level permission for the browser: on iPhone/iPad check **Settings → Apps → Safari (or Chrome) → Microphone** if no permission prompt appears. In-app browsers (links opened from Mail, WhatsApp…) block the mic entirely. The module display always tells you what happened.
- Recording is captured as raw PCM and encoded to WAV in-page — deliberately **not** `MediaRecorder`, whose output iOS Safari cannot reliably decode back.

## Architecture

```
index.html
css/style.css            one stylesheet, desktop + mobile views
js/
  main.js                wiring: engine ↔ rack UI ↔ input ↔ state
  state.js               defaults + localStorage persistence
  audio/
    engine.js            shared context, master bus, reverb/delay
    instance.js          per-module voice management (all 4 engines)
    presets.js           the 12 ANALOG presets
    soundfont.js         midi-js-soundfonts loader
    sampledb.js          IndexedDB for samples & patches
    sfz/                 SFZ parser, region model, opus/wav loader, voices
  input/                 PC keyboard (physical codes), touch piano, Web MIDI
  ui/                    rack cards, knobs, dialogs, manual
  i18n/                  12 languages, per-language note naming
sfz/                     built-in SFZ banks (CC0 samples)
serve.mjs                dev server only — not used in production
```

Per-module audio chain: `voices → filter → volume → pan → mute → out`, with sends into a shared convolution reverb (generated impulse response) and delay.

## Credits

- GM soundfonts: [gleitz/midi-js-soundfonts](https://github.com/gleitz/midi-js-soundfonts) (CDN).
- Built-in SFZ banks assembled from CC0 samples.
