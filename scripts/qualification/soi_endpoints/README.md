# SOI endpoint qualification

Use `scripts/slurm_prepare_benchmark_workspace.py --build-via-slurm` to
prepare a clean candidate snapshot, reusing provisioned dependencies and the
matching C++ header manifest where needed. Run its `--check-only --no-sync`
preflight before submitting any batch. All solver batches run in SLURM.

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
