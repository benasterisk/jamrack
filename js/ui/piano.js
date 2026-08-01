// Touch piano: multi-touch, glissando, PC key captions

import { isBlack } from '../util.js';
import { noteName } from '../i18n/index.js';

export function createPiano(container, { onNoteOn, onNoteOff }) {
  let keyEls = new Map();   // midi -> element
  let low = 48, count = 3;  // first note shown, number of octaves
  const pointers = new Map(); // pointerId -> midi
  let keycaps = new Map();  // midi -> PC key caption

  function render(newLow, octaves) {
    low = newLow;
    count = octaves;
    container.innerHTML = '';
    keyEls = new Map();

    const high = low + octaves * 12;
    const whites = [];
    for (let m = low; m <= high; m++) if (!isBlack(m)) whites.push(m);
    const whiteW = 100 / whites.length;

    whites.forEach(m => {
      const el = document.createElement('div');
      el.className = 'pk w';
      el.dataset.midi = m;
      if (m % 12 === 0) {
        const label = document.createElement('span');
        label.className = 'notename';
        label.textContent = noteName(m);
        el.appendChild(label);
      }
      container.appendChild(el);
      keyEls.set(m, el);
    });

    for (let m = low; m <= high; m++) {
      if (!isBlack(m)) continue;
      const whitesBefore = whites.filter(w => w < m).length;
      const el = document.createElement('div');
      el.className = 'pk b';
      el.dataset.midi = m;
      el.style.left = `calc(${(whitesBefore * whiteW).toFixed(3)}% - var(--bkw) / 2)`;
      container.appendChild(el);
      keyEls.set(m, el);
    }

    container.style.setProperty('--bkw', `${(whiteW * 0.58).toFixed(3)}%`);
    paintKeycaps();
  }

  function setKeycaps(map) {
    keycaps = map;
    paintKeycaps();
  }

  function paintKeycaps() {
    keyEls.forEach(el => {
      const old = el.querySelector('.keycap');
      if (old) old.remove();
    });
    keycaps.forEach((label, midi) => {
      const el = keyEls.get(midi);
      if (!el) return;
      const cap = document.createElement('span');
      cap.className = 'keycap';
      cap.textContent = label;
      el.appendChild(cap);
    });
  }

  function setActive(midi, on) {
    const el = keyEls.get(midi);
    if (el) el.classList.toggle('active', on);
  }

  function keyFromPoint(x, y) {
    const el = document.elementFromPoint(x, y);
    const key = el && el.closest && el.closest('.pk');
    return key ? Number(key.dataset.midi) : null;
  }

  container.addEventListener('pointerdown', e => {
    if (e.pointerType === 'mouse' && e.button !== 0) return; // right/middle click isn't a note
    const midi = keyFromPoint(e.clientX, e.clientY);
    if (midi === null) return;
    e.preventDefault();
    try { container.setPointerCapture(e.pointerId); } catch { /* ok */ }
    pointers.set(e.pointerId, midi);
    onNoteOn(midi);
  });

  container.addEventListener('pointermove', e => {
    if (!pointers.has(e.pointerId)) return;
    const cur = pointers.get(e.pointerId);
    const midi = keyFromPoint(e.clientX, e.clientY);
    if (midi !== null && midi !== cur) {
      onNoteOff(cur);
      pointers.set(e.pointerId, midi);
      onNoteOn(midi);
    }
  });

  const lift = e => {
    if (!pointers.has(e.pointerId)) return;
    onNoteOff(pointers.get(e.pointerId));
    pointers.delete(e.pointerId);
  };
  container.addEventListener('pointerup', lift);
  container.addEventListener('pointercancel', lift);
  container.addEventListener('contextmenu', e => e.preventDefault());

  return { render, setActive, setKeycaps, get low() { return low; }, get count() { return count; } };
}
