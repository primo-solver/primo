# QF_UF Full SLURM 109913 Tuned Online Fixed 10s

- Solver: `build-slurm-rel/primo --quiet --euf-prop=online --uf-symmetry-breaking --preprocess-passes 4 --eq-bridge --prop-interval 32 --prop-assign-threshold 0.25 --prop-delivery-budget 1000`
- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows: 7503

| metric | value |
| --- | ---: |
| solved | 6684 |
| timeouts | 773 |
| errors | 0 |
| wrong | 0 |
| par2 | 23621.484721 |

| family | total | solved | timeouts | wrong |
| --- | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 | 0 |
| 20190906-CLEARSY | 46 | 0 | 0 | 0 |
| Goel | 773 | 663 | 110 | 0 |
| NEQ | 48 | 16 | 32 | 0 |
| PEQ | 47 | 20 | 27 | 0 |
| QG | 6396 | 5803 | 593 | 0 |
| SEQ | 56 | 45 | 11 | 0 |
| TypeSafe | 3 | 3 | 0 | 0 |
| eq_diamond | 100 | 100 | 0 | 0 |
