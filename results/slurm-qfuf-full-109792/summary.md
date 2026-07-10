# QF_UF Full SLURM Run 109792

- Commit: `71dd313`
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`
- Files: 7503 SMT-LIB instances
- Timeout: 2 seconds per file
- Solver command: `build-slurm-rel/primo --quiet`
- Result CSV: `primo-auto-qfuf-full-2s-merged.csv`

## Summary

Overall: 4705 solved, 2752 timeouts, 0 errors, 0 wrong, PAR2 13062.151991.

The harness counts only `sat` and `unsat` answers as solved. Primo also returns
`unsupported` on all 46 CLEARSY files; those are `ok` rows in the merged CSV but
are not counted as solved here. Counting `ok` rows including `unsupported`, this
run has 4751 ok rows and 2752 timeouts.

Compared with the previous committed full run
`results/slurm-qfuf-full-109728/primo-final-qfuf-full-2s-merged.csv`, this run
has 79 newly solved rows and 5 solved-row regressions, for a net gain of 74
solved rows. PAR2 improved from 13374.822232 to 13062.151991. The regressions
are:

- `tmp/QF_UF/SEQ/SEQ015_size3.smt2`
- `tmp/QF_UF/SEQ/SEQ017_size4.smt2`
- `tmp/QF_UF/PEQ/PEQ019_size5.smt2`
- `tmp/QF_UF/NEQ/NEQ004_size4.smt2`
- `tmp/QF_UF/PEQ/PEQ011_size5.smt2`

| Family | Solved | Total | Wrong |
| --- | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 |
| 20190906-CLEARSY | 0 | 46 | 0 |
| Goel | 625 | 773 | 0 |
| NEQ | 1 | 48 | 0 |
| PEQ | 3 | 47 | 0 |
| QG | 3922 | 6396 | 0 |
| SEQ | 17 | 56 | 0 |
| TypeSafe | 3 | 3 | 0 |
| eq_diamond | 100 | 100 | 0 |

## Notes

An existing-profile comparison run with `--qf-uf-preprocess=basic` was submitted
as SLURM job 109760. It solved only 783 rows with zero wrong answers and PAR2
26948.555198, so plain `basic` preprocessing is not a viable default for the
full archive.

Representative remaining fast-reference gaps still timing out for Primo at 2s:

- `tmp/QF_UF/SEQ/SEQ032_size4.smt2`
- `tmp/QF_UF/SEQ/SEQ009_size7.smt2`
- `tmp/QF_UF/SEQ/SEQ009_size8.smt2`
- `tmp/QF_UF/QG-classification/loops6/dead_dnd008.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1008.smt2`
- `tmp/QF_UF/QG-classification/loops6/dead_dnd007.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl247.smt2`
- `tmp/QF_UF/NEQ/NEQ032_size3.smt2`
