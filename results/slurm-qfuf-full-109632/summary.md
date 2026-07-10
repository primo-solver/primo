# QF_UF Full SLURM Run 109632

- Benchmark set: `sandbox/QF_UF.tar.zst` extracted as `tmp/QF_UF`
- Files: 7503 SMT-LIB instances
- Timeout: 2 seconds per file
- Solver command: `primo --quiet` at commit `894d479`
- Result CSV: `primo-fixed-default-qfuf-full-2s-merged.csv`

## Summary

| Family | Solved | Total |
| --- | ---: | ---: |
| 20170829-Rodin | 34 | 34 |
| 20190906-CLEARSY | 46 | 46 |
| Goel | 618 | 773 |
| NEQ | 1 | 48 |
| PEQ | 4 | 47 |
| QG | 3854 | 6396 |
| SEQ | 16 | 56 |
| TypeSafe | 3 | 3 |
| eq_diamond | 100 | 100 |

Overall: 4676 solved, 2827 timeouts, PAR2 13364.910310.

Against the earlier z3/cvc5 reference run, there were zero non-unsupported
answer disagreements among fast-reference cases. CLEARSY remains reported as
unsupported by Primo.
