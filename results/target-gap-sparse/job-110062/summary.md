# Target Gap Sparse Trail Campaign 110062

43 selected QF_UF files from `results/target-gap-campaign/targets.txt`, 15s timeout, 4-way SLURM array. Current Primo default uses sparse flat-closure rollback. Baseline is job 110033 with the same target set and comparable timeout.

## Overall

| solver | solved | timeouts | errors | PAR2 | total solved seconds |
|---|---:|---:|---:|---:|---:|
| previous-primo-110033 | 40 | 3 | 0 | 256.892 | 166.892 |
| current-primo-110062 | 40 | 3 | 0 | 161.608 | 71.608 |
| llm2smt-tuned-110033 | 42 | 1 | 0 | 50.142 | 20.142 |

## Largest Improvements

| delta seconds | previous | current | file |
|---:|---:|---:|---|
| -11.623 | 12.300 | 0.677 | `tmp/QF_UF/SEQ/SEQ020_size4.smt2` |
| -8.642 | 9.162 | 0.520 | `tmp/QF_UF/SEQ/SEQ011_size4.smt2` |
| -7.434 | 7.840 | 0.405 | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` |
| -5.842 | 6.309 | 0.467 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` |
| -5.492 | 9.865 | 4.372 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| -5.402 | 6.424 | 1.021 | `tmp/QF_UF/NEQ/NEQ033_size4.smt2` |
| -5.105 | 6.550 | 1.446 | `tmp/QF_UF/NEQ/NEQ015_size3.smt2` |
| -3.421 | 3.794 | 0.372 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_itc99_b12_ab_br_max.smt2` |
| -3.205 | 10.865 | 7.660 | `tmp/QF_UF/SEQ/SEQ035_size6.smt2` |
| -3.032 | 3.958 | 0.926 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen019.smt2` |
| -2.970 | 3.641 | 0.671 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk008.smt2` |
| -2.759 | 3.616 | 0.856 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk048.smt2` |

## Largest Regressions

| delta seconds | previous | current | file |
|---:|---:|---:|---|
| 0.417 | 12.924 | 13.342 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_v_Unidec_ab_br_max.smt2` |
| 0.021 | 1.156 | 1.177 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_sokoban.2.prop1_ab_cti_max.smt2` |
| 0.018 | 0.768 | 0.786 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_fp_max.smt2` |
| 0.017 | 0.251 | 0.267 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_cti_max.smt2` |
| 0.013 | 8.250 | 8.263 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_h_TicTacToe_ab_cti_max.smt2` |
| 0.007 | 0.777 | 0.784 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_sokoban.1.prop1_ab_br_max.smt2` |
| 0.007 | 0.253 | 0.260 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_cti_max.smt2` |
| 0.005 | 1.493 | 1.498 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.4.prop3_ab_cti_max.smt2` |

## Remaining Largest Gaps To LLM2SMT

| current / LLM2SMT | current seconds | LLM2SMT seconds | file |
|---:|---:|---:|---|
| 16.064 | 7.642 | 0.476 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_hanoi.3.prop1_ab_fp_max.smt2` |
| 11.550 | 1.800 | 0.156 | `tmp/QF_UF/PEQ/PEQ012_size4.smt2` |
| 10.418 | 2.511 | 0.241 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen050.smt2` |
| 9.407 | 13.342 | 1.418 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_v_Unidec_ab_br_max.smt2` |
| 8.875 | 2.114 | 0.238 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` |
| 8.591 | 4.372 | 0.509 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| 7.926 | 1.420 | 0.179 | `tmp/QF_UF/NEQ/NEQ048_size6.smt2` |
| 6.061 | 1.294 | 0.213 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen020.smt2` |
| 5.616 | 1.900 | 0.338 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk042.smt2` |
| 5.489 | 8.263 | 1.505 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_h_TicTacToe_ab_cti_max.smt2` |
| 4.666 | 1.462 | 0.313 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk049.smt2` |
| 4.184 | 0.956 | 0.228 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk010.smt2` |
