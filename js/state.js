// Global state, localStorage persistence and a small pub/sub

import { uid, debounce } from './util.js';
import { detectLanguage } from './i18n/languages.js';

const LS_KEY = 'jamrack-state-v1';

export const BUILTIN_BANKS = [
  { id: 'musyngkite', name: 'MusyngKite · HQ', url: 'https://gleitz.github.io/midi-js-soundfonts/MusyngKite/', builtin: true },
  { id: 'fluidr3', name: 'FluidR3 GM', url: 'https://gleitz.github.io/midi-js-soundfonts/FluidR3_GM/', builtin: true },
  { id: 'fatboy', name: 'FatBoy', url: 'https://gleitz.github.io/midi-js-soundfonts/FatBoy/', builtin: true },
];

// Parameters of the modern analog engine (js/audio/instance.js).
// Everything here is per-voice unless stated otherwise.
export function defaultSynth() {
  return {
    // --- oscillators ---
    unison: 1,          // 1..7 detuned copies per oscillator (supersaw when >1)
    uniDetune: 0.18,    // 0..1 spread between unison voices
    uniSpread: 0.7,     // 0..1 stereo width of the unison stack
    osc2: 'off',        // 'off' | 'sawtooth' | 'square' | 'triangle' | 'sine'
    osc2Semi: 0,        // second oscillator pitch offset, in semitones
    osc2Mix: 0.5,       // 0..1 level of the second oscillator
    sub: 0,             // 0..1 level of the sub oscillator (one octave down)
    subWave: 'sine',    // 'sine' | 'square'
    // --- per-voice filter (on top of the module-wide filter) ---
    fEnvAmt: 0,         // 0..1 how far the filter envelope opens the cutoff
    fAttack: 0.005,
    fDecay: 0.25,
    fSustain: 0.2,      // 0..1 sustained cutoff level
    // --- LFO ---
    lfoRate: 5,         // Hz
    lfoAmt: 0,          // 0..1 amount
    lfoTarget: 'filter',// 'filter' | 'pitch' | 'volume'
    // --- colouring ---
    drive: 0,           // 0..1 waveshaper saturation, what makes it "fat"
    glide: 0,           // portamento time in seconds
  };
}

// Parameters of the SAMPLER engine. Positions (start/end/loop) are fractions
// 0..1 of the sample length, so they stay meaningful whatever gets loaded.
export function defaultSampler() {
  return {
    sampleId: null,     // IndexedDB key (js/audio/sampledb.js); null = empty
    sampleName: '',
    rootMidi: 60,       // key that plays the sample at its recorded pitch
    keytrack: true,     // false = fixed pitch on every key (drums)
    tune: 0,            // cents
    gain: 1,            // 0..2
    start: 0,
    end: 1,
    loopOn: false,
    loopStart: 0.25,
    loopEnd: 0.75,
    reverse: false,
    oneShot: false,     // ignore note-off, play through
    chop: 0,            // 0 = off | 4 | 8 | 16 equal slices
    chopBase: 60,       // first slice key (C4 = where the PC keyboard sits)
    // Granular mode: pitch and time become independent. Keys shift the pitch
    // without changing duration; `stretch` changes speed without touching
    // pitch. `grain` (seconds) sets the texture of the engine.
    stretchOn: false,
    stretch: 1,         // 0.25..4, playback speed multiplier
    grain: 0.09,        // 0.03..0.2 s
  };
}

// The GUITAR → MIDI section (js/input/guitar.js + js/audio/guitar/). Whether
// it is running is never saved: starting it asks for the audio input, which
// must stay a deliberate gesture.
export function defaultGuitar() {
  return {
    deviceId: '',       // chosen audio input ('' = the browser's default)
    gain: 1,            // input gain 0.1..10
    sens: 0.5,          // 0..1 onset sensitivity
    release: 0.5,       // 0..1 how far a string may decay before note-off
    dyn: 0.7,           // 0..1 velocity dynamics
    bend: true,         // bends/vibrato become pitch bend (false = chromatic)
    octave: 0,          // -2..2
    collapsed: false,
  };
}

export function defaultInstance(overrides = {}) {
  return {
    id: uid(),
    // 'bank' (midi-js samples) | 'analog' (oscillators) | 'sfz' (SFZ bank)
    engine: 'bank',
    bankId: 'musyngkite',
    instrument: 'acoustic_grand_piano',
    sfzUrl: '',                // URL of a .sfz file (engine 'sfz')
    wave: 'sawtooth',          // analog engine
    detune: 7,                 // cents between the two analog oscillators
    preset: 'init',            // analog preset id (see js/audio/presets.js)
    // Modern synth section. Kept in its own object so older saved states simply
    // fall back to these defaults (see load()), and so the analog engine can
    // grow without cluttering the instance root.
    synth: defaultSynth(),
    volume: 0.8,
    pan: 0,
    octave: 0,
    transpose: 0,
    keyboardOn: true,
    rangeLo: 21,               // A0 — matches the range selectors
    rangeHi: 108,              // C8
    // Per-module note repeat: this module rolls its held notes at its own
    // division (hats at 1/16 while a pad holds a chord next to it).
    repeatOn: false,
    repeatDiv: '16',           // '4' | '8' | '8t' | '16' | '16t' | '32'
    sampler: defaultSampler(), // SAMPLER engine settings (see defaultSampler)
    adsr: { a: 0.002, d: 0.08, s: 1, r: 0.35 },
    filterCut: 1,              // 0..1 log -> 40 Hz..18 kHz
    filterRes: 0,
    revSend: 0.25,
    delSend: 0,
    mute: false,
    solo: false,
    collapsed: false,
    ...overrides,
  };
}

function defaultState() {
  return {
    instances: [defaultInstance()],
    master: {
      volume: 0.85,
      reverbDecay: 2.2,
      delayTime: 0.35,
      delayFeedback: 0.35,
    },
    kb: {
      octave: 4,        // the PC keyboard row starts at C4
      velocity: 100,
      layout: 'auto',   // 'auto' | 'azerty' | 'qwerty'
    },
    metronome: { bpm: 100, on: false },
    guitar: defaultGuitar(),
    customBanks: [],
    // low: lowest note shown on the piano (C3).
    // mobile: which view is showing on narrow screens ('play' | 'edit'),
    // ignored on desktop where both areas fit at once.
    view: { low: 48, mobile: 'play' },
    lang: null,         // null = follow the browser
  };
}

function load() {
  try {
    const raw = localStorage.getItem(LS_KEY);
    if (!raw) return defaultState();
    const saved = JSON.parse(raw);
    const st = defaultState();
    // defensive merge: start from the defaults, apply whatever was saved
    if (Array.isArray(saved.instances) && saved.instances.length) {
      st.instances = saved.instances.map(i => {
        const inst = defaultInstance(i);
        // defaultInstance spreads shallowly: nested objects saved by an older
        // version can miss keys added since, so merge them onto fresh defaults.
        inst.synth = { ...defaultSynth(), ...(i.synth || {}) };
        inst.sampler = { ...defaultSampler(), ...(i.sampler || {}) };
        inst.adsr = { ...defaultInstance().adsr, ...(i.adsr || {}) };
        return inst;
      });
      // Solo and note repeat are performance states, not patch settings: like
      // the metronome they never survive a reload (an app that machine-guns
      // the first key you press on arrival would be a trap).
      st.instances.forEach(i => { i.solo = false; i.repeatOn = false; });
    }
    Object.assign(st.master, saved.master || {});
    Object.assign(st.kb, saved.kb || {});
    Object.assign(st.metronome, saved.metronome || {});
    st.metronome.on = false;
    Object.assign(st.guitar, saved.guitar || {});
    if (Array.isArray(saved.customBanks)) st.customBanks = saved.customBanks;
    Object.assign(st.view, saved.view || {});
    if (typeof saved.lang === 'string') st.lang = saved.lang;
    return st;
  } catch {
    return defaultState();
  }
}

export const state = load();

// Language actually in use: the saved choice, or the browser's
export function effectiveLang() {
  return state.lang || detectLanguage();
}

const listeners = new Map(); // topic -> Set<fn>

export function on(topic, fn) {
  if (!listeners.has(topic)) listeners.set(topic, new Set());
  listeners.get(topic).add(fn);
  return () => listeners.get(topic).delete(fn);
}

export function emit(topic, data) {
  (listeners.get(topic) || []).forEach(fn => fn(data));
  scheduleSave();
}

const scheduleSave = debounce(() => {
  try {
    localStorage.setItem(LS_KEY, JSON.stringify({
      instances: state.instances,
      master: state.master,
      kb: state.kb,
      metronome: { bpm: state.metronome.bpm, on: false },
      guitar: state.guitar,
      customBanks: state.customBanks,
      view: state.view,
      lang: state.lang,
    }));
  } catch { /* storage full or blocked: never mind */ }
}, 400);

export function allBanks() {
  return [...BUILTIN_BANKS, ...state.customBanks];
}

export function bankById(id) {
  return allBanks().find(b => b.id === id) || BUILTIN_BANKS[0];
}
