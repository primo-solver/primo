# Medium QF_UF Sparse Trail Campaign 110066

397 selected QF_UF files from `results/medium-qfuf-campaign/targets.txt`, 10s timeout, 8-way SLURM array. Current Primo default uses sparse flat-closure rollback. Baseline and LLM2SMT reference are job 110049.

## Overall

| solver | files | solved | timeouts | errors | wrong | PAR2 | solved seconds |
|---|---:|---:|---:|---:|---:|---:|---:|
| previous-primo-110049 | 397 | 318 | 79 | 0 | 0 | 2151.832 | 571.832 |
| current-primo-110066 | 397 | 341 | 56 | 0 | 0 | 1518.269 | 398.269 |
| llm2smt-tuned-110049 | 397 | 378 | 19 | 0 | 0 | 704.922 | 324.922 |

Current-only solved vs previous: 23. Previous-only solved vs current: 0.

## By Family

| solver | family | files | solved | timeouts | wrong | PAR2 |
|---|---|---:|---:|---:|---:|---:|
| previous-primo-110049 | Goel | 131 | 115 | 16 | 0 | 435.438 |
| previous-primo-110049 | NEQ | 47 | 16 | 31 | 0 | 679.798 |
| previous-primo-110049 | PEQ | 43 | 21 | 22 | 0 | 493.855 |
| previous-primo-110049 | QG | 131 | 129 | 2 | 0 | 350.842 |
| previous-primo-110049 | SEQ | 45 | 37 | 8 | 0 | 191.900 |
| current-primo-110066 | Goel | 131 | 115 | 16 | 0 | 431.610 |
| current-primo-110066 | NEQ | 47 | 30 | 17 | 0 | 414.975 |
| current-primo-110066 | PEQ | 43 | 22 | 21 | 0 | 465.387 |
| current-primo-110066 | QG | 131 | 131 | 0 | 0 | 118.011 |
| current-primo-110066 | SEQ | 45 | 43 | 2 | 0 | 88.286 |
| llm2smt-tuned-110049 | Goel | 131 | 131 | 0 | 0 | 67.516 |
| llm2smt-tuned-110049 | NEQ | 47 | 39 | 8 | 0 | 223.826 |
| llm2smt-tuned-110049 | PEQ | 43 | 33 | 10 | 0 | 277.079 |
| llm2smt-tuned-110049 | QG | 131 | 131 | 0 | 0 | 96.602 |
| llm2smt-tuned-110049 | SEQ | 45 | 44 | 1 | 0 | 39.899 |

## Largest Improvements

| delta seconds | previous | current | file |
|---:|---:|---:|---|
| -9.463 | 10.021 | 0.558 | `tmp/QF_UF/NEQ/NEQ004_size7.smt2` |
| -9.335 | 10.034 | 0.699 | `tmp/QF_UF/SEQ/SEQ020_size4.smt2` |
| -9.030 | 9.589 | 0.560 | `tmp/QF_UF/SEQ/SEQ011_size4.smt2` |
| -8.856 | 10.035 | 1.179 | `tmp/QF_UF/NEQ/NEQ041_size7.smt2` |
| -8.768 | 10.038 | 1.270 | `tmp/QF_UF/NEQ/NEQ041_size8.smt2` |
| -8.569 | 10.021 | 1.452 | `tmp/QF_UF/NEQ/NEQ032_size4.smt2` |
| -8.249 | 10.073 | 1.823 | `tmp/QF_UF/NEQ/NEQ031_size8.smt2` |
| -8.071 | 8.496 | 0.425 | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` |
| -7.117 | 7.496 | 0.380 | `tmp/QF_UF/NEQ/NEQ004_size6.smt2` |
| -6.098 | 6.591 | 0.493 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` |
| -6.047 | 10.039 | 3.992 | `tmp/QF_UF/SEQ/SEQ010_size8.smt2` |
| -6.031 | 10.031 | 4.000 | `tmp/QF_UF/NEQ/NEQ023_size6.smt2` |
| -5.930 | 8.197 | 2.266 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk054.smt2` |
| -5.888 | 10.084 | 4.196 | `tmp/QF_UF/NEQ/NEQ031_size9.smt2` |
| -5.828 | 10.025 | 4.197 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen039.smt2` |
| -5.750 | 6.431 | 0.681 | `tmp/QF_UF/NEQ/NEQ023_size5.smt2` |
| -5.651 | 10.046 | 4.394 | `tmp/QF_UF/NEQ/NEQ006_size4.smt2` |
| -5.643 | 10.024 | 4.381 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| -5.597 | 6.684 | 1.088 | `tmp/QF_UF/NEQ/NEQ033_size4.smt2` |
| -5.435 | 7.753 | 2.318 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk037.smt2` |
| -5.363 | 6.844 | 1.481 | `tmp/QF_UF/NEQ/NEQ015_size3.smt2` |
| -5.356 | 8.252 | 2.896 | `tmp/QF_UF/PEQ/PEQ004_size6.smt2` |
| -5.240 | 10.112 | 4.872 | `tmp/QF_UF/NEQ/NEQ027_size8.smt2` |
| -4.905 | 10.023 | 5.117 | `tmp/QF_UF/NEQ/NEQ048_size7.smt2` |
| -4.609 | 6.731 | 2.122 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk052.smt2` |

## Largest Regressions

| delta seconds | previous | current | file |
|---:|---:|---:|---|
| 1.630 | 1.242 | 2.873 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop2_ab_fp_max.smt2` |
| 1.135 | 1.826 | 2.961 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.4.prop5_ab_reg_max.smt2` |
| 1.058 | 0.455 | 1.513 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.7.prop5_ab_cti_max.smt2` |
| 0.828 | 0.480 | 1.308 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.6.prop2_ab_cti_max.smt2` |
| 0.797 | 1.025 | 1.822 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_firewire_tree.2.prop3_ab_reg_max.smt2` |
| 0.745 | 0.462 | 1.207 | `tmp/QF_UF/QG-classification/qg5/gensys_brn890.smt2` |
| 0.708 | 0.012 | 0.721 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pouring.1.prop1_ab_reg_max.smt2` |
| 0.600 | 0.467 | 1.067 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.1.prop6_ab_cti_max.smt2` |
| 0.585 | 0.135 | 0.719 | `tmp/QF_UF/PEQ/PEQ012_size3.smt2` |
| 0.498 | 0.150 | 0.647 | `tmp/QF_UF/PEQ/PEQ019_size4.smt2` |
| 0.483 | 0.143 | 0.627 | `tmp/QF_UF/PEQ/PEQ020_size5.smt2` |
| 0.453 | 0.500 | 0.953 | `tmp/QF_UF/SEQ/SEQ018_size7.smt2` |
| 0.344 | 0.087 | 0.431 | `tmp/QF_UF/QG-classification/qg5/gensys_brn490.smt2` |
| 0.290 | 0.468 | 0.758 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.2.prop5_ab_cti_max.smt2` |
| 0.263 | 1.970 | 2.232 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.3.prop2_ab_reg_max.smt2` |
| 0.252 | 0.174 | 0.426 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_brp2.5.prop3_ab_cti_max.smt2` |
| 0.252 | 0.470 | 0.721 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pgm_protocol.6.prop5_ab_cti_max.smt2` |
| 0.252 | 0.146 | 0.398 | `tmp/QF_UF/QG-classification/qg5/iso_brn644.smt2` |
| 0.239 | 0.116 | 0.355 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_loyd.3.prop1_ab_cti_max.smt2` |
| 0.224 | 0.013 | 0.237 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_swap_three_ab_br_max.smt2` |

## Remaining Largest Gaps To LLM2SMT

| current / LLM2SMT | current seconds | LLM2SMT seconds | current status | LLM2SMT status | file |
|---:|---:|---:|---|---|---|
| 53.697 | 7.404 | 0.138 | ok | ok | `tmp/QF_UF/PEQ/PEQ016_size4.smt2` |
| 52.837 | 8.647 | 0.164 | ok | ok | `tmp/QF_UF/SEQ/SEQ026_size4.smt2` |
| 49.224 | 0.721 | 0.015 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pouring.1.prop1_ab_reg_max.smt2` |
| 33.889 | 6.538 | 0.193 | ok | ok | `tmp/QF_UF/SEQ/SEQ005_size6.smt2` |
| 23.518 | 0.647 | 0.028 | ok | ok | `tmp/QF_UF/PEQ/PEQ019_size4.smt2` |
| 20.632 | 5.337 | 0.259 | ok | ok | `tmp/QF_UF/PEQ/PEQ011_size6.smt2` |
| 20.126 | 0.719 | 0.036 | ok | ok | `tmp/QF_UF/PEQ/PEQ012_size3.smt2` |
| 17.334 | 0.237 | 0.014 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_swap_three_ab_br_max.smt2` |
| 16.565 | 8.110 | 0.490 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_hanoi.3.prop1_ab_fp_max.smt2` |
| 13.252 | 2.896 | 0.219 | ok | ok | `tmp/QF_UF/PEQ/PEQ004_size6.smt2` |
| 12.881 | 9.845 | 0.764 | ok | ok | `tmp/QF_UF/PEQ/PEQ004_size7.smt2` |
| 12.337 | 0.133 | 0.011 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_pipeline_ab_reg_max.smt2` |
| 11.876 | 9.078 | 0.764 | ok | ok | `tmp/QF_UF/NEQ/NEQ032_size5.smt2` |
| 11.866 | 1.869 | 0.158 | ok | ok | `tmp/QF_UF/PEQ/PEQ012_size4.smt2` |
| 11.616 | 7.956 | 0.685 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_rushhour.1.prop1_ab_reg_max.smt2` |
| 11.541 | 5.117 | 0.443 | ok | ok | `tmp/QF_UF/NEQ/NEQ048_size7.smt2` |
| 11.223 | 0.627 | 0.056 | ok | ok | `tmp/QF_UF/PEQ/PEQ020_size5.smt2` |
| 11.194 | 4.273 | 0.382 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_sokoban.1.prop1_ab_reg_max.smt2` |
| 9.841 | 2.538 | 0.258 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen050.smt2` |
| 8.790 | 1.452 | 0.165 | ok | ok | `tmp/QF_UF/NEQ/NEQ032_size4.smt2` |
| 8.702 | 2.106 | 0.242 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` |
| 8.472 | 4.381 | 0.517 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| 8.221 | 9.139 | 1.112 | ok | ok | `tmp/QF_UF/SEQ/SEQ026_size5.smt2` |
| 7.999 | 1.620 | 0.203 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk053.smt2` |
| 7.881 | 1.421 | 0.180 | ok | ok | `tmp/QF_UF/NEQ/NEQ048_size6.smt2` |
