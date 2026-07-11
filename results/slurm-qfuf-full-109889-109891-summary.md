# QF_UF Full SLURM 10s Default Tuning Comparison

- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows per run: 7503

| job | configuration | solved | timeouts | errors | wrong | par2 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 109889 | current default before legacy-switch promotion | 6611 | 846 | 0 | 0 | 24870.478583 |
| 109890 | legacy preprocessing switches with `--euf-prop=conflict` | 6647 | 810 | 0 | 0 | 24259.631290 |
| 109891 | legacy preprocessing switches with `--euf-prop=online` | 6662 | 777 | 18 | 1 | 23916.617431 |

The accepted default candidate is job 109890. Job 109891 is faster but rejected
for default use because it produced a wrong answer and errors.

| family | default solved | conflict solved | online solved | default timeouts | conflict timeouts | online timeouts | online wrong |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 34 | 0 | 0 | 0 | 0 |
| 20190906-CLEARSY | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Goel | 662 | 662 | 663 | 111 | 111 | 110 | 0 |
| NEQ | 8 | 15 | 16 | 40 | 33 | 31 | 0 |
| PEQ | 9 | 20 | 20 | 38 | 27 | 27 | 0 |
| QG | 5761 | 5770 | 5782 | 635 | 626 | 598 | 0 |
| SEQ | 34 | 43 | 44 | 22 | 13 | 11 | 1 |
| TypeSafe | 3 | 3 | 3 | 0 | 0 | 0 | 0 |
| eq_diamond | 100 | 100 | 100 | 0 | 0 | 0 | 0 |
