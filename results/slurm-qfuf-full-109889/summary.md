# QF_UF Full SLURM 109889 Default 10s

- Solver: `build-slurm-rel/primo --quiet`
- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows: 7503

| metric | value |
| --- | ---: |
| solved | 6611 |
| timeouts | 846 |
| errors | 0 |
| wrong | 0 |
| par2 | 24870.478583 |

| family | total | solved | timeouts | wrong |
| --- | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 | 0 |
| 20190906-CLEARSY | 46 | 0 | 0 | 0 |
| Goel | 773 | 662 | 111 | 0 |
| NEQ | 48 | 8 | 40 | 0 |
| PEQ | 47 | 9 | 38 | 0 |
| QG | 6396 | 5761 | 635 | 0 |
| SEQ | 56 | 34 | 22 | 0 |
| TypeSafe | 3 | 3 | 0 | 0 |
| eq_diamond | 100 | 100 | 0 | 0 |
