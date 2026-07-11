# QF_UF Full SLURM Run 109978

Current Primo tree with finite-domain auto preprocessing adjusted and online EUF
backtracking changed to assignment-trail checkpoints.

Command shape:

```text
python3 scripts/bench_qf_uf_full.py --root tmp/QF_UF \
  --solver build-slurm-rel/primo --solver-arg=--quiet \
  --timeout 10 --shards 8 --shard-index ${SLURM_ARRAY_TASK_ID}
```

Result artifact:

- merged CSV: `primo-trail10-qfuf-full-10s-merged.csv`
- remote job: `109978`
- all 8 shards completed with exit code `0`

Against llm2smt reference job `109926`:

| solver | solved | timeout | errors | PAR2 | disagreements |
| --- | ---: | ---: | ---: | ---: | ---: |
| Primo 109978 | 6957 | 546 | 0 | 2.445 | 0 |
| llm2smt 109926 | 7351 | 152 | 0 | 0.918 | 0 |

Against previous Primo run `109945`:

| solver | solved | timeout | errors | PAR2 |
| --- | ---: | ---: | ---: | ---: |
| Primo 109978 | 6957 | 546 | 0 | 2.445 |
| Primo 109945 | 6728 | 775 | 0 | 3.155 |

Family solved counts compared with previous Primo:

| family | 109978 solved | 109945 solved | delta |
| --- | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 |
| 2018-Goel-hwbench | 723 | 663 | +60 |
| 20190906-CLEARSY | 46 | 46 | 0 |
| NEQ | 21 | 16 | +5 |
| PEQ | 18 | 20 | -2 |
| QG-classification | 5970 | 5802 | +168 |
| SEQ | 42 | 44 | -2 |
| TypeSafe | 3 | 3 | 0 |
| eq_diamond | 100 | 100 | 0 |

File-level delta against `109945`: 237 newly solved, 8 newly timed out.

Remaining fast-reference/slow-Primo cases against llm2smt: 151 with llm2smt at
or below 1s. The top remaining target is still
`tmp/QF_UF/QG-classification/qg7/iso_brn_repgen_sk033.smt2`.
