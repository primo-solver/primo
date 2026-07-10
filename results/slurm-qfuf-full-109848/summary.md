# QF_UF Full SLURM Run 109848

- Change under evaluation: `auto` routes single-assertion formulas with Bool-valued uninterpreted predicate applications to the existing strong QF_UF preprocessing pipeline.
- Decision: rejected; compared with job 109840, solved delta -9, timeout delta +9, wrong answers 0, PAR2 delta +77.018374.
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`.
- Solver command: `build-slurm-rel/primo --quiet`.
- Timeout: 2 seconds per file.
- SLURM job: `109848`, 8 array shards.
- Result CSV: `primo-autobool-qfuf-full-2s-merged.csv`.

## Summary

- Total files: 7503
- Solved sat/unsat: 4697
- Timeouts: 2760
- Wrong answers: 0
- PAR2: 13121.455099

Compared with `results/slurm-qfuf-full-109840/primo-diseqscan-qfuf-full-2s-merged.csv`:

- Solved delta: -9
- Timeout delta: +9
- PAR2 delta: +77.018374
- Newly solved rows: 2
- Regressed solved rows: 11

## Family Solved Counts

- 20170829-Rodin: 34/34 solved, newly solved 0, regressed 0
- 20190906-CLEARSY: 0/46 solved, newly solved 0, regressed 0
- Goel: 625/773 solved, newly solved 0, regressed 1
- NEQ: 2/48 solved, newly solved 2, regressed 0
- PEQ: 3/47 solved, newly solved 0, regressed 0
- QG: 3915/6396 solved, newly solved 0, regressed 8
- SEQ: 15/56 solved, newly solved 0, regressed 2
- TypeSafe: 3/3 solved, newly solved 0, regressed 0
- eq_diamond: 100/100 solved, newly solved 0, regressed 0

## Regressed Rows

- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2`
- `tmp/QF_UF/QG-classification/loops6/iso_icl046.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn264.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn385.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn928.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl1118.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl1135.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl1264.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl567.smt2`
- `tmp/QF_UF/SEQ/SEQ011_size3.smt2`
- `tmp/QF_UF/SEQ/SEQ020_size3.smt2`

## Newly Solved Rows

- `tmp/QF_UF/NEQ/NEQ004_size4.smt2`
- `tmp/QF_UF/NEQ/NEQ004_size5.smt2`
