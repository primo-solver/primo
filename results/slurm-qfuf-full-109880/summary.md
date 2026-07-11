# QF_UF Full SLURM 109880 Conflict Callback

- Solver: `build-slurm-rel/primo --quiet --euf-prop=conflict`
- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 2s per file, 8 SLURM shards
- Rows: 7503

| metric | baseline 109872 | conflict 109880 | delta |
| --- | ---: | ---: | ---: |
| solved | 4778 | 5455 | 677 |
| timeouts | 2679 | 2002 | -677 |
| errors | 0 | 0 | 0 |
| wrong | 0 | 0 | 0 |
| par2 | 12875.631455 | 10557.353167 | -2318.2782880000013 |

| family | baseline solved | conflict solved | baseline timeouts | conflict timeouts | wrong |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 0 | 0 | 0 |
| 20190906-CLEARSY | 0 | 0 | 0 | 0 | 0 |
| Goel | 627 | 606 | 146 | 167 | 0 |
| NEQ | 0 | 3 | 48 | 45 | 0 |
| PEQ | 3 | 5 | 44 | 42 | 0 |
| QG | 3995 | 4679 | 2401 | 1717 | 0 |
| SEQ | 16 | 25 | 40 | 31 | 0 |
| TypeSafe | 3 | 3 | 0 | 0 | 0 |
| eq_diamond | 100 | 100 | 0 | 0 | 0 |
