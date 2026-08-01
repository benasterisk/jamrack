// Sample storage for the SAMPLER engine.
//
// User samples are binary audio, far too big for localStorage (~5 MB cap,
// string-only): they live in IndexedDB instead, keyed by a per-sample id that
// the instance state references. This is what lets a loaded or recorded sample
// survive a page reload like every other setting.

const DB_NAME = 'jamrack-samples';
const STORE = 'samples';    // live samples, referenced by module state
const PATCHES = 'patches';  // named saves: sample audio + full settings

let dbPromise = null;

function open() {
  if (dbPromise) return dbPromise;
  dbPromise = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 2);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
      if (!db.objectStoreNames.contains(PATCHES)) db.createObjectStore(PATCHES);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  return dbPromise;
}

function tx(db, mode, store = STORE) {
  return db.transaction(store, mode).objectStore(store);
}

/** Stores a sample. `data` is an ArrayBuffer of the original audio file. */
export async function putSample(id, name, data) {
  const db = await open();
  return new Promise((resolve, reject) => {
    const req = tx(db, 'readwrite').put({ name, data }, id);
    req.onsuccess = () => resolve();
    req.onerror = () => reject(req.error);
  });
}

/** @returns {Promise<{name, data}|null>} */
export async function getSample(id) {
  const db = await open();
  return new Promise((resolve, reject) => {
    const req = tx(db, 'readonly').get(id);
    req.onsuccess = () => resolve(req.result || null);
    req.onerror = () => reject(req.error);
  });
}

/** Best-effort removal (an orphaned sample is only wasted space). */
export async function deleteSample(id) {
  try {
    const db = await open();
    tx(db, 'readwrite').delete(id);
  } catch { /* never blocks anything */ }
}

// ---------------------------------------------------------------- patches
// A patch is self-contained: it embeds the audio bytes alongside the settings,
// so it survives the module (or the live sample) being deleted, and can be
// loaded into any sampler module.

/** patch = { name, settings, data:ArrayBuffer } */
export async function putPatch(id, patch) {
  const db = await open();
  return new Promise((resolve, reject) => {
    const req = tx(db, 'readwrite', PATCHES).put(patch, id);
    req.onsuccess = () => resolve();
    req.onerror = () => reject(req.error);
  });
}

export async function getPatch(id) {
  const db = await open();
  return new Promise((resolve, reject) => {
    const req = tx(db, 'readonly', PATCHES).get(id);
    req.onsuccess = () => resolve(req.result || null);
    req.onerror = () => reject(req.error);
  });
}

export async function deletePatch(id) {
  try {
    const db = await open();
    tx(db, 'readwrite', PATCHES).delete(id);
  } catch { /* best effort */ }
}

/** @returns {Promise<Array<{id, name}>>} sorted by name */
export async function listPatches() {
  const db = await open();
  return new Promise((resolve, reject) => {
    const out = [];
    const req = tx(db, 'readonly', PATCHES).openCursor();
    req.onsuccess = () => {
      const cur = req.result;
      if (cur) {
        out.push({ id: cur.key, name: (cur.value && cur.value.name) || String(cur.key) });
        cur.continue();
      } else {
        resolve(out.sort((a, b) => a.name.localeCompare(b.name)));
      }
    };
    req.onerror = () => reject(req.error);
  });
}
