# QF_UF Symmetry Hard-Instance A/B

SLURM job: `109996`

Per-instance timeout: 12s.

Compared default preprocessing against `--no-uf-symmetry-breaking` on five
previously inspected fast-reference/slow-Primo instances.

| mode | solved | timeout | generated symmetry constraints |
| --- | ---: | ---: | ---: |
| default | 3 | 2 | 21 |
| `--no-uf-symmetry-breaking` | 1 | 4 | 0 |

Per-file result:

| file | default | no symmetry | note |
| --- | ---: | ---: | --- |
| `tmp/QF_UF/SEQ/SEQ026_size4.smt2` | unsat, 8.34s, 9 constraints | timeout | symmetry-sensitive |
| `tmp/QF_UF/NEQ/NEQ006_size4.smt2` | timeout | timeout | still unresolved |
| `tmp/QF_UF/PEQ/PEQ016_size4.smt2` | unsat, 1.51s, 12 constraints | timeout | symmetry-sensitive |
| `tmp/QF_UF/QG-classification/qg5/gensys_icl092.smt2` | unsat, 10.16s, rejected 1 | unsat, 10.31s | not explained by symmetry |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2` | timeout | timeout | still unresolved |

Interpretation: the hard slice confirms that the default symmetry pass is
material for SEQ and PEQ. It does not explain the remaining QG gap by itself;
`gensys_icl092` solves despite rejecting the symmetry candidate, and
`iso_brn_repgen_sk033` still times out.
