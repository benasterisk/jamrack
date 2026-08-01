// Built-in SFZ instruments.
//
// These are hosted alongside the app rather than fetched from a CDN, because
// the SFZ world has almost nothing modern under a licence that allows
// redistribution — the format comes from acoustic multi-sampling. So the
// electronic kits below are built here: plain WAV one-shots (CC0) wrapped in a
// .sfz mapping written for this app.
//
// `nameKey` falls back to `name` when the language has no translation, which is
// the right default for proper nouns like "808".

export const SFZ_BANKS = [
  {
    id: 'tr808',
    name: '808 Kit',
    url: 'sfz/808/808.sfz',
    kind: 'drums',
  },
  {
    id: 'analogdrums',
    name: 'Analog Drums',
    url: 'sfz/analog-drums/analog-drums.sfz',
    kind: 'drums',
  },
  {
    id: 'basses',
    name: 'Synth Basses',
    url: 'sfz/basses/basses.sfz',
    kind: 'bass',
  },
];

export const SFZ_BANKS_BY_URL = Object.fromEntries(SFZ_BANKS.map(b => [b.url, b]));
