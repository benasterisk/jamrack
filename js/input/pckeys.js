// Computer keyboard -> notes, mapped by PHYSICAL POSITION (event.code), so it
// works the same on any layout (AZERTY, QWERTY, QWERTZ…).
//
//   Home row (asdfghjkl;'\ / qsdfghjklmù*) = white keys
//   Row above (wetyuop] / zetyuop$)        = black keys
//
// just like a real piano: the black keys sit physically between the white ones.

// physical code -> semitones above the keyboard's base C
export const NOTE_CODES = {
  KeyA: 0,          // C   (Q on AZERTY)
  KeyW: 1,          // C#  (Z on AZERTY)
  KeyS: 2,          // D
  KeyE: 3,          // D#
  KeyD: 4,          // E
  KeyF: 5,          // F
  KeyT: 6,          // F#
  KeyG: 7,          // G
  KeyY: 8,          // G#
  KeyH: 9,          // A
  KeyU: 10,         // A#
  KeyJ: 11,         // B
  KeyK: 12,         // C+1
  KeyO: 13,         // C#+1
  KeyL: 14,         // D+1
  KeyP: 15,         // D#+1
  Semicolon: 16,    // E+1 (M on AZERTY)
  Quote: 17,        // F+1 (Ù on AZERTY)
  BracketRight: 18, // F#+1 ($ on AZERTY)
  Backslash: 19,    // G+1 (* on AZERTY)
};

const AZERTY_LABELS = {
  KeyA: 'Q', KeyW: 'Z', KeyS: 'S', KeyE: 'E', KeyD: 'D', KeyF: 'F', KeyT: 'T',
  KeyG: 'G', KeyY: 'Y', KeyH: 'H', KeyU: 'U', KeyJ: 'J', KeyK: 'K', KeyO: 'O',
  KeyL: 'L', KeyP: 'P', Semicolon: 'M', Quote: 'Ù', BracketRight: '$', Backslash: '*',
  KeyZ: 'W', KeyX: 'X', KeyC: 'C', KeyV: 'V', KeyQ: 'A', KeyR: 'R', KeyI: 'I', BracketLeft: '^',
};
const QWERTY_LABELS = {
  KeyA: 'A', KeyW: 'W', KeyS: 'S', KeyE: 'E', KeyD: 'D', KeyF: 'F', KeyT: 'T',
  KeyG: 'G', KeyY: 'Y', KeyH: 'H', KeyU: 'U', KeyJ: 'J', KeyK: 'K', KeyO: 'O',
  KeyL: 'L', KeyP: 'P', Semicolon: ';', Quote: "'", BracketRight: ']', Backslash: '\\',
  KeyZ: 'Z', KeyX: 'X', KeyC: 'C', KeyV: 'V', KeyQ: 'Q', KeyR: 'R', KeyI: 'I', BracketLeft: '[',
};

export function guessLayout() {
  const langs = [navigator.language, ...(navigator.languages || [])];
  return langs.some(l => /^fr/i.test(l || '')) ? 'azerty' : 'qwerty';
}

// Key captions: uses the KeyboardLayoutMap API when available, otherwise a table
export async function getKeyLabels(pref = 'auto') {
  const fallback = (pref === 'azerty' || (pref === 'auto' && guessLayout() === 'azerty'))
    ? AZERTY_LABELS : QWERTY_LABELS;
  if (pref === 'auto' && navigator.keyboard && navigator.keyboard.getLayoutMap) {
    try {
      const map = await navigator.keyboard.getLayoutMap();
      const labels = { ...fallback };
      for (const code of Object.keys(fallback)) {
        const k = map.get(code);
        if (k) labels[code] = k.length === 1 ? k.toUpperCase() : k;
      }
      return labels;
    } catch { /* API denied: use the fallback table */ }
  }
  return { ...fallback };
}

function isTypingTarget(e) {
  const t = e.target;
  return t && (t.closest && t.closest('input, textarea, select, [contenteditable="true"]'));
}

// Wires up the listeners. handlers:
//   noteOn(offset), noteOff(offset), octave(±1), velocity(±10), sustain(bool)
export function attachPcKeyboard(handlers) {
  const down = new Set();

  window.addEventListener('keydown', e => {
    if (isTypingTarget(e)) return;
    if (e.ctrlKey || e.metaKey || e.altKey) return; // don't steal Ctrl+S, Ctrl+C…
    const openDlg = document.querySelector('dialog[open]');
    if (openDlg && e.code !== 'Escape') return;

    if (e.code in NOTE_CODES) {
      e.preventDefault();
      if (e.repeat || down.has(e.code)) return;
      down.add(e.code);
      handlers.noteOn(NOTE_CODES[e.code]);
      return;
    }
    switch (e.code) {
      case 'Space':
        e.preventDefault();
        if (!e.repeat) handlers.sustain(true);
        break;
      case 'KeyZ': if (!e.repeat) handlers.octave(-1); break;
      case 'KeyX': if (!e.repeat) handlers.octave(1); break;
      case 'KeyC': if (!e.repeat) handlers.velocity(-10); break;
      case 'KeyV': if (!e.repeat) handlers.velocity(10); break;
    }
  });

  window.addEventListener('keyup', e => {
    if (e.code in NOTE_CODES) {
      if (down.delete(e.code)) handlers.noteOff(NOTE_CODES[e.code]);
      return;
    }
    if (e.code === 'Space') handlers.sustain(false);
  });

  // if the window loses focus while a key is held down
  window.addEventListener('blur', () => {
    down.forEach(code => handlers.noteOff(NOTE_CODES[code]));
    down.clear();
    handlers.sustain(false);
  });
}
