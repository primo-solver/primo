# QF_UF Full SLURM Run 109840

- Change under evaluation: order-preserving per-class disequality candidate scans in `FlatCongruenceClosure`.
- Decision: accepted; the full suite improved solved count and PAR2 with zero wrong answers.
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`.
- Solver command: `build-slurm-rel/primo --quiet`.
- Timeout: 2 seconds per file.
- SLURM job: `109840`, 8 array shards.
- Result CSV: `primo-diseqscan-qfuf-full-2s-merged.csv`.

## Summary

- Total files: 7503
- Solved sat/unsat: 4706
- Timeouts: 2751
- Wrong answers: 0
- PAR2: 13044.436725

Compared with `results/slurm-qfuf-full-109792/primo-auto-qfuf-full-2s-merged.csv`:

- Solved delta: +1
- Timeout delta: -1
- PAR2 delta: -17.715266
- Newly solved rows: 3
- Regressed solved rows: 2

## Family Solved Counts

- 20170829-Rodin: 34/34 solved, newly solved 0, regressed 0
- 20190906-CLEARSY: 0/46 solved, newly solved 0, regressed 0
- Goel: 626/773 solved, newly solved 1, regressed 0
- NEQ: 0/48 solved, newly solved 0, regressed 1
- PEQ: 3/47 solved, newly solved 0, regressed 0
- QG: 3923/6396 solved, newly solved 2, regressed 1
- SEQ: 17/56 solved, newly solved 0, regressed 0
- TypeSafe: 3/3 solved, newly solved 0, regressed 0
- eq_diamond: 100/100 solved, newly solved 0, regressed 0

## Regressed Rows

- `tmp/QF_UF/NEQ/NEQ032_size3.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn416.smt2`

## Newly Solved Rows

- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_frogs.4.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/QG-classification/loops6/iso_icl046.smt2`
- `tmp/QF_UF/QG-classification/qg6/iso_brn_sk009.smt2`
