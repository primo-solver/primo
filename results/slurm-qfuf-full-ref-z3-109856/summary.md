# QF_UF Full SLURM Reference Run 109856

- Reference solver: `Python Z3 4.16.0` via `scripts/ref_smtlib_solver.py`.
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`.
- Timeout: 2 seconds per file.
- SLURM job: `109856`, 8 array shards.
- Result CSV: `z3-qfuf-full-2s-merged.csv`.

## Summary

- Total files: 7503
- Solved sat/unsat: 7179
- Timeouts: 324
- Errors: 0
- Wrong answers vs SMT-LIB status: 0
- PAR2: 3706.713576

## Family Solved Counts

- 20170829-Rodin: 34/34 solved, wrong 0
- 20190906-CLEARSY: 46/46 solved, wrong 0
- Goel: 770/773 solved, wrong 0
- NEQ: 25/48 solved, wrong 0
- PEQ: 18/47 solved, wrong 0
- QG: 6135/6396 solved, wrong 0
- SEQ: 48/56 solved, wrong 0
- TypeSafe: 3/3 solved, wrong 0
- eq_diamond: 100/100 solved, wrong 0
