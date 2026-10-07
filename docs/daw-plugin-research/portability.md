# Portability of the JAMRACK guitar-to-MIDI code to a DAW plugin

Scope: what in `js/audio/guitar/` and `js/input/guitar.js` would move into a native
plugin (C++ or Rust), what stays in the browser, what the port has to watch, and how
the existing test assets become the oracle of the port. Everything below comes from
reading the repository at commit `bcbb30d` (branch `main`, 7 October 2026) and from
running its test suites in this container (Node v22.22.0, 4 cores). No web research
was needed for this topic; the few statements about plugin frameworks are marked
"general knowledge, not verified here" and listed at the end.

Source convention: `file:line` refers to
`https://github.com/benasterisk/jamrack/blob/bcbb30d/<file>#L<line>` (the GitHub
origin is `https://github.com/benasterisk/jamrack`, `git remote -v`). Line numbers
were read with `cat -n` on that commit.

---

## 1. Inventory: what a plugin must contain, what stays in the browser

### 1.1 Moves into the plugin (pure DSP, no Web API)

| File (URL base above) | Lines | Role | Web/JS dependency |
|---|---:|---|---|
| `js/audio/guitar/tracker.js` | 550 | MONO engine: DC block, biquads, decimation, YIN, note state machine, bend | none (header says so: `tracker.js:3-6`) |
| `js/audio/guitar/poly/engine.js` | 301 | POLY engine: resampler, two Hann windows + FFT, flux, octave guards, AGC, decomposer + rule, ECO load control | `performance.now()` for cost metering only (`engine.js:73`) |
| `js/audio/guitar/poly/nmf.js` | 212 | sparse beta-NMF step (beta 0.5), active set, warm start | per-hop `Array.from().sort()` (see 2.3) |
| `js/audio/guitar/poly/bank.js` | 121 | template bank renderer (122 columns), Hann window, window kernel | none |
| `js/audio/guitar/poly/notes.js` | 207 | per-hop note rule (onset window, agree, guards, note-off) | `Map` for voices (`notes.js:67`) |
| `js/audio/guitar/poly/fft.js` | 66 | radix-2 complex FFT, magnitude output | `Math.hypot` (`fft.js:46`) |
| `js/audio/guitar/poly/resample.js` | 138 | polyphase Kaiser resampler to 24 kHz (scipy `resample_poly` kernel) | per-output-sample callback (`resample.js:133`) |
| `js/audio/guitar/poly/profile.js` | 39 | data: B law and partial profiles per string (generic bank) | none (generated file, `profile.js:1-4`) |
| `js/audio/guitar/poly/calibrate.js` | 291 | calibration DSP: pluck capture, 65536-point spectrum, inharmonicity fit, partial profile, `makeProfile` | `Map`, `Array.from().sort()` (offline code, fine) |
| **Total DSP** | **1 925** | | |

Of these, the real-time path is 1 634 lines (everything but `calibrate.js`);
`calibrate.js` runs offline on a captured buffer (`calibrate.js:152-193`) and can live on a
background thread in the plugin.

### 1.2 Stays in the browser (or is re-done natively in the plugin shell)

| File | Lines | What it does today | Plugin equivalent |
|---|---:|---|---|
| `js/audio/guitar/worklet.js` | 98 | `AudioWorkletProcessor`: hosts both engines, `sampleRate` global (`worklet.js:29,67`), `port.postMessage` of event arrays (`worklet.js:84`), message-driven mode / params / bank / flush / capture (`worklet.js:6-17`) | the `processBlock` + parameter/message plumbing of the plugin shell |
| `js/input/guitar.js` | 299 | `getUserMedia` with EC/NS/AGC off (`guitar.js:146-153`), device list, worklet load, ScriptProcessor fallback (`guitar.js:186-195`), `sounding` safety set (`guitar.js:45,52-68`), bank built on the main thread (`guitar.js:92-95`), latency readout (`guitar.js:293-297`) | the host owns the audio input and the buffer; only the `sounding` safety set and the bank-off-audio-thread logic must be re-done |
| `js/audio/guitar/poly/profiles.js` | 75 | IndexedDB store of calibration profiles, JSON export/import (`profiles.js:6-8,64-75`) | JSON files on disk + plugin state chunk |
| `js/ui/calibration.js` | 243 | the calibration dialog (one pluck per string, optional 12th fret, live level, summary, naming) | plugin UI (section 3.4) |
| `js/ui/rack.js` (guitar card part) | ~60 | knobs SENS / DECAY / DYN / BEND greyed in POLY (`rack.js:233-247`), BUFFER select (`rack.js:220-226`), PROFILE select (`rack.js:251-260`) | plugin parameters + editor |
| `js/main.js` routing | — | `routeNoteOn/Off/Bend` into the rack's instruments (`main.js:219,240,313`) | the DAW does this: the plugin emits MIDI |
| `js/state.js` | — | persisted `state.guitar` (`state.js:73-86`) | plugin state |
| `js/i18n/strings.js` | — | 12-language labels (`strings.js:150-165`) | optional; a plugin UI is usually English-only at first |

Measured sizes that matter for the plugin: the dense bank is 122 x 1025 float32 =
500 200 bytes (`bank.js:86,89`, K = 6 x 20 + 2 at `bank.js:85`); the sparse CSR copy
at tau = 1e-3 keeps about 250 bins per column (`nmf.js:21-22`), i.e. roughly
122 x 250 x (4 + 4) bytes = 240 KB (`nmf.js:77-80`). Building the bank takes ~50 ms
(`guitar.js:93`, measured 53-56 ms in `docs/poly-implementation.md:86`) and must happen
off the audio thread, exactly as the worklet already requires (`worklet.js:7-10`).

---

## 2. JS-specific and Web-specific dependencies inside the DSP, and the mapping

### 2.1 Numeric types: what is float32 today, what is float64

The question "does the engine already use Float32Array?" has a precise answer:
**storage is mixed, arithmetic is always IEEE double** (every JS number is a double;
a `Float32Array` store rounds to float32, a load promotes back to double).

MONO (`tracker.js`):
- float32 storage: `rmsHist`, `hfHist` (`tracker.js:127-128`), `midiHist` (`tracker.js:143`,
  uses `NaN` as a sentinel, `tracker.js:396`), the decimated `ring`, the YIN window `win`,
  the difference `d` and the normalised difference `cmnd` (`tracker.js:177-182`).
- double state: biquad coefficients and `z1/z2` (`tracker.js:82-103`), DC blocker
  `dcX/dcY` (`tracker.js:121,199-200`), `sumSq/sumHf` (`tracker.js:201-203`), and every
  local accumulator (`sum` in the YIN difference loop, `tracker.js:497-503`).
- Rounding points a bit-faithful C++ port must keep: the store into `ring` after the
  low-pass (`tracker.js:207`), `d[tau] = sum` (`tracker.js:503`), `cmnd[tau]` (`tracker.js:510`),
  `H[k & HM] = rmsDb` (`tracker.js:245`), `midiHist` (`tracker.js:396`). With `float` arrays
  and `double` scalars in C++ this is reproduced exactly; a port that keeps everything in
  `float` will differ at the YIN dips (the difference function sums W = 512+ terms,
  `tracker.js:173`, in double today) and the parabolic interpolation (`tracker.js:539-546`).

POLY (`poly/*.js`):
- float64 everywhere in the hop path: `ring`, `frameM/S`, `V/Vs`, `dbHist`, `odd/low`
  (`engine.js:92-103`), FFT buffers and twiddles (`fft.js:15-18`), resampler kernel and
  history (`resample.js:36,73-85`), NMF `h/sal/ha/y/g1/g2/P` (`nmf.js:48-56`), note-rule
  `levels/Phist/before` (`notes.js:61-64`).
- float32 only for the bank `W` (`bank.js:86`) and its sparse copy `sVal` (`nmf.js:79`).
  The Python prototype's bank is float32 too, and the JS bank matches it to 3e-8
  (`docs/poly-implementation.md:85-86`).
- Consequence for a port: **a double-precision C++/Rust port with a float32 bank is the
  faithful one.** A float32-only port changes the NMF recurrence: the activations `h` are
  carried from hop to hop as a warm start (`nmf.js:4,48,143-150`), so a float32 difference
  in one hop feeds the next. Whether that drift changes decisions cannot be deduced; it
  must be measured with the oracle of section 4 (the shipped sparse setting already
  differs from the exact prototype by 2 missing and 1 extra note out of 13 624 plus 31
  onsets shifted by one hop, `docs/poly-implementation.md:93-107`, so "same notes at
  +-1 hop" is the realistic bar, not bit identity).
- Transcendental functions: `Math.log10/log2/pow/sqrt/sin/cos/exp` are V8's
  implementations; a C/Rust `libm` can differ by an ulp. Every threshold comparison in
  the rule (`notes.js:129,143-144,159`) or the tracker (`tracker.js:302,320,349`) can in
  principle flip on an ulp. Expected effect: rare +-1 hop shifts, which the oracle
  tolerates. `Math.hypot` (`fft.js:46`) maps to `std::hypot`; `sqrt(re*re + im*im)` is
  faster and differs by an ulp at most.

### 2.2 Sample-rate assumptions

- MONO decimates by `D = round(sampleRate / 24000)` (`tracker.js:110-111`): 48 kHz and
  96 kHz give exactly 24 kHz; 44.1 kHz gives 22 050 Hz; 88.2 kHz gives 22 050 Hz;
  192 kHz gives 24 kHz. All YIN lag ranges and windows are derived from `srD`
  (`tracker.js:167-183`), so the tests pass at 44.1/48/96 kHz with 128 and 256-sample
  blocks (`test/guitar-tracker.test.mjs:160-167`). The biquads are designed at the input
  rate (`tracker.js:118-120`). The "24 kHz" is therefore a target, not an assumption; a
  host at 44.1 kHz is covered.
- POLY resamples to exactly 24 000 Hz with a rational polyphase filter
  (`resample.js:58-63`, `bank.js:16`): 48 kHz is 1/2 with 41 taps, 44.1 kHz is 80/147 with
  2 941 taps of which 37 per output sample (`resample.js:9-12`), group delay 0.42 ms at
  both (`engine.js:142-144`, `test/poly-engine.test.mjs:66-68`). Any host rate works;
  192 kHz would be 1/8 with 161 taps per output sample (from `resample.js:66-67`,
  `halfLen = 10 x max(up, down)`), i.e. about 4x the 48 kHz cost of the resampler, which
  is itself negligible next to the NMF.
- Block size: both engines are fed sample by sample (`tracker.js:196-214`,
  `engine.js:148-152,179-185`), so the host buffer size is irrelevant to the decisions.
  It matters only for event timestamps (2.4) and for the CPU budget (5.2).

### 2.3 Allocations, closures and containers on the audio path (must be removed in a port)

- `nmf.js:125,132,138`: three `Array.from(...).sort(comparator)` per hop to build the
  active set (strongest 60 of 122 templates, then ascending column order). In C++ or
  Rust: preallocated index arrays and `std::partial_sort` / `select_nth_unstable` on
  them; the ascending reorder exists only to reproduce numpy's summation order
  (`nmf.js:137`), and must be kept for equivalence.
- `notes.js:67`: `voices` is a `Map` of at most `maxVoices` = 6 entries (`notes.js:41`);
  iteration order of a JS `Map` is insertion order, which the weakest-voice search
  (`notes.js:183-187`) and `flush` (`notes.js:200`) depend on only through ties. A
  fixed array of 44 pitch slots (`N_PITCH`, `bank.js:24`) plus an insertion-ordered list
  of 6 reproduces it.
- `engine.js:129,289-299`: `_sent` map from raw pitch to emitted midi (octave shift at
  note-on time); 44 entries max, becomes an `int8` array.
- Event objects: `out.push({ t: 'on', ... })` (`tracker.js:404-407,417-446`,
  `notes.js:192,205`, `engine.js:292,297`) allocate per event; `_yin` returns a fresh
  `{ hz, conf }` per frame (`tracker.js:525,548`); `_pitchAtOnset` allocates `[128, 256]`
  per call (`tracker.js:461`); `this.pend = { ... }` per onset (`tracker.js:277`). In a
  port these are structs in a fixed-capacity ring (an event FIFO of, say, 64 entries per
  block is far above the maximum of 6 voices x on/off plus a few bends).
- `resample.js:108-137`: `push(input, onSample)` calls a closure per output sample
  (`engine.js:130,150`). In C++ the resampler writes straight into the engine's ring
  (`engine.js:179-185`) or returns a count; no closure.
- `engine.js:73,189,243-245,251-255`: `performance.now()` three times per hop for the CPU
  meter and the ECO control. Maps to `std::chrono::steady_clock` (cheap, but note that
  the ECO budget definition changes in a plugin, section 5.2).
- `tracker.js:150-157`, `engine.js:134-140`: `setParams` with `Object.keys` and object
  spread. Becomes atomics or a parameter FIFO from the UI thread.
- `tracker.js:399-401`: the `onFrame` tracing hook (unset in production) and the
  `TUNING` / `NOTE_RULE` / `DECOMPOSER` mutable exports used by the ablation scripts
  (`tracker.js:51`, `notes.js:28`, `engine.js:46`, `test/poly-dump-events.mjs:26-27`).
  A port should keep them as runtime-settable structs: the equivalence harness relies on
  them (dense NMF for exact arithmetic, `docs/poly-implementation.md:176-177`).

### 2.4 Event timing

- MONO events carry no sample position; the tests stamp them at the end of the block
  that produced them (`test/plucks.mjs:108-117`). The decision happens in `_frame`
  (`tracker.js:229`), once every 64 decimated samples. For sample-accurate MIDI in a host
  buffer, the port adds a sample counter at the `_frame` call (`tracker.js:209-212`) and
  stamps each event with the offset inside the host buffer. This is a 5-line change.
- POLY events carry `hop` (`engine.js:20-23`), and the host stamps them at
  `(hop + 1) x 64 / 24000 s` plus the resampler delay (`engine.js:142-144`); the Node
  harness does exactly this (`test/poly-engine.test.mjs:22-30`). Same mapping in a plugin.

### 2.5 Out-of-range reads: silent in JS, undefined behaviour in C++

A JS typed-array read outside its bounds returns `undefined` (NaN after arithmetic)
instead of crashing. The code guards its indices (`notes.js:109,138`, `nmf.js:144`,
`bank.js:66-67`, `tracker.js:492-493` where `n = W + tHi <= win.length` by construction,
`tracker.js:174,180`), but a C++ port has no safety net. The cheap, decisive safeguard is
to run the whole equivalence corpus (section 4) under AddressSanitizer and
UndefinedBehaviorSanitizer once. Rust gets bounds checks by default (section 6).

### 2.6 Denormals

- The NMF never divides by zero: `y` is pre-filled with 1e-9 (`nmf.js:30,154`) and the
  warm start floors `ha` at `1e-4 x max(vmax, 1e-6)` (`nmf.js:144-147`). Activations can
  still shrink multiplicatively towards 0 inside the 8 updates (`nmf.js:184,195`); in
  double they never reach the denormal range (2e-308); in a float32 port they could, and
  a denormal `a` then enters the accumulate loop (`nmf.js:157-160`).
- The MONO biquads and DC blocker (`tracker.js:97-102,199-200`) decay geometrically on
  digital silence: with the 0.995 pole, a unit input decays to 1e-308 after about
  141 000 samples (2.9 s at 48 kHz); double denormals are then possible in the browser
  too, but unobservable (real inputs carry a noise floor; the tests use -80 dBFS noise,
  `test/plucks.mjs:64-69`).
- Standard remedy in a plugin: flush-to-zero / denormals-are-zero on the audio thread
  (JUCE `ScopedNoDenormals` or the equivalent MXCSR bits; general knowledge, not verified
  here). This changes nothing measurable for the oracle.

---

## 3. Parameters, MIDI semantics, calibration in a plugin

### 3.1 Parameters to expose

From `state.js:73-86` (the persisted card state) and the engine defaults
(`tracker.js:29-39`, `engine.js:35-38`):

| Plugin parameter | Source | Range / default | Engine mapping |
|---|---|---|---|
| GAIN | `state.guitar.gain` 0.1..10 (`state.js:76`), applied by a `GainNode` (`guitar.js:275-278`) | 0.1..10, default 1 | multiply the input block before the engine (POLY also has an internal AGC on the spectrum, `engine.js:53-63,233-240`) |
| SENS | `sens` 0..1 (`state.js:77`) | default 0.5 | MONO: gate -36..-60 dBFS and rise 12..6 dB (`tracker.js:161-162`); POLY: not used (greyed, `rack.js:233`) |
| DECAY | `release` 0..1 (`state.js:78`; the card label is DECAY, `strings.js:151`) | default 0.5 | MONO: note-off 15..45 dB under the peak (`tracker.js:163`); POLY: fixed `releaseDb` 20 (`notes.js:36`), could be mapped but is untuned |
| DYN | `dyn` 0..1 (`state.js:79`) | default 0.7 | MONO velocity law (`tracker.js:411-414`); POLY has its own fixed law (`notes.js:189`) |
| BEND | `bend` bool (`state.js:80`) | default on | MONO: pitch bend vs chromatic retrigger (`tracker.js:349,364-365`); POLY: no bend exists (`docs/poly-implementation.md:62-65`) |
| OCTAVE | `octave` -2..2 (`state.js:81`) | default 0 | `shift = 12 x octave + transpose` (`tracker.js:164`, `engine.js:138`) |
| TRANSPOSE | engine-only today (`tracker.js:34`, `engine.js:37`) | -12..12, default 0 | same |
| MODE | `mode` mono/poly (`state.js:82`) | default mono | engine switch with flush (`worklet.js:62-77`) |
| PROFILE | `profileId` (`state.js:83`) | generic or a saved profile | re-render the bank off-thread, swap, flush (`worklet.js:39-47`, `guitar.js:98-110`) |
| A4 / FMIN / FMAX | `tracker.js:36-38` | 440 Hz; 70..1400 Hz | optional "bass mode" (fmin 30, `tracker.js:36`) |
| BUFFER | `state.audio.buffer` min/balanced/safe (`state.js:177`) -> `latencyHint` 0 / 0.02 / 0.03 (`js/audio/engine.js:22-24`) | — | **disappears: the host owns the buffer size** |
| INPUT device | `deviceId` (`state.js:75`) | — | disappears: the host routes audio to the track |

The MONO/POLY greying rule of the card (`rack.js:233-247`) should be kept in the plugin
UI so the owner does not expect SENS/DECAY/DYN/BEND to act in POLY.

### 3.2 MIDI output semantics

- Note-on: `midi` 0..127 after shift (POLY drops out-of-range notes, `engine.js:290`;
  MONO does not clamp: add a clamp in the shell), velocity 0.05..1 in MONO
  (`tracker.js:413`), 0.1..1 in POLY (`notes.js:189`) -> scale to 1..127.
- Note-off: MONO always releases the sounding note before a new one (`tracker.js:417`)
  and on level / periodicity loss (`tracker.js:367-368,384-391`); POLY on decay or
  silence (`notes.js:153-165,99-104`). The browser shell keeps a `sounding` set and, in
  MONO, releases whatever is held if a note-off names an unknown pitch (a shift change
  mid-note mislabels the note-off, `guitar.js:52-55,56-68`). The plugin shell must keep
  that same safety set; POLY handles it internally (`engine.js:285-300`).
- Pitch bend (MONO only): a stream of `{ t: 'bend', semis }` within +-2 semitones
  (`BEND_RANGE`, `tracker.js:58`), emitted when the smoothed value moves by more than
  0.03 semitone (`tracker.js:440-448`), reset to 0 at every note-on and note-off
  (`tracker.js:420,433`). In the rack the value is applied in semitones directly
  (`main.js:313-315`, `instance.js:359-361`). A plugin has to emit 14-bit pitch wheel
  values, which means **declaring a bend range the receiving instrument must match**:
  expose a RANGE parameter (2 / 12 / 24 / 48 semitones) and scale `semis / range x 8192`;
  default 2 to match `BEND_RANGE`. The 0.03-semitone granularity is 123 wheel steps at
  range 2, so no extra smoothing is needed. MPE is not required: MONO has one voice,
  POLY has no per-note pitch.
- Meter / tuner events (`tracker.js:403-407`: `db, hz, midiF, conf, note, latMs`;
  `engine.js:256-258`: `db, hop, nmfMs, hopMs, voices, eco, gainDb`) go to the UI
  through a lock-free FIFO, never as MIDI.
- Flush: on transport stop, bypass, mode change or profile change the engines' `flush()`
  (`tracker.js:219-225`, `engine.js:162-167`) must produce the note-offs, plus a bend
  reset; the worklet does this on every mode switch (`worklet.js:62-64`).

### 3.3 Latency to report to the host

The engines are detectors, not delays: the host's plugin-delay compensation cannot
absorb the 12-36 ms of tracking (`docs/guitar-to-midi.md:13-17`). Report 0 samples of
latency (or the resampler's 0.42 ms, `engine.js:142-144`) and show the measured
onset-to-note-on time (`lastLatMs`, `tracker.js:144,325,361`) in the UI, as the card
does.

### 3.4 Calibration assistant and profiles in a plugin

- Generic bank first: the plugin ships `profile.js` data (`profile.js:14-32`) and renders
  the bank at load (`bank.js:81-121`), as the browser does (`worklet.js:67`).
- Profile format: the JSON already defined by `exportProfile`
  (`profiles.js:64-66`): `{ format: 'jamrack-guitar-profile', version: 1, id, name,
  created, bLaw[6][2], prof[6][40], tuning[6], measured[6] }`, validated by
  `validProfile` (`profiles.js:33-37`: B in (0, 1e-2), slope in (0.3, 2)). A plugin can
  read and write the same files (a folder in the user's documents) and store the
  selected profile in its state chunk, so profiles made in the browser transfer to the
  plugin and back.
- Calibration DSP: `calibrate.js` is a pure port of `test/poly/bank/extract.py`
  (`calibrate.js:7-20`); it needs a 65 536-point FFT (`calibrate.js:27,47`) and a
  31-second float32 capture buffer of about 3 MB (`calibrate.js:206`). `PluckCapture`
  consumes the raw input blocks (`calibrate.js:220-224`), so in a plugin it runs on the
  audio thread only for the copy into its buffer, and `analysePluck` runs on a worker
  when the capture completes (`calibrate.js:254-256`). Requirement: the plugin must
  receive the dry guitar, i.e. be placed first on the track with no amp simulation
  before it (same advice as `docs/guitar-to-midi.md:170-172`).
- UI needed: the dialog state machine of `js/ui/calibration.js` (243 lines): one open
  string at a time, optional 12th fret for the B slope (`calibrate.js:277-282`), live
  level bar, named refusals `tooQuiet / noPartials / wrongNote` with the note heard
  (`calibrate.js:169,182,185,190`), summary, naming, delete, import/export.
- Accuracy on synthetic strings: B within 12 %, f0 within 3 cents
  (`docs/poly-implementation.md:47-48`; `test/poly-calibrate.test.mjs`, 4 tests passing
  here).

---

## 4. Test assets that transfer, and the equivalence oracle

| Asset | Lines | Transfers how |
|---|---:|---|
| `test/plucks.mjs` | 223 | synthetic plucks, chords, strums, repicks, palm mutes with a deterministic LCG (`plucks.mjs:8-14`). Either port the generator or, simpler, render the signals once from Node into WAV files and feed both implementations the same files (one generator, two engines). |
| `test/guitar-tracker.test.mjs` | 259 | 17 MONO tests: per-string latency bounds, noise/knock rejection, repick, legato, bend vs chromatic, missing fundamental, cents accuracy, velocity, octave/transpose, chromatic run, decay note-off, 44.1/96 kHz and 256-sample blocks, flush, CPU, two POLY-baseline chord tests. Re-expressed as event-list diffs once the C++ build dumps events. |
| `test/poly-engine.test.mjs` | 149 | 9 POLY tests: FFT vs direct DFT at 1e-9, resampler kernel and 1 kHz pass-through, bank norms and peaks, single-template decomposition, hiss rejection, 8 single plucks (note-on < 60 ms, note-off < 80 ms), a dyad, hop cost. The first four are closed-form checks a port should reproduce verbatim. |
| `test/poly-calibrate.test.mjs` | 95 | calibration on synthetic strings (B, f0, profile, bank from a profile). |
| `test/dump-events.mjs` | 90 | MONO: WAV -> events JSON `{ label, source, takes: { take: { duration, events: [{ onset, offset, midi, velocity, why }] } } }` (`dump-events.mjs:7-10`), 16-bit PCM WAV reader (`dump-events.mjs:17-32`). |
| `test/poly-dump-events.mjs` | 133 | POLY: same JSON, hops aligned on scipy's zero-phase resampling by dropping the first `delay` samples (`poly-dump-events.mjs:9-14,37,65`), `--align live` for the causal stream, cost mean / p99 / max per take (`poly-dump-events.mjs:100-103`), `DECOMP` / `RULE` env overrides (`poly-dump-events.mjs:26-27`). |
| `test/score.py` | 375 | mir_eval scorer: F1 at +-50 / +-20 ms with and without offsets, recall by chord size, ghosts, stuck, octave, early notes, latency corrected by the annotation lag (`score.py:20-35`). Reads the JSON above. |
| `test/takes.json` | — | the fixed GuitarSet bench list (24 solo + 12 comp takes), with the rule that generated it. |
| `test/poly/make_mixes.py`, `gate.py` | — | the six-set bench (solo, comp, mix2, mix3, hex2, hex3; 13 624 notes) and the section-12 gate table. |
| `test/guitarset-eval.mjs`, `test/trace.mjs`, `test/poly-latency.mjs` | 108 / 24 / 161 | MONO evaluation with the `why` field, frame-by-frame trace, window latency measurement: tuning aids, not needed for the port. |

How to hold the port to the same standard as the JS engine:

1. **Reference dumps.** GuitarSet is not in the repository (4.3 GB,
   `python3 test/fetch-guitarset.py <dir> --hex`, CLAUDE.md); the event dumps the docs
   refer to live in a scratch `OUT/` directory (`docs/poly-implementation.md:72-80`) and
   are **not versioned**. Step one of the port is therefore to regenerate the JS dumps
   once (`node test/dump-events.mjs --guitarset ...`, `node test/poly-dump-events.mjs
   --guitarset ... --set solo|comp`, and the mixes) and commit them, or at least keep
   them next to the port.
2. **Which reference.** The JS POLY engine matches the Python prototype note for note in
   dense mode on all six sets (13 624 / 13 624, `docs/poly-implementation.md:95-104`);
   the shipped sparse tau = 1e-3 / 8-iteration setting (`engine.js:46`) is held to scores
   within 0.1 F1 point (`docs/poly-implementation.md:151-165`). So the port's reference
   is the **JS engine at the same settings**, with the dense mode
   (`DECOMP='{"sparse":0,"iter":15}'`) available for an exact-arithmetic check.
3. **Comparison.** `score.py` scores each dump against the annotations but does not diff
   two dumps note for note; the "same midi, onset within +-1 hop" comparison quoted in
   the docs (`docs/poly-implementation.md:79-80`) is not a script in the repository (I
   found none in `test/` or `test/poly/merge.py`, whose subcommands are
   `analyze / grid / events / time / bank`). A 50-line Python diff (same take, same
   midi, onset within 2.67 ms, offset within one hop, velocity within 1e-3) is needed;
   the acceptance bar is the one the JS sparse engine met: a handful of notes out of
   13 624 and onsets within one hop.
4. **Synthetic corpus.** Even without GuitarSet, the plucks corpus (section 4 table) gives
   a hardware-free regression suite that fails loudly: it is what the Node tests run in
   15.7 s here (`node --test test/guitar-tracker.test.mjs test/poly-engine.test.mjs`).
5. **Cost benchmark.** `test/poly-engine.test.mjs:142-149` and the per-take cost lines of
   `poly-dump-events.mjs` are the benchmark. Measured here (Node v22.22.0, 4-core
   container, shipped setting): decomposer **0.689 ms/hop**, whole hop **0.971 ms**
   average, **2.73 ms** max; MONO: 1 s of audio in **136 ms** (13.6 % of a core). On the
   owner's PC: 1.10 ms/hop in Node, 1.62 ms (61 % of the 2.67 ms hop) inside the Chrome
   worklet with the synth running (`docs/poly-implementation.md:158,164-165`). The port's
   first C++ build should print the same three numbers on the same takes.

---

## 5. Effort estimate and risks

Assumptions: the port is done by agent sessions that can build and run CLI harnesses
but cannot open a DAW or hear a guitar; the owner tests each milestone with the
Gigcaster and Ableton Live on Windows (CLAUDE.md). "Session" = one focused agent
session of the kind that produced the POLY port (jalon 1 was one branch of work from
Python to JS with equivalence, `docs/poly-implementation.md:1-5`). Human-equivalent days
are for one experienced audio developer.

| Block | What it covers | Agent sessions | Human days | Reasoning |
|---|---|---:|---:|---|
| A. MONO tracker | 550 lines -> ~600 lines of C++/Rust, float/double rounding points (2.1), event FIFO with sample offsets (2.4), CLI harness dumping the JSON of `dump-events.mjs`, plucks corpus diff | 1-2 | 2-3 | straight arithmetic, no containers; the only subtlety is matching the float32 stores |
| B. POLY engine | fft 66, resample 138, bank 121, profile 39, nmf 212, notes 207, engine 301 = 1 084 lines; remove per-hop sorts/allocs (2.3), fixed voice table, bank build on a worker + atomic swap, dense/sparse switch, JSON dump, diff script, ASan/UBSan run | 2-3 | 5-8 | the arithmetic is simple but the equivalence work is the job: the JS port took the same path and needed the dense mode to prove itself |
| C. Plugin shell | audio-in / MIDI-out processor, parameters of 3.1, state chunk, sounding safety set, flush on stop/bypass, meter FIFO, latency report, ECO budget tied to the host buffer (5.2) | 1-2 | 3-5 | boilerplate in either framework; the host-specific MIDI-output quirks are the unknown (covered by the DAW-side topics of this study) |
| D. UI | generic parameter editor is free; a card-like editor (level, tuner / voices, CPU + ECO, mode, profile menu) | 1-2 | 3-5 | the browser card is the spec (`rack.js:220-260`) |
| E. Calibration | `calibrate.js` port (291 lines, offline, 65 536 FFT), capture on the audio thread, worker analysis, dialog of `js/ui/calibration.js` (243 lines), JSON profile files in the format of `profiles.js:64-75` | 2 | 4-6 | the DSP is already a port of a tested extractor; the dialog is the larger part |
| F. Installers / CI | Windows VST3 (+ CLAP) first (the owner's platform), then macOS AU/VST3 with signing and notarisation, Linux VST3; GitHub Actions matrix | 1-2 | 2-4 | mostly waiting on certificates and CI; the agent can set up the workflow, the owner must hold the Apple account |
| **Total** | | **8-13** | **19-31** | plus the owner's test time, which is the serial bottleneck (a session cannot hear a guitar) |

### 5.1 Risks, in order of likelihood

1. **Real-time safety.** Every item in 2.3 is a lock or an allocation on the audio thread
   in a naive port. The bank rebuild (50 ms) and the calibration analysis (65 536-point
   FFTs) must never run in the callback; the browser already enforces this by
   construction (`worklet.js:7-10`, `calibrate.js:254-256`), the plugin must do it with a
   worker thread and an atomic pointer swap followed by a flush.
2. **CPU budget per callback (POLY).** A hop completes every 128 input samples at
   48 kHz (`bank.js:17`, resampler 1/2). With a host buffer of 64 samples (1.33 ms) the
   callback that completes a hop must run the whole decomposition inside 1.33 ms, not
   2.67 ms. The JS average fits (0.97 ms here), the max does not (2.73 ms here, 4.05 ms
   on the owner's loaded PC, `docs/poly-implementation.md:146-149`). A native build
   removes GC and JIT jitter but the ECO controller must compare the hop cost to
   `min(hop, host buffer)` instead of the hop alone (`engine.js:264`), or the decomposer
   must move to a worker thread with one hop of added latency. This is the one design
   decision to settle before block C.
3. **Numeric drift in a float32-only port** (2.1): avoid by keeping double arithmetic;
   measure with the oracle if float32 is chosen for SIMD.
4. **Denormals** (2.6): FTZ/DAZ on the audio thread; no code change.
5. **SIMD: not needed first.** The JS engine already meets the budget at 61 % in a
   worklet shared with a synth (`docs/poly-implementation.md:164-165`); a scalar native
   build of the same loops is expected to be faster (unmeasured; general expectation, not
   verified here). The sparse CSR layout (`nmf.js:94-110,155-185`) is a gather pattern
   that vectorises poorly anyway; if headroom is ever needed, the dense layout
   (`nmf.js:103-110,163-168,187-196`) vectorises trivially and is the exact-arithmetic
   mode.
6. **Threading of the bank rendering.** The renderer is a single pass over 122 columns
   x 1025 bins x up to 40 partials (`bank.js:56-72,99-108`); it is already fast enough
   (53-56 ms) on one thread and only runs at load and after a calibration. No
   parallelism needed; just not on the audio thread.
7. **Undefined behaviour** (2.5): one sanitizer run over the corpus.

---

## 6. C++ (JUCE or DPF) versus Rust (nih-plug) for this code

What the code is: 1 634 lines of scalar loops over typed arrays, a handful of small
state machines, no recursion, no dynamic structures larger than 6 voices, double
arithmetic with float32 storage in a few places, and an equivalence oracle that
tolerates +-1 hop. Both languages map it one to one; the differences are in what each
one prevents.

Rust (nih-plug) for the DSP core:
- Typed arrays become `Vec<f32>` / `Vec<f64>` slices with bounds checks: the silent
  NaN-on-overrun of JS (2.5) becomes a panic in debug and can be kept or removed per loop
  in release. No UB class of bugs, so the port is less error-prone to write.
- The NMF inner loops (`nmf.js:94-110,155-185`) are iterator-shaped; the compiler
  vectorises them about as well as C++ does.
- `cargo test` mirrors the Node tests one to one; the JSON dump harness is a 100-line
  binary with `serde`.
- The project already chose Rust for the native-helper design (`docs/guitar-to-midi.md:135-141`,
  cpal + midir).
- Costs: the plugin ecosystem is younger; GUI toolkits are less mature; AU export is not
  native to nih-plug as far as I know (general knowledge, not verified here); fewer
  reference plugins to copy host quirks from.

C++ (JUCE):
- Covers every target at once: VST3, AU, AAX, CLAP (via an extension), and a
  **standalone application**, which is exactly the "native helper" of
  `docs/guitar-to-midi.md:125-141` (ASIO input, virtual MIDI out) for free.
- Parameter / state / editor boilerplate is the most documented in the field; a generic
  editor exists; denormal guards exist (general knowledge, not verified here).
- Costs: UB is possible on every index (2.5); the port needs the sanitizer run and the
  float/double discipline of 2.1 enforced by code review, because nothing in the
  language enforces it.
- Licensing: JAMRACK is MIT (`LICENSE`); JUCE's free tier requires the plugin to be
  GPL-licensed, DPF is permissive (general knowledge, not verified here). This is a
  decision for the owner, not a technical blocker.

DPF: lighter than JUCE, permissive, but a smaller community and less GUI tooling;
worth it only if the licence matters more than the standalone/AU coverage.

Verdict for this specific code: the **DSP port itself is less error-prone in Rust**
(bounds checks, no UB, same double arithmetic, tests that mirror Node); the **plugin
shell is less error-prone in JUCE** (host coverage, standalone target, editor). Since
the owner's DAW is Ableton Live on Windows, VST3 is the one format that matters first and
both frameworks produce it. Two defensible paths:

1. **All C++ with JUCE**, DSP in a separate static library with its own CLI harness and
   sanitizers: one toolchain, every format, standalone for free. Recommended if the AU /
   macOS / standalone targets are wanted within the year.
2. **Rust core + nih-plug** (VST3 + CLAP): fewest ways to get the DSP wrong, one
   `cargo build`, GPL-free; add a C ABI later if an AU or JUCE shell becomes necessary.
   Recommended if the deliverable is "Windows VST3 for Ableton, soon".

Either way the DSP must be a separate library with the dump/diff harness as its only
dependency, so that the framework decision can be revisited without touching the port.

---

## 7. Sharing code with the web app: WebAssembly in the AudioWorklet

Feasibility: yes. An AudioWorklet can instantiate a WebAssembly module (compiled on the
main thread and passed through `processorOptions`, like the bank is today,
`guitar.js:175-183`), copy each 128-sample block into linear memory, and read events
back from a ring in that memory. A freestanding C++ (clang `--target=wasm32`, no
Emscripten runtime) or Rust (`wasm32-unknown-unknown`) build of the 1 634-line core
would be a few tens of kilobytes; the bank (500 KB) is rendered inside the module at load
as it is now. (The mechanics are general knowledge, not verified here; nothing in the
repository does this today.)

What it would change:

- **The zero-dependency, no-build rule** (CLAUDE.md: "aucun build, zéro dépendance").
  A `.wasm` file in `js/` is a committed binary produced by a compiler the repository
  does not otherwise need. Static hosting and "no npm" survive; "no build" and
  "readable source in the browser" do not. Two honest options: (a) keep the JS engines as
  the canonical implementation and the native port as a second implementation held to
  them by the oracle of section 4 (what this document assumes); (b) make the native core
  canonical, commit the `.wasm` with its build command and a CI check that the committed
  binary matches the source, and delete the JS engines. Option (b) removes 1 634 lines of
  JS but makes every DSP change require a compiler, which the owner's workflow (editing
  in cloud sessions, testing on gh-pages) does not have today.
- **Performance.** MONO gains nothing (`docs/guitar-to-midi.md:147-148`: the JS version
  uses a tenth of a core). POLY is the only part near its budget (61 % in the worklet on
  the owner's PC); a wasm build might lower it, but this is unmeasured and wasm's f64
  SIMD (`f64x2`) only doubles throughput at best on the dense path. Not a reason by
  itself.
- **Equivalence.** Both builds would run the same source, so the oracle collapses to a
  libm/rounding check: fewer drift risks than two implementations, which is the real
  argument for option (b) in the long run.
- **ScriptProcessor fallback** (`guitar.js:186-195`) and the Node tests would also load
  the wasm (Node runs WebAssembly natively), so the three execution contexts of today
  (`tracker.js:3-6`) remain.

Recommendation: do not start with wasm. Port natively, prove equivalence with the
section-4 oracle, ship the plugin; revisit wasm only if the two implementations start
to diverge in maintenance or if POLY needs headroom in the browser.

---

## Unverified statements (general knowledge, not checked against sources in this task)

- nih-plug exports VST3 and CLAP, not AU; JUCE covers VST3 / AU / AAX / standalone and
  CLAP through an extension; JUCE's free tier imposes GPL on the plugin; DPF is
  permissively licensed.
- Ableton Live loads VST3 and AU plugins, not CLAP.
- A scalar native build of the NMF loops is faster than V8's JIT output (expected, not
  measured).
- The mechanics of running WebAssembly inside an AudioWorklet (`processorOptions`
  transfer, linear-memory ring) and the size of a freestanding wasm build.
- Denormal handling via FTZ/DAZ in JUCE (`ScopedNoDenormals`) or raw MXCSR bits.

Everything else above is read from the repository at `bcbb30d` (file:line references)
or measured in this container (Node v22.22.0, 4 cores, 7 October 2026).
