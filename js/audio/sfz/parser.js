// SFZ parser: turns a .sfz text file into a flat list of regions.
//
// SFZ is a plain-text format: `<header>` sections holding `opcode=value` pairs,
// with values inherited down the chain global -> master -> group -> region.
// Only a subset of the format is implemented — the opcodes that real banks
// actually use. Unknown opcodes are kept as raw strings and simply ignored by
// the engine: an unsupported opcode must never break the load.
//
// Spec: https://sfzformat.com/

// Opcodes whose value is a number. Everything else stays a string.
const NUMERIC = new Set([
  'lokey', 'hikey', 'key', 'lovel', 'hivel', 'pitch_keycenter',
  'tune', 'pitch', 'transpose', 'volume', 'pan', 'width', 'position',
  'offset', 'end', 'loop_start', 'loopstart', 'loop_end', 'loopend', 'count',
  'seq_length', 'seq_position', 'group', 'off_by', 'offby',
  'ampeg_delay', 'ampeg_start', 'ampeg_attack', 'ampeg_hold',
  'ampeg_decay', 'ampeg_sustain', 'ampeg_release',
  'amp_veltrack', 'amp_keytrack', 'amp_keycenter', 'amp_random', 'rt_decay',
  'cutoff', 'resonance', 'fil_keytrack', 'fil_keycenter', 'fil_veltrack',
  'fileg_depth', 'fileg_attack', 'fileg_decay', 'fileg_sustain', 'fileg_release',
  'pitch_keytrack', 'pitch_veltrack', 'pitch_random', 'delay', 'delay_random',
  'lorand', 'hirand', 'lochan', 'hichan', 'lobend', 'hibend',
  'sw_lokey', 'sw_hikey', 'sw_last', 'sw_down', 'sw_up', 'sw_default',
  'xfin_lokey', 'xfin_hikey', 'xfout_lokey', 'xfout_hikey',
  'xfin_lovel', 'xfin_hivel', 'xfout_lovel', 'xfout_hivel',
  'note_offset', 'octave_offset', 'note_polyphony', 'polyphony',
]);

const HEADER_RE = /<(\w+)>/g;

// c4 = 60 is the SFZ convention (sfzformat.com). Banks that disagree ship
// note_offset / octave_offset in <control>, which we apply afterwards.
const PITCH_CLASS = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };

/** Parses "60", "c4", "c#4", "db-1" -> MIDI number, or null. */
export function parseNote(raw) {
  if (raw === undefined || raw === null) return null;
  const s = String(raw).trim();
  if (s === '') return null;
  if (/^-?\d+$/.test(s)) return parseInt(s, 10);
  const m = /^([a-gA-G])([#b]?)(-?\d+)$/.exec(s);
  if (!m) return null;
  let pc = PITCH_CLASS[m[1].toLowerCase()];
  if (m[2] === '#') pc += 1;
  else if (m[2] === 'b') pc -= 1;
  return (parseInt(m[3], 10) + 1) * 12 + pc;
}

/**
 * Splits an opcode line into [key, value] pairs.
 *
 * The subtle part: values are NOT quoted and may contain spaces
 * (`sample=My Piano C4.wav` is legal). So we split on whitespace, then glue
 * any token that has no `=` back onto the previous value. Splitting naively
 * would truncate every path containing a space.
 */
function parseOpcodes(text, into) {
  const tokens = text.split(/\s+/).filter(Boolean);
  let lastKey = null;
  for (const tok of tokens) {
    const eq = tok.indexOf('=');
    if (eq > 0) {
      lastKey = tok.slice(0, eq).toLowerCase();
      into[lastKey] = tok.slice(eq + 1);
    } else if (lastKey) {
      into[lastKey] += ' ' + tok;      // continuation of a spaced value
    }
  }
  return into;
}

/** Applies #define substitutions ($VAR -> value), including inside opcode names. */
function applyDefines(text) {
  const defines = new Map();
  const cleaned = text.replace(/^\s*#define\s+(\$\w+)\s+(\S+)\s*$/gm, (_, k, v) => {
    defines.set(k, v);
    return '';
  });
  if (!defines.size) return cleaned;
  // Longest first, so $STR_RES is not clobbered by $STR.
  const keys = [...defines.keys()].sort((a, b) => b.length - a.length);
  let out = cleaned;
  for (const k of keys) {
    out = out.split(k).join(defines.get(k));
  }
  return out;
}

/** Strips `//` comments without eating the `//` inside `http://`. */
function stripComments(text) {
  return text.replace(/(^|\s)\/\/[^\n]*/g, '$1');
}

function toNumber(v) {
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : 0;
}

/**
 * Parses SFZ text into { control, regions }.
 *
 * `regions` is a flat array of plain objects: inheritance has already been
 * resolved, so each region carries every opcode that applies to it.
 *
 * `#include` is NOT resolved here (it needs network access): the loader
 * pre-resolves includes and hands us the assembled text.
 */
export function parseSfz(text) {
  const src = stripComments(applyDefines(text));

  // Cut the file at every header, keeping the header names.
  const parts = [];
  let match, lastIndex = 0, lastName = null;
  HEADER_RE.lastIndex = 0;
  while ((match = HEADER_RE.exec(src)) !== null) {
    if (lastName !== null) {
      parts.push([lastName, src.slice(lastIndex, match.index)]);
    }
    lastName = match[1].toLowerCase();
    lastIndex = HEADER_RE.lastIndex;
  }
  if (lastName !== null) parts.push([lastName, src.slice(lastIndex)]);

  const control = {};
  const regions = [];
  // Inheritance levels. A new <global> resets master/group too, and so on.
  let global = {}, master = {}, group = {};

  for (const [name, body] of parts) {
    switch (name) {
      case 'control':
        parseOpcodes(body, control);
        break;
      case 'global':
        global = parseOpcodes(body, {});
        master = {}; group = {};
        break;
      case 'master':
        master = parseOpcodes(body, {});
        group = {};
        break;
      case 'group':
        group = parseOpcodes(body, {});
        break;
      case 'region': {
        const region = parseOpcodes(body, { ...global, ...master, ...group });
        regions.push(region);
        break;
      }
      default:
        // <curve>, <effect>, <sample>, <midi>… : not supported, ignored on
        // purpose so that a bank using them still loads.
        break;
    }
  }

  return { control, regions: regions.map(r => normaliseRegion(r, control)) };
}

/**
 * Turns raw opcode strings into the typed shape the engine consumes.
 * Defaults follow the spec, except where real-world banks need otherwise
 * (see ampeg_release below).
 */
function normaliseRegion(raw, control) {
  const num = (k, def) => (raw[k] === undefined ? def : toNumber(raw[k]));
  const noteOff = toNumber(control.note_offset || 0)
    + toNumber(control.octave_offset || 0) * 12;
  const note = (k, def) => {
    const v = parseNote(raw[k]);
    return v === null ? def : v + noteOff;
  };

  // `key` is shorthand for lokey + hikey + pitch_keycenter.
  const keyShorthand = parseNote(raw.key);
  const lokey = keyShorthand !== null ? keyShorthand + noteOff : note('lokey', 0);
  const hikey = keyShorthand !== null ? keyShorthand + noteOff : note('hikey', 127);
  const keycenter = keyShorthand !== null
    ? keyShorthand + noteOff
    : note('pitch_keycenter', 60);

  const loopMode = (raw.loop_mode || raw.loopmode || '').toLowerCase();

  return {
    sample: (raw.sample || '').trim(),
    lokey, hikey, keycenter,
    lovel: num('lovel', 0),
    hivel: num('hivel', 127),
    // pitch
    tune: num('tune', num('pitch', 0)),
    transpose: num('transpose', 0),
    keytrack: num('pitch_keytrack', 100),
    // level
    volume: num('volume', 0),
    pan: num('pan', 0),
    ampVeltrack: num('amp_veltrack', 100),
    rtDecay: num('rt_decay', 0),
    // playback window (sample frames, in the FILE's sample rate)
    offset: num('offset', 0),
    end: num('end', -2),               // -2 = unset, -1 = silent region
    loopMode,
    loopStart: raw.loop_start !== undefined || raw.loopstart !== undefined
      ? num('loop_start', num('loopstart', 0)) : null,
    loopEnd: raw.loop_end !== undefined || raw.loopend !== undefined
      ? num('loop_end', num('loopend', 0)) : null,
    // amplitude envelope (seconds, except sustain which is a percentage)
    ampeg: {
      delay: num('ampeg_delay', 0),
      attack: num('ampeg_attack', 0),
      hold: num('ampeg_hold', 0),
      decay: num('ampeg_decay', 0),
      sustain: num('ampeg_sustain', 100) / 100,
      // Spec says 0.001 s, but banks are authored against ARIA (~0.02 s) and a
      // 1 ms release clicks audibly. Use the ARIA-like value.
      release: Math.max(0.02, num('ampeg_release', 0.02)),
    },
    // filter (absent cutoff = no filter node at all)
    cutoff: raw.cutoff !== undefined ? toNumber(raw.cutoff) : null,
    resonance: num('resonance', 0),
    filType: (raw.fil_type || raw.filtype || 'lpf_2p').toLowerCase(),
    // round robin & choke groups
    seqLength: num('seq_length', 1),
    seqPosition: num('seq_position', 1),
    group: num('group', 0),
    offBy: num('off_by', num('offby', 0)),
    offMode: (raw.off_mode || 'fast').toLowerCase(),
    // triggering
    trigger: (raw.trigger || 'attack').toLowerCase(),
    loRand: num('lorand', 0),
    hiRand: num('hirand', 1),
    swLast: raw.sw_last !== undefined ? note('sw_last', null) : null,
    swDefault: raw.sw_default !== undefined ? note('sw_default', null) : null,
    swLokey: raw.sw_lokey !== undefined ? note('sw_lokey', null) : null,
    swHikey: raw.sw_hikey !== undefined ? note('sw_hikey', null) : null,
    delay: num('delay', 0),
    // velocity crossfades
    xfinLovel: num('xfin_lovel', 0),
    xfinHivel: num('xfin_hivel', 0),
    xfoutLovel: num('xfout_lovel', 127),
    xfoutHivel: num('xfout_hivel', 127),
  };
}

/** Resolves a region's sample path against <control> default_path and the SFZ dir. */
export function samplePath(region, control, baseUrl) {
  const dir = (control.default_path || '').replace(/\\/g, '/');
  const file = region.sample.replace(/\\/g, '/');
  const rel = dir ? dir.replace(/\/?$/, '/') + file : file;
  // Percent-encode each segment: real banks contain '#' in filenames
  // (D#1v3.flac) and a bare '#' would be read as a URL fragment.
  const encoded = rel.split('/').map(encodeURIComponent).join('/');
  return new URL(encoded, baseUrl).href;
}
