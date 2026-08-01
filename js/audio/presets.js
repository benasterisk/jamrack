// Factory presets for the analog engine.
//
// A synth with twenty knobs and no presets goes unused: these are the starting
// points people actually reach for. Each preset sets the oscillator section,
// the amp envelope, the filter and the colouring in one go — the sounds that
// define modern electronic music rather than the General MIDI catalogue.
//
// A preset only carries the fields it cares about; anything it omits falls back
// to defaultSynth() / the instance defaults, so presets stay readable.

import { defaultSynth } from '../state.js';

// id      : stable key stored in the instance
// nameKey : i18n key (js/i18n/strings.js), falls back to `name`
// synth   : partial defaultSynth() override
// adsr    : partial amp envelope override
// patch   : instance-level fields (wave, filterCut, filterRes, detune…)
export const ANALOG_PRESETS = [
  {
    id: 'init',
    name: 'Init',
    patch: { wave: 'sawtooth', detune: 7, filterCut: 1, filterRes: 0 },
    adsr: { a: 0.002, d: 0.08, s: 1, r: 0.35 },
    synth: {},
  },

  // --- leads -------------------------------------------------------------
  {
    id: 'supersaw',
    name: 'Supersaw Lead',
    // Seven detuned saws spread wide: the trance/festival lead.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.86, filterRes: 0.12 },
    adsr: { a: 0.01, d: 0.25, s: 0.85, r: 0.45 },
    synth: { unison: 7, uniDetune: 0.32, uniSpread: 0.95, sub: 0.18, drive: 0.22 },
  },
  {
    id: 'hoover',
    name: 'Hoover',
    // The rave stab: detuned saws, heavy drive and a fast filter sweep.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.62, filterRes: 0.42 },
    adsr: { a: 0.008, d: 0.5, s: 0.7, r: 0.3 },
    synth: {
      unison: 5, uniDetune: 0.5, uniSpread: 0.7, osc2: 'square', osc2Semi: -12,
      osc2Mix: 0.4, drive: 0.45, fEnvAmt: 0.4, fDecay: 0.5, fSustain: 0.35,
    },
  },
  {
    id: 'pluck',
    name: 'Pluck',
    // Short filter envelope, no sustain: the house/future-bass pluck.
    patch: { wave: 'sawtooth', detune: 12, filterCut: 0.3, filterRes: 0.45 },
    adsr: { a: 0.002, d: 0.35, s: 0, r: 0.25 },
    synth: { unison: 3, uniDetune: 0.22, uniSpread: 0.6, fEnvAmt: 0.75, fDecay: 0.22, fSustain: 0, drive: 0.15 },
  },

  // --- basses ------------------------------------------------------------
  {
    id: 'reese',
    name: 'Reese Bass',
    // Two heavily detuned saws beating against each other: DnB / dubstep.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.34, filterRes: 0.3 },
    adsr: { a: 0.005, d: 0.2, s: 0.95, r: 0.15 },
    synth: {
      unison: 4, uniDetune: 0.55, uniSpread: 0.35, sub: 0.5, subWave: 'sine',
      drive: 0.4, lfoRate: 0.6, lfoAmt: 0.25, lfoTarget: 'filter',
    },
  },
  {
    id: 'sub808',
    name: '808 Sub',
    // Almost pure sine with a long tail: the trap/techno low end.
    patch: { wave: 'sine', detune: 0, filterCut: 0.28, filterRes: 0 },
    adsr: { a: 0.004, d: 0.6, s: 0.6, r: 0.7 },
    synth: { unison: 1, sub: 0.85, subWave: 'sine', drive: 0.3 },
  },
  {
    id: 'acid',
    name: 'Acid 303',
    // Squelchy resonant sweep on every note: the TB-303 signature.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.16, filterRes: 0.82 },
    adsr: { a: 0.003, d: 0.3, s: 0.25, r: 0.12 },
    synth: { unison: 1, sub: 0.25, drive: 0.5, fEnvAmt: 0.8, fAttack: 0.002, fDecay: 0.28, fSustain: 0.05, glide: 0.06 },
  },
  {
    id: 'fmbass',
    name: 'FM Bass',
    // Square sub an octave down against a saw: metallic, aggressive.
    patch: { wave: 'sawtooth', detune: 4, filterCut: 0.4, filterRes: 0.25 },
    adsr: { a: 0.003, d: 0.25, s: 0.8, r: 0.18 },
    synth: { unison: 2, uniDetune: 0.15, osc2: 'square', osc2Semi: 12, osc2Mix: 0.35, sub: 0.45, subWave: 'square', drive: 0.55 },
  },

  // --- pads & textures ---------------------------------------------------
  {
    id: 'dreampad',
    name: 'Dream Pad',
    // Slow attack, wide unison, gentle filter movement.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.5, filterRes: 0.1 },
    adsr: { a: 0.8, d: 1.2, s: 0.8, r: 1.6 },
    synth: { unison: 5, uniDetune: 0.3, uniSpread: 1, osc2: 'triangle', osc2Semi: 12, osc2Mix: 0.3, lfoRate: 0.25, lfoAmt: 0.3, lfoTarget: 'filter' },
  },
  {
    id: 'wobble',
    name: 'Wobble',
    // LFO swinging the cutoff hard: the dubstep growl.
    patch: { wave: 'sawtooth', detune: 0, filterCut: 0.35, filterRes: 0.6 },
    adsr: { a: 0.005, d: 0.2, s: 0.9, r: 0.2 },
    synth: { unison: 3, uniDetune: 0.4, sub: 0.4, drive: 0.5, lfoRate: 5.5, lfoAmt: 0.85, lfoTarget: 'filter' },
  },
  {
    id: 'chiptune',
    name: 'Chiptune',
    // Pure square, no detune, snappy envelope: 8-bit.
    patch: { wave: 'square', detune: 0, filterCut: 1, filterRes: 0 },
    adsr: { a: 0.001, d: 0.06, s: 0.7, r: 0.08 },
    synth: { unison: 1, sub: 0.2, subWave: 'square' },
  },
  {
    id: 'organ',
    name: 'Soft Keys',
    // Sine stack with a touch of body: mellow electric-piano flavour.
    patch: { wave: 'sine', detune: 5, filterCut: 0.78, filterRes: 0 },
    adsr: { a: 0.006, d: 0.5, s: 0.55, r: 0.4 },
    synth: { unison: 2, uniDetune: 0.12, osc2: 'triangle', osc2Semi: 12, osc2Mix: 0.35, drive: 0.12 },
  },
];

export const PRESETS_BY_ID = Object.fromEntries(ANALOG_PRESETS.map(p => [p.id, p]));

/** Applies a preset onto an instance object (mutates it). Unknown id = no-op. */
export function applyPreset(inst, presetId) {
  const p = PRESETS_BY_ID[presetId];
  if (!p) return false;
  inst.preset = p.id;
  Object.assign(inst, p.patch || {});
  inst.adsr = { ...inst.adsr, ...(p.adsr || {}) };
  // Start from a clean synth section: presets only list what they change, so
  // merging onto the previous patch would leak settings between presets.
  inst.synth = { ...defaultSynth(), ...(p.synth || {}) };
  return true;
}
