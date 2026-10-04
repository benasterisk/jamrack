// JAMRACK — entry point: wires up audio, inputs and the interface

import { state, emit, bankById, defaultInstance, effectiveLang } from './state.js';
import { Engine } from './audio/engine.js';
import { Instrument } from './audio/instance.js';
import {
  loadInstrument, fetchBankInstruments, touchInstrument,
  instrumentKey, setProtectedKeysProvider,
} from './audio/soundfont.js';
import {
  loadSfz, touchSfz, SfzPlayer,
  setProtectedKeysProvider as setProtectedSfz,
} from './audio/sfz/index.js';
import { getSample, deleteSample } from './audio/sampledb.js';
import { applyPreset, ANALOG_PRESETS } from './audio/presets.js';
import { Metronome } from './audio/metronome.js';
import { Recorder } from './audio/recorder.js';
import { attachPcKeyboard, getKeyLabels, NOTE_CODES } from './input/pckeys.js';
import { initMidi } from './input/midi.js';
import { createGuitarInput } from './input/guitar.js';
import { createLooper } from './audio/looper/index.js';
import { createRack } from './ui/rack.js';
import { createPiano } from './ui/piano.js';
import { setupDialogs } from './ui/dialogs.js';
import {
  t, setLang, getLang, instrumentName, noteName, LANGUAGES,
} from './i18n/index.js';
import { clamp, formatTime, debounce, esc } from './util.js';

const engine = new Engine();
const metronome = new Metronome(engine);
const recorder = new Recorder(engine);
const audios = new Map(); // instance.id -> Instrument

// the soundfont cache must never evict an instrument a module is using
setProtectedKeysProvider(() => new Set(
  state.instances
    .filter(i => i.engine === 'bank')
    .map(i => instrumentKey(bankById(i.bankId).url, i.instrument))
));
// same idea for the SFZ cache, which is budgeted in bytes
setProtectedSfz(() => new Set(
  state.instances.filter(i => i.engine === 'sfz' && i.sfzUrl).map(i => i.sfzUrl.trim())
));

// ---------------------------------------------------------------- per-instance audio

function ensureAudio(inst) {
  let audio = audios.get(inst.id);
  if (!audio) {
    audio = new Instrument(engine, inst);
    audios.set(inst.id, audio);
  }
  audio.applySettings(inst);
  return audio;
}

function loadSound(inst) {
  const audio = ensureAudio(inst);
  if (inst.engine === 'analog') {
    const waveKey = { sawtooth: 'waveSaw', square: 'waveSquare', triangle: 'waveTriangle', sine: 'waveSine' }[inst.wave];
    rack.setStatus(inst.id, `${t('engineAnalog')} · ${t(waveKey)} — ${t('ready')}`);
    return;
  }
  if (inst.engine === 'sfz') {
    loadSfzSound(inst, audio);
    return;
  }
  if (inst.engine === 'sampler') {
    loadSamplerSound(inst, audio);
    return;
  }
  const bank = bankById(inst.bankId);
  const name = instrumentName(inst.instrument);
  const key = instrumentKey(bank.url, inst.instrument);
  // is this load still what the module expects? (the user may have switched
  // instrument/bank/engine, or deleted the module, in the meantime)
  const current = () => state.instances.includes(inst) && inst.engine === 'bank'
    && instrumentKey(bankById(inst.bankId).url, inst.instrument) === key;

  rack.setStatus(inst.id, `${name} — ${t('loading')}…`);
  const entry = loadInstrument(engine.ctx, bank.url, inst.instrument, pct => {
    if (!current()) return;
    if (pct < 1) rack.setStatus(inst.id, `${name} — ${t('loading')} ${Math.round(pct * 100)}%`);
    else rack.setStatus(inst.id, `${name} — ${t('ready')}`);
  });

  if (entry.buffers.size > 0 || audio._sfKey !== key) {
    audio.setBuffers(entry.buffers);
    audio._sfKey = key;
  } else {
    // same instrument being re-downloaded (cache evicted): keep the current
    // buffers and only swap once the new load is playable
    entry.ready.then(() => { if (current() && !entry.error) audio.setBuffers(entry.buffers); });
  }

  entry.ready.then(() => {
    if (!current() || entry.error) return;
    rack.setStatus(inst.id, entry.progress < 1
      ? `${name} — ${t('playable')} (${t('loading')} ${Math.round(entry.progress * 100)}%)`
      : `${name} — ${t('ready')}`);
  });
  entry.done.catch(() => {
    if (!current()) return;
    rack.setStatus(inst.id, `${name} — ${t('errorBank')}`, true);
  });
  touchInstrument(bank.url, inst.instrument);
}

// SFZ banks: a .sfz file plus its samples, fetched from any URL. Unlike the
// midi-js engine the samples are not one-per-note, so the player owns its own
// voices and the Instrument just routes notes to it.
function loadSfzSound(inst, audio) {
  const url = (inst.sfzUrl || '').trim();
  const label = sfzLabel(inst);
  if (!url) {
    audio.setSfzPlayer(null);
    rack.setStatus(inst.id, t('sfzNoUrl'));
    return;
  }
  // Still the load this module expects? (the user may have switched engine,
  // changed the URL, or deleted the module while it downloaded)
  const current = () => state.instances.includes(inst)
    && inst.engine === 'sfz' && (inst.sfzUrl || '').trim() === url;

  rack.setStatus(inst.id, `${label} — ${t('loading')}…`);
  const entry = loadSfz(engine.ctx, url, pct => {
    if (!current()) return;
    rack.setStatus(inst.id, pct < 1
      ? `${label} — ${t('loading')} ${Math.round(pct * 100)}%`
      : `${label} — ${t('ready')}`);
  });

  entry.ready.then(() => {
    if (!current() || entry.error) return;
    if (!audio.sfz) audio.setSfzPlayer(new SfzPlayer(engine.ctx, audio.input));
    audio.sfz.setInstrument(entry.instrument);
    const n = entry.instrument.regions.length;
    rack.setStatus(inst.id, `${label} — ${t('ready')} (${n} ${t('sfzRegions')})`);
  });
  entry.done.catch(err => {
    if (!current()) return;
    audio.setSfzPlayer(null);
    rack.setStatus(inst.id, `${label} — ${t('errorBank')}`, true);
    console.warn('SFZ load failed:', err.message);
  });
  touchSfz(url);
}

// SAMPLER engine: the user's own sample, persisted in IndexedDB so it comes
// back after a reload like every other setting.
async function loadSamplerSound(inst, audio) {
  const p = inst.sampler;
  if (!p.sampleId) {
    audio.setSampler(null);
    rack.setStatus(inst.id, t('smpNoSample'));
    return;
  }
  const wantedId = p.sampleId;
  const current = () => state.instances.includes(inst)
    && inst.engine === 'sampler' && inst.sampler.sampleId === wantedId;

  rack.setStatus(inst.id, `${p.sampleName} — ${t('loading')}…`);
  try {
    const rec = await getSample(wantedId);
    if (!rec) throw new Error('sample not in DB');
    // decodeAudioData detaches the buffer: hand it a copy so the DB record
    // could be re-read later.
    const buf = await new Promise((res, rej) => {
      const q = engine.ctx.decodeAudioData(rec.data.slice(0), res, rej);
      if (q && typeof q.then === 'function') q.then(res, rej);
    });
    if (!current()) return;
    audio.setSampler(buf);
    rack.setStatus(inst.id, `${p.sampleName} — ${buf.duration.toFixed(2)}s — ${t('ready')}`);
    rack.refreshSampler(inst.id);
  } catch (err) {
    if (!current()) return;
    audio.setSampler(null);
    rack.setStatus(inst.id, `${p.sampleName} — ${t('errorBank')}`, true);
    console.warn('Sampler load failed:', err.message);
  }
}

/** Human-readable name for an SFZ instrument: the file name, minus extension. */
function sfzLabel(inst) {
  const url = (inst.sfzUrl || '').trim();
  if (!url) return t('engineSfz');
  try {
    const file = decodeURIComponent(new URL(url, location.href).pathname.split('/').pop() || '');
    return file.replace(/\.sfz$/i, '') || t('engineSfz');
  } catch {
    return t('engineSfz');
  }
}

// Sampled banks are mastered far below full scale (MusyngKite peaks near
// -23 dBFS), so they need make-up gain to sit at a normal loudness; the analog
// engine already runs hot. Pick the gain from the quietest engine in use, and
// back off a little as modules stack so the limiter isn't doing all the work.
function refreshRouting() {
  const anySolo = state.instances.some(i => i.solo);
  state.instances.forEach(inst => {
    const audio = audios.get(inst.id);
    if (audio) audio.setMuted(inst.mute || (anySolo && !inst.solo));
  });
  rack.refreshSoloMute();
  emit('routing');
}

// ---------------------------------------------------------------- note routing

// Several sources (PC, touch, MIDI) can hold the same note: count them, and
// only trigger audio on the first press and the last release.
const held = new Map(); // midi -> number of sources holding the note

function routeNoteOn(midi, vel) {
  engine.resume();
  const n = held.get(midi) || 0;
  held.set(midi, n + 1);
  if (n > 0) return;
  state.instances.forEach(inst => {
    if (!inst.keyboardOn) return;
    const audio = audios.get(inst.id);
    if (!audio) return;
    // Keyswitches select an articulation rather than play a note, so they must
    // reach the instrument even when they sit outside its playing range (they
    // usually live in the bottom octave, below the default range).
    const isSwitch = inst.engine === 'sfz' && audio.sfz && audio.sfz.isKeyswitchNote(midi);
    if (!isSwitch && (midi < inst.rangeLo || midi > inst.rangeHi)) return;
    audio.noteOn(midi, vel);
  });
  rep.held.set(midi, vel);
  repSync();
  piano.setActive(midi, true);
}

function routeNoteOff(midi) {
  const n = held.get(midi) || 0;
  if (n > 1) { held.set(midi, n - 1); return; }
  held.delete(midi);
  rep.held.delete(midi);
  if (!rep.held.size) repStop();
  state.instances.forEach(inst => {
    const audio = audios.get(inst.id);
    if (audio) audio.noteOff(midi);
  });
  piano.setActive(midi, false);
}

function clearAllHeld() {
  held.forEach((_, midi) => piano.setActive(midi, false));
  held.clear();
  rep.held.clear();
  repStop();
}

// ---------------------------------------------------------------- note repeat
// Per module: each module with REPEAT on rolls the held notes at its OWN
// division (hats at 1/16 while the pad next to it just holds the chord), all
// clocks following the metronome BPM live.
const rep = { held: new Map(), timers: new Map() };  // held: midi -> vel 0..1

const REP_FRAC = { 4: 1, 8: 1 / 2, '8t': 1 / 3, 16: 1 / 4, '16t': 1 / 6, 32: 1 / 8 };

function repInterval(inst) {
  const beat = 60000 / (state.metronome.bpm || 100);   // one quarter note
  return beat * (REP_FRAC[inst.repeatDiv] || 1 / 4);
}

function repTick(inst) {
  // The module was deleted, switched off, or every key was released: this
  // clock stops itself.
  if (!state.instances.includes(inst) || !inst.repeatOn || !rep.held.size) {
    clearTimeout(rep.timers.get(inst.id));
    rep.timers.delete(inst.id);
    return;
  }
  rep.timers.set(inst.id, setTimeout(() => repTick(inst), repInterval(inst)));
  const audio = audios.get(inst.id);
  if (!audio || !inst.keyboardOn) return;
  rep.held.forEach((vel, midi) => {
    // Rolling a keyswitch would re-select the articulation, not play a note.
    if (inst.engine === 'sfz' && audio.sfz && audio.sfz.isKeyswitchNote(midi)) return;
    if (midi < inst.rangeLo || midi > inst.rangeHi) return;
    audio.noteOff(midi);
    audio.noteOn(midi, vel);
  });
}

/** Starts a clock for every module that wants one (idempotent). */
function repSync() {
  if (!rep.held.size) { repStop(); return; }
  state.instances.forEach(inst => {
    if (inst.repeatOn && !rep.timers.has(inst.id)) {
      rep.timers.set(inst.id, setTimeout(() => repTick(inst), repInterval(inst)));
    }
  });
}

function repStop() {
  rep.timers.forEach(id => clearTimeout(id));
  rep.timers.clear();
}

function routeSustain(on) {
  audios.forEach(a => a.setSustain(on));
  document.getElementById('susLed').classList.toggle('on', on);
}

function routeBend(semis) {
  audios.forEach(a => a.setBend(semis));
}

// ---------------------------------------------------------------- rack

const rack = createRack(document.getElementById('rack'), {
  applyParams(inst) {
    const audio = audios.get(inst.id);
    if (audio) audio.applySettings(inst);
    repSync();   // a module may have just switched its note repeat on
    emit('params');
  },
  changeEngine(inst) {
    const audio = audios.get(inst.id);
    if (audio) audio.allNotesOff();
    loadSound(inst); // ensureAudio -> applySettings picks the new make-up gain
    emit('params');
    rack.refreshLooperSources();   // the SOURCE menu names the module by its sound
  },
  refreshRouting,
  audioCtx: engine.ctx,
  samplerBuffer(inst) {
    const audio = audios.get(inst.id);
    return (audio && audio.smp) ? audio.smp.buffer : null;
  },
  removeInstance(id) {
    const idx = state.instances.findIndex(i => i.id === id);
    if (idx < 0) return;
    const gone = state.instances[idx];
    if (gone && gone.sampler && gone.sampler.sampleId) deleteSample(gone.sampler.sampleId);
    const audio = audios.get(id);
    if (audio) { audio.dispose(); audios.delete(id); }
    state.instances.splice(idx, 1);
    if (!state.instances.length) state.instances.push(defaultInstance());
    rack.rebuild();
    state.instances.forEach(loadSound);
    refreshRouting();
    emit('instances');
    // The looper was recording that module: it says so and falls back to the rack.
    if (state.looper.source === id) applyLooperSource();
  },
  // Hardware-rack model: you ADD a unit of a given type, you never morph an
  // existing one. Each type arrives ready to play.
  addInstance(engineType = 'bank') {
    const overrides = { engine: engineType, revSend: 0.3 };
    if (engineType === 'bank') {
      // a little variety: the 2nd module suggests strings, the 3rd a bass, etc.
      const suggestions = ['string_ensemble_1', 'electric_bass_finger', 'electric_piano_1', 'drawbar_organ'];
      overrides.instrument = suggestions[(state.instances.length - 1) % suggestions.length];
    } else if (engineType === 'sfz') {
      overrides.sfzUrl = 'sfz/808/808.sfz';   // sounds the moment it lands
    } else if (engineType === 'analog') {
      overrides.preset = 'supersaw';
    }
    const inst = defaultInstance(overrides);
    if (engineType === 'analog') applyPreset(inst, 'supersaw');
    state.instances.push(inst);
    rack.rebuild();
    state.instances.forEach(loadSound);
    refreshRouting();
    emit('instances');
  },
  instrumentsFor(bankId) {
    return fetchBankInstruments(bankById(bankId).url);
  },
  masterChanged: debounce(() => {
    engine.setMasterVolume(state.master.volume);
    engine.setReverbDecay(state.master.reverbDecay);
    engine.setDelay(state.master.delayTime, state.master.delayFeedback);
    emit('master');
  }, 30),
  // --- GUITAR → MIDI section (the input object is created right below)
  async guitarToggle() {
    if (guitar.running) { guitar.stop(); return; }
    await engine.resume();
    await guitar.start(state.guitar.deviceId);
  },
  async guitarDevice(id) {
    state.guitar.deviceId = id;
    emit('guitar');
    if (guitar.running) await guitar.start(id);
  },
  guitarChanged() {
    applyGuitarParams();
    emit('guitar');
  },
  guitarRunning: () => guitar.running,
  guitarDevices: () => guitar.devices,
  guitarStatusText: () => guitarStatusText(guitar.status),
  // --- LOOPER section (the looper object is created further below)
  async looperToggle(i) { if (await looperGesture()) looper.toggle(i); },
  async looperPlay() { if (await looperGesture()) looper.play(); },
  looperStop: () => looper.stop(),
  looperClear: i => looper.clear(i),
  looperClearAll: () => looper.clearAll(),
  looperUndo: i => looper.undo(i),
  looperTrack: (i, params) => looper.setTrack(i, params),
  looperSource(value) {
    if (value !== undefined) state.looper.source = value;
    return applyLooperSource();
  },
  looperSync: () => applyLooperSync(),
  looperMax: sec => looper.setMaxSeconds(sec),
  looperMonitor: v => looper.setMonitor(v),
  looperSources() {
    const engineLabel = { bank: 'engineBank', analog: 'engineAnalog', sfz: 'engineSfz', sampler: 'engineSampler' };
    const soundName = inst => {
      if (inst.engine === 'analog') {
        const p = ANALOG_PRESETS.find(x => x.id === inst.preset);
        const key = `preset_${inst.preset}`;
        return t(key) !== key ? t(key) : (p ? p.name : inst.preset);
      }
      if (inst.engine === 'sfz') return sfzLabel(inst);
      if (inst.engine === 'sampler') return inst.sampler.sampleName || t('engineSampler');
      return instrumentName(inst.instrument);
    };
    return [
      { value: 'rack', label: t('lpSourceRack') },
      { value: 'input', label: t('lpSourceInput') },
      ...state.instances.map((inst, i) =>
        ({ value: inst.id, label: `${i + 1} · ${t(engineLabel[inst.engine] || 'engineBank')} · ${soundName(inst)}` })),
    ];
  },
});

// ---------------------------------------------------------------- guitar → MIDI
// A sixth way to play: an audio input tracked into notes. It feeds the same
// routing as the MIDI keyboard, so every module with PLAY on answers to it.

const guitar = createGuitarInput(engine.ctx, {
  noteOn: (midi, vel) => routeNoteOn(midi, vel),
  noteOff: midi => routeNoteOff(midi),
  bend: routeBend,
  meter: info => rack.setGuitarMeter(info, guitar.latency()),
  status: st => rack.setGuitarStatus(guitarStatusText(st), st.key === 'noMic' || st.key === 'denied'),
  mode: () => rack.setGuitarStatus(guitarStatusText(guitar.status), false),   // the LCD names the engine
  devices: (list, id) => rack.setGuitarDevices(list, id),
  running: on => {
    rack.setGuitarRunning(on);
    // the input latency is only known once a stream is open
    if (on) looper.refreshLatency();
  },
});

function applyGuitarParams() {
  const g = state.guitar;
  guitar.setGain(g.gain);
  guitar.setParams({ sens: g.sens, release: g.release, dyn: g.dyn, bend: g.bend, octave: g.octave });
  guitar.setMode(g.mode);
}
applyGuitarParams();

function guitarStatusText(st) {
  const key = { off: 'gtrOff', starting: 'gtrStarting', listening: 'gtrListening', compat: 'gtrCompat',
    noMic: 'gtrNoMic', denied: 'gtrDenied', ended: 'gtrEnded' }[st.key] || 'gtrOff';
  const detail = st.detail ? ` — ${st.detail}` : '';
  const mode = (st.key === 'listening' || st.key === 'compat') && state.guitar.mode === 'poly' ? ' · POLY β' : '';
  return t(key) + mode + detail;
}

// ---------------------------------------------------------------- looper
// Six loop tracks fed by the rack's dry bus, one module, or the audio input.
// The engine lives in an AudioWorklet (js/audio/looper/); here we only turn
// state.looper into engine calls and relay what it reports to the card.

// What the card shows. Kept apart from looper.status because a failed source
// falls back to the rack (status 'ready') while the message must stay.
const looperShown = { st: { key: 'off', detail: '' }, text: '', isErr: false };

const looper = createLooper(engine, {
  meter: m => rack.setLooperMeter(m, looper.latency()),
  events: list => rack.setLooperEvents(list),
  status: st => showLooperStatus(st),
}, {
  maxSeconds: state.looper.maxSeconds,
  moduleOut: id => (audios.get(id) ? audios.get(id).out : null),
});

function looperStatusText(st) {
  const key = { ready: 'lpReady', unsupported: 'lpUnsupported', noMic: 'lpNoMic', denied: 'lpDenied',
    ended: 'lpEnded', noModule: 'lpNoModule' }[st.key] || 'lpReady';
  return t(key) + (st.detail ? ` — ${st.detail}` : '');
}

function showLooperStatus(st) {
  looperShown.st = st;
  looperShown.text = looperStatusText(st);
  looperShown.isErr = st.key !== 'off' && st.key !== 'ready';
  rack.setLooperStatus(looperShown.text, looperShown.isErr);
}

/** The beat quantum follows the METRO tempo while SYNC is on. */
function applyLooperSync() {
  const bpm = state.metronome.bpm;
  looper.setSync(state.looper.sync && bpm ? bpm : 0);
}

function applyLooperParams() {
  const L = state.looper;
  L.tracks.forEach((p, i) => looper.setTrack(i, p));
  looper.setMaxSeconds(L.maxSeconds);
  looper.setMonitor(L.monitor);
  applyLooperSync();
}

/**
 * Points the engine at state.looper.source: 'rack', 'input' (the device the
 * GUITAR section chose) or a module id. A module that no longer exists, or an
 * input that cannot be opened, falls back to the rack — keeping the message
 * that says why, and the menu in step. Calls run one at a time: an INPUT
 * permission prompt can stay up for seconds, and a RACK pick meanwhile must
 * not race it (both sources would end up summed into the worklet).
 */
let looperSourceChain = Promise.resolve(false);
function applyLooperSource() {
  looperSourceChain = looperSourceChain.catch(() => false).then(applyLooperSourceNow);
  return looperSourceChain;
}

async function applyLooperSourceNow() {
  const L = state.looper;
  let src = null, why = null;
  if (L.source === 'input') src = { kind: 'input', deviceId: state.guitar.deviceId };
  else if (L.source === 'rack') src = { kind: 'rack' };
  else if (audios.has(L.source)) src = { kind: 'module', id: L.source };
  else why = { key: 'noModule', detail: '' };   // deleted module (or a stale save)
  let ok = false;
  if (src) {
    ok = await looper.setSource(src);
    if (!ok) why = { ...looper.status };
  }
  if (!ok) {
    L.source = 'rack';
    emit('looper');
    await looper.setSource({ kind: 'rack' });
    showLooperStatus(why);
    rack.refreshLooperSources();
  }
  return ok;
}

// The worklet loads at startup (cheap, no permission involved) and the
// saved source follows — except an audio INPUT, which would ask for the
// microphone on page load: that one waits for the first transport gesture.
const looperReady = looper.init().then(async ok => {
  if (!ok) return false;
  applyLooperParams();
  if (state.looper.source !== 'input') await applyLooperSource();
  return true;
});

/** Before a transport command: audio unlocked, engine loaded, source open. */
async function looperGesture() {
  await engine.resume();
  if (!(await looperReady)) return false;
  // the input opens on the first gesture, and again after its device went away
  if (state.looper.source === 'input' && !looper.inputOpen) await applyLooperSource();
  // "that module is gone" has been read by now: back to the live status
  if (looperShown.st.key === 'noModule') showLooperStatus(looper.status);
  return true;
}

// ---------------------------------------------------------------- touch piano

const piano = createPiano(document.getElementById('piano'), {
  onNoteOn: midi => routeNoteOn(midi, state.kb.velocity / 127),
  onNoteOff: midi => routeNoteOff(midi),
});

function pianoOctaves() {
  const w = document.getElementById('piano').clientWidth || window.innerWidth;
  return clamp(Math.round(w / 240), 2, 6);
}

// Range the PC keyboard covers above its base note
const PC_SPAN = Math.max(...Object.values(NOTE_CODES));

// Scrolls the displayed keyboard so the PC keys stay in view. Without this,
// raising the octave moves the notes but leaves the visible range behind, and
// the key captions slide off the edge.
function followKeyboardOctave(octaves) {
  const base = 12 * (state.kb.octave + 1);
  if (base >= state.view.low && base + PC_SPAN <= state.view.low + octaves * 12) return;
  // centre the PC range in the visible span, snapped to whole octaves
  const ideal = base - Math.floor((octaves * 12 - PC_SPAN) / 2);
  state.view.low = Math.round(ideal / 12) * 12;
}

// follow = true when the redraw is caused by the keyboard octave changing
async function renderPiano(follow = false) {
  const octaves = pianoOctaves();
  if (follow) followKeyboardOctave(octaves);
  const maxLow = 108 - octaves * 12;
  state.view.low = clamp(state.view.low, 12, maxLow);
  piano.render(state.view.low, octaves);
  held.forEach((_, midi) => piano.setActive(midi, true)); // notes still held
  await refreshKeycaps();
}

async function refreshKeycaps() {
  const labels = await getKeyLabels(state.kb.layout);
  const base = 12 * (state.kb.octave + 1);
  const map = new Map();
  for (const [code, offset] of Object.entries(NOTE_CODES)) {
    if (labels[code]) map.set(base + offset, labels[code]);
  }
  piano.setKeycaps(map);
  document.getElementById('octRead').textContent = noteName(base);
}

// Called whenever the keyboard octave changes: move the notes AND the view.
function setKeyboardOctave(oct) {
  state.kb.octave = clamp(oct, 1, 6);
  emit('kb');
  renderPiano(true);
}

// ---------------------------------------------------------------- PC keyboard

const pcHeld = new Map(); // offset -> the midi note actually played

attachPcKeyboard({
  noteOn(offset) {
    const midi = 12 * (state.kb.octave + 1) + offset;
    pcHeld.set(offset, midi);
    routeNoteOn(midi, state.kb.velocity / 127);
  },
  noteOff(offset) {
    const midi = pcHeld.get(offset);
    if (midi !== undefined) {
      pcHeld.delete(offset);
      routeNoteOff(midi);
    }
  },
  octave(dir) {
    setKeyboardOctave(state.kb.octave + dir);
  },
  velocity(d) {
    state.kb.velocity = clamp(state.kb.velocity + d, 1, 127);
    document.getElementById('velSlider').value = state.kb.velocity;
    document.getElementById('velRead').textContent = state.kb.velocity;
    emit('kb');
  },
  sustain: routeSustain,
});

// ---------------------------------------------------------------- keyboard controls (UI)

document.getElementById('octDown').addEventListener('click', () => {
  setKeyboardOctave(state.kb.octave - 1);
});
document.getElementById('octUp').addEventListener('click', () => {
  setKeyboardOctave(state.kb.octave + 1);
});
document.getElementById('velSlider').addEventListener('input', e => {
  state.kb.velocity = Number(e.target.value);
  document.getElementById('velRead').textContent = state.kb.velocity;
  emit('kb');
});
document.getElementById('velSlider').addEventListener('change', e => e.target.blur());
document.getElementById('viewLeft').addEventListener('click', () => {
  state.view.low -= 12; renderPiano(); emit('view');
});
document.getElementById('viewRight').addEventListener('click', () => {
  state.view.low += 12; renderPiano(); emit('view');
});

window.addEventListener('resize', debounce(renderPiano, 200));

// ---------------------------------------------------------------- topbar

const metroBtn = document.getElementById('metroToggle');
metroBtn.addEventListener('click', async () => {
  await engine.resume();
  const running = metronome.toggle();
  metroBtn.setAttribute('aria-pressed', String(running));
  metroBtn.querySelector('.led').classList.toggle('on', running);
  state.metronome.on = running;
  emit('metro');
});
const bpmInput = document.getElementById('bpmInput');
bpmInput.value = state.metronome.bpm;
bpmInput.addEventListener('change', () => {
  metronome.setBpm(Number(bpmInput.value));
  bpmInput.value = metronome.bpm;
  state.metronome.bpm = metronome.bpm;
  applyLooperSync();
  emit('metro');
  bpmInput.blur(); // hand focus back to the musical keyboard
});
metronome.setBpm(state.metronome.bpm);

const recBtn = document.getElementById('recBtn');
const recTime = document.getElementById('recTime');
let recTimer = 0;
if (!Recorder.supported()) recBtn.disabled = true;
recBtn.addEventListener('click', async () => {
  if (recorder.recording) {
    clearInterval(recTimer);
    await recorder.stop();
    recBtn.setAttribute('aria-pressed', 'false');
    recBtn.querySelector('.led').classList.remove('on', 'red', 'blink');
    recTime.textContent = '0:00';
  } else {
    await engine.resume();
    if (recorder.start()) {
      recBtn.setAttribute('aria-pressed', 'true');
      recBtn.querySelector('.led').classList.add('on', 'red', 'blink');
      recTimer = setInterval(() => { recTime.textContent = formatTime(recorder.elapsed()); }, 500);
    }
  }
});

// ---------------------------------------------------------------- MIDI

const midiLed = document.getElementById('midiLed');
let midiDeviceCount = 0;
initMidi({
  noteOn: (midi, vel) => routeNoteOn(midi, vel),
  noteOff: midi => routeNoteOff(midi),
  sustain: routeSustain,
  bend: routeBend,
  allOff: () => {
    audios.forEach(a => a.allNotesOff());
    clearAllHeld(); // also clear the on-screen keys
  },
  devices: n => {
    midiDeviceCount = n;
    midiLed.classList.toggle('on', n > 0);
    midiLed.classList.toggle('teal', n > 0);
    refreshMidiTitle();
  },
  activity: () => {
    midiLed.classList.add('on', 'teal');
  },
});

function refreshMidiTitle() {
  document.getElementById('midiGroup').title = midiDeviceCount > 0
    ? t('midiDevices', midiDeviceCount) : t('midiNone');
}

// ---------------------------------------------------------------- dialogs

const dialogs = setupDialogs({
  onBanksChanged() {
    rack.rebuild();
    state.instances.forEach(loadSound);
    refreshRouting();
  },
  onLayoutChanged() {
    refreshKeycaps();
  },
});

// ---------------------------------------------------------------- language

const langSelect = document.getElementById('langSelect');
langSelect.innerHTML = LANGUAGES
  .map(l => `<option value="${l.code}">${esc(l.native)}</option>`)
  .join('');

function applyLanguage() {
  // static labels carrying a data-t key
  document.querySelectorAll('[data-t]').forEach(node => {
    node.textContent = t(node.dataset.t);
  });
  document.getElementById('tagline').textContent = t('tagline');
  document.getElementById('audioHintText').textContent = t('audioHint');
  document.getElementById('banksBtn').textContent = t('banksBtn');
  document.title = `JAMRACK · ${t('tagline')}`;

  // tooltips
  const titles = [
    ['metroGroup', 'titleMetro'], ['bpmInput', 'titleTempo'], ['recBtn', 'titleRec'],
    ['helpBtn', 'titleHelp'], ['langSelect', 'titleLang'], ['banksBtn', 'banksTitle'],
    ['octDown', 'titleOctDown'], ['octUp', 'titleOctUp'],
    ['viewLeft', 'titleViewLeft'], ['viewRight', 'titleViewRight'],
    ['velSlider', 'velocity'],
  ];
  titles.forEach(([id, key]) => {
    const node = document.getElementById(id);
    if (node) node.title = t(key);
  });
  refreshMidiTitle();

  langSelect.value = getLang();
  rack.rebuild();
  // rebuild() blanks every LCD: re-post each module's status in the new language
  state.instances.forEach(loadSound);
  showLooperStatus(looperShown.st);   // same message, new language
  refreshRouting();
  renderPiano(true); // keep the PC keys in view
  dialogs.refresh();
}

langSelect.addEventListener('change', () => {
  state.lang = langSelect.value;
  setLang(state.lang);
  emit('lang');
  applyLanguage();
  langSelect.blur();
});

// ---------------------------------------------------------------- audio start & hint

const hint = document.getElementById('audioHint');
setTimeout(() => {
  if (engine.ctx.state !== 'running') hint.hidden = false;
}, 500);
engine.ctx.addEventListener('statechange', () => {
  if (engine.ctx.state === 'running') hint.hidden = true;
});
// iOS Safari does NOT treat pointerdown/touchstart as an activation gesture:
// also try to resume on the events that end a gesture
const unlockEvents = ['pointerdown', 'pointerup', 'touchend', 'click', 'keydown'];
const unlock = () => {
  engine.resume();
  if (engine.ctx.state === 'running') {
    unlockEvents.forEach(evt => document.removeEventListener(evt, unlock, true));
  }
};
unlockEvents.forEach(evt => document.addEventListener(evt, unlock, true));

// ---------------------------------------------------------------- VU meter

const vuData = new Uint8Array(engine.analyser.fftSize);
let vuCanvas = null, vuCtx = null, vuPeak = 0;
(function drawVU() {
  requestAnimationFrame(drawVU);
  const c = document.getElementById('vuCanvas');
  if (!c) return;
  if (c !== vuCanvas) { vuCanvas = c; vuCtx = c.getContext('2d'); }
  engine.analyser.getByteTimeDomainData(vuData);
  let sum = 0;
  for (let i = 0; i < vuData.length; i++) {
    const d = (vuData[i] - 128) / 128;
    sum += d * d;
  }
  const rms = Math.sqrt(sum / vuData.length);
  vuPeak = Math.max(rms, vuPeak * 0.94);
  const w = vuCanvas.width, h = vuCanvas.height;
  vuCtx.clearRect(0, 0, w, h);
  const segs = 12;
  const lit = Math.min(segs, Math.round(vuPeak * 2.2 * segs));
  for (let i = 0; i < segs; i++) {
    vuCtx.fillStyle = i < lit
      ? (i > segs - 3 ? '#ff5040' : i > segs - 6 ? '#ffb454' : '#5fbf72')
      : '#2a241d';
    const bw = (w - (segs + 1) * 3) / segs;
    vuCtx.fillRect(3 + i * (bw + 3), 5, bw, h - 10);
  }
})();

// ---------------------------------------------------------------- init

engine.setMasterVolume(state.master.volume);
engine.setReverbDecay(state.master.reverbDecay);
engine.setDelay(state.master.delayTime, state.master.delayFeedback);

document.getElementById('velSlider').value = state.kb.velocity;
document.getElementById('velRead').textContent = state.kb.velocity;

setLang(effectiveLang());
applyLanguage(); // builds the rack, loads the sounds and draws the piano

// ---------------------------------------------------------------- mobile views

// On a phone the rack and the keyboard cannot share the screen: the rack needs
// far more height than what is left under the sticky keyboard, which leaves the
// modules unusable. Below the CSS breakpoint they become two dedicated views.
// Desktop keeps everything on screen at once and ignores all of this.
// Narrow OR short: a phone held sideways is ~900x400 CSS px — wider than the
// breakpoint but far too short to show the rack and the keyboard together.
const MOBILE_VIEW_MQ = window.matchMedia('(max-width: 780px), (max-height: 520px)');
const tabPlay = document.getElementById('tabPlay');
const tabEdit = document.getElementById('tabEdit');

function setMobileView(view) {
  const edit = view === 'edit';
  document.body.classList.toggle('m-edit', edit);
  document.body.classList.toggle('m-play', !edit);
  tabPlay.classList.toggle('is-on', !edit);
  tabEdit.classList.toggle('is-on', edit);
  tabPlay.setAttribute('aria-selected', String(!edit));
  tabEdit.setAttribute('aria-selected', String(edit));
  state.view.mobile = view;
  emit('view');   // persists the choice (same path as the octave arrows)
  // The piano was display:none while editing, so its width was 0: redraw it.
  if (!edit) renderPiano();
}

function syncMobileView() {
  if (MOBILE_VIEW_MQ.matches) {
    setMobileView(state.view.mobile === 'edit' ? 'edit' : 'play');
  } else {
    // Wide screen: drop both classes so the desktop layout applies untouched.
    document.body.classList.remove('m-play', 'm-edit');
  }
}

tabPlay.addEventListener('click', () => setMobileView('play'));
tabEdit.addEventListener('click', () => setMobileView('edit'));
MOBILE_VIEW_MQ.addEventListener('change', syncMobileView);
// Also on resize: some browsers (and devtools) resize without firing the
// media-query change event, which would leave a stale class on <body>.
window.addEventListener('resize', debounce(syncMobileView, 200));
syncMobileView();

// console handle for debugging
window.JAMRACK = { engine, state, audios, routeNoteOn, routeNoteOff, loadSound, metronome, rack, looper };
