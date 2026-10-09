// Note-for-note diff of two dump-events JSON files (docs/plugin-plan.md 4.3):
// the reference first (JS), the candidate second (C++, or another JS run).
//
//   node test/diff-events.mjs --hop <s> [--vel 0.00787] [--max-missing N|N%] [--max-shifted N|N%] <ref.json> <cand.json>
//
// --hop is the length of one decision frame, in seconds (required):
//   MONO  64 * round(rate / 24000) / rate   (0.0029025 at 44.1 kHz, 0.0026667 at 48 and 96 kHz)
//   POLY  0.0026667
//
// Matching, per take: notes sorted by onset; for each reference note, the
// closest not-yet-matched candidate note of the same midi whose onset lies
// within 1.5 hops (greedy, one by one). Categories:
//   identical     |d onset| <= 1e-6 s, |d offset| <= 1e-6 s, |d velocity| <= --vel, same why
//   shifted       matched, not identical, |d offset| <= 1.5 hops
//   off-diverge   matched, |d offset| > 1.5 hops
//   missing       reference note without a candidate
//   extra         candidate note without a reference
// and, counted apart (they overlap the categories above): why differs,
// velocity differs by more than --vel.
// Thresholds (exit code 1 beyond; a count or a percentage of the reference notes):
//   --max-missing  bounds missing + extra
//   --max-shifted  bounds shifted + off-diverge
// Without thresholds the exit code is 0 whatever the diff (a measurement).
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const SAME = 1e-6;            // seconds: "identical" timing
const SLACK = 1e-9;           // absorbs the rounding of 1.5 * hop

/** Sorted (stable) copy of a take's notes; null offsets read as NaN. */
function sortedNotes(take) {
  const ev = (take && take.events) || [];
  return ev.map((n, i) => ({ ...n, offset: n.offset == null ? NaN : n.offset, _i: i }))
    .sort((a, b) => a.onset - b.onset || a._i - b._i);
}

function offsetDelta(a, b) {
  const na = Number.isNaN(a.offset), nb = Number.isNaN(b.offset);
  if (na && nb) return 0;
  if (na || nb) return Infinity;
  return Math.abs(a.offset - b.offset);
}

/** Diffs one take; returns the counts and the largest deltas of the matched notes. */
export function diffTake(refTake, candTake, { hop, vel = 1 / 127 }) {
  const ref = sortedNotes(refTake), cand = sortedNotes(candTake);
  const tol = 1.5 * hop + SLACK;
  const byMidi = new Map();
  cand.forEach((n, i) => {
    if (!byMidi.has(n.midi)) byMidi.set(n.midi, []);
    byMidi.get(n.midi).push(i);
  });
  const used = new Uint8Array(cand.length);
  const r = {
    ref: ref.length, cand: cand.length, identical: 0, shifted: 0, offDiverge: 0,
    missing: 0, extra: 0, whyDiff: 0, velDiff: 0, maxOnset: 0, maxOffset: 0, maxVel: 0,
    onsetHops: {},             // histogram of round(d onset / hop) over the matched notes
    examples: [],              // a few non-identical notes, for the report
  };
  for (const a of ref) {
    let best = -1, bestD = Infinity;
    for (const j of byMidi.get(a.midi) || []) {
      if (used[j]) continue;
      const d = Math.abs(cand[j].onset - a.onset);
      if (d <= tol && d < bestD) { best = j; bestD = d; }
    }
    if (best < 0) {
      r.missing++;
      if (r.examples.length < 8) r.examples.push({ kind: 'missing', midi: a.midi, onset: a.onset });
      continue;
    }
    used[best] = 1;
    const b = cand[best];
    const dOn = Math.abs(b.onset - a.onset), dOff = offsetDelta(a, b), dVel = Math.abs(b.velocity - a.velocity);
    const sameWhy = a.why === b.why;
    r.maxOnset = Math.max(r.maxOnset, dOn);
    if (Number.isFinite(dOff)) r.maxOffset = Math.max(r.maxOffset, dOff);
    if (Number.isFinite(dVel)) r.maxVel = Math.max(r.maxVel, dVel);
    const k = Math.round((b.onset - a.onset) / hop);
    r.onsetHops[k] = (r.onsetHops[k] || 0) + 1;
    if (!sameWhy) r.whyDiff++;
    if (!(dVel <= vel)) r.velDiff++;
    if (dOn <= SAME && dOff <= SAME && dVel <= vel && sameWhy) r.identical++;
    else {
      if (dOff > tol) r.offDiverge++; else r.shifted++;
      if (r.examples.length < 8) r.examples.push({ kind: dOff > tol ? 'off-diverge' : 'shifted', midi: a.midi, onset: a.onset, dOn: b.onset - a.onset, dOff: b.offset - a.offset, dVel: b.velocity - a.velocity, why: sameWhy ? a.why : `${a.why}/${b.why}` });
    }
  }
  for (let j = 0; j < cand.length; j++) {
    if (used[j]) continue;
    r.extra++;
    if (r.examples.length < 8) r.examples.push({ kind: 'extra', midi: cand[j].midi, onset: cand[j].onset });
  }
  return r;
}

/**
 * Diffs two dumps (parsed JSON objects). Takes are taken in the reference's
 * order, then the candidate's takes that the reference lacks.
 * Returns { hop, vel, takes: [{ take, ...counts }], total }.
 */
export function diffEvents(ref, cand, { hop, vel = 1 / 127 } = {}) {
  if (!(hop > 0)) throw new Error('diffEvents: hop (seconds) is required');
  const names = Object.keys(ref.takes || {});
  for (const t of Object.keys(cand.takes || {})) if (!names.includes(t)) names.push(t);
  const takes = names.map(take => ({ take, ...diffTake(ref.takes?.[take], cand.takes?.[take], { hop, vel }), inRef: !!ref.takes?.[take], inCand: !!cand.takes?.[take] }));
  const total = { take: 'TOTAL', ref: 0, cand: 0, identical: 0, shifted: 0, offDiverge: 0, missing: 0, extra: 0, whyDiff: 0, velDiff: 0, maxOnset: 0, maxOffset: 0, maxVel: 0, onsetHops: {} };
  for (const r of takes) {
    for (const k of ['ref', 'cand', 'identical', 'shifted', 'offDiverge', 'missing', 'extra', 'whyDiff', 'velDiff']) total[k] += r[k];
    for (const k of ['maxOnset', 'maxOffset', 'maxVel']) total[k] = Math.max(total[k], r[k]);
    for (const [h, c] of Object.entries(r.onsetHops)) total.onsetHops[h] = (total.onsetHops[h] || 0) + c;
  }
  return { hop, vel, takes, total };
}

/** Parses a threshold: "12" -> 12 notes, "0.5%" -> 0.5 % of `of` notes. */
export function threshold(spec, of) {
  if (spec === undefined || spec === null) return Infinity;
  const s = String(spec).trim();
  const m = /^(\d+(?:\.\d+)?)(%?)$/.exec(s);
  if (!m) throw new Error(`bad threshold "${spec}" (expected N or N%)`);
  return m[2] ? (Number(m[1]) / 100) * of : Number(m[1]);
}

/** Checks a diffEvents() result against --max-missing / --max-shifted. Returns failure messages. */
export function checkThresholds(result, { maxMissing, maxShifted } = {}) {
  const t = result.total, fails = [];
  const lost = t.missing + t.extra, moved = t.shifted + t.offDiverge;
  const capLost = threshold(maxMissing, t.ref), capMoved = threshold(maxShifted, t.ref);
  if (lost > capLost) fails.push(`missing + extra = ${lost} > ${maxMissing} (${capLost.toFixed(1)} notes)`);
  if (moved > capMoved) fails.push(`shifted + off-diverge = ${moved} > ${maxShifted} (${capMoved.toFixed(1)} notes)`);
  return fails;
}

const pct = (n, of) => (of ? (100 * n / of).toFixed(2) + ' %' : '-');

/** Text table, one row per take plus the total, and a summary paragraph. */
export function formatDiff(result, { title = '', examples = true, onlyDiff = false } = {}) {
  const cols = [
    ['take', r => r.take + (r.inRef === false ? ' (cand only)' : r.inCand === false ? ' (ref only)' : '')],
    ['ref', r => r.ref], ['cand', r => r.cand], ['identical', r => r.identical], ['shifted', r => r.shifted],
    ['off-div', r => r.offDiverge], ['why!=', r => r.whyDiff], ['vel!=', r => r.velDiff],
    ['missing', r => r.missing], ['extra', r => r.extra],
    ['max dOn ms', r => (1000 * r.maxOnset).toFixed(3)], ['max dOff ms', r => (1000 * r.maxOffset).toFixed(3)],
  ];
  const shown = onlyDiff ? result.takes.filter(r => r.identical !== r.ref || r.identical !== r.cand) : result.takes;
  const rows = [...shown, result.total].map(r => cols.map(([, f]) => String(f(r))));
  const widths = cols.map(([h], i) => Math.max(h.length, ...rows.map(row => row[i].length)));
  const line = cells => cells.map((c, i) => (i === 0 ? c.padEnd(widths[i]) : c.padStart(widths[i]))).join('  ');
  const out = [];
  if (title) out.push(title);
  out.push(line(cols.map(([h]) => h)));
  out.push(widths.map(w => '-'.repeat(w)).join('  '));
  rows.forEach((row, i) => {
    if (i === rows.length - 1) out.push(widths.map(w => '-'.repeat(w)).join('  '));
    out.push(line(row));
  });
  const t = result.total;
  const hist = Object.entries(t.onsetHops).sort((a, b) => a[0] - b[0]).map(([h, c]) => `${h > 0 ? '+' : ''}${h}: ${c}`).join(', ');
  out.push(`hop ${result.hop} s, velocity tolerance ${result.vel.toFixed(5)}`);
  out.push(`reference ${t.ref} notes, candidate ${t.cand}: identical ${t.identical} (${pct(t.identical, t.ref)}), shifted ${t.shifted}, off-diverge ${t.offDiverge}, missing ${t.missing}, extra ${t.extra} -> missing + extra ${t.missing + t.extra} (${pct(t.missing + t.extra, t.ref)}), shifted + off-diverge ${t.shifted + t.offDiverge} (${pct(t.shifted + t.offDiverge, t.ref)}); why differs on ${t.whyDiff}, velocity beyond tolerance on ${t.velDiff}`);
  if (hist) out.push(`onset difference of the matched notes, in hops: { ${hist} }`);
  if (examples) {
    const ex = result.takes.flatMap(r => r.examples.map(e => ({ take: r.take, ...e }))).slice(0, 12);
    for (const e of ex) {
      const where = `${e.take} midi ${e.midi} at ${e.onset.toFixed(4)} s`;
      out.push(e.kind === 'missing' || e.kind === 'extra' ? `  ${e.kind}: ${where}`
        : `  ${e.kind}: ${where}, d onset ${(1000 * e.dOn).toFixed(3)} ms, d offset ${(1000 * e.dOff).toFixed(3)} ms, d vel ${e.dVel.toExponential(2)}, why ${e.why}`);
    }
  }
  return out.join('\n');
}

function main() {
  const a = process.argv.slice(2);
  const opt = {}, pos = [];
  for (let i = 0; i < a.length; i++) {
    if (a[i].startsWith('--')) {
      if (i + 1 >= a.length) throw new Error(`missing value after ${a[i]}`);
      opt[a[i].slice(2)] = a[++i];
    } else pos.push(a[i]);
  }
  const usage = 'usage: node test/diff-events.mjs --hop <s> [--vel 0.00787] [--max-missing N|N%] [--max-shifted N|N%] <ref.json> <cand.json>';
  for (const k of Object.keys(opt)) if (!['hop', 'vel', 'max-missing', 'max-shifted'].includes(k)) { console.error(`unknown option --${k}\n${usage}`); process.exit(2); }
  const hop = Number(opt.hop), vel = opt.vel === undefined ? 1 / 127 : Number(opt.vel);
  if (pos.length !== 2 || !(hop > 0) || !(vel >= 0)) { console.error(usage); process.exit(2); }
  // validate the thresholds before the work
  threshold(opt['max-missing'], 1); threshold(opt['max-shifted'], 1);
  const ref = JSON.parse(readFileSync(pos[0], 'utf8')), cand = JSON.parse(readFileSync(pos[1], 'utf8'));
  const result = diffEvents(ref, cand, { hop, vel });
  console.log(formatDiff(result, { title: `${pos[0]} (${ref.label || '?'})  vs  ${pos[1]} (${cand.label || '?'})` }));
  const fails = checkThresholds(result, { maxMissing: opt['max-missing'], maxShifted: opt['max-shifted'] });
  for (const f of fails) console.log(`FAIL: ${f}`);
  if (fails.length) process.exit(1);
}

if (process.argv[1] && pathToFileURL(resolve(process.argv[1])).href === import.meta.url) {
  try { main(); } catch (e) { console.error(`diff-events: ${e.message}`); process.exit(2); }
}
