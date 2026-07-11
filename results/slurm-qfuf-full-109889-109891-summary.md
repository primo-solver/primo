# QF_UF Full SLURM 10s Default Tuning Comparison

- Benchmark root: `tmp/QF_UF` from `sandbox/QF_UF.tar.zst`
- Timeout: 10s per file, 8 SLURM shards
- Rows per run: 7503

| job | configuration | solved | timeouts | errors | wrong | par2 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 109889 | current default before legacy-switch promotion | 6611 | 846 | 0 | 0 | 24870.478583 |
| 109890 | legacy preprocessing switches with `--euf-prop=conflict` | 6647 | 810 | 0 | 0 | 24259.631290 |
| 109891 | legacy preprocessing switches with `--euf-prop=online` | 6662 | 777 | 18 | 1 | 23916.617431 |
| 109913 | fixed online reason cache with legacy preprocessing switches | 6684 | 773 | 0 | 0 | 23621.484721 |

The accepted default candidate is job 109913. Job 109891 showed that online
propagation was faster but unsound before signed-literal reason caching was
fixed; job 109913 reran the same online configuration after the fix and had no
errors or wrong answers.

| family | default solved | conflict solved | old online solved | fixed online solved | fixed online timeouts | fixed online wrong |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 20170829-Rodin | 34 | 34 | 34 | 34 | 0 | 0 |
| 20190906-CLEARSY | 0 | 0 | 0 | 0 | 0 | 0 |
| Goel | 662 | 662 | 663 | 663 | 110 | 0 |
| NEQ | 8 | 15 | 16 | 16 | 32 | 0 |
| PEQ | 9 | 20 | 20 | 20 | 27 | 0 |
| QG | 5761 | 5770 | 5782 | 5803 | 593 | 0 |
| SEQ | 34 | 43 | 44 | 45 | 11 | 0 |
| TypeSafe | 3 | 3 | 3 | 3 | 0 | 0 |
| eq_diamond | 100 | 100 | 100 | 100 | 0 | 0 |
