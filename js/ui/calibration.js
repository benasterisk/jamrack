// Calibration assistant of the POLY bank: a dialog that asks for one pluck
// per string (then, optionally, one at the 12th fret), measures it with
// js/audio/guitar/poly/calibrate.js on the raw input relayed by the guitar
// worklet, and saves the resulting profile (profiles.js). The guitar input
// must be running: the dialog starts it if needed.

import { PluckCapture, makeProfile, OPEN_STRING_MIDI } from '../audio/guitar/poly/calibrate.js';
import { putProfile, deleteProfile, listProfiles, exportProfile, importProfile } from '../audio/guitar/poly/profiles.js';
import { t, noteName } from '../i18n/index.js';
import { esc } from '../util.js';

const fmtB = b => (b > 0 ? b.toExponential(2).replace('e-', '·10⁻') : '0');
const fmtCents = c => `${c >= 0 ? '+' : ''}${Math.round(c)}`;

/**
 * api: { guitar (createGuitarInput result), ctxRate, onProfilesChanged(list),
 *        selectProfile(profileOrNull) }
 */
export function setupCalibration(api) {
  const dlg = document.getElementById('calDlg');
  const body = document.getElementById('calBody');
  dlg.querySelector('[data-close]').addEventListener('click', () => dlg.close());
  dlg.addEventListener('close', () => stopStep());

  let open = [];          // results per open string
  let fret12 = [];        // results at the 12th fret (optional pass)
  let step = null;        // { s, fret12 }
  let cap = null;
  let meterTimer = 0;

  function show() {
    open = []; fret12 = []; step = null;
    renderIntro();
    dlg.showModal();
  }

  function stopStep() {
    if (cap) { api.guitar.stopCapture(); cap = null; }
    clearInterval(meterTimer);
  }

  // ---------------------------------------------------------------- views

  function renderIntro() {
    body.innerHTML = `
      <p>${t('calIntro')}</p>
      <div class="cal-actions">
        <button class="tb-btn cal-start">${esc(t('calStart'))}</button>
      </div>
      <div class="cal-msg"></div>
      <h3>${esc(t('calProfiles'))}</h3>
      <div class="cal-list"></div>
      <div class="cal-actions">
        <label class="tb-btn cal-import">${esc(t('calImport'))}<input type="file" accept=".json,application/json" hidden></label>
      </div>`;
    body.querySelector('.cal-start').addEventListener('click', async () => {
      const msg = body.querySelector('.cal-msg');
      if (!api.guitar.running) {
        msg.textContent = t('calNeedInput');
        const ok = await api.guitar.start();
        if (!ok) { msg.textContent = t('gtrDenied'); return; }
      }
      startStep(0, false);
    });
    body.querySelector('.cal-import input').addEventListener('change', async e => {
      const file = e.target.files && e.target.files[0];
      if (!file) return;
      try {
        const p = importProfile(await file.text());
        p.name = p.name || file.name.replace(/\.json$/i, '');
        const rec = await putProfile(p);
        await refreshList();
        api.selectProfile(rec);
        body.querySelector('.cal-msg').textContent = t('calSaved', rec.name);
      } catch {
        body.querySelector('.cal-msg').textContent = t('calImportBad');
      }
    });
    refreshList();
  }

  async function refreshList() {
    const list = await listProfiles();
    api.onProfilesChanged(list);
    const box = body.querySelector('.cal-list');
    if (!box) return;
    if (!list.length) { box.innerHTML = `<p class="small">${esc(t('calNone'))}</p>`; return; }
    box.innerHTML = list.map(p => `<div class="bank-row" data-id="${esc(p.id)}">
        <span class="bank-name">${esc(p.name)}</span>
        <span class="bank-url">${new Date(p.created || 0).toLocaleDateString()} · ${(p.measured || []).filter(Boolean).length}/6</span>
        <button class="sq-btn cal-use" title="${esc(t('calUse'))}">▶</button>
        <button class="sq-btn cal-export" title="${esc(t('calExport'))}">⇩</button>
        <button class="sq-btn cal-del" title="${esc(t('calDelete'))}">✕</button>
      </div>`).join('');
    box.querySelectorAll('.bank-row').forEach(row => {
      const p = list.find(x => x.id === row.dataset.id);
      row.querySelector('.cal-use').addEventListener('click', () => { api.selectProfile(p); dlg.close(); });
      row.querySelector('.cal-export').addEventListener('click', () => {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([exportProfile(p)], { type: 'application/json' }));
        a.download = `${p.name.replace(/[^\w.-]+/g, '_')}.jamrack-guitar.json`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 30000);
      });
      row.querySelector('.cal-del').addEventListener('click', async () => {
        await deleteProfile(p.id);
        if (api.guitar.profile && api.guitar.profile.id === p.id) api.selectProfile(null);
        refreshList();
      });
    });
  }

  function stringLabel(s, at12) {
    const n = 6 - s;                              // string 6 = low E
    const midi = OPEN_STRING_MIDI[s] + (at12 ? 12 : 0);
    return t(at12 ? 'calStringFret12' : 'calStringOpen', n, noteName(midi));
  }

  function renderStep(status, extra = '') {
    const done = open.filter(r => r && r.ok).length;
    body.innerHTML = `
      <div class="cal-progress">${esc(t('calProgress', done, 6))}${step.fret12 ? ' · 12' : ''}</div>
      <div class="cal-string">${esc(stringLabel(step.s, step.fret12))}</div>
      <div class="cal-meter"><i></i></div>
      <div class="cal-status ${status}">${extra}</div>
      <div class="cal-actions">
        <button class="tb-btn cal-retry">${esc(t('calRetry'))}</button>
        <button class="tb-btn cal-skip">${esc(t('calSkip'))}</button>
      </div>`;
    body.querySelector('.cal-retry').addEventListener('click', () => startStep(step.s, step.fret12));
    body.querySelector('.cal-skip').addEventListener('click', () => advance(null));
  }

  function startStep(s, at12) {
    stopStep();
    step = { s, fret12: at12 };
    renderStep('wait', esc(t('calListening')));
    const midi = OPEN_STRING_MIDI[s] + (at12 ? 12 : 0);
    cap = new PluckCapture(api.ctxRate(), midi);
    const bar = body.querySelector('.cal-meter i');
    meterTimer = setInterval(() => {
      if (!cap || !bar) return;
      const v = Math.max(0, Math.min(1, (cap.levelDb + 60) / 60));
      bar.style.width = `${Math.round(100 * v)}%`;
      if (cap.onset >= 0) body.querySelector('.cal-status').textContent = t('calAnalysing');
    }, 80);
    api.guitar.startCapture(block => {
      if (!cap) return;
      const r = cap.push(block);
      if (r) { stopStep(); finishStep(r); }
    });
  }

  function finishStep(r) {
    const st = body.querySelector('.cal-status');
    if (r.ok) {
      st.className = 'cal-status ok';
      st.textContent = t('calResult', fmtB(r.B), fmtCents(r.cents), r.n);
      setTimeout(() => advance(r), 900);
      return;
    }
    st.className = 'cal-status err';
    if (r.error === 'tooQuiet') st.textContent = t('calErrTooQuiet');
    else if (r.error === 'noPartials') st.textContent = t('calErrNoPartials');
    else if (r.error === 'timeout') st.textContent = t('calErrTimeout');
    else st.textContent = t('calErrWrongNote', noteName(r.heardMidi), 6 - step.s);
  }

  function advance(result) {
    if (!step) return;
    (step.fret12 ? fret12 : open)[step.s] = result;
    if (step.s < 5) { startStep(step.s + 1, step.fret12); return; }
    if (!step.fret12) renderAskFret12(); else renderSave();
  }

  function renderAskFret12() {
    stopStep();
    body.innerHTML = `
      <p>${t('calAskFret12')}</p>
      <div class="cal-actions">
        <button class="tb-btn cal-yes">${esc(t('calYes'))}</button>
        <button class="tb-btn cal-no">${esc(t('calNo'))}</button>
      </div>`;
    body.querySelector('.cal-yes').addEventListener('click', () => startStep(0, true));
    body.querySelector('.cal-no').addEventListener('click', () => renderSave());
  }

  function renderSave() {
    stopStep();
    step = null;
    const p = makeProfile(open, fret12);
    const d = new Date();
    const defName = `${t('calDefaultName')} ${d.toLocaleDateString()}`;
    const rows = p.measured.map((m, s) => `<tr><td>${esc(stringLabel(s, false))}</td><td>${m
      ? `B ${fmtB(p.bLaw[s][0])} · ${fmtCents(p.tuning[s])} ¢` : esc(t('calGenericStr'))}</td></tr>`).join('');
    body.innerHTML = `
      <table class="help-table cal-summary">${rows}</table>
      <div class="bank-form">
        <input type="text" class="cal-name" maxlength="40" value="${esc(defName)}">
        <div class="cal-actions">
          <button class="tb-btn cal-save">${esc(t('calSave'))}</button>
          <button class="tb-btn cal-again">${esc(t('calAgain'))}</button>
        </div>
        <div class="cal-msg"></div>
      </div>`;
    body.querySelector('.cal-again').addEventListener('click', () => { open = []; fret12 = []; startStep(0, false); });
    body.querySelector('.cal-save').addEventListener('click', async () => {
      const name = body.querySelector('.cal-name').value.trim() || defName;
      const rec = await putProfile({ ...p, name, created: Date.now() });
      api.selectProfile(rec);
      await refreshList();
      dlg.close();
    });
  }

  return { show, refreshList };
}
