#!/bin/sh
# Track A of the POLY improvement session: data-derived template banks.
#   sh test/poly/bank/run.sh <guitarset> <mixdir> <out> [stage] [idmt files dir]
# GS = GuitarSet root (annotation, audio_mono-pickup_mix, audio_hex-pickup_debleeded),
# MIX = make_mixes.py output (split by player, test/poly/split.json), OUT = scratch dir.
# Stages: measure | banks | analyze | grid | score | idmt | all (default = the first five).
# The decomposer is the BASELINE one (analyze.py --iter 15 --lambda 400 --windows medium,short);
# the synth bank equals templates.py bit-exactly, so an existing baseline cache can be linked
# as cache-synth. The idmt stage (cross-guitar check) needs the IDMT-SMT-Guitar subset
# fetched by the scratch helper (poly2/bankA/idmt/fetch.py) in the fifth argument.
# ANALYZE_FLAGS=--skip-short-nmf halves the analysis (see analyze_bank.py), events unchanged.
set -e
GS=$1; MIX=$2; OUT=$3; STAGE=${4:-all}; IDMT=$5
cd "$(dirname "$0")/../../.."
SETS="solo comp mix2 mix3 hex2 hex3"
run() { [ "$STAGE" = all ] || [ "$STAGE" = "$1" ]; }

if run measure; then
  python3 test/poly/bank/extract.py --guitarset $GS --players 00,03 --out $OUT/measure --jobs 4 > $OUT/extract.log 2>&1
fi
if run banks; then
  for k in synth avg fit; do
    python3 test/poly/bank/make_bank.py --measure $OUT/measure/measurements.npz --kind $k --out $OUT/bank-$k.npz
  done
fi
if run analyze; then
  for k in synth avg fit; do
    [ -d $OUT/cache-$k ] && continue
    python3 test/poly/bank/analyze_bank.py --bank $OUT/bank-$k.npz --guitarset $MIX --takes $MIX/takes.json \
      --set solo,comp,mix2,mix3,hex2,hex3 --cache $OUT/cache-$k --jobs 4 --iter 15 --lambda 400 --windows medium,short \
      $ANALYZE_FLAGS > $OUT/analyze-$k.log 2>&1
  done
fi
if run grid; then                       # DEV lists only, the baseline's grid 1 space and fixed thresholds
  for k in synth avg fit; do
    python3 test/poly/notes.py grid --cache $OUT/cache-$k --takes $MIX/takes.json --guitarset $MIX --out $OUT/grid-$k.json \
      --jobs 4 --params test/poly/params-baseline.json --space test/poly/params-baseline.json > $OUT/grid-$k.log 2>&1
  done
fi
if run score; then                      # events with each bank's own DEV thresholds, scored on all takes and on TEST by report.py
  for k in synth avg fit; do
    mkdir -p $OUT/$k
    for s in $SETS; do
      python3 test/poly/notes.py events --cache $OUT/cache-$k --takes $MIX/takes.json --set $s --params $OUT/grid-$k.json \
        --label poly-$k --out $OUT/$k/events-poly-$s.json
    done
    python3 test/poly/report.py --mixdir $MIX --out $OUT/$k > $OUT/$k/report.md
    python3 test/score.py --guitarset $MIX --takes $MIX/takes.json \
      $OUT/$k/events-poly-solo-test.json $OUT/$k/events-poly-mix2-test.json $OUT/$k/events-poly-mix3-test.json \
      $OUT/$k/events-poly-hex2-test.json $OUT/$k/events-poly-hex3-test.json $OUT/$k/events-poly-comp-test.json \
      --json $OUT/$k/score-test.json > $OUT/$k/test-table.md
  done
fi
if [ "$STAGE" = idmt ]; then             # cross-guitar check: thresholds = each bank's DEV grid, nothing re-tuned
  I=$OUT/idmt-mix
  python3 test/poly/bank/idmt.py convert --files $IDMT --out $I > $OUT/idmt-convert.log
  python3 test/poly/bank/idmt.py bfit --files $IDMT --out $OUT/idmt-B.json --gs-stats $OUT/measure/stats.json > $OUT/idmt-B.txt
  LISTS="frets_AR frets_FS frets_LP licks_AR licks_FS licks_LP licks_muted pieces chords_strat chords_ibanez"
  for k in synth avg fit; do
    python3 test/poly/bank/analyze_bank.py --bank $OUT/bank-$k.npz --guitarset $I --takes $I/takes.json --set all \
      --cache $OUT/idmt-cache-$k --jobs 4 --iter 15 --lambda 400 --windows medium,short $ANALYZE_FLAGS > $OUT/idmt-analyze-$k.log 2>&1
    mkdir -p $OUT/$k
    for l in $LISTS; do
      python3 test/poly/notes.py events --cache $OUT/idmt-cache-$k --takes $I/takes.json --set $l --params $OUT/grid-$k.json \
        --label poly-$k --out $OUT/$k/events-idmt-$l.json
    done
    python3 test/score.py --guitarset $I --takes $I/takes.json $(for l in $LISTS; do echo $OUT/$k/events-idmt-$l.json; done) \
      --json $OUT/$k/score-idmt.json > $OUT/$k/idmt-table.md
  done
fi
