# CaDiCaL Profile Check

- SLURM job: `109953`
- Source commit: `013dfba` (`git` is `unknown` in the remote binary because the
  SLURM workspace is synced without `.git`)
- Solver: `primo 0.1.0 | git: unknown | cadical: cadical-3.0.0-7b99c07`
- Timeout: 10s
- Incremental profile: `ilb=2, sweep=0, congruence=0, backbone=0, condition=0, elim=0, vivify=0, probe=0, subsume=0, transred=0, walk=0`

| profile | file | status | answer | seconds |
|---|---|---|---|---:|
| default | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2` | timeout | error | 10.047435 |
| incremental | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2` | timeout | error | 10.046768 |
| default | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` | timeout | error | 10.038119 |
| incremental | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` | timeout | error | 10.039490 |
| default | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` | ok | unsat | 9.956821 |
| incremental | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` | ok | unsat | 9.953184 |
