# QF_UF Full SLURM Run 109728

- Commit: `915c768`
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`
- Files: 7503 SMT-LIB instances
- Timeout: 2 seconds per file
- Solver command: `build-slurm-rel/primo --quiet`
- Result CSV: `primo-final-qfuf-full-2s-merged.csv`

## Summary

Overall: 4631 solved, 2826 timeouts, 0 errors, 0 wrong, PAR2 13374.822232.

The harness counts only `sat` and `unsat` answers as solved. Primo also returns
`unsupported` on all 46 CLEARSY files; those are `ok` rows in the merged CSV but
are not counted as solved here. Counting `ok` rows including `unsupported`, this
run has 4677 ok rows and 2826 timeouts.

Compared with the previous committed full run
`results/slurm-qfuf-full-109632/primo-fixed-default-qfuf-full-2s-merged.csv`,
status/answer behavior changed for one file:

- `tmp/QF_UF/QG-classification/qg5/gensys_brn1167.smt2`: timeout at 2.002457s
  before, `sat` at 1.994656s in this run.

| Family | Solved | Total | Wrong |
| --- | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 |
| 20190906-CLEARSY | 0 | 46 | 0 |
| Goel | 618 | 773 | 0 |
| NEQ | 1 | 48 | 0 |
| PEQ | 4 | 47 | 0 |
| QG | 3855 | 6396 | 0 |
| SEQ | 16 | 56 | 0 |
| TypeSafe | 3 | 3 | 0 |
| eq_diamond | 100 | 100 | 0 |

## Remaining Fast-Reference Gaps

Representative files still timing out for Primo at 2s while Z3 solves below
0.1s in the reference run:

- `tmp/QF_UF/SEQ/SEQ032_size4.smt2`
- `tmp/QF_UF/SEQ/SEQ009_size7.smt2`
- `tmp/QF_UF/SEQ/SEQ009_size8.smt2`
- `tmp/QF_UF/QG-classification/loops6/dead_dnd008.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1008.smt2`
- `tmp/QF_UF/QG-classification/loops6/dead_dnd007.smt2`
- `tmp/QF_UF/QG-classification/qg5/gensys_icl247.smt2`
- `tmp/QF_UF/NEQ/NEQ032_size3.smt2`
