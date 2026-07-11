# QF_UF 10s SLURM comparison: llm2smt vs Primo

- Corpus: `tmp/QF_UF`, 7,503 SMT-LIB files on SLURM.
- llm2smt: `/home/janotmik/llm2smt-reference/build-slurm-rel/bin/llm2smt`, version 0.2.4, commit unknown from local `/tmp/llm2smt` checkout.
- llm2smt options: `--quiet --uf-symmetry-breaking --preprocess-passes 4 --eq-bridge --prop-interval 32 --prop-assign-threshold 0.25 --prop-delivery-budget 1000`.
- Primo comparator: SLURM job 109913, commit `a03eae9`, same 10s timeout and old-style performance defaults.
- Solved means a non-wrong `sat` or `unsat` answer; `unsupported` is not counted as solved.

| solver | solved | timeout | unsupported | wrong | PAR2 |
|---|---:|---:|---:|---:|---:|
| llm2smt | 7351 | 152 | 0 | 0 | 6884.286 |
| Primo 109913 | 6684 | 773 | 46 | 0 | 24539.846 |
| delta Primo - llm2smt | -667 | +621 | +46 | +0 | +17655.561 |

## Pairwise solved set

- both_solved: 6679
- llm2smt_only: 672
- primo_only: 5
- neither_solved: 147
- llm2smt_faster_on_both: 6360
- primo_faster_on_both: 319

## Family summary

| family | total | llm2smt solved | Primo solved | delta | llm2smt timeout | Primo timeout | llm2smt unsupported | Primo unsupported |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 20170829-Rodin | 34 | 34 | 34 | +0 | 0 | 0 | 0 | 0 |
| 20190906-CLEARSY | 46 | 46 | 0 | -46 | 0 | 0 | 0 | 46 |
| Goel | 773 | 770 | 663 | -107 | 3 | 110 | 0 | 0 |
| NEQ | 48 | 41 | 16 | -25 | 7 | 32 | 0 | 0 |
| PEQ | 47 | 36 | 20 | -16 | 11 | 27 | 0 | 0 |
| QG | 6396 | 6268 | 5803 | -465 | 128 | 593 | 0 | 0 |
| SEQ | 56 | 53 | 45 | -8 | 3 | 11 | 0 | 0 |
| TypeSafe | 3 | 3 | 3 | +0 | 0 | 0 | 0 | 0 |
| eq_diamond | 100 | 100 | 100 | +0 | 0 | 0 | 0 | 0 |

## Largest Primo/llm2smt time ratios among files both solved

| file | Primo seconds | llm2smt seconds | ratio Primo/llm2smt |
|---|---:|---:|---:|
| `tmp/QF_UF/SEQ/SEQ026_size4.smt2` | 9.905501 | 0.157468 | 62.90 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk008.smt2` | 9.373198 | 0.221824 | 42.26 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk001.smt2` | 8.280224 | 0.202582 | 40.87 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen041.smt2` | 7.684654 | 0.188506 | 40.77 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_sk052.smt2` | 4.351704 | 0.130581 | 33.33 |
| `tmp/QF_UF/QG-classification/qg5/gensys_brn890.smt2` | 7.342356 | 0.230022 | 31.92 |
| `tmp/QF_UF/QG-classification/qg6/iso_brn_repgen_sk016.smt2` | 5.462947 | 0.174996 | 31.22 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen008.smt2` | 8.081034 | 0.265640 | 30.42 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen032.smt2` | 5.684886 | 0.196337 | 28.95 |
| `tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk006.smt2` | 8.094541 | 0.287775 | 28.13 |
