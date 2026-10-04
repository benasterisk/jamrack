#!/bin/sh
# Jalon 1 bis prototype, end to end. GS = GuitarSet root, OUT = scratch dir.
#   sh test/poly/run.sh <guitarset> <out>
set -e
GS=$1; OUT=$2; MIX=$OUT/polymix; CACHE=$OUT/cache
cd "$(dirname "$0")/../.."
python3 test/poly/make_mixes.py --guitarset $GS --out $MIX                       # mixes + hex remixes + takes.json (dev/test)
for s in mix2 mix3 hex2 hex3; do                                                  # mono tracker on the same sets
  node test/dump-events.mjs --guitarset $MIX --takes $MIX/takes.json --set $s --out $OUT/events-mono-$s.json
done
python3 test/poly/analyze.py --guitarset $MIX --takes $MIX/takes.json --set solo,comp,mix2,mix3,hex2,hex3 --cache $CACHE --jobs 4
python3 test/poly/notes.py grid --cache $CACHE --takes $MIX/takes.json --guitarset $MIX --out $OUT/grid.json --jobs 4
for s in solo comp mix2 mix3 hex2 hex3; do
  python3 test/poly/notes.py events --cache $CACHE --takes $MIX/takes.json --set $s --params $OUT/grid.json --out $OUT/events-poly-$s.json
done
python3 test/score.py --guitarset $MIX --takes $MIX/takes.json $OUT/events-poly-solo.json $OUT/events-poly-comp.json \
  $OUT/events-poly-mix2.json $OUT/events-poly-mix3.json $OUT/events-poly-hex2.json $OUT/events-poly-hex3.json --json $OUT/score-poly.json
python3 test/score.py --guitarset $MIX --takes $MIX/takes.json $OUT/events-mono-mix2.json $OUT/events-mono-mix3.json \
  $OUT/events-mono-hex2.json $OUT/events-mono-hex3.json --json $OUT/score-mono.json
