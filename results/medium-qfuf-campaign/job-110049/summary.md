# Medium QF_UF Campaign 110049

397 selected QF_UF files, 10s timeout, 8-way SLURM array. Primo default, two propagation ablations, and tuned LLM2SMT reference.

## Overall

| solver | files | solved | timeouts | errors | wrong | PAR2 | total seconds |
|---|---:|---:|---:|---:|---:|---:|---:|
| llm2smt-tuned | 397 | 378 | 19 | 0 | 0 | 704.922 | 515.248 |
| primo-conflict | 397 | 298 | 99 | 0 | 0 | 2594.080 | 1611.280 |
| primo-default | 397 | 318 | 79 | 0 | 0 | 2151.832 | 1364.043 |
| primo-prop-off | 397 | 206 | 191 | 0 | 0 | 4082.327 | 2175.285 |

## By Family

| solver | family | files | solved | timeouts | wrong | PAR2 |
|---|---|---:|---:|---:|---:|---:|
| llm2smt-tuned | Goel | 131 | 131 | 0 | 0 | 67.516 |
| llm2smt-tuned | NEQ | 47 | 39 | 8 | 0 | 223.826 |
| llm2smt-tuned | PEQ | 43 | 33 | 10 | 0 | 277.079 |
| llm2smt-tuned | QG | 131 | 131 | 0 | 0 | 96.602 |
| llm2smt-tuned | SEQ | 45 | 44 | 1 | 0 | 39.899 |
| primo-conflict | Goel | 131 | 96 | 35 | 0 | 868.338 |
| primo-conflict | NEQ | 47 | 15 | 32 | 0 | 692.093 |
| primo-conflict | PEQ | 43 | 21 | 22 | 0 | 493.233 |
| primo-conflict | QG | 131 | 129 | 2 | 0 | 348.382 |
| primo-conflict | SEQ | 45 | 37 | 8 | 0 | 192.033 |
| primo-default | Goel | 131 | 115 | 16 | 0 | 435.438 |
| primo-default | NEQ | 47 | 16 | 31 | 0 | 679.798 |
| primo-default | PEQ | 43 | 21 | 22 | 0 | 493.855 |
| primo-default | QG | 131 | 129 | 2 | 0 | 350.842 |
| primo-default | SEQ | 45 | 37 | 8 | 0 | 191.900 |
| primo-prop-off | Goel | 131 | 115 | 16 | 0 | 420.434 |
| primo-prop-off | NEQ | 47 | 8 | 39 | 0 | 814.097 |
| primo-prop-off | PEQ | 43 | 13 | 30 | 0 | 617.447 |
| primo-prop-off | QG | 131 | 47 | 84 | 0 | 1735.183 |
| primo-prop-off | SEQ | 45 | 23 | 22 | 0 | 495.166 |

## Default Compared With Ablations

| comparison | files default-only solved | files other-only solved | sum seconds delta other-default on common solved |
|---|---:|---:|---:|
| primo-prop-off vs primo-default | 115 | 3 | 59.597 |
| primo-conflict vs primo-default | 24 | 4 | 79.744 |
| llm2smt-tuned vs primo-default | 2 | 62 | -378.745 |

## Largest Default Gaps To LLM2SMT

| ratio | Primo seconds | LLM2SMT seconds | Primo status | LLM2SMT status | file |
|---:|---:|---:|---|---|---|
| 71.720 | 8.495968 | 0.118460 | ok | ok | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` |
| 63.530 | 8.759318 | 0.137877 | ok | ok | `tmp/QF_UF/PEQ/PEQ016_size4.smt2` |
| 61.202 | 10.016023 | 0.163656 | timeout | ok | `tmp/QF_UF/SEQ/SEQ026_size4.smt2` |
| 60.679 | 10.020635 | 0.165142 | timeout | ok | `tmp/QF_UF/NEQ/NEQ032_size4.smt2` |
| 51.938 | 10.019905 | 0.192919 | timeout | ok | `tmp/QF_UF/SEQ/SEQ005_size6.smt2` |
| 51.541 | 10.015205 | 0.194316 | timeout | ok | `tmp/QF_UF/PEQ/PEQ019_size6.smt2` |
| 43.478 | 7.496332 | 0.172418 | ok | ok | `tmp/QF_UF/NEQ/NEQ004_size6.smt2` |
| 42.173 | 5.050461 | 0.119755 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2` |
| 40.856 | 10.021169 | 0.245282 | timeout | ok | `tmp/QF_UF/NEQ/NEQ004_size7.smt2` |
| 37.764 | 8.252005 | 0.218514 | ok | ok | `tmp/QF_UF/PEQ/PEQ004_size6.smt2` |
| 35.230 | 9.112977 | 0.258670 | ok | ok | `tmp/QF_UF/PEQ/PEQ011_size6.smt2` |
| 30.935 | 10.017477 | 0.323828 | timeout | ok | `tmp/QF_UF/PEQ/PEQ016_size5.smt2` |
| 30.482 | 4.084892 | 0.134009 | ok | ok | `tmp/QF_UF/NEQ/NEQ006_size3.smt2` |
| 29.461 | 10.035039 | 0.340619 | timeout | ok | `tmp/QF_UF/NEQ/NEQ041_size7.smt2` |
| 26.023 | 8.196548 | 0.314969 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk054.smt2` |
| 25.181 | 3.966320 | 0.157511 | ok | ok | `tmp/QF_UF/PEQ/PEQ012_size4.smt2` |
| 24.639 | 4.989816 | 0.202514 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk053.smt2` |
| 24.626 | 1.454411 | 0.059059 | ok | ok | `tmp/QF_UF/NEQ/NEQ032_size3.smt2` |
| 24.404 | 3.027682 | 0.124065 | ok | ok | `tmp/QF_UF/NEQ/NEQ004_size5.smt2` |
| 22.603 | 10.022873 | 0.443427 | timeout | ok | `tmp/QF_UF/NEQ/NEQ048_size7.smt2` |
| 22.450 | 5.791079 | 0.257954 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen050.smt2` |
| 22.192 | 6.590826 | 0.296995 | ok | ok | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` |
| 22.103 | 3.917055 | 0.177220 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk008.smt2` |
| 19.625 | 4.750246 | 0.242056 | ok | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` |
| 19.383 | 10.024470 | 0.517191 | timeout | ok | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
