# QF_UF Full SLURM Run 109832

- Change under evaluation: Boolean constructor duplicate/complement simplification for `and`/`or`.
- Decision: rejected and reverted; the full suite regressed solved count and PAR2 despite local qg5 wins.
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`.
- Solver command: `build-slurm-rel/primo --quiet`.
- Timeout: 2 seconds per file.
- SLURM job: `109832`, 8 array shards.
- Result CSV: `primo-boolsimp-qfuf-full-2s-merged.csv`.

## Summary

- Total files: 7503
- Solved sat/unsat: 4700
- Timeouts: 2757
- Wrong answers: 0
- PAR2: 13096.419741

Compared with `results/slurm-qfuf-full-109792/primo-auto-qfuf-full-2s-merged.csv`:

- Solved delta: -5
- Timeout delta: +5
- PAR2 delta: +34.267750
- Newly solved rows: 92
- Regressed solved rows: 97

## Family Solved Counts

- 20170829-Rodin: 34/34 solved, newly solved 0, regressed 0
- 20190906-CLEARSY: 0/46 solved, newly solved 0, regressed 0
- Goel: 625/773 solved, newly solved 0, regressed 0
- NEQ: 0/48 solved, newly solved 0, regressed 1
- PEQ: 3/47 solved, newly solved 0, regressed 0
- QG: 3918/6396 solved, newly solved 92, regressed 96
- SEQ: 17/56 solved, newly solved 0, regressed 0
- TypeSafe: 3/3 solved, newly solved 0, regressed 0
- eq_diamond: 100/100 solved, newly solved 0, regressed 0

## Regressed Rows

- `tmp/QF_UF/NEQ/NEQ032_size3.smt2`
- `tmp/QF_UF/QG-classification/loops6/gensys_brn049.smt2`
- `tmp/QF_UF/QG-classification/loops6/iso_brn073.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn094.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1006.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1029.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1036.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1047.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1049.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1072.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1079.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn108.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1086.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1201.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1282.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn169.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn264.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn265.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn303.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn312.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn313.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn319.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn331.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn336.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn343.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn344.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn349.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn353.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn357.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn359.smt2`
- ... 67 more

## Newly Solved Rows

- `tmp/QF_UF/QG-classification/loops6/iso_icl071.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn020.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1008.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1014.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1023.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1032.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1033.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1035.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1048.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1078.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1093.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1106.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1175.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn118.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn120.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1232.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1248.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1265.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn127.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1273.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1300.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1304.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn148.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn170.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn171.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn305.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn309.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn314.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn320.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn337.smt2`
- ... 62 more
