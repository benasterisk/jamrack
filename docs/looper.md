# LOOPER section — contract between the audio engine and the rack UI

Six synchronised loop tracks, up to 5 minutes, recorded from the whole rack,
from one module, or from the audio input, each with a small mixer strip and a
few effects. The timing-critical engine is done (`js/audio/looper/core.js`,
tested in `test/looper-core.test.mjs`; worklet host `worklet.js`; Web Audio
plumbing and command API `index.js`). This note is the contract the rack
card, the state, the wiring in `main.js` and the translations are built on.

## Behaviour (what the player experiences)

- **First recording sets the loop length.** Press a track's REC: the
  transport starts and the track records. Press again: the loop closes at
  that length (press-to-press) and the track goes straight to OVERDUB (the
  Boss RC cycle: REC → DUB → PLAY → DUB → PLAY…). Press again: PLAY.
- **SYNC on** (and the metronome tempo known): the first loop's length snaps
  to whole beats at the METRO tempo; what was played past the snapped end
  wraps into the start of the loop. The metronome is not phase-locked to
  the loop (v1): start recording on a beat.
- **Other tracks** record exactly one loop length, in sync, from wherever the
  play head is when REC is pressed, then continue as overdub.
- **Overdub feedback** (FB): 1 = layers pile up forever, lower = older layers
  fade with each pass.
- **UNDO** on a track swaps back to the audio before its last overdub pass
  (press again = redo).
- **PLAY** restarts all tracks from the top; **STOP** stops the transport
  (and closes the first loop if it was recording); **CLEAR** empties a track;
  clearing every track resets the loop length. **CLEAR ALL** does all six.
- **REVERSE** and **½ SPEED** act on playback only (a track in REC/DUB
  ignores them).
- **MAX LENGTH** (30 s / 1 min / 2 min / 5 min): the first loop closes by
  itself when it reaches it. Memory: a 5-minute stereo loop is 115 MB per
  track (230 MB with undo). Default 5 min on desktop, 1 min on phones
  (`matchMedia('(hover: hover) and (pointer: fine)')` false).
- **Source**: RACK (every module, dry, without the metronome, the shared
  effects or the looper itself — `engine.dry`), one module (its dry output),
  or INPUT (the audio input chosen in the GUITAR section's input menu,
  `state.guitar.deviceId`, raw constraints, with a MONITOR level so the
  player hears themselves). Recording from the input compensates the input
  and output latencies; recording internal sources compensates the output
  latency (the player plays along with what they hear).
- Loops are **not persisted** across reloads in v1 (mixer settings are).

## Engine API (`js/audio/looper/index.js`)

```js
import { createLooper, defaultTrack, TRACKS } from './audio/looper/index.js';
const looper = createLooper(engine, {
  meter(m)    { /* ~30/s: { length, pos, running, recordingFirst, input, levels[6], tracks[6]: {mode, has, undo} } (frames) */ },
  events(list){ /* state changes: {t:'length', length} {t:'track', i, mode, has, undo} {t:'transport', running} */ },
  status(st)  { /* {key: 'off'|'ready'|'unsupported'|'noMic'|'denied'|'ended'|'noModule', detail} */ },
}, {
  maxSeconds: 300,
  moduleOut(id) { /* return the Instrument's `out` GainNode for a rack instance id, or null */ },
});
await looper.init();                      // loads the worklet once; false if unsupported
looper.toggle(i); looper.play(); looper.stop(); looper.clear(i); looper.clearAll(); looper.undo(i);
looper.setTrack(i, { vol, pan, cutoff, rev, del, mute, solo, reverse, half, feedback });  // any subset
await looper.setSource({ kind: 'rack' } | { kind: 'module', id } | { kind: 'input', deviceId });
looper.setSync(bpmOr0);                   // beat quantum from the METRO tempo; 0 = free length
looper.setMaxSeconds(sec);
looper.setMonitor(0..1);                  // input monitoring level (input source only)
looper.refreshLatency();                  // after the context's latency may have changed
looper.latency();                         // { input, output } ms, for display
looper.params                             // the 6 track parameter objects (defaultTrack())
looper.status, looper.source
```

Track modes reported by `meter().tracks[i].mode` / `track` events:
`idle` (empty), `rec` (recording the first pass), `dub` (overdubbing),
`play`. `has` says the track holds audio; `undo` that an undo layer exists.

## State (`js/state.js`)

```js
export function defaultLooperTrack() { return { vol: 0.8, pan: 0, cutoff: 1, rev: 0, del: 0, mute: false, solo: false, reverse: false, half: false, feedback: 1 }; }
state.looper = {
  source: 'rack',          // 'rack' | 'input' | an instance id (string)
  sync: true,
  maxSeconds: 300,         // 30 | 60 | 120 | 300
  monitor: 1,              // 0..1
  collapsed: false,
  tracks: [6 × defaultLooperTrack()],
};
```
`solo` and the transport are performance state: like `solo` on modules they
are reset to false on load. Merge saved tracks onto defaults in `load()`
(older saves miss keys), exactly as `sampler`/`synth` are merged.

## Rack card (`js/ui/rack.js`), after the GUITAR card

Follow the GUITAR card (`buildGuitar`) and the module cards for markup,
classes, knobs (`createKnob`, `small: true` in strips), toggles (`.toggle`),
steppers and `section()/row()`. Layout:

- **Header** (`.mod-head`): `.master-title` "LOOPER"; a `.mod-lcd` with the
  loop length / position readout (`0:00.0 / 0:12.0`, "—" when empty) and the
  status text; transport buttons `.tb-btn`: ▶ PLAY, ■ STOP, UNDO (last
  track that overdubbed — keep it simple: UNDO applies to the most recently
  toggled track), ✕ CLEAR ALL (with `confirm`); SOURCE `<select>` (RACK,
  INPUT, then one entry per module named `${index+1} · ${engine label} · ${instrument or preset or bank name}`
  — rebuild its options whenever the rack rebuilds); SYNC toggle; MAX
  LENGTH `<select>`; MONITOR small knob (shown only when SOURCE = INPUT);
  fold button.
- **Progress bar**: a thin canvas or div under the header showing the play
  head position in the loop (full width = loop length), red while the first
  loop records.
- **Six track strips** (`.lp-track`), each on one row on desktop, wrapping
  on phones: track number; the REC/DUB/PLAY button (`.tb-btn.led-btn` whose
  LED is red in `rec`, amber blinking in `dub`, teal in `play`, off when
  idle; its label shows the mode); MUTE and SOLO square buttons
  (`.sq-btn`, `.active-amber` / `.active-teal` like modules); ✕ clear
  (`confirm` only if the track has audio); UNDO (enabled when `undo`); small
  knobs LEVEL, PAN, CUT (low-pass; format like the module filter), REV, DLY,
  FB; toggles REVERSE and ½; a small level meter (reuse the VU drawing
  style, 8 segments).
- Every control writes `state.looper…`, calls the matching `api.looper*`
  function and `emit('looper')`.

Rack API additions (implemented in `main.js`, called by the card):
`looperToggle(i)`, `looperPlay()`, `looperStop()`, `looperClear(i)`,
`looperClearAll()`, `looperUndo(i)`, `looperTrack(i, params)`,
`looperSource(value)`, `looperSync(on)`, `looperMax(seconds)`,
`looperMonitor(v)`, `looperStatusText()`, `looperSources()` (the select
options), `looperRunning()`.
Rack methods the wiring calls: `setLooperMeter(m, latency)`,
`setLooperEvents(list)`, `setLooperStatus(text, isErr)`.

## Wiring (`js/main.js`)

Create the looper after the rack (like the guitar input): `moduleOut(id)`
returns `audios.get(id)?.out`. On `events` with `t:'length'` and the
metronome running, nothing to do in v1. Keep `looper.setSync(state.looper.sync && state.metronome.bpm ? bpm : 0)`
in sync with the BPM input and the SYNC toggle. `looperSource` resolves the
select value: 'rack', 'input' (deviceId from `state.guitar.deviceId`) or an
instance id; it must survive a module being deleted (fall back to 'rack').
`applyLanguage()` rebuilds the card: re-apply status, meters and the select.
Call `looper.refreshLatency()` when the guitar input starts (its latency is
known then). Expose `looper` on `window.JAMRACK`.

## Strings (`js/i18n/strings.js`, all 12 languages)

Keys (English values): `looper` "LOOPER", `lpPlay` "PLAY", `lpStop` "STOP",
`lpRec` "REC", `lpDub` "DUB", `lpPlaying` "PLAY", `lpUndo` "UNDO",
`lpClear` "CLEAR", `lpClearAll` "CLEAR ALL", `lpConfirmClear` "Clear this
track?", `lpConfirmClearAll` "Clear all six tracks?", `lpSource` "SOURCE",
`lpSourceRack` "RACK (all modules)", `lpSourceInput` "AUDIO INPUT", `lpSync`
"SYNC", `lpMax` "MAX", `lpMonitor` "MONITOR", `lpLevel` "LEVEL", `lpPan`
"PAN", `lpCut` "CUT", `lpRev` "REV", `lpDel` "DLY", `lpFb` "FB", `lpReverse`
"REVERSE", `lpHalf` "½ SPEED", `lpMute` "M", `lpSolo` "S", `lpEmpty` "Empty
— press REC on a track to record the first loop", `lpRecordingFirst`
"Recording the loop… press again to close it", `lpReady` "Ready",
`lpUnsupported` "Looper unavailable in this browser", `lpNoMic` "Audio input
unavailable", `lpDenied` "Input refused", `lpEnded` "Input disconnected",
`lpNoModule` "That module is gone — recording the rack", titles for every
control (`lpTitle…`), and a manual entry `helpLooperTitle` "Looper" +
`helpLooperText` (the behaviour section above, in plain words) in the RACK
tab of the manual (`js/ui/dialogs.js`, after the MASTER entry).

## CSS (`css/style.css`)

`.module.looper` (a slightly different tint, like `.module.guitar`),
`.lp-bar` progress, `.lp-track` strips (flex, wrap), LED colours per mode,
phone layout under the existing `@media (max-width: 780px), (max-height: 520px)`
rules (strips wrap, 40px touch targets like `body.m-edit` does for modules).

## Acceptance (Playwright, headless Chromium, `serve.mjs`)

1. Page loads with no console errors beyond the known CDN ones; the LOOPER
   card renders after GUITAR with 6 strips; the SOURCE select lists RACK,
   INPUT and the modules.
2. With an ANALOG module playing a note through `JAMRACK.routeNoteOn`, press
   track 1 REC, wait 1.5 s, press again: the LCD shows a loop length ≈ 1.5 s,
   the track LED turns amber (DUB); press again → PLAY; the track meter
   moves; `JAMRACK.looper` meter reports `has` on track 1.
3. Track 2 REC → after one cycle its mode is `dub` by itself.
4. UNDO, CLEAR (confirm dialog accepted), CLEAR ALL reset the LCD to "—".
5. Language switch to French rebuilds the card with the loop still
   running; phone viewport (390×800, SOUNDS tab) shows the card without
   horizontal overflow.
6. `node --test test/looper-core.test.mjs` passes.
