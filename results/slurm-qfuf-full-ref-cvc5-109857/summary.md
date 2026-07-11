# QF_UF Full SLURM Reference Run 109857

- Reference solver: `Python cvc5 1.3.4` via `scripts/ref_smtlib_solver.py`.
- Benchmark set: `sandbox/QF_UF.tar.zst` extracted to `tmp/QF_UF`.
- Timeout: 2 seconds per file.
- SLURM job: `109857`, 8 array shards.
- Result CSV: `cvc5-qfuf-full-2s-merged.csv`.

## Summary

- Total files: 7503
- Solved sat/unsat: 7071
- Timeouts: 432
- Errors: 0
- Wrong answers vs SMT-LIB status: 0
- PAR2: 4300.494351

## Family Solved Counts

- 20170829-Rodin: 34/34 solved, wrong 0
- 20190906-CLEARSY: 46/46 solved, wrong 0
- Goel: 753/773 solved, wrong 0
- NEQ: 18/48 solved, wrong 0
- PEQ: 9/47 solved, wrong 0
- QG: 6070/6396 solved, wrong 0
- SEQ: 38/56 solved, wrong 0
- TypeSafe: 3/3 solved, wrong 0
- eq_diamond: 100/100 solved, wrong 0
