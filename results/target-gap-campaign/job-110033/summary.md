# Target Gap Campaign 110033

43 selected QF_UF files where reference solvers were fast and Primo was slow. Primo was run with default options; LLM2SMT was run with the tuned QF_UF option set used for comparison.

- Previous Primo target total, job 110032: 226.562206s
- Current Primo target total, job 110033: 211.957442s
- Delta: -14.604764s
- Previous Primo timeouts: 4
- Current Primo timeouts: 3
- Wrong answers observed: 0

## Largest Improvements

| delta seconds | previous | current | file |
|---:|---:|---:|---|
| -8.751489 | 15.060426 | 6.308937 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` |
| -1.012377 | 4.806045 | 3.793668 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_itc99_b12_ab_br_max.smt2` |
| -0.759614 | 10.624197 | 9.864583 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| -0.412928 | 4.965699 | 4.552771 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` |
| -0.282740 | 5.441415 | 5.158675 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen050.smt2` |
| -0.280139 | 1.048392 | 0.768253 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_cambridge.7.prop1_ab_fp_max.smt2` |
| -0.213323 | 4.786623 | 4.573300 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk042.smt2` |
| -0.206676 | 3.331166 | 3.124490 | `tmp/QF_UF/NEQ/NEQ048_size6.smt2` |
| -0.189312 | 2.195980 | 2.006668 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen014.smt2` |
| -0.163442 | 4.043939 | 3.880497 | `tmp/QF_UF/PEQ/PEQ012_size4.smt2` |

## Remaining Largest Gaps

| Primo / LLM2SMT | Primo seconds | LLM2SMT seconds | file |
|---:|---:|---:|---|
| 70.763 | 7.839534 | 0.110786 | `tmp/QF_UF/NEQ/NEQ041_size6.smt2` |
| 24.903 | 3.880497 | 0.155822 | `tmp/QF_UF/PEQ/PEQ012_size4.smt2` |
| 24.479 | 6.308937 | 0.257724 | `tmp/QF_UF/2018-Goel-hwbench/QF_UF_peg_solitaire.5.prop1_ab_reg_max.smt2` |
| 23.834 | 12.300070 | 0.516076 | `tmp/QF_UF/SEQ/SEQ020_size4.smt2` |
| 21.620 | 3.640946 | 0.168404 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk008.smt2` |
| 21.401 | 5.158675 | 0.241047 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen050.smt2` |
| 19.383 | 9.864583 | 0.508940 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk051.smt2` |
| 19.115 | 4.552771 | 0.238182 | `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk027.smt2` |
| 18.484 | 2.072610 | 0.112131 | `tmp/QF_UF/SEQ/SEQ013_size6.smt2` |
| 18.280 | 9.162337 | 0.501234 | `tmp/QF_UF/SEQ/SEQ011_size4.smt2` |
