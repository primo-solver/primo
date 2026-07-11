# QF_UF Symmetry Activation Preprocess Pass

SLURM job: `109988`

Command shape:

```text
python3 scripts/bench_qf_uf_full.py --root tmp/QF_UF \
  --solver build-slurm-rel/primo --solver-arg=--quiet \
  --solver-arg=--preprocess-only --solver-arg=--stats \
  --timeout 5 --shards 8 --shard-index ${SLURM_ARRAY_TASK_ID}
```

All eight shards completed with exit code `0`.

The job was a preprocessing/statistics pass, not a solve comparison. It records
where the default-on UF symmetry recognizer accepts finite-domain value sets.

| metric | value |
| --- | ---: |
| files | 7503 |
| status ok | 7447 |
| timeout | 56 |
| active files with accepted symmetry sets | 354 |
| accepted symmetry sets | 354 |
| covered values | 2225 |
| covered terms | 1327 |
| generated symmetry constraints | 6753 |
| rejected non-invariant candidate sets | 6138 |

Family activation:

| family | files | active | accepted sets | values | terms | constraints | rejected |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| NEQ | 48 | 45 | 45 | 269 | 257 | 1131 | 0 |
| PEQ | 47 | 47 | 47 | 307 | 203 | 1080 | 0 |
| QG | 6396 | 206 | 206 | 1350 | 673 | 3679 | 6138 |
| SEQ | 56 | 56 | 56 | 299 | 194 | 863 | 0 |

Interpretation: the current recognizer is doing substantial work on the
traditional finite-domain families, especially NEQ, PEQ, and SEQ. QG contains
many candidate value sets, but most are rejected by the invariance check.
