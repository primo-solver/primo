# Flat Explanation Scratch Hard-Instance Slice

SLURM job: `109987`

This targeted slice reran five QF_UF files where earlier comparisons showed
Primo behind the predecessor/reference solvers. The run used the remote
`build-slurm-rel/primo` binary after changing `FlatCongruenceClosure`
explanations to reuse closure-owned BFS and reason-mark scratch storage.

Per-instance timeout: 12s.

| File | Previous Primo 10s job 109978 | This run | Reference note |
| --- | ---: | ---: | --- |
| `tmp/QF_UF/SEQ/SEQ026_size4.smt2` | timeout | unsat, 8.90s | Z3 0.45s at 2s run |
| `tmp/QF_UF/NEQ/NEQ006_size4.smt2` | timeout | timeout | Z3 and cvc5 also timed out at 2s |
| `tmp/QF_UF/PEQ/PEQ016_size4.smt2` | unsat, 2.58s | unsat, 1.58s | Z3 0.50s at 2s run |
| `tmp/QF_UF/QG-classification/qg5/gensys_icl092.smt2` | timeout | unsat, 10.23s | Z3 0.75s at 2s run |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2` | timeout | timeout | Z3 0.25s at 2s run |

Summary:

- solved: 3
- timeout: 2
- wrong answers: 0 observed in this slice

Remaining high-value targets are `NEQ006_size4` and
`iso_brn_repgen_sk033`. The latter is still a clear fast-reference/slow-Primo
case; the former is less clear from the 2s Z3/cvc5 reference run but remains
useful because the predecessor solved it quickly in the earlier comparison.
