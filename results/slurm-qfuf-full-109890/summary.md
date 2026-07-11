# QF_UF Full SLURM 109890 Tuned Conflict 10s

- Solver: `build-slurm-rel/primo --quiet --uf-symmetry-breaking --preprocess-passes 4 --eq-bridge --prop-interval 32 --prop-assign-threshold 0.25 --prop-delivery-budget 1000`
- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows: 7503

| metric | value |
| --- | ---: |
| solved | 6647 |
| timeouts | 810 |
| errors | 0 |
| wrong | 0 |
| par2 | 24259.631290 |

| family | total | solved | timeouts | wrong |
| --- | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 | 0 |
| 20190906-CLEARSY | 46 | 0 | 0 | 0 |
| Goel | 773 | 662 | 111 | 0 |
| NEQ | 48 | 15 | 33 | 0 |
| PEQ | 47 | 20 | 27 | 0 |
| QG | 6396 | 5770 | 626 | 0 |
| SEQ | 56 | 43 | 13 | 0 |
| TypeSafe | 3 | 3 | 0 | 0 |
| eq_diamond | 100 | 100 | 0 | 0 |
