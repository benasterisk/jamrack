// Dialogs: sound bank manager and help/shortcuts

import { state, allBanks, emit } from '../state.js';
import { probeBank } from '../audio/soundfont.js';
import { uid, el, esc } from '../util.js';
import { getKeyLabels } from '../input/pckeys.js';
import { t, noteName } from '../i18n/index.js';

export function setupDialogs(api) {
  const banksDlg = document.getElementById('banksDlg');
  const helpDlg = document.getElementById('helpDlg');

  // old browsers without <dialog>: minimal fallback through the open attribute
  [banksDlg, helpDlg].forEach(dlg => {
    if (typeof dlg.showModal !== 'function') {
      dlg.showModal = () => dlg.setAttribute('open', '');
      dlg.close = () => dlg.removeAttribute('open');
    }
  });

  document.getElementById('banksBtn').addEventListener('click', () => {
    renderBanks();
    banksDlg.showModal();
  });
  document.getElementById('helpBtn').addEventListener('click', () => {
    renderHelp();
    helpDlg.showModal();
  });

  [banksDlg, helpDlg].forEach(dlg => {
    dlg.querySelector('[data-close]').addEventListener('click', () => dlg.close());
    dlg.addEventListener('click', e => {
      const r = dlg.getBoundingClientRect();
      const outside = e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom;
      if (outside) dlg.close();
    });
  });

  // ---------- Sound banks ----------

  function renderBanks() {
    const body = document.getElementById('banksBody');
    body.innerHTML = `
      <h3>${esc(t('banksAvailable'))}</h3>
      <div class="bank-list"></div>
      <h3>${esc(t('banksAdd'))}</h3>
      <p class="small">${t('banksFormat')}</p>
      <div class="bank-form">
        <input type="text" class="bf-name" placeholder="${esc(t('bankNamePh'))}" maxlength="40">
        <div class="form-row">
          <input type="url" class="bf-url" placeholder="${esc(t('bankUrlPh'))}">
          <button class="tb-btn bf-add">${esc(t('bankTestAdd'))}</button>
        </div>
        <div class="form-msg"></div>
      </div>`;

    const list = body.querySelector('.bank-list');
    allBanks().forEach(b => {
      const row = el(`<div class="bank-row">
        <span class="bank-name">${esc(b.name)}</span>
        <span class="bank-url">${esc(b.url)}</span>
        <span class="tag">${esc(b.builtin ? t('bankBuiltin') : t('bankCustom'))}</span>
        ${b.builtin ? '' : `<button class="sq-btn" title="${esc(t('titleDeleteBank'))}">✕</button>`}
      </div>`);
      if (!b.builtin) {
        row.querySelector('button').addEventListener('click', () => {
          state.customBanks = state.customBanks.filter(x => x.id !== b.id);
          state.instances.forEach(i => { if (i.bankId === b.id) i.bankId = 'musyngkite'; });
          emit('banks');
          api.onBanksChanged();
          renderBanks();
        });
      }
      list.appendChild(row);
    });

    const msg = body.querySelector('.form-msg');
    const nameIn = body.querySelector('.bf-name');
    const urlIn = body.querySelector('.bf-url');
    body.querySelector('.bf-add').addEventListener('click', async () => {
      let url = urlIn.value.trim();
      const name = nameIn.value.trim() || t('bankCustom');
      if (!/^https?:\/\//.test(url)) {
        msg.textContent = t('bankBadUrl');
        msg.className = 'form-msg err';
        return;
      }
      if (!url.endsWith('/')) url += '/';
      msg.textContent = t('bankTesting');
      msg.className = 'form-msg';
      const probe = await probeBank(url);
      if (!probe.ok) {
        msg.textContent = t('bankUnreachable');
        msg.className = 'form-msg err';
        return;
      }
      state.customBanks.push({ id: 'custom-' + uid(), name, url });
      emit('banks');
      api.onBanksChanged();
      renderBanks();
      // renderBanks() rebuilt the form: write the message into the new node
      const newMsg = body.querySelector('.form-msg');
      newMsg.textContent = t('bankAdded', name, !probe.hasNames);
      newMsg.className = 'form-msg ok';
    });
  }

  // ---------- Help ----------

  // [physical code, semitones above the base C or null when unused]
  const TOP_ROW = [
    ['KeyQ', null], ['KeyW', 1], ['KeyE', 3], ['KeyR', null], ['KeyT', 6],
    ['KeyY', 8], ['KeyU', 10], ['KeyI', null], ['KeyO', 13], ['KeyP', 15],
    ['BracketLeft', null], ['BracketRight', 18],
  ];
  const HOME_ROW = [
    ['KeyA', 0], ['KeyS', 2], ['KeyD', 4], ['KeyF', 5], ['KeyG', 7],
    ['KeyH', 9], ['KeyJ', 11], ['KeyK', 12], ['KeyL', 14], ['Semicolon', 16],
    ['Quote', 17], ['Backslash', 19],
  ];

  // The manual is split into tabs: playing, the sound engines, and the rack.
  // One long scroll buried the engine documentation, which is the part people
  // actually need once they get past the keyboard.
  let helpTab = 'play';

  async function renderHelp() {
    const body = document.getElementById('helpBody');
    const labels = await getKeyLabels(state.kb.layout);
    // note names shown relative to C4, matching the default keyboard octave
    const capRow = (keys, cls) => `<div class="helpkb-row ${cls}">${
      keys.map(([code, semis]) => `<span class="cap ${semis !== null ? (cls === 'offset' ? 'black' : '') : 'unused'}">
        ${esc(labels[code] || code)}<small>${semis !== null ? esc(noteName(60 + semis).replace(/-?\d+$/, '')) : '·'}</small></span>`).join('')
    }</div>`;

    const TABS = [
      ['play', t('helpTabPlay')],
      ['sound', t('helpTabSound')],
      ['rack', t('helpTabRack')],
    ];

    const playPane = `
      <h3>${esc(t('helpPcTitle'))}</h3>
      <p>${t('helpPcText')}</p>
      <div class="layout-switch">
        <button data-l="auto">AUTO</button>
        <button data-l="azerty">AZERTY</button>
        <button data-l="qwerty">QWERTY</button>
      </div>
      <div class="helpkb">
        ${capRow(TOP_ROW, 'offset')}
        ${capRow(HOME_ROW, '')}
      </div>
      <h3>${esc(t('helpShortcuts'))}</h3>
      <table class="shortcuts">
        <tr><td>${esc(labels.KeyZ)} / ${esc(labels.KeyX)}</td><td>${esc(t('scOctave'))}</td></tr>
        <tr><td>${esc(labels.KeyC)} / ${esc(labels.KeyV)}</td><td>${esc(t('scVelocity'))}</td></tr>
        <tr><td>${esc(t('scSpace'))}</td><td>${esc(t('scSustain'))}</td></tr>
      </table>
      <h3>${esc(t('helpTouchTitle'))}</h3>
      <p>${t('helpTouchText')}</p>
      <h3>${esc(t('helpMidiTitle'))}</h3>
      <p>${t('helpMidiText')}</p>
      <h3>${esc(t('helpGuitarTitle'))}</h3>
      <p>${t('helpGuitarText')}</p>
      <h3>${esc(t('helpPolyTitle'))}</h3>
      <p>${t('helpPolyText')}</p>
      <h3>${esc(t('helpCalibTitle'))}</h3>
      <p>${t('helpCalibText')}</p>
      <h3>${esc(t('helpRepeatTitle'))}</h3>
      <p>${t('helpRepeatText')}</p>`;

    const soundPane = `
      <p class="help-intro">${t('helpEnginesIntro')}</p>
      <h3>${esc(t('helpBankTitle'))}</h3>
      <p>${t('helpBankText')}</p>
      <h3>${esc(t('helpAnalogTitle'))}</h3>
      <p>${t('helpAnalogText')}</p>
      <table class="help-table">
        <tr><th>${esc(t('unison'))}</th><td>${esc(t('helpUnison'))}</td></tr>
        <tr><th>${esc(t('sub'))}</th><td>${esc(t('helpSub'))}</td></tr>
        <tr><th>${esc(t('filterEnv'))}</th><td>${esc(t('helpFilterEnv'))}</td></tr>
        <tr><th>${esc(t('lfo'))}</th><td>${esc(t('helpLfo'))}</td></tr>
        <tr><th>${esc(t('drive'))}</th><td>${esc(t('helpDrive'))}</td></tr>
      </table>
      <h3>${esc(t('helpSfzTitle'))}</h3>
      <p>${t('helpSfzText')}</p>
      <h3>${esc(t('helpSamplerTitle'))}</h3>
      <p>${t('helpSamplerText')}</p>
      <h3>${esc(t('helpMicTitle'))}</h3>
      <p>${t('helpMicText')}</p>
      <h3>${esc(t('helpResetTitle'))}</h3>
      <p>${t('helpResetText')}</p>`;

    const rackPane = `
      <h3>${esc(t('helpMultiTitle'))}</h3>
      <p>${t('helpMultiText')}</p>
      <h3>${esc(t('helpMasterTitle'))}</h3>
      <p>${t('helpMasterText')}</p>
      <h3>${esc(t('helpLooperTitle'))}</h3>
      <p>${t('helpLooperText')}</p>
      <h3>${esc(t('helpRecTitle'))}</h3>
      <p>${t('helpRecText')}</p>
      <h3>${esc(t('helpSaveTitle'))}</h3>
      <p>${t('helpSaveText')}</p>`;

    const panes = { play: playPane, sound: soundPane, rack: rackPane };

    body.innerHTML = `
      <div class="help-tabs" role="tablist">${
        TABS.map(([id, label]) => `<button role="tab" data-tab="${id}"
          class="${id === helpTab ? 'on' : ''}" aria-selected="${id === helpTab}">${esc(label)}</button>`).join('')
      }</div>
      <div class="help-pane">${panes[helpTab] || panes.play}</div>`;

    body.querySelectorAll('.help-tabs button').forEach(btn => {
      btn.addEventListener('click', () => { helpTab = btn.dataset.tab; renderHelp(); });
    });
    body.querySelectorAll('.layout-switch button').forEach(btn => {
      btn.classList.toggle('on', btn.dataset.l === state.kb.layout);
      btn.addEventListener('click', () => {
        state.kb.layout = btn.dataset.l;
        emit('kb');
        api.onLayoutChanged();
        renderHelp();
      });
    });
  }

  // re-render an open dialog when the language changes
  return {
    refresh() {
      if (banksDlg.hasAttribute('open')) renderBanks();
      if (helpDlg.hasAttribute('open')) renderHelp();
    },
  };
}
