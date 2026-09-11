---
name: primo-model-validation
description: Validate Primo's printed SAT models on real SMT-LIB corpora or after solver benchmarks, independently with Dolmen. Use for real-corpus correctness checks and before accepting full evaluations or default promotions; keep validation separate from speed measurements.
---

# Primo real-corpus model validation

Keep timing results immutable. Produce a separately identified correctness run
whose coverage can be joined to those results. This skill does not authorize
new cluster jobs beyond the user's task. Resolve missing run scope or approval
before launching substantial work; a speed-only diagnostic can remain explicitly
unvalidated without silently broadening it into a campaign.

## Select the work

- **Post-benchmark:** select every successful Primo `sat` row, even when the
  benchmark's declared status is unknown. Require zero exit status and no timeout
  or error. Keep different option profiles and query identities distinct.
- **Standalone correctness check:** use the requested corpus or a deterministic
  family-balanced manifest. Record this as fresh correctness evidence, not as
  validation of an older timing run on a different binary. A sample is not a
  full-corpus gate. Known-SAT cases answered UNSAT require separate answer triage.
- Full evaluations and default promotions require coverage of every selected
  SAT row for each candidate profile being claimed. Development samples and
  generated/mutation gates do not replace that coverage.

Record the parent run ID, binary SHA-256 and git/build/dependency versions, exact
ordered solver arguments and random seeds, input content hashes, manifest hash,
timeouts, validator version/options, and artifact location. Preserve the binary
and inputs before a workspace rebuild can replace them. Missing provenance or
missing files mean incomplete coverage, not permission to substitute current HEAD.

## Run separately

1. Estimate the rerun budget from recorded SAT runtimes plus model production
   and validator work; known satisfiability does not remove the search cost.
   Use bounded, resumable SLURM shards for nontrivial work. Follow
   [benchmark execution](../benchmarking/references/execution.md) and the available
   `slurm-yinyang` skill for preparation/preflight and collection. Do not run
   validation concurrently on the same resources as headline timing measurements.
2. Replay the same solver profile, adding model production, `get-model` and
   `--check-model`. Preserve the original assertions; modify only a separate
   command-adapted copy. Check process exit status before accepting an answer.
3. Require independent Dolmen acceptance with `scripts/validate_model.py
   --require-dolmen-pass`; pass each solver option as a repeated `--solver-arg`.
   `--dolmen-arg=--strict=false` permits nonessential command/attribute handling;
   input-grammar rejection still needs separate classification. Do not silently
   count limited Z3 fallback or a changed logic as direct Dolmen acceptance.
4. Check the replay answer against the selected row. SAT-to-UNSAT is a failure,
   not a harmless `not-sat` result. A nonzero exit after an answer is an error.
   A timeout/unknown or unsupported validator input leaves validation incomplete.
5. Persist each completed row and retain original/adapted inputs, printed models,
   stdout/stderr, command lines and durations. Resume only with identical binary,
   input, profile and validator identities; do not reuse cached passes across them.

## Current machinery and boundaries

`scripts/slurm_qf_uflra_model_validation.sbatch` already accepts a manifest,
corpus root, sharding, exact solver options and independent solver/Dolmen budgets;
despite its name it has also been used for QF_UFLIA. It calls the shared validator.
Use its explicit environment overrides, not its QF_UFLRA defaults accidentally.

`scripts/benchmark_model_replay.py benchmark` records input/binary identities
and invokes the timing harness. Its `replay` phase selects successful SAT rows,
checks those identities, writes the manifest, runs the shared validator with
`--expect-sat`, and joins coverage into `model-validation/coverage.json`.
`scripts/slurm_real_corpus_validation.sbatch` exposes the two phases; submit
replay with a dependency after timing jobs finish. Scheduling the dependent jobs
and joining reference-answer comparisons remain the campaign driver's responsibility.
Matching complete shards can be reused; partial attempts must be preserved and
retried under a new `--attempt` directory name. No per-row resume is claimed.

The shared adapter scans top-level command boundaries, preserving assertion text,
overriding existing model/print-success options and placing `get-model` immediately
after the single query, before `exit`. Multiple queries, assumptions, push/pop
and post-query assertion changes are explicitly `input-unsupported`; do not
flatten those sessions or omit them and call the gate complete.

For scoped sessions supported by the new command interpreter, use
`scripts/validate_incremental.py` explicitly. It checks each actual session
model against its query-indexed active context, including declaration and
definition scopes; it does not substitute models from fresh Primo solves.
Results remain provisional until the candidate exits successfully. Keep the
original session, adapted command stream, query snapshots, model responses and
reference/validator output together. See `docs/incremental-smt.md` for the
supported command subset and bounded SLURM driver. Assumptions, global
declarations and unhandled options still require explicit unsupported/incomplete
classification. This correctness driver does not extend the single-query timing
adapter or authorize additional evaluations.

## Acceptance and reporting

Report expected SAT rows, direct Dolmen passes, invalid models, changed answers,
solver errors, solver timeouts/unknowns, validator timeouts, unsupported inputs,
and missing rows, with per-family/profile coverage. For a standalone mixed-status
sample also report the total inputs and UNSAT rows, without calling them model passes.

Keep solve/model-production/validation durations outside the original runtime and
PAR2 data. Mark the correctness stage pending, complete, incomplete or failed;
only complete coverage with no failures is fully model-validated. Preserve partial
speed results without labeling them correctness-qualified. Model validation does
not certify UNSAT answers or replace reference comparison, fuzzing or proof checks.

Record the linked runs, exact commands, job IDs, counts and artifact paths in
`docs/run-log.json` before citing results. Stop performance promotion on invalid
models or answer changes; retain/reduce failures and add regression coverage.
Do not start an unrelated solver redesign merely because this workflow finds a bug.
