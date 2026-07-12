# Targeted Online EUF Backtracking Smoke

- SLURM job: `110031`
- Solver: `build-slurm-rel/primo --quiet --stats`
- Timeout: 12s
- Target class: finite-domain QF_UF files where llm2smt solved quickly and
  Primo spent seconds in online EUF callback backtracking and zero-delivery
  propagation scans.

| file | previous full-run seconds | current seconds | answer |
| --- | ---: | ---: | --- |
| `tmp/QF_UF/PEQ/PEQ012_size4.smt2` | 9.193466 | 4.133877 | unsat |
| `tmp/QF_UF/NEQ/NEQ048_size6.smt2` | 6.970764 | 3.362847 | unsat |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` | 9.624879 | 4.971114 | sat |

The current run reduced zero-delivery propagation candidate checks to 304, 240,
and 176 respectively while preserving the conflict-callback path.
