# QF_UF Full SLURM 109891 Tuned Online 10s

- Solver: `build-slurm-rel/primo --quiet --euf-prop=online --uf-symmetry-breaking --preprocess-passes 4 --eq-bridge --prop-interval 32 --prop-assign-threshold 0.25 --prop-delivery-budget 1000`
- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows: 7503

| metric | value |
| --- | ---: |
| solved | 6662 |
| timeouts | 777 |
| errors | 18 |
| wrong | 1 |
| par2 | 23916.617431 |

| family | total | solved | timeouts | wrong |
| --- | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 | 0 |
| 20190906-CLEARSY | 46 | 0 | 0 | 0 |
| Goel | 773 | 663 | 110 | 0 |
| NEQ | 48 | 16 | 31 | 0 |
| PEQ | 47 | 20 | 27 | 0 |
| QG | 6396 | 5782 | 598 | 0 |
| SEQ | 56 | 44 | 11 | 1 |
| TypeSafe | 3 | 3 | 0 | 0 |
| eq_diamond | 100 | 100 | 0 | 0 |
