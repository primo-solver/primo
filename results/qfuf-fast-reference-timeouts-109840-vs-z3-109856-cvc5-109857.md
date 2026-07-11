# QF_UF Fast Reference Timeout Triage

- Primo baseline: `results/slurm-qfuf-full-109840/primo-diseqscan-qfuf-full-2s-merged.csv`.
- Z3 reference: SLURM job `109856`, `results/slurm-qfuf-full-ref-z3-109856/z3-qfuf-full-2s-merged.csv`.
- cvc5 reference: SLURM job `109857`, `results/slurm-qfuf-full-ref-cvc5-109857/cvc5-qfuf-full-2s-merged.csv`.
- Fast threshold: solved sat/unsat within 1.0s at the same 2s timeout.

- Primo unsolved rows solved fast by both references with matching answer: 1780.
- Primo unsolved rows solved fast by at least one reference: 1997.

## Both-Fast Family Counts

- 20190906-CLEARSY: 46
- Goel: 104
- NEQ: 12
- PEQ: 5
- QG: 1595
- SEQ: 18

## Both-Fast Subdirectories

- QG-classification/qg5: 1028
- QG-classification/qg7: 269
- QG-classification/loops6: 162
- QG-classification/qg6: 136
- 20190906-CLEARSY/0016: 9
- 20190906-CLEARSY/0009: 8
- 20190906-CLEARSY/0015: 8
- 20190906-CLEARSY/0001: 7
- 20190906-CLEARSY/0023: 5
- 20190906-CLEARSY/0005: 2
- 20190906-CLEARSY/0021: 2
- 2018-Goel-hwbench/QF_UF_Heap_ab_br_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_blocks.3.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.2.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.6.prop1_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.6.prop2_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_reg_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_cti_max.smt2: 1

## Representative Both-Fast Primo Timeouts

- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_Heap_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_blocks.3.prop1_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.2.prop1_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.6.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.6.prop2_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_reg_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_reg_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.2.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.2.prop2_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.2.prop3_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_frogs.2.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_frogs.3.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_frogs.3.prop1_ab_reg_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_frogs.5.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_h_TicTacToe_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_h_b05_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_hanoi.3.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_hanoi.3.prop1_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_itc99_b12_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_itc99_b12_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_itc99_b12_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.1.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.2.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.3.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.3.prop1_ab_reg_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.4.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.5.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_leader_election.6.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_loyd.2.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_loyd.3.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.1.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.1.prop1_ab_fp_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.2.prop1_ab_br_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.2.prop1_ab_cti_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.2.prop1_ab_reg_max.smt2`
- `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.3.prop1_ab_fp_max.smt2`
