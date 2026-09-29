# SOI endpoint qualification

Use `scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm` to
prepare a clean candidate snapshot, reusing provisioned dependencies and the
matching C++ header manifest where needed. Run its `--check-only --no-sync`
preflight before submitting any batch. All solver batches run in SLURM.

If the helper validated a reused dependency installation, archive timestamps
can make its installation stamps older than the unchanged installer scripts.
After checking the installed headers, libraries and version information,
refresh those stamps before submitting the test jobs:

```sh
touch -r contrib/install_cli11.py contrib/local/.cli11.stamp
touch -r contrib/install_cadical.py contrib/local/.cadical.stamp
touch -r contrib/install_gmp.py contrib/local/.gmp.stamp
make -n contrib-required
```

Use this only after the dependency preflight succeeds; the dry run must show
no pending installation. Supply `rg`, `clang-format`, and the
configured Ruff environment for remote lint, or record a separate complete
local `make lint` result if the remote environment lacks those tools.

- `checks.sbatch` runs `make check`, compares the small arithmetic seed/smoke suites
  against Z3 under SOI and default sparse pivoting, checks the two SMT-LIB
  endpoint regressions with preprocessing off, and runs the documented
  component example. Supply `PRIMO_WORKDIR`, `PRIMO_EXPECTED_GIT`, and
  `PRIMO_CHECK_OUT`, plus `PRIMO_CXX_HEADER_MANIFEST` if the build needed a
  header manifest.
- `asan.sbatch` runs `make asan-test`. Supply `PRIMO_WORKDIR`,
  `PRIMO_EXPECTED_GIT`, `PRIMO_ASAN_OUT`, `PRIMO_ASAN_TOOLCHAIN`, and
  `PRIMO_C_SYSTEM_HEADERS`; compiler locations remain environment settings.
- The existing `scripts/slurm_typed_model_static_review.sbatch` checks all
  source/test translation units with clang-tidy and advisory include-cleaner.
- The existing `scripts/slurm_qfuf_fuzz_gate.sbatch` can generate the QF_LRA
  merge gate with SOI forced. Use four 250-case FuzzSMT shards, explicit seeds,
  five-second limits, Z3 as reference, and no mutation tasks. Retain each
  shard's summary and failures, and classify every non-pass outcome.

The new tableau regressions check pivot choice as well as answers: inward
and outward zero crossings, both movement directions and coefficient signs,
coincident bounds of fixed variables at zero and nonzero steps, rational and
infinitesimal steps, and replay after rollback. This is a correctness repair
to an opt-in policy, not a default change or a full performance evaluation.
