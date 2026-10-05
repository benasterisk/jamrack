// Saved guitar profiles for the POLY bank (calibration assistant). A profile
// is { id, name, created, bLaw, prof, tuning, measured } as produced by
// calibrate.js makeProfile(); it lives in IndexedDB (a few kB each), apart
// from the sample database. Export / import as JSON files.

const DB_NAME = 'jamrack-guitar';
const STORE = 'profiles';
export const PROFILE_FORMAT = 'jamrack-guitar-profile';

let dbPromise = null;

function open() {
  if (dbPromise) return dbPromise;
  dbPromise = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  return dbPromise;
}

const run = (mode, fn) => open().then(db => new Promise((resolve, reject) => {
  const req = fn(db.transaction(STORE, mode).objectStore(STORE));
  req.onsuccess = () => resolve(req.result);
  req.onerror = () => reject(req.error);
}));

/** Shape check of a profile (also used for imported files). */
export function validProfile(p) {
  const rows = (a, n) => Array.isArray(a) && a.length === 6 && a.every(r => Array.isArray(r) && r.length === n && r.every(v => Number.isFinite(v)));
  return !!p && typeof p === 'object' && rows(p.bLaw, 2) && rows(p.prof, 40)
    && p.bLaw.every(([b, s]) => b > 0 && b < 1e-2 && s > 0.3 && s < 2);
}

export async function putProfile(profile) {
  if (!validProfile(profile)) throw new Error('invalid profile');
  const id = profile.id || `p${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;
  const rec = { ...profile, id };
  await run('readwrite', st => st.put(rec, id));
  return rec;
}

export async function getProfile(id) {
  try { return (await run('readonly', st => st.get(id))) || null; } catch { return null; }
}

export async function deleteProfile(id) {
  try { await run('readwrite', st => st.delete(id)); } catch { /* never blocks anything */ }
}

/** @returns {Promise<Array<{id, name, created, measured}>>} newest first */
export async function listProfiles() {
  try {
    const all = await run('readonly', st => st.getAll());
    return all.sort((a, b) => (b.created || 0) - (a.created || 0));
  } catch { return []; }
}

/** JSON text of a profile for download. */
export function exportProfile(profile) {
  return JSON.stringify({ format: PROFILE_FORMAT, version: 1, ...profile }, null, 1);
}

/** Parses an exported file; throws on anything that is not a profile. */
export function importProfile(text) {
  const p = JSON.parse(text);
  if (p.format !== PROFILE_FORMAT || !validProfile(p)) throw new Error('not a JAMRACK guitar profile');
  const { format, version, id, ...rest } = p;
  void format; void version; void id;
  return { ...rest, created: Date.now() };
}
