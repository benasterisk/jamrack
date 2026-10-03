// The rack: a master module plus one module per instrument instance

import { createKnob } from './knob.js';
import { GM } from '../data/gm.js';
import { el, clamp, esc } from '../util.js';
import { state, allBanks, defaultInstance } from '../state.js';
import {
  putSample, getSample, deleteSample,
  putPatch, getPatch, deletePatch, listPatches,
} from '../audio/sampledb.js';
import { uid } from '../util.js';
import { ANALOG_PRESETS, applyPreset } from '../audio/presets.js';
import { SFZ_BANKS } from '../data/sfz-banks.js';
import { t, tCat, instrumentName, noteName } from '../i18n/index.js';

const WAVES = [
  ['sawtooth', 'waveSaw'], ['square', 'waveSquare'],
  ['triangle', 'waveTriangle'], ['sine', 'waveSine'],
];

const LFO_TARGETS = [
  ['filter', 'lfoFilter'], ['pitch', 'lfoPitch'], ['volume', 'lfoVolume'],
];

// Preset names are proper nouns of the synth world (Supersaw, Reese, 808…):
// they stay as-is unless a translation explicitly overrides them.
function presetName(p) {
  const key = `preset_${p.id}`;
  const translated = t(key);
  return translated === key ? p.name : translated;
}

// Same rule for the built-in SFZ instruments.
function sfzBankName(b) {
  const key = `sfzBank_${b.id}`;
  const translated = t(key);
  return translated === key ? b.name : translated;
}

/**
 * Puts every sound parameter of a module back to its factory value.
 *
 * Deliberately keeps what identifies the module — engine, chosen instrument or
 * bank, and its place in the rack — so "reset" means "undo my tweaks", not
 * "throw my instrument away". For the analog engine the current preset is
 * re-applied rather than wiped, which is what its knobs are relative to.
 */
function resetInstanceSound(inst) {
  const d = defaultInstance();
  const KEEP = new Set(['id', 'engine', 'bankId', 'instrument', 'sfzUrl',
    'preset', 'wave', 'collapsed', 'solo', 'mute', 'keyboardOn', 'sampler']);
  for (const k of Object.keys(d)) {
    if (KEEP.has(k)) continue;
    const v = d[k];
    inst[k] = (v && typeof v === 'object') ? JSON.parse(JSON.stringify(v)) : v;
  }
  // The analog knobs only make sense against the selected preset, so restore
  // that patch rather than the bare defaults.
  if (inst.engine === 'analog' && inst.preset) applyPreset(inst, inst.preset);
}

function noteOptions(selected) {
  let html = '';
  for (let m = 21; m <= 108; m++) {
    html += `<option value="${m}" ${m === selected ? 'selected' : ''}>${esc(noteName(m))}</option>`;
  }
  return html;
}

/** Mono 16-bit WAV from captured Float32 chunks — decodes everywhere. */
function encodeWav(chunks, total, sampleRate) {
  const buf = new ArrayBuffer(44 + total * 2);
  const v = new DataView(buf);
  const w = (o, str) => { for (let i = 0; i < str.length; i++) v.setUint8(o + i, str.charCodeAt(i)); };
  w(0, 'RIFF'); v.setUint32(4, 36 + total * 2, true); w(8, 'WAVE'); w(12, 'fmt ');
  v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, sampleRate, true); v.setUint32(28, sampleRate * 2, true);
  v.setUint16(32, 2, true); v.setUint16(34, 16, true);
  w(36, 'data'); v.setUint32(40, total * 2, true);
  let o = 44;
  for (const c of chunks) {
    for (let i = 0; i < c.length; i++, o += 2) {
      const x = Math.max(-1, Math.min(1, c[i]));
      v.setInt16(o, x * 32767, true);
    }
  }
  return buf;
}

const fmtPct = v => `${Math.round(v * 100)}`;
const fmtPan = v => Math.abs(v) < 0.05 ? 'C' : (v < 0 ? `L${Math.round(-v * 50)}` : `R${Math.round(v * 50)}`);
const fmtSec = v => v < 1 ? `${Math.round(v * 1000)}ms` : `${v.toFixed(2)}s`;
const fmtHz = v => v < 1000 ? `${Math.round(v)}Hz` : `${(v / 1000).toFixed(1)}k`;

export function createRack(container, api) {
  const cards = new Map(); // id -> { root, lcd }

  // A <select> that keeps focus would swallow typed letters (native option
  // cycling): hand focus back to the musical keyboard as soon as a value is set.
  container.addEventListener('change', e => {
    if (e.target && (e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT')) {
      e.target.blur();
    }
  });

  function rebuild() {
    container.innerHTML = '';
    cards.clear();
    container.appendChild(buildMaster());
    container.appendChild(buildGuitar());
    state.instances.forEach(inst => container.appendChild(buildCard(inst)));
    const add = el(`<div id="addModule">
      <span class="add-label">${esc(t('addTitle'))}</span>
      ${[['bank', 'engineBank'], ['analog', 'engineAnalog'],
         ['sfz', 'engineSfz'], ['sampler', 'engineSampler']]
        .map(([v, k]) => `<button class="add-type" data-type="${v}">＋ ${esc(t(k))}</button>`).join('')}
    </div>`);
    add.querySelectorAll('.add-type').forEach(b =>
      b.addEventListener('click', () => api.addInstance(b.dataset.type)));
    container.appendChild(add);
    refreshSoloMute();
  }

  function buildMaster() {
    const m = state.master;
    const root = el(`<section class="module master">
      <div class="master-row">
        <span class="master-title">${esc(t('master'))}</span>
        <canvas id="vuCanvas" width="90" height="26"></canvas>
        <div class="mod-sec-row" data-knobs></div>
      </div>
    </section>`);
    const row = root.querySelector('[data-knobs]');
    row.appendChild(createKnob({
      label: t('volume'), value: m.volume, def: 0.85, format: fmtPct,
      onInput: v => { m.volume = v; api.masterChanged(); },
    }).el);
    row.appendChild(createKnob({
      label: t('reverb'), value: m.reverbDecay, min: 0.3, max: 8, def: 2.2, curve: 'log', format: fmtSec,
      onInput: v => { m.reverbDecay = v; api.masterChanged(); },
    }).el);
    row.appendChild(createKnob({
      label: t('delayTime'), value: m.delayTime, min: 0.06, max: 1.5, def: 0.35, curve: 'log', format: fmtSec,
      onInput: v => { m.delayTime = v; api.masterChanged(); },
    }).el);
    row.appendChild(createKnob({
      label: t('delayFb'), value: m.delayFeedback, min: 0, max: 0.85, def: 0.35, format: fmtPct,
      onInput: v => { m.delayFeedback = v; api.masterChanged(); },
    }).el);
    return root;
  }

  // ------------------------------------------------------------ GUITAR → MIDI
  // An input, not an instrument: what it hears is played by every module
  // whose PLAY switch is on, exactly like a MIDI keyboard. The display is a
  // tuner (note heard, cents) plus the input level and the measured latencies.
  let gtr = null;   // { root, lcd, led, sel, noteEl, centsEl, meter, lat }

  function buildGuitar() {
    const g = state.guitar;
    const root = el(`<section class="module guitar ${g.collapsed ? 'collapsed' : ''}">
      <div class="mod-head">
        <button class="mod-power gtr-power" title="${esc(t('gtrTitlePower'))}"><span class="led"></span></button>
        <span class="master-title gtr-title">${esc(t('gtrTitle'))}</span>
        <div class="mod-lcd gtr-lcd">—</div>
        <div class="mod-selects">
          <select class="sel-bank sel-input" title="${esc(t('gtrTitleDevice'))}"></select>
        </div>
        <div class="mod-btns">
          <button class="sq-btn btn-fold" title="${esc(t('titleFold'))}">${g.collapsed ? '▸' : '▾'}</button>
        </div>
      </div>
      <div class="mod-body">
        <div class="gtr-tuner" title="${esc(t('gtrTitleTuner'))}">
          <div class="gtr-note">—</div>
          <div class="gtr-cents"><i></i></div>
          <canvas class="gtr-meter" width="90" height="26"></canvas>
          <div class="gtr-lat mono"></div>
        </div>
      </div>
    </section>`);
    const body = root.querySelector('.mod-body');
    gtr = {
      root,
      lcd: root.querySelector('.gtr-lcd'),
      led: root.querySelector('.gtr-power .led'),
      sel: root.querySelector('.sel-input'),
      noteEl: root.querySelector('.gtr-note'),
      centsEl: root.querySelector('.gtr-cents'),
      meter: root.querySelector('.gtr-meter'),
      lat: root.querySelector('.gtr-lat'),
    };

    root.querySelector('.gtr-power').addEventListener('click', () => api.guitarToggle());
    root.querySelector('.btn-fold').addEventListener('click', e => {
      g.collapsed = !g.collapsed;
      root.classList.toggle('collapsed', g.collapsed);
      e.currentTarget.textContent = g.collapsed ? '▸' : '▾';
      api.guitarChanged();
    });
    gtr.sel.addEventListener('change', () => api.guitarDevice(gtr.sel.value));

    const changed = () => api.guitarChanged();
    body.appendChild(section(t('gtrInput'), row(
      createKnob({ label: t('gtrGain'), value: g.gain, min: 0.1, max: 10, def: 1, curve: 'log',
        format: v => `${v >= 1 ? '+' : ''}${Math.round(20 * Math.log10(v))}dB`,
        onInput: v => { g.gain = v; changed(); } }).el,
      createKnob({ label: t('gtrSens'), value: g.sens, def: 0.5, format: fmtPct,
        onInput: v => { g.sens = v; changed(); } }).el,
    )));

    const notesRow = row(
      createKnob({ label: t('gtrRelease'), value: g.release, def: 0.5, format: fmtPct,
        onInput: v => { g.release = v; changed(); } }).el,
      createKnob({ label: t('gtrDyn'), value: g.dyn, def: 0.7, format: fmtPct,
        onInput: v => { g.dyn = v; changed(); } }).el,
    );
    const bendTog = el(`<label class="toggle" title="${esc(t('gtrTitleBend'))}">
      <input type="checkbox" ${g.bend ? 'checked' : ''}><span class="sw"></span>${esc(t('gtrBend'))}</label>`);
    bendTog.querySelector('input').addEventListener('change', e => { g.bend = e.target.checked; changed(); });
    notesRow.appendChild(bendTog);
    notesRow.appendChild(stepper(t('octave'), g.octave, -2, 2, v => { g.octave = v; changed(); }));
    body.appendChild(section(t('gtrNotes'), notesRow));

    // current state (a language change rebuilds the card while it runs)
    setGuitarRunning(api.guitarRunning());
    setGuitarDevices(api.guitarDevices(), state.guitar.deviceId);
    setGuitarStatus(api.guitarStatusText());
    drawGuitarMeter(-200);
    return root;
  }

  function setGuitarStatus(text, isErr = false) {
    if (!gtr) return;
    gtr.lcd.textContent = text;
    gtr.lcd.classList.toggle('err', isErr);
  }

  function setGuitarRunning(on) {
    if (!gtr) return;
    gtr.led.classList.toggle('on', on);
    gtr.led.classList.toggle('teal', on);
    gtr.root.classList.toggle('running', on);
    if (!on) {
      gtr.noteEl.textContent = '—';
      gtr.noteEl.classList.remove('heard', 'playing');
      gtr.centsEl.classList.remove('heard', 'intune');
      gtr.centsEl.style.setProperty('--c', '0');
      gtr.lat.textContent = '';
      drawGuitarMeter(-200);
    }
  }

  // Labels only exist once the input has been allowed; before that the
  // browser's default input is the only choice.
  function setGuitarDevices(list, currentId) {
    if (!gtr) return;
    const known = list.some(d => d.id === currentId);
    gtr.sel.innerHTML = `<option value="" ${!known ? 'selected' : ''}>${esc(t('gtrDefaultDevice'))}</option>`
      + list.map(d => `<option value="${esc(d.id)}" ${d.id === currentId ? 'selected' : ''}>${esc(d.label)}</option>`).join('');
  }

  function drawGuitarMeter(db) {
    if (!gtr) return;
    const c = gtr.meter, g2 = c.getContext('2d');
    const w = c.width, h = c.height, segs = 12;
    g2.clearRect(0, 0, w, h);
    const lit = Math.max(0, Math.min(segs, Math.round((db + 60) / 60 * segs)));
    const bw = (w - (segs + 1) * 3) / segs;
    for (let i = 0; i < segs; i++) {
      g2.fillStyle = i < lit ? (i > segs - 3 ? '#ff5040' : i > segs - 6 ? '#ffb454' : '#5fbf72') : '#2a241d';
      g2.fillRect(3 + i * (bw + 3), 5, bw, h - 10);
    }
  }

  /** Tuner + level, from a tracker meter event and the browser's latencies. */
  function setGuitarMeter(info, lat) {
    if (!gtr) return;
    drawGuitarMeter(info.db);
    const heard = !Number.isNaN(info.midiF);
    if (heard) {
      const nearest = Math.round(info.midiF);
      const cents = (info.midiF - nearest) * 100;
      gtr.noteEl.textContent = noteName(nearest);
      gtr.centsEl.style.setProperty('--c', (clamp(cents, -50, 50) / 50).toFixed(3));
      gtr.centsEl.classList.toggle('intune', Math.abs(cents) < 5);
    } else {
      gtr.noteEl.textContent = info.note >= 0 ? noteName(info.note) : '—';
      gtr.centsEl.classList.remove('intune');
    }
    gtr.noteEl.classList.toggle('heard', heard);
    gtr.noteEl.classList.toggle('playing', info.note >= 0);
    gtr.centsEl.classList.toggle('heard', heard);
    const ms = v => (Number.isNaN(v) ? '?' : String(Math.round(v)));
    gtr.lat.innerHTML = `IN <b>${ms(lat.input)}</b> · OUT <b>${ms(lat.output)}</b> · TRK <b>${ms(info.latMs)}</b> ms`;
  }

  function buildCard(inst) {
    const root = el(`<section class="module ${inst.collapsed ? 'collapsed' : ''}" data-id="${inst.id}">
      <div class="mod-head">
        <button class="mod-power" title="${esc(t('titlePower'))}"><span class="led"></span></button>
        <div class="mod-lcd">—</div>
        <div class="mod-selects">
          <span class="mod-type">${esc(t({ bank: 'engineBank', analog: 'engineAnalog',
            sfz: 'engineSfz', sampler: 'engineSampler' }[inst.engine] || 'engineBank'))}</span>
        </div>
        <div class="mod-btns">
          <button class="sq-btn btn-reset" title="${esc(t('titleReset'))}">↺</button>
          <button class="sq-btn btn-solo" title="${esc(t('titleSolo'))}">S</button>
          <button class="sq-btn btn-fold" title="${esc(t('titleFold'))}">${inst.collapsed ? '▸' : '▾'}</button>
          <button class="sq-btn btn-del" title="${esc(t('titleDelete'))}">✕</button>
        </div>
      </div>
      <div class="mod-body"></div>
    </section>`);

    const lcd = root.querySelector('.mod-lcd');
    const selects = root.querySelector('.mod-selects');
    const body = root.querySelector('.mod-body');

    // --- bank/instrument selectors, or waveform for the analog engine ---
    if (inst.engine === 'bank') {
      const selBank = el(`<select class="sel-bank" title="${esc(t('titleBank'))}">${
        allBanks().map(b => `<option value="${esc(b.id)}" ${b.id === inst.bankId ? 'selected' : ''}>${esc(b.name)}</option>`).join('')
      }</select>`);
      const selInst = el(`<select class="sel-inst" title="${esc(t('titleInstrument'))}"><option>${esc(instrumentName(inst.instrument))}</option></select>`);
      selects.append(selBank, selInst);
      // first render: if the saved instrument isn't in this bank, resync the
      // state to whatever the selector actually shows
      populateInstruments(selInst, inst.bankId, inst.instrument).then(effective => {
        if (effective && effective !== inst.instrument) {
          inst.instrument = effective;
          api.changeEngine(inst);
        }
      });
      selBank.addEventListener('change', () => {
        inst.bankId = selBank.value;
        populateInstruments(selInst, inst.bankId, inst.instrument).then(effective => {
          inst.instrument = effective || inst.instrument;
          api.changeEngine(inst);
        });
      });
      selInst.addEventListener('change', () => {
        inst.instrument = selInst.value;
        api.changeEngine(inst);
      });
    } else if (inst.engine === 'sfz') {
      // Built-in SFZ instruments, plus a free-form URL for any other bank.
      const isCustom = !SFZ_BANKS.some(b => b.url === inst.sfzUrl);
      const selBank = el(`<select class="sel-bank sel-sfz" title="${esc(t('titleBank'))}">${
        SFZ_BANKS.map(b => `<option value="${esc(b.url)}" ${b.url === inst.sfzUrl ? 'selected' : ''}>${esc(sfzBankName(b))}</option>`).join('')
      }<option value="__custom__" ${isCustom ? 'selected' : ''}>${esc(t('sfzCustom'))}</option></select>`);
      const urlIn = el(`<input type="url" class="sfz-url" placeholder="https://…/instrument.sfz"
        value="${esc(inst.sfzUrl || '')}" title="${esc(t('sfzUrlTitle'))}">`);
      urlIn.classList.toggle('hidden', !isCustom);
      selects.append(selBank, urlIn);

      selBank.addEventListener('change', () => {
        const custom = selBank.value === '__custom__';
        urlIn.classList.toggle('hidden', !custom);
        if (!custom) {
          inst.sfzUrl = selBank.value;
          api.changeEngine(inst);
        } else {
          urlIn.focus();
        }
      });
      // Only reload once the user is done typing, not on every keystroke.
      urlIn.addEventListener('change', () => {
        inst.sfzUrl = urlIn.value.trim();
        api.changeEngine(inst);
      });
    } else if (inst.engine === 'sampler') {
      const p = inst.sampler;
      const fileIn = el('<input type="file" accept="audio/*" hidden>');
      const loadBtn = el(`<button class="tb-btn smp-btn" title="${esc(t('smpLoadTitle'))}">${esc(t('smpLoad'))}</button>`);
      const recBtn = el(`<button class="tb-btn smp-btn led-btn" title="${esc(t('smpRecTitle'))}"><span class="led"></span>${esc(t('smpRec'))}</button>`);
      selects.append(fileIn, loadBtn, recBtn);

      const storeSample = async (name, data) => {
        const oldId = p.sampleId;
        p.sampleId = 'smp_' + uid();
        p.sampleName = name;
        await putSample(p.sampleId, name, data);
        if (oldId) deleteSample(oldId);
        api.changeEngine(inst);
      };

      loadBtn.addEventListener('click', () => fileIn.click());
      fileIn.addEventListener('change', async () => {
        const f = fileIn.files[0];
        if (!f) return;
        storeSample(f.name.replace(/\.[^.]+$/, ''), await f.arrayBuffer());
      });

      // Mic capture: permission is only requested when the user actually
      // presses REC; press again to stop. NOT MediaRecorder: its container
      // varies per browser and iOS Safari cannot reliably decode its own
      // fragmented-MP4 recordings. Raw PCM through the audio graph, encoded
      // to WAV by hand, decodes everywhere.
      let rec = null;
      const stopRec = () => {
        if (!rec) return;
        const r = rec;
        rec = null;
        clearTimeout(r.timer);
        recBtn.querySelector('.led').classList.remove('on');
        try { r.node.disconnect(); r.src.disconnect(); r.mute.disconnect(); } catch { /* done */ }
        r.stream.getTracks().forEach(tr => tr.stop());
        const total = r.chunks.reduce((a, c) => a + c.length, 0);
        if (total < r.sr * 0.05) { lcd.textContent = t('smpTooShort'); return; }
        storeSample(t('smpRecName'), encodeWav(r.chunks, total, r.sr));
      };
      recBtn.addEventListener('click', async () => {
        if (rec) { stopRec(); return; }
        // Every failure path must say WHY on the module display — a mic that
        // silently does nothing is undebuggable on a phone.
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
          lcd.textContent = t('smpMicUnavailable');
          return;
        }
        try {
          const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
          // The engine's context, not a fresh one: iOS caps concurrent
          // AudioContexts, and this one is already unlocked by user gestures.
          const ctx = api.audioCtx;
          if (ctx.state !== 'running') await ctx.resume();
          const src = ctx.createMediaStreamSource(stream);
          const node = ctx.createScriptProcessor(4096, 1, 1);
          const chunks = [];
          node.onaudioprocess = e => {
            chunks.push(new Float32Array(e.inputBuffer.getChannelData(0)));
          };
          // The processor only runs while routed to an output; a muted gain
          // avoids hearing yourself while recording.
          const mute = ctx.createGain();
          mute.gain.value = 0;
          src.connect(node);
          node.connect(mute);
          mute.connect(ctx.destination);
          rec = {
            stream, src, node, mute, chunks, sr: ctx.sampleRate,
            timer: setTimeout(stopRec, 30000),   // 30 s hard cap
          };
          recBtn.querySelector('.led').classList.add('on');
          lcd.textContent = t('smpRecording');
        } catch (err) {
          lcd.textContent = `${t('smpMicDenied')} — ${(err && err.name) || 'Error'}`;
        }
      });

      // --- patch library: the sample AND its settings under a name ---
      const patchSel = el(`<select class="sel-patch" title="${esc(t('smpPatchesTitle'))}">
        <option value="">${esc(t('smpPatches'))}</option></select>`);
      const saveBtn = el(`<button class="tb-btn smp-btn" title="${esc(t('smpSaveTitle'))}">${esc(t('smpSave'))}</button>`);
      const delBtn = el(`<button class="tb-btn smp-btn" title="${esc(t('smpDelTitle'))}">✕</button>`);
      selects.append(patchSel, saveBtn, delBtn);

      async function refreshPatches(selectedId = '') {
        const list = await listPatches();
        patchSel.innerHTML = `<option value="">${esc(t('smpPatches'))}</option>`
          + list.map(x => `<option value="${esc(x.id)}" ${x.id === selectedId ? 'selected' : ''}>${esc(x.name)}</option>`).join('');
      }
      refreshPatches();

      saveBtn.addEventListener('click', async () => {
        if (!p.sampleId) return;
        const name = (prompt(t('smpSaveName'), p.sampleName || '') || '').trim();
        if (!name) return;
        const rec = await getSample(p.sampleId);
        if (!rec) return;
        // Everything but the live sample id: the audio itself is embedded, so
        // the patch stays valid even if this module (and its sample) go away.
        const { sampleId, ...settings } = p;
        const id = 'patch_' + uid();
        await putPatch(id, { name, settings, data: rec.data });
        refreshPatches(id);
      });

      patchSel.addEventListener('change', async () => {
        const id = patchSel.value;
        if (!id) return;
        const patch = await getPatch(id);
        if (!patch) return;
        const oldId = p.sampleId;
        const newId = 'smp_' + uid();
        await putSample(newId, patch.settings.sampleName || patch.name, patch.data);
        Object.assign(inst.sampler, patch.settings,
          { sampleId: newId, sampleName: patch.settings.sampleName || patch.name });
        if (oldId) deleteSample(oldId);
        rebuild();                      // the knobs/toggles must show the patch
        api.changeEngine(inst);
      });

      delBtn.addEventListener('click', async () => {
        const id = patchSel.value;
        if (!id) return;
        if (!confirm(t('smpDelConfirm'))) return;
        await deletePatch(id);
        refreshPatches();
      });
    } else {
      // Presets first: a synth with this many parameters is unusable without
      // ready-made starting points, and this is what people reach for.
      const selPreset = el(`<select class="sel-bank sel-preset" title="${esc(t('titlePreset'))}">${
        ANALOG_PRESETS.map(p => `<option value="${esc(p.id)}" ${p.id === inst.preset ? 'selected' : ''}>${esc(presetName(p))}</option>`).join('')
      }</select>`);
      const selWave = el(`<select class="sel-inst" title="${esc(t('titleWave'))}">${
        WAVES.map(([v, key]) => `<option value="${v}" ${v === inst.wave ? 'selected' : ''}>${esc(t(key))}</option>`).join('')
      }</select>`);
      selects.append(selPreset, selWave);
      selPreset.addEventListener('change', () => {
        applyPreset(inst, selPreset.value);
        rebuild();            // a preset moves the knobs: redraw the panel
        api.changeEngine(inst);
      });
      selWave.addEventListener('change', () => {
        inst.wave = selWave.value;
        api.applyParams(inst);
      });
    }

    // --- header buttons ---
    root.querySelector('.mod-power').addEventListener('click', () => {
      inst.mute = !inst.mute;
      api.refreshRouting();
    });
    root.querySelector('.btn-solo').addEventListener('click', () => {
      inst.solo = !inst.solo;
      api.refreshRouting();
    });
    root.querySelector('.btn-fold').addEventListener('click', e => {
      inst.collapsed = !inst.collapsed;
      root.classList.toggle('collapsed', inst.collapsed);
      e.currentTarget.textContent = inst.collapsed ? '▸' : '▾';
      api.applyParams(inst);
    });
    root.querySelector('.btn-del').addEventListener('click', () => api.removeInstance(inst.id));
    root.querySelector('.btn-reset').addEventListener('click', () => {
      if (!confirm(t('confirmReset'))) return;
      resetInstanceSound(inst);
      rebuild();                       // the knobs must show the new values
      api.changeEngine(inst);
    });

    // --- body ---
    body.appendChild(section(t('level'), row(
      createKnob({ label: t('volume'), value: inst.volume, def: 0.8, format: fmtPct,
        onInput: v => { inst.volume = v; api.applyParams(inst); } }).el,
      createKnob({ label: t('pan'), value: inst.pan, min: -1, max: 1, def: 0, format: fmtPan,
        onInput: v => { inst.pan = v; api.applyParams(inst); } }).el,
    )));

    const kbSec = section(t('keyboard'), el(`<div class="mod-sec-row"></div>`));
    const kbRow = kbSec.querySelector('.mod-sec-row');
    const tog = el(`<label class="toggle" title="${esc(t('titlePlay'))}">
      <input type="checkbox" ${inst.keyboardOn ? 'checked' : ''}><span class="sw"></span>${esc(t('play'))}</label>`);
    tog.querySelector('input').addEventListener('change', e => {
      inst.keyboardOn = e.target.checked;
      api.refreshRouting();
    });
    kbRow.appendChild(tog);
    kbRow.appendChild(stepper(t('octave'), inst.octave, -3, 3, v => {
      inst.octave = v; api.applyParams(inst);
    }));
    kbRow.appendChild(stepper(t('transpose'), inst.transpose, -12, 12, v => {
      inst.transpose = v; api.applyParams(inst);
    }));
    const range = el(`<div class="stepper"><div class="range-selects">
        <select class="sel-lo" title="${esc(t('titleRangeLo'))}">${noteOptions(clamp(inst.rangeLo, 21, 108))}</select>
        <span>→</span>
        <select class="sel-hi" title="${esc(t('titleRangeHi'))}">${noteOptions(clamp(inst.rangeHi, 21, 108))}</select>
      </div><span class="stepper-lab">${esc(t('range'))}</span></div>`);
    range.querySelector('.sel-lo').addEventListener('change', e => {
      inst.rangeLo = Number(e.target.value); api.applyParams(inst);
    });
    range.querySelector('.sel-hi').addEventListener('change', e => {
      inst.rangeHi = Number(e.target.value); api.applyParams(inst);
    });
    kbRow.appendChild(range);

    // Per-module note repeat: a switch plus its own division, so this module
    // can roll hats at 1/16 while the pad next to it just holds.
    const repDivs = ['4', '8', '8t', '16', '16t', '32'];
    const repCtl = el(`<div class="stepper">
      <div class="rep-ctl">
        <label class="toggle" title="${esc(t('titleRepeat'))}">
          <input type="checkbox" ${inst.repeatOn ? 'checked' : ''}><span class="sw"></span></label>
        <select class="sel-rep" title="${esc(t('titleRepeat'))}">${
          repDivs.map(d => `<option value="${d}" ${d === inst.repeatDiv ? 'selected' : ''}>1/${d.toUpperCase()}</option>`).join('')
        }</select>
      </div>
      <span class="stepper-lab">${esc(t('noteRepeat'))}</span></div>`);
    repCtl.querySelector('input').addEventListener('change', e => {
      inst.repeatOn = e.target.checked;
      api.applyParams(inst);
    });
    repCtl.querySelector('.sel-rep').addEventListener('change', e => {
      inst.repeatDiv = e.target.value;
      api.applyParams(inst);
    });
    kbRow.appendChild(repCtl);
    body.appendChild(kbSec);

    body.appendChild(section(t('envelope'), row(
      createKnob({ small: true, label: 'A', value: inst.adsr.a, min: 0.001, max: 3, def: 0.002, curve: 'log', format: fmtSec,
        onInput: v => { inst.adsr.a = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: 'D', value: inst.adsr.d, min: 0.01, max: 3, def: 0.08, curve: 'log', format: fmtSec,
        onInput: v => { inst.adsr.d = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: 'S', value: inst.adsr.s, def: 1, format: fmtPct,
        onInput: v => { inst.adsr.s = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: 'R', value: inst.adsr.r, min: 0.02, max: 5, def: 0.35, curve: 'log', format: fmtSec,
        onInput: v => { inst.adsr.r = v; api.applyParams(inst); } }).el,
    )));

    body.appendChild(section(t('filter'), row(
      createKnob({ label: t('cutoff'), value: inst.filterCut, def: 1,
        format: v => fmtHz(40 * Math.pow(450, v)),
        onInput: v => { inst.filterCut = v; api.applyParams(inst); } }).el,
      createKnob({ label: t('reso'), value: inst.filterRes, def: 0, format: fmtPct,
        onInput: v => { inst.filterRes = v; api.applyParams(inst); } }).el,
    )));

    const fx = [
      createKnob({ label: t('reverb'), value: inst.revSend, def: 0.25, format: fmtPct,
        onInput: v => { inst.revSend = v; api.applyParams(inst); } }).el,
      createKnob({ label: t('delay'), value: inst.delSend, def: 0, format: fmtPct,
        onInput: v => { inst.delSend = v; api.applyParams(inst); } }).el,
    ];
    if (inst.engine === 'analog') {
      fx.push(createKnob({ label: t('detune'), value: inst.detune, min: 0, max: 30, def: 7,
        format: v => `${Math.round(v)}ct`,
        onInput: v => { inst.detune = v; api.applyParams(inst); } }).el);
    }
    body.appendChild(section(t('effects'), row(...fx)));

    if (inst.engine === 'analog') body.appendChild(synthSection(inst));

    let drawSampler = null;
    if (inst.engine === 'sampler') {
      const ss = samplerSection(inst);
      body.appendChild(ss.el);
      drawSampler = ss.draw;
      requestAnimationFrame(ss.draw);   // the canvas needs layout first
    }

    cards.set(inst.id, { root, lcd, drawSampler });
    return root;
  }

  // ------------------------------------------------------------- sampler UI
  // Waveform editor: start/end handles (amber) trim the sample, loop handles
  // (teal) appear when LOOP is on, and CHOP overlays the slice grid. The four
  // handles are draggable straight on the canvas.
  function samplerSection(inst) {
    const p = inst.sampler;
    const wrap = el('<div class="mod-sec sampler-sec"></div>');
    const canvas = el('<canvas class="smp-wave" height="70"></canvas>');
    wrap.appendChild(canvas);

    function draw() {
      const buffer = api.samplerBuffer(inst);
      const W = canvas.width = canvas.clientWidth || 600;
      const H = canvas.height;
      const g = canvas.getContext('2d');
      g.clearRect(0, 0, W, H);
      if (!buffer) {
        g.fillStyle = '#94876f66';
        g.font = '12px sans-serif';
        g.textAlign = 'center';
        g.fillText(t('smpNoSample'), W / 2, H / 2 + 4);
        return;
      }
      const data = buffer.getChannelData(0);
      const step = Math.max(1, Math.floor(data.length / W));
      g.strokeStyle = '#ffb45499';
      g.beginPath();
      for (let x = 0; x < W; x++) {
        let lo = 1, hi = -1;
        const base = x * step;
        const sub = Math.max(1, Math.floor(step / 20));
        for (let i = 0; i < step; i += sub) {
          const v = data[base + i] || 0;
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
        g.moveTo(x + 0.5, (1 - hi) * H / 2);
        g.lineTo(x + 0.5, (1 - lo) * H / 2);
      }
      g.stroke();
      // dim what the trim excludes
      g.fillStyle = '#000000a0';
      g.fillRect(0, 0, p.start * W, H);
      g.fillRect(p.end * W, 0, W - p.end * W, H);
      // chop grid, with the slice number in each segment
      if (p.chop > 0) {
        g.strokeStyle = '#ece3d355';
        g.fillStyle = '#ffd9a0cc';
        g.font = '10px monospace';
        g.textAlign = 'center';
        for (let i = 0; i < p.chop; i++) {
          const x0 = (p.start + (p.end - p.start) * i / p.chop) * W;
          const x1 = (p.start + (p.end - p.start) * (i + 1) / p.chop) * W;
          if (i > 0) { g.beginPath(); g.moveTo(x0, 0); g.lineTo(x0, H); g.stroke(); }
          if (x1 - x0 > 14) g.fillText(String(i + 1), (x0 + x1) / 2, 12);
        }
      }
      const handle = (frac, color) => {
        const x = frac * W;
        g.strokeStyle = color;
        g.lineWidth = 2;
        g.beginPath(); g.moveTo(x, 0); g.lineTo(x, H); g.stroke();
        g.fillStyle = color;
        g.fillRect(x - 4, 0, 8, 7);
        g.lineWidth = 1;
      };
      handle(p.start, '#ffb454');
      handle(p.end, '#ffb454');
      if (p.loopOn && p.chop === 0) {
        handle(p.loopStart, '#3ad0c4');
        handle(p.loopEnd, '#3ad0c4');
      }
    }

    // dragging the nearest handle
    let dragKey = null;
    const frac = e => {
      const r = canvas.getBoundingClientRect();
      return clamp((e.clientX - r.left) / r.width, 0, 1);
    };
    canvas.addEventListener('pointerdown', e => {
      const f = frac(e);
      const candidates = [['start', p.start], ['end', p.end]];
      if (p.loopOn && p.chop === 0) candidates.push(['loopStart', p.loopStart], ['loopEnd', p.loopEnd]);
      let best = null, bestD = 0.03;          // ~3% of the width grabs a handle
      candidates.forEach(([k, v]) => {
        const d = Math.abs(v - f);
        if (d < bestD) { best = k; bestD = d; }
      });
      if (!best) return;
      dragKey = best;
      canvas.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    canvas.addEventListener('pointermove', e => {
      if (!dragKey) return;
      const f = frac(e);
      if (dragKey === 'start') p.start = Math.min(f, p.end - 0.01);
      else if (dragKey === 'end') p.end = Math.max(f, p.start + 0.01);
      else if (dragKey === 'loopStart') p.loopStart = Math.min(f, p.loopEnd - 0.01);
      else p.loopEnd = Math.max(f, p.loopStart + 0.01);
      draw();
    });
    const endDrag = () => {
      if (!dragKey) return;
      dragKey = null;
      api.applyParams(inst);
    };
    canvas.addEventListener('pointerup', endDrag);
    canvas.addEventListener('pointercancel', endDrag);

    // --- controls under the waveform ---
    const rootWrap = el(`<div class="stepper">
      <select class="sel-root" title="${esc(t('smpRoot'))}">${noteOptions(clamp(p.rootMidi, 21, 108))}</select>
      <span class="stepper-lab">${esc(t('smpRoot'))}</span></div>`);
    rootWrap.querySelector('select').addEventListener('change', e => {
      p.rootMidi = Number(e.target.value);
      api.applyParams(inst);
    });

    const tgl = (key, label) => {
      const l = el(`<label class="toggle"><input type="checkbox" ${p[key] ? 'checked' : ''}>
        <span class="sw"></span>${esc(label)}</label>`);
      l.querySelector('input').addEventListener('change', e => {
        p[key] = e.target.checked;
        api.applyParams(inst);
        draw();
      });
      return l;
    };

    const chopWrap = el(`<div class="stepper">
      <select class="sel-chop" title="${esc(t('smpChopTitle'))}">${
        [0, 4, 8, 16].map(v => `<option value="${v}" ${v === p.chop ? 'selected' : ''}>${v === 0 ? esc(t('osc2Off')) : v}</option>`).join('')
      }</select>
      <span class="stepper-lab">${esc(t('smpChop'))}</span></div>`);
    chopWrap.querySelector('select').addEventListener('change', e => {
      p.chop = Number(e.target.value);
      api.applyParams(inst);
      draw();
      updateHint();
    });

    const baseWrap = el(`<div class="stepper">
      <select class="sel-chopbase" title="${esc(t('smpChopBaseTitle'))}">${noteOptions(clamp(p.chopBase, 21, 108))}</select>
      <span class="stepper-lab">${esc(t('smpChopBase'))}</span></div>`);
    baseWrap.querySelector('select').addEventListener('change', e => {
      p.chopBase = Number(e.target.value);
      api.applyParams(inst);
      updateHint();
    });

    // Which keys actually play: this is what made CHOP look broken — the
    // slices sat on keys nobody was pressing, with nothing saying so.
    const hint = el('<div class="smp-hint"></div>');
    function updateHint() {
      if (p.chop > 0) {
        hint.textContent = `${t('smpChop')} ${p.chop} : ${noteName(p.chopBase)} → ${noteName(Math.min(127, p.chopBase + p.chop - 1))}`;
        hint.classList.add('on');
      } else {
        hint.textContent = '';
        hint.classList.remove('on');
      }
    }
    updateHint();

    const controls = el('<div class="mod-sec-row smp-row"></div>');
    controls.append(
      rootWrap,
      tgl('keytrack', t('smpKeytrack')),
      tgl('loopOn', t('smpLoop')),
      tgl('reverse', t('smpReverse')),
      tgl('oneShot', t('smpOneShot')),
      chopWrap,
      baseWrap,
      tgl('stretchOn', t('smpStretch')),
      createKnob({ small: true, label: t('smpSpeed'), value: p.stretch, min: 0.25, max: 4, def: 1,
        curve: 'log', format: v => '×' + v.toFixed(2),
        onInput: v => { p.stretch = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: t('smpGrain'), value: p.grain, min: 0.03, max: 0.2, def: 0.09,
        format: v => Math.round(v * 1000) + 'ms',
        onInput: v => { p.grain = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: t('smpTune'), value: p.tune, min: -100, max: 100, def: 0,
        format: v => `${Math.round(v)}ct`, onInput: v => { p.tune = v; api.applyParams(inst); } }).el,
      createKnob({ small: true, label: t('smpGain'), value: p.gain, min: 0, max: 2, def: 1,
        format: v => `${Math.round(v * 100)}`, onInput: v => { p.gain = v; api.applyParams(inst); } }).el,
    );
    wrap.appendChild(hint);
    wrap.appendChild(controls);

    return { el: wrap, draw };
  }

  // Advanced synth parameters, folded away by default: presets cover the common
  // case, and unfolding two dozen knobs by default would drown the module —
  // especially on a phone.
  function synthSection(inst) {
    const syn = inst.synth;
    const wrap = el('<div class="mod-sec synth-sec"></div>');
    const head = el(`<button type="button" class="synth-toggle" aria-expanded="false">
      <span class="synth-caret">▸</span> ${esc(t('synthAdvanced'))}
    </button>`);
    const panel = el('<div class="synth-panel hidden"></div>');

    const set = (key, v) => { syn[key] = v; api.applyParams(inst); };

    // --- oscillators ---
    panel.appendChild(section(t('oscillators'), row(
      createKnob({ small: true, label: t('unison'), value: syn.unison, min: 1, max: 7, def: 1, step: 1,
        format: v => `${Math.round(v)}`, onInput: v => set('unison', Math.round(v)) }).el,
      createKnob({ small: true, label: t('uniDetune'), value: syn.uniDetune, def: 0.18, format: fmtPct,
        onInput: v => set('uniDetune', v) }).el,
      createKnob({ small: true, label: t('uniSpread'), value: syn.uniSpread, def: 0.7, format: fmtPct,
        onInput: v => set('uniSpread', v) }).el,
      createKnob({ small: true, label: t('sub'), value: syn.sub, def: 0, format: fmtPct,
        onInput: v => set('sub', v) }).el,
    )));

    // --- second oscillator ---
    const selOsc2 = el(`<select class="sel-osc2" title="${esc(t('osc2'))}">
      <option value="off" ${syn.osc2 === 'off' ? 'selected' : ''}>${esc(t('osc2Off'))}</option>
      ${WAVES.map(([v, key]) => `<option value="${v}" ${v === syn.osc2 ? 'selected' : ''}>${esc(t(key))}</option>`).join('')}
    </select>`);
    selOsc2.addEventListener('change', () => set('osc2', selOsc2.value));
    const osc2Row = row(
      createKnob({ small: true, label: t('osc2Semi'), value: syn.osc2Semi, min: -24, max: 24, def: 0, step: 1,
        format: v => `${v > 0 ? '+' : ''}${Math.round(v)}`, onInput: v => set('osc2Semi', Math.round(v)) }).el,
      createKnob({ small: true, label: t('osc2Mix'), value: syn.osc2Mix, def: 0.5, format: fmtPct,
        onInput: v => set('osc2Mix', v) }).el,
    );
    osc2Row.prepend(selOsc2);
    panel.appendChild(section(t('osc2'), osc2Row));

    // --- filter envelope ---
    panel.appendChild(section(t('filterEnv'), row(
      createKnob({ small: true, label: t('amount'), value: syn.fEnvAmt, def: 0, format: fmtPct,
        onInput: v => set('fEnvAmt', v) }).el,
      createKnob({ small: true, label: 'A', value: syn.fAttack, min: 0.001, max: 2, def: 0.005, curve: 'log', format: fmtSec,
        onInput: v => set('fAttack', v) }).el,
      createKnob({ small: true, label: 'D', value: syn.fDecay, min: 0.01, max: 3, def: 0.25, curve: 'log', format: fmtSec,
        onInput: v => set('fDecay', v) }).el,
      createKnob({ small: true, label: 'S', value: syn.fSustain, def: 0.2, format: fmtPct,
        onInput: v => set('fSustain', v) }).el,
    )));

    // --- LFO ---
    const selLfo = el(`<select class="sel-lfo" title="${esc(t('lfoTarget'))}">${
      LFO_TARGETS.map(([v, key]) => `<option value="${v}" ${v === syn.lfoTarget ? 'selected' : ''}>${esc(t(key))}</option>`).join('')
    }</select>`);
    selLfo.addEventListener('change', () => set('lfoTarget', selLfo.value));
    const lfoRow = row(
      createKnob({ small: true, label: t('rate'), value: syn.lfoRate, min: 0.05, max: 20, def: 5, curve: 'log',
        format: v => `${v < 10 ? v.toFixed(2) : v.toFixed(1)}Hz`, onInput: v => set('lfoRate', v) }).el,
      createKnob({ small: true, label: t('amount'), value: syn.lfoAmt, def: 0, format: fmtPct,
        onInput: v => set('lfoAmt', v) }).el,
    );
    lfoRow.prepend(selLfo);
    panel.appendChild(section(t('lfo'), lfoRow));

    // --- colouring ---
    panel.appendChild(section(t('colour'), row(
      createKnob({ small: true, label: t('drive'), value: syn.drive, def: 0, format: fmtPct,
        onInput: v => set('drive', v) }).el,
      // A log scale cannot start at 0 (log(v/0) is NaN), so glide bottoms out
      // at 1 ms and is treated as "off" below 5 ms.
      createKnob({ small: true, label: t('glide'), value: Math.max(0.001, syn.glide || 0.001),
        min: 0.001, max: 1, def: 0.001, curve: 'log',
        format: v => (v <= 0.005 ? t('osc2Off') : fmtSec(v)),
        onInput: v => set('glide', v <= 0.005 ? 0 : v) }).el,
    )));

    head.addEventListener('click', () => {
      const open = panel.classList.toggle('hidden');
      head.setAttribute('aria-expanded', String(!open));
      head.querySelector('.synth-caret').textContent = open ? '▸' : '▾';
    });

    wrap.append(head, panel);
    return wrap;
  }

  // Fills the instrument selector and returns the id actually selected.
  // A per-<select> token discards stale responses when the user switches banks
  // faster than the network answers.
  function populateInstruments(sel, bankId, current) {
    const token = (sel._popToken || 0) + 1;
    sel._popToken = token;
    return api.instrumentsFor(bankId).then(ids => {
      if (sel._popToken !== token || !sel.isConnected) return null;
      const available = new Set(ids);
      const groups = new Map();
      GM.forEach(g => {
        if (!available.has(g.id)) return;
        if (!groups.has(g.cat)) groups.set(g.cat, []);
        groups.get(g.cat).push(g);
      });
      // instruments outside the GM naming (custom banks): remote data, escaped
      const extra = ids.filter(id => !GM.some(g => g.id === id));
      let html = '';
      groups.forEach((list, cat) => {
        html += `<optgroup label="${esc(tCat(cat))}">${
          list.map(g => `<option value="${g.id}" ${g.id === current ? 'selected' : ''}>${esc(instrumentName(g.id))}</option>`).join('')
        }</optgroup>`;
      });
      if (extra.length) {
        html += `<optgroup label="${esc(tCat('Other'))}">${
          extra.map(id => `<option value="${esc(id)}" ${id === current ? 'selected' : ''}>${esc(String(id).replace(/_/g, ' '))}</option>`).join('')
        }</optgroup>`;
      }
      sel.innerHTML = html;
      if (!available.has(current) && ids.length) sel.value = ids[0];
      return sel.value || null;
    }).catch(() => null /* bank not answering: the LCD shows the error */);
  }

  // Every section gets a small reset button. Knobs already reset on
  // double-click (and long-press on touch), but that is invisible: an explicit
  // control is the only one people actually find.
  function section(title, content) {
    const s = el(`<div class="mod-sec">
      <div class="mod-sec-head">
        <div class="mod-sec-title"></div>
        <button type="button" class="sec-reset" title="${esc(t('resetSection'))}"
          aria-label="${esc(t('resetSection'))}">↺</button>
      </div>
    </div>`);
    s.querySelector('.mod-sec-title').textContent = title;
    s.appendChild(content);
    s.querySelector('.sec-reset').addEventListener('click', () => {
      content.querySelectorAll('.knob').forEach(k => {
        if (k._knob) k._knob.reset();
      });
    });
    return s;
  }

  function row(...children) {
    const r = el(`<div class="mod-sec-row"></div>`);
    children.forEach(c => r.appendChild(c));
    return r;
  }

  function stepper(lab, value, min, max, onChange) {
    const s = el(`<div class="stepper">
      <div class="stepper-ctl">
        <button class="sq-btn">–</button>
        <span class="stepper-read">${value > 0 ? '+' + value : value}</span>
        <button class="sq-btn">+</button>
      </div>
      <span class="stepper-lab"></span></div>`);
    s.querySelector('.stepper-lab').textContent = lab;
    const [minus, plus] = s.querySelectorAll('button');
    const read = s.querySelector('.stepper-read');
    let v = value;
    const set = nv => {
      v = clamp(nv, min, max);
      read.textContent = v > 0 ? `+${v}` : String(v);
      onChange(v);
    };
    minus.addEventListener('click', () => set(v - 1));
    plus.addEventListener('click', () => set(v + 1));
    return s;
  }

  function setStatus(id, text, isErr = false) {
    const card = cards.get(id);
    if (!card) return;
    card.lcd.textContent = text;
    card.lcd.classList.toggle('err', isErr);
  }

  function refreshSoloMute() {
    const anySolo = state.instances.some(i => i.solo);
    state.instances.forEach(inst => {
      const card = cards.get(inst.id);
      if (!card) return;
      const led = card.root.querySelector('.mod-power .led');
      const effMute = inst.mute || (anySolo && !inst.solo);
      led.classList.toggle('on', !effMute);
      card.root.querySelector('.btn-solo').classList.toggle('active-teal', inst.solo);
    });
  }

  return {
    rebuild, setStatus, refreshSoloMute,
    setGuitarStatus, setGuitarRunning, setGuitarDevices, setGuitarMeter,
    refreshSampler(id) {
      const c = cards.get(id);
      if (c && c.drawSampler) c.drawSampler();
    },
  };
}
