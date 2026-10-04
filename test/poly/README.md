# test/poly — POLY prototype and improvement session (offline evaluation tooling)

Everything here is evaluation code (Python: numpy, scipy, soundfile, mir_eval; `pip3 install --user mir_eval`),
never the app: `js/` stays plain JS with zero dependencies. Plan and verdicts: `docs/polyphonic-plan.md`
(sections 11, 12). Scorer: `test/score.py` (mir_eval, pooled F1 ±50 / ±20 ms, recall by chord size, ghosts,
octave errors, latency corrected by the annotation lag). Bench list: `test/takes.json`; split by player:
`test/poly/split.json` (DEV = players 00 and 03, TEST = 01, 02, 04, 05; every mix built inside one split).

## Files

| file | role |
|---|---|
| `templates.py` | synthetic generic bank (Barbancho B, 1/n^1.2 amplitudes), 120 string × fret templates + 2 noise columns |
| `analyze.py` | front-end (24 kHz, hop 64 = 2.67 ms, newest-anchored 43 ms Hann, 2048 FFT) + sparse β-NMF (β 0.5), cache per take |
| `notes.py` | per-hop note rule (two consecutive hops), `events` and DEV `grid` |
| `make_mixes.py` | exactly-labelled polyphony: mixes of solo takes (mix2/mix3) and 2-3 debleeded hex strings (hex2/hex3) |
| `report.py` | scores every set on all takes and on the TEST takes |
| `params-baseline.json` | BASELINE of the improvement session: `analyze.py --iter 15 --lambda 400`, synth bank, per-hop thresholds |
| `bank/` | track A: measured per-string partial profile and B law (`profile-dev.json`), `make_bank.py` (synth / avg / fit / bfit banks), `analyze_bank.py`, `extract.py`, `idmt.py` (cross-guitar check), `ceiling.py` |
| `decomp/` | track B: `decomp.py` (decomposer variants: active set, iterations, λ, warm start), `onset_notes.py` (per-onset integrated decision), `ceiling.py`, `latency.py` |
| `merge.py` | MERGE: one configuration file drives bank + decomposer + rule (`bank`, `analyze`, `grid`, `events`, `time`) |
| `gate.py` | compact table from `score.py --json` outputs and the section-12 gate (dyads ≥ 70 %, triads ≥ 60 %, precision ≥ 80 %) |
| `params-merged.json` | the MERGED configuration (fitatt bank, active 60 / 15 iterations / λ 400, rule and thresholds tuned on DEV) |
| `run.sh`, `bank/run.sh` | end-to-end runners of the baseline and of track A |

## Running the merged configuration

`GS` = GuitarSet v1.1 root (`annotation/`, `audio_mono-pickup_mix/`, `audio_hex-pickup_debleeded/`), `OUT` = a scratch
directory. The merged caches are compact (activations only, ~0.6 MB per take, compressed); the spectral features
odd/low are read from the `--features` directory (written there by `analyze` when no `--base-cache` holds the take).

```sh
cd <repo>
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
python3 test/poly/make_mixes.py --guitarset $GS --out $OUT/mixes                 # bench mixes, split by player
M=$OUT/mixes; CFG=test/poly/params-merged.json
# 1. analysis (fitatt bank + decomposer of the config) on the six sets
python3 test/poly/merge.py analyze --config $CFG --guitarset $M --takes $M/takes.json --set solo,comp,mix2,mix3,hex2,hex3 \
  --cache $OUT/cache-merged --features $OUT/features --jobs 4
# 2. (optional) re-tune the thresholds on the DEV lists only; the committed "best" came from this grid
python3 test/poly/merge.py grid --config $CFG --cache $OUT/cache-merged --takes $M/takes.json --guitarset $M \
  --out $OUT/grid.json --jobs 4 --params $CFG --space $CFG
# 3. events with the committed thresholds (or --params $OUT/grid.json), then the TEST tables
for s in solo comp mix2 mix3 hex2 hex3; do
  python3 test/poly/merge.py events --config $CFG --cache $OUT/cache-merged --takes $M/takes.json --set $s --out $OUT/events-poly-$s.json
done
python3 test/poly/report.py --mixdir $M --out $OUT > $OUT/report.md            # all takes + TEST subsets per set
python3 test/score.py --guitarset $M --takes $M/takes.json $OUT/events-poly-{solo,mix2,mix3,hex2,hex3,comp}-test.json --json $OUT/score-test.json
python3 test/poly/gate.py --score $OUT/score-test.json                           # compact table
# 4. cost of the decomposer alone (numpy single-threaded, best of 3)
python3 test/poly/merge.py time --config $CFG --wav $M/audio_mono-pickup_mix/00_BN1-129-Eb_solo_mix.wav
```

`--rule notes|onset` selects the per-hop rule of `notes.py` or the per-onset rule of `decomp/onset_notes.py` for
`grid` / `events` (default: the `rule` entry of the config); each rule has its own threshold keys, so `--params` must
come from a grid of the same rule. A fresh set (takes never used by the tuning) is scored the same way with its own
mix directory and `takes.json` (lists + a `solo` list for the annotation-lag measurement); `gate.py --gate a,b,c`
pools the named sets for the section-12 gate.

## Result of the merge (thresholds tuned on DEV only; TEST = players 01, 02, 04, 05; FRESH = the verifier's takes, none in takes.json)

Merged = fitatt bank + active 60 / 15 iterations / λ 400 + per-hop rule (`params-merged.json`). F1 ±50 ms / precision / recall,
recall by chord size, ghosts and octave errors per emitted note, corrected latency (raw + 9 ms annotation lag), median (p25/p75/p90):

| set | takes/ref/emitted | F1 ±50 | F1 ±20 | P | R | rec 1 | rec 2 | rec 3 | rec 4 | rec 5-6 | ghosts | octave | latency ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TEST solo | 16/1483/1604 | 81.1 | 69.3 | 78.1 | 84.4 | 86.2 (1403) | 52.5 (80) | - | - | - | 0.2 % | 0.1 % | +23 (19/27/31) |
| TEST mix2 | 16/2430/2464 | 77.2 | 59.7 | 76.6 | 77.7 | 80.8 (1592) | 72.1 (802) | 66.7 (36) | - | - | 0.2 % | 0.3 % | +25 (22/29/34) |
| TEST mix3 | 8/1659/1592 | 72.7 | 52.4 | 74.2 | 71.2 | 75.2 (846) | 69.4 (674) | 56.3 (135) | 50.0 (4) | - | 0.3 % | 1.1 % | +26 (23/30/36) |
| TEST hex2 | 24/2297/1489 | 66.3 | 39.1 | 84.3 | 54.6 | 54.2 (1181) | 55.1 (1116) | - | - | - | 0.3 % | 1.1 % | +27 (23/35/42) |
| TEST hex3 | 16/2036/1204 | 60.2 | 29.7 | 81.1 | 47.9 | 42.9 (648) | 51.1 (992) | 48.2 (396) | - | - | 0.7 % | 2.5 % | +29 (24/36/43) |
| TEST comp | 8/2036/1068 | 49.4 | 22.7 | 71.7 | 37.6 | 38.4 (380) | 45.0 (436) | 44.7 (552) | 27.2 (548) | 23.3 (120) | 1.0 % | 3.8 % | +30 (25/38/46) |
| FRESH solo2 | 12/976/936 | 81.5 | 63.7 | 83.2 | 79.8 | 81.0 (917) | 62.5 (56) | 33.3 (3) | - | - | 0.3 % | 0.3 % | +23 (19/28/34) |
| FRESH mix2b | 6/832/785 | 78.7 | 62.6 | 81.0 | 76.4 | 79.1 (574) | 71.4 (248) | 66.7 (6) | 25.0 (4) | - | 0.1 % | 0.0 % | +23 (20/28/34) |
| FRESH mix3b | 4/874/777 | 70.7 | 54.0 | 75.2 | 66.8 | 74.0 (388) | 63.0 (370) | 55.6 (108) | 50.0 (8) | - | 0.6 % | 0.8 % | +24 (21/29/37) |
| FRESH hex2b | 12/1249/872 | 72.8 | 38.8 | 88.5 | 61.8 | 68.3 (565) | 56.4 (684) | - | - | - | 0.0 % | 0.5 % | +29 (23/36/44) |
| FRESH hex3b | 8/1035/647 | 65.6 | 29.0 | 85.3 | 53.3 | 55.3 (246) | 49.8 (624) | 63.6 (165) | - | - | 0.5 % | 1.1 % | +31 (24/39/45) |
| FRESH comp2 | 4/1035/558 | 54.5 | 17.5 | 77.8 | 41.9 | 20.8 (72) | 38.5 (182) | 49.1 (456) | 39.3 (300) | 28.0 (25) | 0.4 % | 2.5 % | +35 (27/43/51) |

Comparison rows, F1 ±50 ms on solo2 / mix2b / mix3b / hex2b / hex3b / comp2 (dyads hex2b, triads hex3b, pooled precision):
baseline (synth bank, active 40, params-baseline.json) 80.7 / 78.6 / 70.8 / 75.3 / 68.9 / 58.9 (63.6, 67.9, 79.8 %);
synth bank + active 60 (track B alone, own DEV grid) 80.6 / 78.8 / 71.9 / 75.5 / 68.9 / 60.9 (63.5, 67.3, 81.2 %);
merged with the per-onset rule 78.8 / 76.1 / 68.7 / 71.4 / 63.6 / 53.8 (66.7, 63.6, 67.8 %; latency +26..+40 ms median, p90 51-54);
mono tracker 72.9 / 46.9 / 30.6 / 40.5 / 24.9 / 4.8.

Gate of section 12 on the pooled fresh polyphonic sets (mix2b+mix3b+hex2b+hex3b+comp2): dyads 55.8 % FAIL (≥ 70),
triads 53.5 % FAIL (≥ 60), precision 81.8 % PASS (≥ 80). No configuration passes the dyad criterion (best 63.6 % with
the per-onset rule, at 68 % precision). Cost: merged decomposer 1.17 ms/hop (numpy single-threaded, best of 3; baseline
0.90). Full tables and the per-chord-size latency: scratch `poly2/merge/` (README.md there).
