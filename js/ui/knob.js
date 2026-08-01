// Hardware-style rotary knob: vertical drag, wheel, double-click to reset,
// arrow keys. Linear or logarithmic curve.

import { clamp } from '../util.js';

export function createKnob({
  label, value, min = 0, max = 1, def = value, curve = 'lin',
  format = v => String(Math.round(v * 100)), onInput, small = false,
  // Quantises the value (e.g. step: 1 for unison voices or semitones), so the
  // readout and the callback stay on whole steps instead of 2.37 voices.
  step = 0,
}) {
  const el = document.createElement('div');
  el.className = 'knob' + (small ? ' small' : '');
  el.tabIndex = 0;
  el.setAttribute('role', 'slider');
  el.setAttribute('aria-label', label);
  el.innerHTML = `
    <div class="k-wrap">
      <div class="k-dial"></div>
      <div class="k-cap"><div class="k-ind"></div></div>
    </div>
    <div class="k-val"></div>
    <div class="k-lab"></div>`;

  const dial = el.querySelector('.k-dial');
  const valEl = el.querySelector('.k-val');
  const labEl = el.querySelector('.k-lab');
  labEl.textContent = label;

  const toT = v => curve === 'log'
    ? Math.log(clamp(v, min, max) / min) / Math.log(max / min)
    : (clamp(v, min, max) - min) / (max - min);
  const quantise = v => step > 0 ? clamp(Math.round(v / step) * step, min, max) : v;
  const toV = t => quantise(curve === 'log'
    ? min * Math.pow(max / min, clamp(t, 0, 1))
    : min + (max - min) * clamp(t, 0, 1));

  let t = toT(value);

  function paint() {
    const v = toV(t);
    el.style.setProperty('--t', t.toFixed(4));
    el.style.setProperty('--ang', (-135 + t * 270).toFixed(1));
    valEl.textContent = format(v);
    el.setAttribute('aria-valuemin', format(min));
    el.setAttribute('aria-valuemax', format(max));
    el.setAttribute('aria-valuenow', format(v));
  }

  function setT(nt, fire = true) {
    t = clamp(nt, 0, 1);
    paint();
    if (fire && onInput) onInput(toV(t));
  }

  /** Back to the factory value, with a short visual confirmation. */
  function resetToDefault() {
    setT(toT(def));
    el.classList.add('knob-reset');
    setTimeout(() => el.classList.remove('knob-reset'), 260);
  }

  let dragging = false, lastY = 0, moved = 0, holdTimer = null;
  const cancelHold = () => { clearTimeout(holdTimer); holdTimer = null; };

  el.addEventListener('pointerdown', e => {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    dragging = true;
    lastY = e.clientY;
    moved = 0;
    el.setPointerCapture(e.pointerId);
    e.preventDefault();
    // Touch has no double-click: a long press resets instead. Cancelled as
    // soon as the finger actually turns the knob.
    if (e.pointerType !== 'mouse') {
      cancelHold();
      holdTimer = setTimeout(() => { if (moved < 6) resetToDefault(); }, 550);
    }
  });
  el.addEventListener('pointermove', e => {
    if (!dragging) return;
    const dy = e.clientY - lastY;
    moved += Math.abs(dy);
    if (moved >= 6) cancelHold();
    lastY = e.clientY;
    setT(t - dy / (e.shiftKey ? 900 : 150));
  });
  const endDrag = () => { dragging = false; cancelHold(); };
  el.addEventListener('pointerup', endDrag);
  el.addEventListener('pointercancel', endDrag);

  el.addEventListener('dblclick', resetToDefault);
  // Right-click is the other habitual "reset this control" gesture.
  el.addEventListener('contextmenu', e => { e.preventDefault(); resetToDefault(); });
  el.addEventListener('wheel', e => {
    e.preventDefault();
    setT(t - Math.sign(e.deltaY) * 0.04);
  }, { passive: false });
  el.addEventListener('keydown', e => {
    const step = e.shiftKey ? 0.005 : 0.03;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); setT(t + step); }
    else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); setT(t - step); }
    else if (e.key === 'Home') { e.preventDefault(); setT(0); }
    else if (e.key === 'End') { e.preventDefault(); setT(1); }
  });
  paint();
  const api = {
    el,
    set(v, fire = false) { setT(toT(v), fire); },
    get() { return toV(t); },
    reset() { resetToDefault(); },
    setLabel(text) { labEl.textContent = text; el.setAttribute('aria-label', text); },
  };
  // Lets a container (a section, the whole module) reset its knobs without
  // having to keep its own registry of them.
  el._knob = api;
  return api;
}
