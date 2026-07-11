# QF_UF Fast Reference Timeout Triage After 109872

- Primo baseline: `results/slurm-qfuf-full-109872/primo-phaseneg-qfuf-full-2s-merged.csv`.
- Z3 reference: SLURM job `109856`, `results/slurm-qfuf-full-ref-z3-109856/z3-qfuf-full-2s-merged.csv`.
- cvc5 reference: SLURM job `109857`, `results/slurm-qfuf-full-ref-cvc5-109857/cvc5-qfuf-full-2s-merged.csv`.
- Fast threshold: solved sat/unsat within 1.0s at the same 2s timeout.

- Primo unsolved rows solved fast by both references with matching answer: 1706.
- Primo unsolved rows solved fast by at least one reference: 1924.

## Both-Fast Family Counts

- 20190906-CLEARSY: 46
- Goel: 103
- NEQ: 12
- PEQ: 5
- QG: 1521
- SEQ: 19

## Any-Fast Family Counts

- 20190906-CLEARSY: 46
- Goel: 134
- NEQ: 19
- PEQ: 11
- QG: 1684
- SEQ: 30

## Both-Fast Subdirectories

- QG-classification/qg5: 955
- QG-classification/qg7: 273
- QG-classification/loops6: 157
- QG-classification/qg6: 136
- 20190906-CLEARSY/0016: 9
- 20190906-CLEARSY/0009: 8
- 20190906-CLEARSY/0015: 8
- 20190906-CLEARSY/0001: 7
- 20190906-CLEARSY/0023: 5
- 20190906-CLEARSY/0005: 2
- 20190906-CLEARSY/0021: 2
- 2018-Goel-hwbench/QF_UF_blocks.3.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.2.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.6.prop1_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.6.prop2_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_fp_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_reg_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_cti_max.smt2: 1
- 2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_fp_max.smt2: 1

## Fastest Remaining qg5 Samples

- `tmp/QF_UF/QG-classification/qg5/gensys_brn1273.smt2`: z3 0.178951 sat, cvc5 0.168717 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn317.smt2`: z3 0.176569 sat, cvc5 0.172580 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn778.smt2`: z3 0.179589 sat, cvc5 0.170709 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn848.smt2`: z3 0.178001 sat, cvc5 0.174001 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1274.smt2`: z3 0.185721 sat, cvc5 0.166424 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn814.smt2`: z3 0.174523 sat, cvc5 0.180977 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1265.smt2`: z3 0.175604 sat, cvc5 0.180188 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn810.smt2`: z3 0.173471 sat, cvc5 0.182519 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn310.smt2`: z3 0.179167 sat, cvc5 0.177422 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn885.smt2`: z3 0.177208 sat, cvc5 0.180366 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn934.smt2`: z3 0.173894 sat, cvc5 0.184730 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn408.smt2`: z3 0.177122 sat, cvc5 0.181939 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn585.smt2`: z3 0.196140 sat, cvc5 0.163211 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn793.smt2`: z3 0.188782 sat, cvc5 0.171018 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn320.smt2`: z3 0.183437 sat, cvc5 0.177001 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn685.smt2`: z3 0.187443 sat, cvc5 0.173295 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn928.smt2`: z3 0.177936 sat, cvc5 0.184315 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn706.smt2`: z3 0.182236 sat, cvc5 0.180870 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn927.smt2`: z3 0.183158 sat, cvc5 0.180188 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn877.smt2`: z3 0.181264 sat, cvc5 0.182312 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn319.smt2`: z3 0.179822 sat, cvc5 0.184041 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1027.smt2`: z3 0.179424 sat, cvc5 0.185155 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1094.smt2`: z3 0.181753 sat, cvc5 0.184010 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn312.smt2`: z3 0.180550 sat, cvc5 0.185409 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn025.smt2`: z3 0.179193 sat, cvc5 0.186935 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn958.smt2`: z3 0.181789 sat, cvc5 0.184554 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn387.smt2`: z3 0.192694 sat, cvc5 0.174018 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn907.smt2`: z3 0.180456 sat, cvc5 0.186315 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn783.smt2`: z3 0.181095 sat, cvc5 0.185751 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn605.smt2`: z3 0.179951 sat, cvc5 0.186941 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn120.smt2`: z3 0.180030 sat, cvc5 0.189133 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1198.smt2`: z3 0.182388 sat, cvc5 0.186830 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn701.smt2`: z3 0.192592 sat, cvc5 0.176683 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn1086.smt2`: z3 0.181007 sat, cvc5 0.188368 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn808.smt2`: z3 0.178676 sat, cvc5 0.190805 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn911.smt2`: z3 0.178760 sat, cvc5 0.191101 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn736.smt2`: z3 0.182141 sat, cvc5 0.187739 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn542.smt2`: z3 0.181969 sat, cvc5 0.188027 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn541.smt2`: z3 0.184583 sat, cvc5 0.186047 sat
- `tmp/QF_UF/QG-classification/qg5/gensys_brn822.smt2`: z3 0.178621 sat, cvc5 0.192504 sat
