#!/bin/sh
# POLY prototype, end to end, with the split by player (test/poly/split.json):
#   sh test/poly/run.sh <guitarset> <out> [iter] [lambda]
# GS = GuitarSet root, OUT = scratch dir; iter / lambda default to the BASELINE
# decomposer of the improvement session (15 / 400; the jalon 1 bis prototype
# was 6 / 0.02). Thresholds are grid-searched on the dev_* lists only, then
# every set is scored on all takes and on the test_* takes by report.py.
set -e
GS=$1; OUT=$2; ITER=${3:-15}; LAMBDA=${4:-400}
MIX=$OUT/mixes; CACHE=$OUT/cache-$ITER-$LAMBDA
cd "$(dirname "$0")/../.."
python3 test/poly/make_mixes.py --guitarset $GS --out $MIX                       # mixes + hex remixes per split.json
for s in mix2 mix3 hex2 hex3; do                                                  # mono tracker on the same sets
  node test/dump-events.mjs --guitarset $MIX --takes $MIX/takes.json --set $s --out $OUT/events-mono-$s.json
done
SETS=solo,comp,mix2,mix3,hex2,hex3
python3 test/poly/analyze.py --guitarset $MIX --takes $MIX/takes.json --set $SETS --cache $CACHE --jobs 4 \
  --iter $ITER --lambda $LAMBDA --windows medium,short
python3 test/poly/notes.py grid --cache $CACHE --takes $MIX/takes.json --guitarset $MIX --out $OUT/grid.json --jobs 4 \
  --params test/poly/params-baseline.json --space test/poly/params-baseline.json
for s in solo comp mix2 mix3 hex2 hex3; do
  python3 test/poly/notes.py events --cache $CACHE --takes $MIX/takes.json --set $s --params $OUT/grid.json --out $OUT/events-poly-$s.json
done
python3 test/poly/report.py --mixdir $MIX --out $OUT > $OUT/report.md
