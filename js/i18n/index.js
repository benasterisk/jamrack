// Translation lookup. English is the fallback for any missing key.

import { STRINGS } from './strings.js';
import { INSTRUMENT_NAMES } from './instruments.js';
import { LANGUAGES, DEFAULT_LANG, langByCode, detectLanguage } from './languages.js';
import { GM } from '../data/gm.js';

export { LANGUAGES, DEFAULT_LANG, langByCode, detectLanguage };

let lang = DEFAULT_LANG;
const subscribers = new Set();

export function getLang() { return lang; }
export function getLangInfo() { return langByCode(lang) || langByCode(DEFAULT_LANG); }

export function setLang(code) {
  lang = langByCode(code) ? code : DEFAULT_LANG;
  const info = getLangInfo();
  document.documentElement.lang = lang;
  document.documentElement.dir = info.dir;
  subscribers.forEach(fn => fn(lang));
}

// Subscribe to language changes; returns an unsubscribe function.
export function onLangChange(fn) {
  subscribers.add(fn);
  return () => subscribers.delete(fn);
}

// Translate a key. Extra arguments are passed to function-valued strings.
export function t(key, ...args) {
  const table = STRINGS[lang] || STRINGS[DEFAULT_LANG];
  let value = table[key];
  if (value === undefined) value = STRINGS[DEFAULT_LANG][key];
  if (value === undefined) return key;
  return typeof value === 'function' ? value(...args) : value;
}

// Translate an instrument category name (categories are keyed in English).
export function tCat(englishCat) {
  const table = STRINGS[lang] || STRINGS[DEFAULT_LANG];
  return (table.cat && table.cat[englishCat]) || englishCat;
}

// Localized display name of a GM instrument id.
const GM_INDEX = new Map(GM.map((g, i) => [g.id, i]));

export function instrumentName(id) {
  const index = GM_INDEX.get(id);
  if (index === undefined) return String(id).replace(/_/g, ' ');
  const names = INSTRUMENT_NAMES[lang];
  return (names && names[index]) || GM[index].en;
}

// --- Note naming ------------------------------------------------------------
// Three traditions: letter names (C D E F G A B), fixed-do solfège
// (Do Re Mi…), and the German convention where B natural is written H.

const LETTER = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const GERMAN = ['C', 'Cis', 'D', 'Dis', 'E', 'F', 'Fis', 'G', 'Gis', 'A', 'B', 'H'];
const FIXED_DO = {
  fr: ['Do', 'Do#', 'Ré', 'Ré#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'],
  es: ['Do', 'Do#', 'Re', 'Re#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'],
  pt: ['Dó', 'Dó#', 'Ré', 'Ré#', 'Mi', 'Fá', 'Fá#', 'Sol', 'Sol#', 'Lá', 'Lá#', 'Si'],
};

// Display name of a MIDI note in the current language, e.g. "C4" / "Do4".
export function noteName(midi) {
  const info = getLangInfo();
  const octave = Math.floor(midi / 12) - 1;
  const pc = midi % 12;
  if (info.note === 'german') return GERMAN[pc] + octave;
  if (info.note === 'fixedDo') return (FIXED_DO[lang] || FIXED_DO.es)[pc] + octave;
  return LETTER[pc] + octave;
}
